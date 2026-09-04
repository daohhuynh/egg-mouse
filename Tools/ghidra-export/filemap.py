#!/usr/bin/env python3
"""filemap.py <tag> [--json PATH] — partition EVERY byte of a PE file.

CLAUDE.md 6 wants coverage stated as a partition computed in one place, with the
residue enumerated. coverage.py does that for Ghidra's function list and
gapscan.py does it for .text bytes. Neither covers the FILE: headers, .rdata,
.data, .rsrc and .reloc are simply outside both, so "exhaustively accounted"
has never meant the whole binary.

This is the denominator. Every byte of the file lands in exactly one class and
the classes sum to the file size, or it exits non-zero.

Classes, coarse by design -- this tool says WHERE bytes are and what structure
claims them, never what they mean:

  DOS_HEADER   MZ header up to e_lfanew
  DOS_STUB     the stub program between the header and the PE signature
  PE_HEADER    signature + COFF header + optional header + data directories
  SECTHDRS     the section header table
  HDRPAD       slack between the end of the headers and the first raw section
  SEC:<name>   a section's raw bytes, minus anything a finer class claims
  RSRC:<type>/<name>  one resource blob, named by its directory entry
  RELOC_BLOCK  one .reloc block (parsed, not read -- mechanical is fine)
  IMPORTS      import directory + INT/IAT/hint-name tables + DLL name strings
  EXPORTS      export directory and its tables
  DEBUGDIR     debug directory entries and the data they point at
  LOADCFG      load config directory, incl. the SEH handler table
  TLS          TLS directory and its callback array
  SECPAD       trailing zero/0xCC padding inside a section's raw size
  OVERLAY      bytes past the end of the last raw section
  GAP          claimed by nothing. THIS IS THE RESIDUE AND IT MUST BE ZERO.

Nothing here executes the binary; it is opened read-only and parsed as a file.
"""
import json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import calledges as CE

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRNAMES = ["EXPORT", "IMPORT", "RESOURCE", "EXCEPTION", "SECURITY", "BASERELOC",
            "DEBUG", "ARCHITECTURE", "GLOBALPTR", "TLS", "LOAD_CONFIG",
            "BOUND_IMPORT", "IAT", "DELAY_IMPORT", "COM_DESCRIPTOR", "RESERVED"]


class Map:
    """Byte-range claim map over the file. First claim wins; overlaps are
    reported rather than silently merged, because an overlap means two
    structures disagree and that is a finding, not a rounding error."""

    def __init__(self, size):
        self.size = size
        self.owner = [None] * size
        self.overlaps = []

    def claim(self, off, length, cls):
        if length <= 0:
            return
        off = max(0, off)
        end = min(self.size, off + length)
        for i in range(off, end):
            if self.owner[i] is None:
                self.owner[i] = cls
            elif self.owner[i] != cls:
                self.overlaps.append((i, self.owner[i], cls))

    def tally(self):
        t = {}
        for o in self.owner:
            k = o or "GAP"
            t[k] = t.get(k, 0) + 1
        return t

    def gaps(self):
        runs, i = [], 0
        while i < self.size:
            if self.owner[i] is None:
                j = i
                while j < self.size and self.owner[j] is None:
                    j += 1
                runs.append((i, j, j - i))
                i = j
            else:
                i += 1
        return runs


def rva2off(secs, rva):
    for nm, va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs):
            d = rva - va
            return ro + d if d < rs else None
    return None


def walk_rsrc(raw, secs, base_rva, m):
    """Resource tree. Named by type/name/lang so a blob can be cited.

    Type and name entries are EITHER a 16-bit integer id OR, with the high bit
    of the entry set, an offset to a length-prefixed UTF-16 string relative to
    the resource directory base. Those are different namespaces and must not be
    conflated: an earlier version of this function reported `t & 0x7fffffff` for
    string types, which is a directory OFFSET, and so labelled the updaters'
    FWFILE resources "type 3304/3340/3388" -- three different numbers for what
    is one and the same string type, "FWFILE", merely stored at a different
    offset in each build. That looked exactly like a per-version type ordinal
    and is not one. FindResourceW(NULL, 0x8c, L"FWFILE") at fw110 0x403213
    settles it: the type is the string, the name is the integer 140.
    """
    root = rva2off(secs, base_rva)
    if root is None:
        return []
    out = []

    def resname(v):
        """Resolve one directory entry key to ('id', n) or ('str', text)."""
        if not (v & 0x80000000):
            return ("id", v)
        o = root + (v & 0x7FFFFFFF)
        if o + 2 > len(raw):
            return ("str", "?")
        ln = struct.unpack_from("<H", raw, o)[0]
        m.claim(o, 2 + 2 * ln, "RSRC_DIRSTR")
        try:
            return ("str", raw[o + 2:o + 2 + 2 * ln].decode("utf-16-le"))
        except Exception:
            return ("str", "?")

    def ents(off):
        if off is None or off + 16 > len(raw):
            return []
        nnamed, nid = struct.unpack_from("<HH", raw, off + 12)
        r = []
        for k in range(nnamed + nid):
            e = off + 16 + 8 * k
            if e + 8 > len(raw):
                break
            nameid, offs = struct.unpack_from("<II", raw, e)
            r.append((nameid, offs))
        return r

    m.claim(root, 16 + 8 * (sum(struct.unpack_from("<HH", raw, root + 12))), "RSRC_DIR")
    for t, toff in ents(root):
        if not (toff & 0x80000000):
            continue
        l1 = root + (toff & 0x7FFFFFFF)
        m.claim(l1, 16 + 8 * (sum(struct.unpack_from("<HH", raw, l1 + 12))), "RSRC_DIR")
        for nm, noff in ents(l1):
            if not (noff & 0x80000000):
                continue
            l2 = root + (noff & 0x7FFFFFFF)
            m.claim(l2, 16 + 8 * (sum(struct.unpack_from("<HH", raw, l2 + 12))), "RSRC_DIR")
            for lang, doff in ents(l2):
                if doff & 0x80000000:
                    continue
                d = root + doff
                m.claim(d, 16, "RSRC_DIR")
                drva, dsize = struct.unpack_from("<II", raw, d)
                o = rva2off(secs, drva)
                tk, tv = resname(t)
                nk, nv = resname(nm)
                cls = f"RSRC:{tv}/{nv}"
                if o is not None:
                    m.claim(o, dsize, cls)
                    out.append({"type": tv, "type_kind": tk,
                                "name": nv, "name_kind": nk, "lang": lang,
                                "off": o, "size": dsize})
    return out


