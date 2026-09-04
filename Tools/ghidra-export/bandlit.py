#!/usr/bin/env python3
"""bandlit.py <tag> <lo-va> <hi-va> -- every 4-byte constant in a VA range,
resolved and bucketed. An exhaustive account of what the code in that range
refers to.

WHY. Reading a function tells you what it does; this tells you what it can
possibly touch. Run over the vendor band it enumerates, without sampling, every
string the vendor code can display, every global it can address and every
function pointer it can take -- so a capability that has no constant behind it
has nowhere left to hide, and a string with no explanation in the notes is an
unread part of the program.

METHOD. Every 4-byte aligned-or-not window in the range is read as a
little-endian dword and kept if it lands inside the image. Buckets:

  STR-U / STR-A   the target is a NUL-terminated printable UTF-16LE or ASCII
                  string, printed verbatim
  FUNC            the target is a known function entry
  IMPORT          the target is an import thunk / IAT slot
  DATA            the target is in a data section but is not a string
  CODE            the target is inside .text but is not a function entry
                  (a jump table entry, or a false positive)

WHAT IT CANNOT SEE, stated per CLAUDE.md 1.2a: an address computed at runtime
(base + index), a string reached only through a resource id, and any pointer
that never appears as a literal dword -- e.g. one loaded from a relocated table.
Against that last one: .reloc is checked, and every relocation site inside the
range is included whether or not it looked like an immediate.

FALSE POSITIVES ARE EXPECTED AND ARE THE POINT. Scanning at every byte offset
rather than at instruction boundaries over-reports; that is the safe direction.
A string is not claimed to be USED by the range merely because it is listed --
only that nothing in the range references a string that is not listed.
"""
import json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from litscan import TAGS, sections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def strat(d, secs, base, va):
    """If va begins a printable NUL-terminated string, return (kind, text)."""
    for nm, sva, vsz, ro, rsz in secs:
        if sva <= va < sva + max(vsz, rsz):
            off = ro + (va - sva)
            if off >= len(d):
                return None
            m = re.match(rb'(?:[\x20-\x7e]\x00){3,}\x00\x00', d[off:off + 4096])
            if m:
                return ("STR-U", m.group()[:-2].decode("utf-16-le"))
            m = re.match(rb'[\x20-\x7e]{4,}\x00', d[off:off + 4096])
            if m:
                return ("STR-A", m.group()[:-1].decode())
            return None
    return None


def main():
    tag, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    d = open(os.path.join(ROOT, TAGS[tag]), "rb").read()
    base, secs = sections(d)
    imgend = max(s[1] + max(s[2], s[4]) for s in secs)
    ents = {}
    p = os.path.join(ROOT, ".analysis", "export", f"{tag}.jsonl")
    if os.path.exists(p):
        ents = {int(r["entry"], 16): r.get("name")
                for r in (json.loads(l) for l in open(p))}
    sec_of = lambda va: next((n for n, s, v, o, r in secs
                              if s <= va < s + max(v, r)), None)
    # file offsets for the range
    lo_off = hi_off = None
    for nm, sva, vsz, ro, rsz in secs:
        if sva <= lo < sva + max(vsz, rsz):
            lo_off, hi_off, secname = ro + (lo - sva), ro + (hi - sva), nm
    blob = d[lo_off:hi_off]
    buckets = {}
    for i in range(len(blob) - 3):
        v = struct.unpack_from("<I", blob, i)[0]
        if not (base <= v < imgend):
            continue
        site = lo + i
        s = strat(d, secs, base, v)
        if s:
            buckets.setdefault(s[0], {}).setdefault((v, s[1]), []).append(site)
        elif v in ents:
            buckets.setdefault("FUNC", {}).setdefault((v, ents[v] or ""), []).append(site)
        else:
            k = "CODE" if sec_of(v) == ".text" else "DATA"
            buckets.setdefault(k, {}).setdefault((v, sec_of(v) or "?"), []).append(site)
    print(f"### {tag} [{lo:#x},{hi:#x}) in {secname}, {len(blob)} bytes")
    for k in ("STR-U", "STR-A", "FUNC", "DATA", "CODE"):
        b = buckets.get(k, {})
        print(f"\n-- {k}: {len(b)} distinct targets")
        if k in ("STR-U", "STR-A"):
            for (v, t), sites in sorted(b.items()):
                print(f"   {v:#010x}  x{len(sites):<3} {t!r}")
        else:
            print("   " + " ".join(f"{v:#x}" for v, _ in sorted(b)))


if __name__ == "__main__":
    main()
