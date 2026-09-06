#!/usr/bin/env python3
"""calledges.py <tag> [--out PATH] [--objdump-check PATH]

Recover call/jump edges for a vendor binary from RAW BYTES, independently of any
Ghidra-exported call-graph field.

Structure only. This tool records where control transfers go; it holds no belief
about what any function, address, or import means (CLAUDE.md 7.1). Nothing is
executed: the binary is opened read-only and parsed as a file.

Method
------
1. Parse the PE by hand: image base, section table, import directory.
2. Take every executable section's raw bytes.
3. Linear byte scan for the four x86-32 control-transfer encodings that carry a
   statically-resolvable target:
       E8 rel32   direct call
       E9 rel32   direct jmp
       FF 15 m32  indirect call through an absolute pointer (IAT slot)
       FF 25 m32  indirect jmp   through an absolute pointer (IAT thunk)
   A byte scan over-generates: an E8 byte can be an operand byte of some other
   instruction. Every candidate is therefore kept with the evidence that decides
   it, and callers choose the filter (see `classify` below). No candidate is
   silently discarded.
4. Attribute each site to the function whose exported body range contains it,
   or, where no exported body does, to the orphan unit that does (see
   `orphan_units`) -- the exported function list does not cover the whole code
   section, so an attribution scheme that only knows about exported bodies
   silently drops call sites.
5. Emit the forward map (site -> target) and the reverse map (target -> sites).

The function boundaries are the only thing taken from the export, and they are
used as *intervals*, never as edges.
"""
import json, os, re, struct, sys, hashlib

TAGS = {
 "fw110": "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe",
 "fw107": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe",
 "fw106": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe",
 "fw104": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe",
 "cfg107": "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe",
 "cfg104": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe",
 "cfg101": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe",
 "cfg100": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe",
 "xm1r":   "XM1r_Flash_Upgrade_1.9.46.exe",
}
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# ---------------------------------------------------------------- PE parsing
class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:      # `open(...).read()` leaks the handle,
            self.buf = fh.read()          # which coverage.py now trips 9x a run

        b = self.buf
        if b[:2] != b"MZ":
            raise SystemExit("not a PE: %s" % path)
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        if b[pe:pe+4] != b"PE\0\0":
            raise SystemExit("bad PE signature")
        self.machine, self.nsec = struct.unpack_from("<HH", b, pe+4)
        opt_size = struct.unpack_from("<H", b, pe+20)[0]
        opt = pe + 24
        self.magic = struct.unpack_from("<H", b, opt)[0]        # 0x10b PE32
        self.pe32plus = (self.magic == 0x20b)
        if self.pe32plus:
            self.image_base = struct.unpack_from("<Q", b, opt+24)[0]
            dd = opt + 112
        else:
            self.image_base = struct.unpack_from("<I", b, opt+28)[0]
            dd = opt + 96
        nrva = struct.unpack_from("<I", b, dd-4)[0]
        self.dirs = [struct.unpack_from("<II", b, dd+8*i) for i in range(nrva)]
        self.sections = []
        so = opt + opt_size
        for i in range(self.nsec):
            o = so + 40*i
            name = b[o:o+8].rstrip(b"\0").decode("latin1")
            vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", b, o+8)
            chars = struct.unpack_from("<I", b, o+36)[0]
            self.sections.append(dict(name=name, vaddr=vaddr, vsize=vsize,
                                      rptr=rptr, rsize=rsize, chars=chars))

    def rva_to_off(self, rva):
        # VIRTUAL SIZE DECIDES OWNERSHIP; the raw size only bounds what is
        # actually present in the file. Those are different questions and
        # conflating them is a real bug this project has already shipped once:
        # a section map built on max(vsize, rsize) resolved .rdata VAs into
        # .text and turned twelve instruction sequences into plausible-looking
        # "strings" (Tests/test_lod.py documents that incident in full).
        #
        # No .exe here is affected -- checked 2026-09-06 across all nine in
        # TAGS, zero section pairs overlap under the max() rule -- so this
        # changes no existing output. It is here because `ingest.py` and
        # `ingest_config.py` take .exe files this project has never seen, and
        # a silent wrong answer on one of those is worth more than the two
        # lines it costs to prevent.
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + s["vsize"]:
                d = rva - s["vaddr"]
                return s["rptr"] + d if d < s["rsize"] else None
        # Nothing claims it virtually. Fall back to the file-backed extent,
        # which is how a section with vsize == 0 (some linkers) stays readable.
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + s["rsize"]:
                return s["rptr"] + (rva - s["vaddr"])
        return None

    def cstr(self, rva):
        o = self.rva_to_off(rva)
        if o is None:
            return None
        e = self.buf.index(b"\0", o)
        return self.buf[o:e].decode("latin1", "replace")

    def imports(self):
        """VA of each IAT slot -> 'DLL!symbol'. Empty dict if no import dir."""
        out = {}
        if len(self.dirs) < 2:
            return out
        rva, size = self.dirs[1]
        if not rva:
            return out
        off = self.rva_to_off(rva)
        ptrsz = 8 if self.pe32plus else 4
        ordmask = (1 << (ptrsz*8 - 1))
        i = 0
        while True:
            o = off + 20*i
            oft, ts, fc, nm, fta = struct.unpack_from("<IIIII", self.buf, o)
            if oft == 0 and fta == 0 and nm == 0:
                break
            dll = self.cstr(nm) or "?"
            thunk_rva = oft or fta
            to = self.rva_to_off(thunk_rva)
            j = 0
            while True:
                v = struct.unpack_from("<Q" if self.pe32plus else "<I", self.buf, to + ptrsz*j)[0]
                if v == 0:
                    break
                if v & ordmask:
                    sym = "#%d" % (v & 0xFFFF)
                else:
                    sym = self.cstr(v + 2) or "?"
                out[self.image_base + fta + ptrsz*j] = "%s!%s" % (dll, sym)
                j += 1
            i += 1
        return out