def main():
    tag = sys.argv[1]
    raw = open(os.path.join(ROOT, CE.TAGS[tag]), "rb").read()
    n = len(raw)
    m = Map(n)

    e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
    m.claim(0, 0x40, "DOS_HEADER")
    m.claim(0x40, e_lfanew - 0x40, "DOS_STUB")

    nsec = struct.unpack_from("<H", raw, e_lfanew + 6)[0]
    optsz = struct.unpack_from("<H", raw, e_lfanew + 20)[0]
    m.claim(e_lfanew, 24 + optsz, "PE_HEADER")
    secoff = e_lfanew + 24 + optsz
    m.claim(secoff, 40 * nsec, "SECTHDRS")

    secs = []
    for i in range(nsec):
        o = secoff + 40 * i
        nm = raw[o:o + 8].rstrip(b"\0").decode(errors="replace")
        vs, va, rs, ro = struct.unpack_from("<IIII", raw, o + 8)
        secs.append((nm, va, vs, ro, rs))

    nrva = struct.unpack_from("<I", raw, e_lfanew + 24 + 92)[0]
    dirs = []
    for i in range(nrva):
        d = e_lfanew + 24 + 96 + 8 * i
        dirs.append(struct.unpack_from("<II", raw, d))

    first_raw = min((s[3] for s in secs if s[3]), default=n)
    m.claim(secoff + 40 * nsec, first_raw - (secoff + 40 * nsec), "HDRPAD")

    # Directories that are worth naming individually.
    res = []
    for idx, (rva, sz) in enumerate(dirs):
        if not rva or not sz:
            continue
        name = DIRNAMES[idx] if idx < len(DIRNAMES) else f"DIR{idx}"
        if name == "RESOURCE":
            res = walk_rsrc(raw, secs, rva, m)
            continue
        if name == "BASERELOC":
            o = rva2off(secs, rva)
            if o is None:
                continue
            end, p = o + sz, o
            while p + 8 <= end:
                prva, bsz = struct.unpack_from("<II", raw, p)
                if bsz < 8:
                    break
                m.claim(p, bsz, "RELOC_BLOCK")
                p += bsz
            continue
        o = rva2off(secs, rva)
        if o is not None:
            m.claim(o, sz, name)

    # Section bodies, minus trailing padding, minus whatever is already claimed.
    for nm, va, vs, ro, rs in secs:
        if not rs:
            continue
        body_end = ro + rs
        p = body_end
        while p > ro and raw[p - 1] in (0x00, 0xCC):
            p -= 1
        for i in range(ro, p):
            if m.owner[i] is None:
                m.owner[i] = f"SEC:{nm}"
        for i in range(p, body_end):
            if m.owner[i] is None:
                m.owner[i] = "SECPAD"

    last = max((s[3] + s[4]) for s in secs if s[3])
    m.claim(last, n - last, "OVERLAY")

    t = m.tally()
    total = sum(t.values())
    print(f"=== {tag}: {os.path.basename(CE.TAGS[tag])}  {n} bytes")
    for k in sorted(t, key=lambda k: -t[k]):
        print(f"   {k:<24} {t[k]:>9}  {100*t[k]/n:6.2f}%")
    print(f"   {'TOTAL':<24} {total:>9}  (must equal {n}: {total == n})")
    if m.overlaps:
        print(f"   OVERLAPS: {len(m.overlaps)} bytes claimed twice, e.g. "
              f"{m.overlaps[:3]}")
    g = m.gaps()
    if g:
        print(f"\n   GAP RESIDUE: {sum(x[2] for x in g)} bytes in {len(g)} runs")
        for s, e, ln in sorted(g, key=lambda x: -x[2])[:10]:
            print(f"      {s:#010x}-{e:#010x}  {ln}")
    if "--json" in sys.argv:
        p = sys.argv[sys.argv.index("--json") + 1]
        json.dump({"tag": tag, "size": n, "tally": t, "resources": res,
                   "gaps": [[hex(s), hex(e), l] for s, e, l in g],
                   "n_overlap_bytes": len(m.overlaps)}, open(p, "w"), indent=1)
    sys.exit(1 if (g or total != n) else 0)


if __name__ == "__main__":
    main()
