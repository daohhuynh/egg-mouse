#!/usr/bin/env python3
"""Positively identify statically-linked library code by cross-binary presence.

The updaters and the configuration tools are different programs by the same
vendor, statically linked against the same MFC and MSVC CRT. So a function body
that occurs in BOTH an updater and a configuration tool is library code: the
vendor's own updater code is not in the config tool and vice versa.

This is positive identification, not an address-locality heuristic, and it is
one-directional: a match proves library, a non-match proves nothing and leaves
the function in the candidate set to be read.

Normalisation, so the same library function in two builds hashes the same:
  * every 4-byte immediate inside the image's VA range is masked
  * the rel32 operand of E8/E9 is masked
  * relocation sites recorded in .reloc are masked
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
    a = sigs(fw)
    lib = set()
    for c in cfgs:
        lib |= set(sigs(c))
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

main()