# --------------------------------------------------------------- edge finder
def code_sections(pe):
    IMAGE_SCN_MEM_EXECUTE = 0x20000000
    IMAGE_SCN_CNT_CODE    = 0x00000020
    return [s for s in pe.sections
            if s["chars"] & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)]


def scan(pe):
    """Every E8/E9/FF15/FF25 candidate in every executable section."""
    out = []
    for s in code_sections(pe):
        base = pe.image_base + s["vaddr"]
        raw = pe.buf[s["rptr"]:s["rptr"] + s["rsize"]]
        n = len(raw)
        for i in range(n):
            b0 = raw[i]
            if b0 == 0xE8 or b0 == 0xE9:
                if i + 5 > n:
                    continue
                rel = struct.unpack_from("<i", raw, i+1)[0]
                va = base + i
                out.append(dict(site=va, kind="call" if b0 == 0xE8 else "jmp",
                                enc="rel32", target=(va + 5 + rel) & 0xFFFFFFFF,
                                sect=s["name"]))
            elif b0 == 0xFF and i + 6 <= n and raw[i+1] in (0x15, 0x25):
                ptr = struct.unpack_from("<I", raw, i+2)[0]
                va = base + i
                out.append(dict(site=va, kind="call" if raw[i+1] == 0x15 else "jmp",
                                enc="mem32", target=None, slot=ptr, sect=s["name"]))
    return out


def load_functions(tag):
    """entry -> (name, [(lo,hi),...]) and the exported callers field, verbatim."""
    p = os.path.join(ROOT, ".analysis", "export", "%s.jsonl" % tag)
    ents, ivals, exported = {}, [], {}
    with open(p) as f:
        for line in f:
            r = json.loads(line)
            e = int(r["entry"], 16)
            ents[e] = r.get("name", "")
            for lo, hi in r.get("body", []):
                ivals.append((int(lo, 16), int(hi, 16), e))
            exported[e] = [int(c.split("|")[0], 16) for c in r.get("callers", [])]
    ivals.sort()
    return ents, ivals, exported


def orphan_units(pe, ivals):
    """Maximal runs of code-section bytes that lie outside every exported
    function body and are not 0xCC.

    They exist because the exported function list does not cover the whole code
    section. Splitting on 0xCC is a structural rule, not a judgement about what
    the runs contain: it is where the compiler's inter-function padding is, and
    nothing here asserts that a run is code, is data, or is one function.
    """
    out = []
    for s in code_sections(pe):
        lo = pe.image_base + s["vaddr"]
        n = s["rsize"]
        raw = pe.buf[s["rptr"]:s["rptr"] + n]
        cov = bytearray(n)
        for a, b, _e in ivals:
            if lo <= a < lo + n:
                for x in range(a, min(b + 1, lo + n)):
                    cov[x - lo] = 1
        i = 0
        while i < n:
            if cov[i] or raw[i] == 0xCC:
                i += 1
                continue
            j = i
            while j < n and not cov[j] and raw[j] != 0xCC:
                j += 1
            out.append((lo + i, lo + j - 1))
            i = j
    return out


def owner_lookup(ivals):
    import bisect
    los = [x[0] for x in ivals]
    def f(va):
        i = bisect.bisect_right(los, va) - 1
        while i >= 0:
            lo, hi, e = ivals[i]
            if lo <= va <= hi:
                return e
            if lo + 0x10000 < va:   # exported bodies are small; stop scanning back
                break
            i -= 1
        return None
    return f


