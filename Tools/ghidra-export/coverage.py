#!/usr/bin/env python3
"""Assert that coverage of a binary is a PARTITION of its function total.

  coverage.py <tag>

WHY THIS EXISTS. On 2026-09-03 a ~40-function "gap" in updater 1.10's accounting
turned out to be pure bookkeeping: the band workflow read all 95 functions in
[0x401000,0x4040ad), classify.py produced its candidate list WITHOUT excluding
that range, so 40 functions sat in both scopes -- and coverage had been reported
as arithmetic between two independently-scoped numbers rather than as one
computation over a known total. Nothing was unread. The claim was just unsound.

The rule this enforces: never state coverage as "we did X, and separately Y".
State it as total - (union of every evidence set) == 0, and print the residue.
Overlap then becomes visible instead of looking like a shortfall, and a real
hole becomes impossible to describe as anything else.

Second-order point, which is the one that actually bit: numbers written in prose
survive a context compaction while the computation that produced them does not,
so they resurface looking like established fact. Regenerate; do not quote.

Evidence sets, weakest last:
  R  read        - an agent or a human actually read the body
  M  byte match  - normalised body occurs in another product's binary (see
                   classify.py, whose premise is an assumption, not a proof)
  N  Ghidra FID  - named by Ghidra's FunctionID signature database, ALONE

Exit status is 1 if anything is unaccounted, so this can gate a commit.
"""
import json, sys, os, hashlib, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXP  = os.path.join(ROOT, ".analysis", "export")
AN   = os.path.join(ROOT, ".analysis")

def norm(b):
    d = bytearray(b); n = len(d); i = 0
    while i < n:
        if d[i] in (0xE8, 0xE9) and i + 5 <= n:
            d[i+1:i+5] = b'\0\0\0\0'; i += 5; continue
        i += 1
    for i in range(max(0, n - 3)):
        if 0x00400000 <= int.from_bytes(d[i:i+4], 'little') < 0x00700000:
            d[i:i+4] = b'\0\0\0\0'
    return bytes(d)

def load(tag):
    return [json.loads(l) for l in open(os.path.join(EXP, f"{tag}.jsonl"))]

def bodyhash(r):
    bh = r.get('bytes_hex')
    if not bh or r['size'] < 16:
        return None
    return hashlib.sha256(norm(bytes.fromhex(bh))).hexdigest()

def readset(tag):
    """Every address any workflow actually read, unioned. Add files here as
    more reading happens -- a missing file is a silent under-count, so this
    warns rather than skipping quietly."""
    out = set()
    for fn in (f"{tag}_band.json", f"sweep_{tag}.json",
               f"sweep_{tag}_big.json", f"sweep_{tag}_tiny.json",
               f"read_{tag}.json"):
        p = os.path.join(AN, fn)
        if not os.path.exists(p):
            print(f"  (no {fn})")
            continue
        for x in json.load(open(p)):
            out.add(int(x, 16) if isinstance(x, str) else x)
    return out

def main():
    tag = sys.argv[1]
    refs = [t for t in ("fw110","fw107","fw106","fw104",
                        "cfg107","cfg104","cfg101","cfg100","xm1r")
            if t != tag and os.path.exists(os.path.join(EXP, f"{t}.jsonl"))]
    recs  = load(tag)
    byadr = {int(r['entry'], 16): r for r in recs}
    ALL   = set(byadr)

    # A reference whose code base IS this one proves nothing: 1.06/1.07/1.10 have
    # byte-identical .text, so every function would "match" itself. Caught on
    # 2026-09-03 when adding them silently drove the Ghidra-only count to zero,
    # i.e. the tool manufactured confidence. Exclude any reference that covers
    # nearly all of the target's bodies, and SAY SO rather than dropping quietly.
    self_h = {h for h in (bodyhash(r) for r in recs) if h}
    lib, used = set(), []
    for t in refs:
        th = {h for h in (bodyhash(r) for r in load(t)) if h}
        overlap = len(self_h & th) / max(1, len(self_h))
        if overlap > 0.90:
            print(f"  EXCLUDED ref {t}: shares {overlap:.1%} of bodies "
                  f"-- same code base, self-matching proves nothing")
            continue
        used.append(t); lib |= th
    refs = used

    R = readset(tag) & ALL
    M = {a for a, r in byadr.items() if bodyhash(r) in lib}
    N = {a for a, r in byadr.items() if not r['name'].startswith('FUN_')}

    acc  = R | M | N
    resid = ALL - acc
    print(f"\n{tag}: {len(ALL)} functions;  refs actually used: {','.join(refs)}")
    print(f"  R read ................. {len(R)}")
    print(f"  M cross-binary match ... {len(M)}")
    print(f"  N Ghidra FID name ...... {len(N)}")
    print(f"  accounted (R|M|N) ...... {len(acc)}")
    print(f"  UNACCOUNTED ............ {len(resid)}")

    # SCOPE. Everything above partitions GHIDRA'S FUNCTION LIST, not the binary.
    # Those are not the same set and the difference is not small: roughly a tenth
    # of each .text is code that lies inside no exported function body, so a
    # function reachable only through an MFC message map (cfg107 0x413f90, the
    # Factory Reset handler) is not in ALL, is not in the residue, and cannot be
    # reported here at all. "UNACCOUNTED 0" read as a statement about the binary
    # is how a 10% hole stayed invisible; printing the scope every run is the
    # cheapest guard against reading it that way again.
    # The byte-level partition is gapscan.py. This number does not replace it.
    try:
        gp = os.path.join(AN, f"gapscan_{tag}.json")
        d = json.load(open(gp))
        dark = d.get("DARK_pct", d.get("dark_pct"))
        extra = (f"; gapscan.py reports DARK {dark}% of .text bytes"
                 if dark is not None else "")
    except Exception:
        extra = ("; gapscan.py has not been run for this tag -- the byte-level "
                 "coverage is UNKNOWN, not zero")
    print(f"\n  SCOPE: the {len(ALL)} above are Ghidra's exported functions. They do")
    print(f"         NOT cover .text{extra}.")
    print( "         A residue of 0 here means the LIST is accounted for. It is")
    print( "         not a claim about the binary. See CLAUDE.md 6 and 1.2b.")

    weak = N - M - R
    big  = sorted(a for a in weak if byadr[a]['size'] >= 32)
    print(f"\n  resting on Ghidra's name ALONE ... {len(weak)}"
          f"  (>=32 bytes: {len(big)})")
    json.dump([hex(a) for a in big], open(os.path.join(AN, f"{tag}_ghidra_only_big.json"), "w"))

    if resid:
        print("\n  RESIDUE (this must be empty):")
        for a in sorted(resid)[:40]:
            print(f"    {a:#010x} size={byadr[a]['size']} {byadr[a]['name'][:40]}")
        sys.exit(1)

if __name__ == "__main__":
    main()
