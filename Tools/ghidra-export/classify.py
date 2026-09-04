#!/usr/bin/env python3
"""Positively identify statically-linked library code by cross-binary presence.

The updaters and the configuration tools are different programs statically
linked against the same MFC and MSVC CRT, so a normalised function body that
occurs in both is very likely library code.

VALIDATED 2026-09-03 against fw110; results and limits, so nobody trusts this
further than it has been shown to go:

  * Of the twelve confirmed device-facing functions in updater 1.10 (0x401000,
    0x4012a0, 0x401330, 0x401890, 0x401980, 0x401ad0, 0x401bb0, 0x401c90,
    0x403200, 0x403600, 0x403750, 0x403960) it matched ZERO. No protocol
    function was falsely called library.
  * It DOES match 54 of the 79 sized functions in [0x401000,0x4040ad). That is
    correct, not a false positive: that address range holds the vendor's code
    AND the ATL/MFC template instantiations the compiler emitted alongside it
    (Ghidra names several of them - CStringT<...>, GetManager, Empty).
    Corollary worth remembering: **that address range is not "the vendor band"
    in the sense of containing only vendor code.**
  * Internal collisions in fw110: 147 normalised hashes cover >1 function, 707
    functions in total, and 126 of the 147 involve bodies under 64 bytes. Only
    21 have a smallest body >= 64 bytes, and the largest (1361 bytes) is
    __ld12tod against __ld12tod - the same CRT routine emitted twice. Every
    collision inspected was a genuine duplicate.
  * Normalisation zeroes 27.1% of all body bytes. That is a lot of destroyed
    information; ~73% still discriminates, and the collision data above says
    that is empirically enough, but do not raise the masked range casually.

THE PREMISE IS AN ASSUMPTION, NOT A PROOF. This tool is sound only if the vendor
shares no source between the updater and the config tool. That is UNPROVEN, and
there is direct reason to doubt it: the two tools use the same transport design
- same two report IDs and lengths, same byte-1 framing, the same five
GetLastError retry codes (notes/config-protocol.md 1). If a shared source file
were compiled into both, its functions would byte-match and be silently labelled
"library" - and those would be exactly the protocol functions that matter.

So: a match is EVIDENCE of library code, not proof. Never let a match alone
exclude a function that is device-adjacent. The guard that actually holds is the
independent confinement proof - every reference to every HID/SetupAPI import is
enumerated by raw byte scan, and every referencing function is read regardless
of what this tool says about it.

Normalisation, so the same library function in two builds hashes the same:
  * the rel32 operand of E8/E9 is masked
  * any 4 bytes anywhere in the body that read as a VA in [0x400000,0x700000)
    are masked - unaligned, so this can mask non-operand bytes by coincidence
  * bodies under 16 bytes are skipped; they collide meaninglessly
  * NOTE: an earlier version of this docstring claimed .reloc sites are masked.
    They are not. No .reloc parsing happens here. Claim removed rather than
    implemented, because the E8/E9 + VA masking is what was actually validated.
"""
import json, sys, os, struct, hashlib, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXP = os.path.join(ROOT, ".analysis", "export")

def norm(b):
    """Mask anything that a different link would change."""
    d = bytearray(b)
    n = len(d)
    i = 0
    while i < n:
        # rel32 branch/call operands
        if d[i] in (0xE8, 0xE9) and i + 5 <= n:
            d[i+1:i+5] = b'\0\0\0\0'
            i += 5
            continue
        i += 1
    # absolute VAs anywhere in the body
    for i in range(max(0, n - 3)):
        v = int.from_bytes(d[i:i+4], 'little')
        if 0x00400000 <= v < 0x00700000:
            d[i:i+4] = b'\0\0\0\0'
    return bytes(d)

def sigs(tag):
    out = {}
    for l in open(os.path.join(EXP, f"{tag}.jsonl")):
        r = json.loads(l)
        bh = r.get('bytes_hex')
        if not bh or r['size'] < 16:      # tiny bodies collide meaninglessly
            continue
        out.setdefault(hashlib.sha256(norm(bytes.fromhex(bh))).hexdigest(), []).append(r['entry'])
    return out

def main():
    fw = sys.argv[1]
    cfgs = sys.argv[2:]
    if not cfgs:
        sys.exit("usage: classify.py <target-tag> <reference-tag>...")
    a = sigs(fw)
    lib = set()
    for c in cfgs:
        cs = sigs(c)
        # A reference that shares nearly every body with the target is the same
        # code, not an independent witness: it self-matches and inflates the
        # library count while proving nothing. fw106/fw107 have .text
        # byte-identical to fw110 and are exactly this case. coverage.py refuses
        # it for the same reason; refusing it here too keeps the rule mechanical
        # rather than something a reader has to remember.
        share = len(set(cs) & set(a)) / max(1, len(a))
        if share > 0.90:
            sys.exit(f"REFUSED: reference '{c}' shares {share:.1%} of '{fw}' bodies. "
                     f"That is the same code base, not an independent witness.")
        lib |= set(cs)
    recs = [json.loads(l) for l in open(os.path.join(EXP, f"{fw}.jsonl"))]
    by = {r['entry']: r for r in recs}
    matched = set()
    for h, entries in a.items():
        if h in lib:
            matched.update(entries)
    unnamed = [r for r in recs if r['name'].startswith('FUN_')]
    un_matched = [r for r in unnamed if r['entry'] in matched]
    un_left = [r for r in unnamed if r['entry'] not in matched]
    print(f"{fw}: total={len(recs)} unnamed={len(unnamed)}")
    print(f"  positively library by cross-binary match: {len(un_matched)}")
    print(f"  REMAINING CANDIDATES (must be read): {len(un_left)}")
    with open(os.path.join(ROOT, ".analysis", f"candidates_{fw}.json"), "w") as f:
        json.dump(sorted(r['entry'] for r in un_left), f)
    lo = [r for r in un_left if int(r['entry'],16) < 0x410000]
    print(f"  of those, below 0x410000: {len(lo)}")
    # State the partition, not a headline number: CLAUDE.md 6 requires the cells
    # to sum to the total in one place, so a scope slip cannot hide in prose.
    tiny = [r for r in un_left if r['size'] < 16]
    sub  = [r for r in un_left if r['size'] >= 16]
    print(f"  PARTITION: {len(tiny)} under 16 bytes + {len(sub)} substantive "
          f"= {len(tiny)+len(sub)} (must equal {len(un_left)})")
    assert len(tiny) + len(sub) == len(un_left)
    print(f"  reference set: {' '.join(cfgs)}")

if __name__ == "__main__":
    main()