def classify(cands, ents, own):
    """Attach owner + target-is-an-entry evidence. Nothing is dropped."""
    for c in cands:
        c["owner"] = own(c["site"])
        c["target_is_entry"] = (c["target"] in ents) if c["target"] is not None else False
    return cands


def objdump_starts(tag, pe):
    """Instruction-start addresses per objdump's linear sweep of each code
    section. A second, independent opinion on where instructions begin. It is
    not ground truth either: a linear sweep desyncs on data embedded in .text,
    and a desync is silent."""
    import subprocess
    dis = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dis.sh")
    pat = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t")
    starts = set()
    for s in code_sections(pe):
        lo = pe.image_base + s["vaddr"]
        hi = lo + s["rsize"]
        out = subprocess.run([dis, tag, hex(lo), hex(hi)],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            m = pat.match(line)
            if m:
                starts.add(int(m.group(1), 16))
    return starts


def main():
    tag = sys.argv[1]
    out = None
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out")+1]
    path = os.path.join(ROOT, TAGS[tag])
    pe = PE(path)
    ents, ivals, exported = load_functions(tag)
    orphans = orphan_units(pe, ivals)
    oivals = ivals + [(a, b, a) for a, b in orphans]
    oivals.sort()
    own = owner_lookup(oivals)
    cands = classify(scan(pe), ents, own)
    imports = pe.imports()

    if "--objdump" in sys.argv:
        sync = objdump_starts(tag, pe)
        for c in cands:
            c["sync"] = (c["site"] in sync)
    else:
        sync = None

    # Accepted direct edges: a rel32 whose target is an exported function entry
    # AND whose site lies inside some exported function body.
    # A byte scan cannot by itself prove the site is an instruction boundary; the
    # `sync` flag carries objdump's independent opinion where it was collected.
    fwd, rev = {}, {}
    for c in cands:
        if c["enc"] != "rel32" or not c["target_is_entry"] or c["owner"] is None:
            continue
        fwd.setdefault(c["owner"], set()).add((c["target"], c["kind"]))
        rev.setdefault(c["target"], set()).add(
            (c["site"], c["owner"], c["kind"], c.get("sync")))

    # Indirect sites recorded separately, keyed by the pointer they read.
    mem = {}
    for c in cands:
        if c["enc"] != "mem32" or c["owner"] is None:
            continue
        mem.setdefault(c["slot"], []).append([c["site"], c["owner"], c["kind"]])

    doc = dict(
        tag=tag, file=os.path.basename(path),
        sha256=hashlib.sha256(pe.buf).hexdigest(),
        image_base=hex(pe.image_base),
        sections=[dict(name=s["name"], va=hex(pe.image_base+s["vaddr"]),
                       vsize=s["vsize"], rsize=s["rsize"], chars=hex(s["chars"]))
                  for s in pe.sections],
        n_functions=len(ents),
        n_orphan_units=len(orphans),
        orphan_bytes=sum(b - a + 1 for a, b in orphans),
        orphan_units=[[hex(a), hex(b)] for a, b in orphans],
        n_candidates=len(cands),
        n_rel32=sum(1 for c in cands if c["enc"] == "rel32"),
        n_rel32_to_entry=sum(1 for c in cands if c["enc"] == "rel32" and c["target_is_entry"]),
        n_accepted=sum(len(v) for v in rev.values()),
        imports={hex(k): v for k, v in sorted(imports.items())},
        n_desync=(None if sync is None
                  else sum(1 for c in cands if c["enc"] == "rel32"
                           and c["target_is_entry"] and c["owner"] is not None
                           and not c["sync"])),
        callers={hex(t): [[hex(s), hex(o), k, sy] for s, o, k, sy in sorted(v)]
                 for t, v in sorted(rev.items())},
        callees={hex(f): [[hex(t), k] for t, k in sorted(v)]
                 for f, v in sorted(fwd.items())},
        indirect_sites={hex(k): [[hex(a), hex(b), kk] for a, b, kk in v]
                        for k, v in sorted(mem.items())},
        unattributed_rel32=[[hex(c["site"]), hex(c["target"]), c["kind"]]
                            for c in cands
                            if c["enc"] == "rel32" and c["target_is_entry"] and c["owner"] is None],
        rel32_target_not_entry=sum(1 for c in cands
                                   if c["enc"] == "rel32" and not c["target_is_entry"]),
    )
    if out is None:
        out = os.path.join(ROOT, ".analysis", "calledges_%s.json" % tag)
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    sys.stderr.write("%s: %d funcs, %d rel32 candidates, %d to an entry, %d accepted edges -> %s\n"
                     % (tag, len(ents), doc["n_rel32"], doc["n_rel32_to_entry"],
                        doc["n_accepted"], out))


if __name__ == "__main__":
    main()
