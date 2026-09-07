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
  S  settled     - microread.py tiled the body exactly and matched a template
                   whose semantics are total (added 2026-09-04). This is the
                   STRONGEST set here: no judgement is involved. It was missing,
                   which made cfg107 report 877 unaccounted functions that were
                   in fact settled from their bytes, and made this tool disagree
                   with readpartition.py for no reason.
  M  byte match  - normalised body occurs in another product's binary (see
                   classify.py, whose premise is an assumption, not a proof)
  N  Ghidra FID  - named by Ghidra's FunctionID signature database, ALONE
  I  sibling read  - THE SAME BYTES, at the same VA, in a binary whose .text
                   is byte-for-byte identical to this one, and READ there
                   (added 2026-09-06). engineering-rules.md 6.1 already grants this for
                   1.06/1.07 against 1.10 -- "1.10's work covers their code by
                   construction" -- and this makes the grant checkable instead
                   of asserted. Note the deliberate inversion: a >90%-sharing
                   sibling is EXCLUDED from M and ADMITTED for I, because the
                   two sets claim different things. M claims "this body also
                   occurs in an unrelated product, so it is probably library",
                   which self-matching would fake. I claims only "somebody read
                   these exact bytes", which is true no matter which file they
                   were read out of.

Exit status is 1 if anything is unaccounted, so this can gate a commit.

STATUS: FINDING-AID, NOT EVIDENCE (demoted 2026-09-04).

This tool's output must not be cited as showing that a binary is accounted for.
It partitions GHIDRA'S FUNCTION LIST, and that list does not cover .text -- the
function reachable only through an MFC message map that produced the retracted
factory-reset conclusion is not in it, is not in the residue, and cannot be
reported here at all. A residue of 0 means the list is internally consistent.
That is a useful thing to know and it is not a completeness claim.

The completeness evidence is elsewhere and is byte-level:
    filemap.py    every byte of the file, partition must sum
    gapscan.py    every byte of .text into KNOWN/CALLED/PTR/PAD/DARK
    darkclass.py  every DARK byte resolved to a known function
    closure.py    every HID/SetupAPI reference attributed to an owner
    cmdscan.py    every report-id immediate in the whole section
Use this tool to find things to check. Never to conclude nothing is there.
"""
import json, sys, os, hashlib, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calledges import TAGS, PE          # noqa: E402  (path set above)

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

def text_of(tag):
    """(sha256 of the raw .text bytes, its VA, the raw bytes) or None."""
    rel = TAGS.get(tag)
    if not rel:
        return None
    try:
        pe = PE(os.path.join(ROOT, rel))
    except Exception:
        return None
    for s_ in pe.sections:
        if s_["name"] == ".text":
            raw = pe.buf[s_["rptr"]:s_["rptr"] + s_["rsize"]]
            return hashlib.sha256(raw).hexdigest(), pe.image_base + s_["vaddr"], raw
    return None


def sibling_read(tag, byadr, already):
    """Evidence set I. Addresses whose bytes were read in a binary with an
    IDENTICAL .text. Two conditions, both proofs rather than inferences:

      1. the sibling's raw .text hashes to the same SHA-256, so the code is
         the same code, and
      2. the sibling's export has a function at the SAME VA with the SAME
         size, and
      3. this function's own bytes at that VA compare equal.

    (2) is the one that is easy to leave out and wrong to. A read list is a
    list of ADDRESSES; what was actually read is a BODY. If Ghidra split the
    sibling differently -- a tail-call turned into its own function, say -- an
    address could appear in the sibling's read list while covering half of what
    this binary calls that function. Identical `.text` makes that unlikely and
    does not make it impossible, and "unlikely" is not the standard for
    something that lets thousands of functions count as read.

    `already` is the set this adds nothing to (the tag's own R). It is passed
    in rather than subtracted afterwards so the per-sibling count printed below
    is the number of functions that sibling ACTUALLY ADDS. The first version
    printed the sibling's whole read list -- 6009 where the true contribution
    was 764 -- which is precisely the "numbers written in prose" failure this
    file's own docstring warns about, committed inside the tool that warns.

    Returns (set, [note, ...]) so the caller can print what it leaned on. A
    silent set here would be the same defect as a silent reference exclusion.
    """
    mine = text_of(tag)
    if mine is None:
        return set(), ["no .exe for this tag -- I is empty"]
    my_h, my_va, my_raw = mine
    out, notes = set(), []
    for t in TAGS:
        if t == tag:
            continue
        other = text_of(t)
        if other is None or other[0] != my_h:
            continue
        o_h, o_va, o_raw = other
        if o_va != my_va:
            notes.append(f"  I: {t} has identical .text bytes but a different "
                         f"VA ({o_va:#x} vs {my_va:#x}) -- not used")
            continue
        their_R = readset(t, quiet=True)
        their_size = {int(x['entry'], 16): x['size'] for x in load(t)}
        n = mismatched = 0
        for a, r in byadr.items():
            if a in out or a in already or a not in their_R:
                continue
            if their_size.get(a) != r["size"]:
                mismatched += 1
                continue
            off, sz = a - my_va, r["size"]
            if off < 0 or off + sz > len(my_raw):
                continue
            if my_raw[off:off+sz] == o_raw[off:off+sz]:
                out.add(a); n += 1
        if mismatched:
            notes.append(f"  I: {t} read {mismatched} addresses whose function "
                         f"SIZE differs here -- not counted")
        notes.append(f"  I: {t} .text is byte-identical (sha256 "
                     f"{my_h[:16]}...); it ADDS {n} functions "
                     f"beyond what is already read here")
    return out, notes


def readset(tag, quiet=False):
    """Every address any workflow actually read, unioned. Add files here as
    more reading happens -- a missing file is a silent under-count, so this
    warns rather than skipping quietly."""
    out = set()
    for fn in (f"{tag}_band.json", f"sweep_{tag}.json",
               f"sweep_{tag}_big.json", f"sweep_{tag}_tiny.json",
               f"read_{tag}.json"):
        p = os.path.join(AN, fn)
        if not os.path.exists(p):
            if not quiet:
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
    mp = os.path.join(AN, f"microread_{tag}.json")
    if os.path.exists(mp):
        S = {int(k, 16) for k in json.load(open(mp))["settled"]} & ALL
    else:
        print(f"  (no microread_{tag}.json -- run microread.py {tag}; "
              f"until then S is empty and the residue is overstated)")
        S = set()

    M = {a for a, r in byadr.items() if bodyhash(r) in lib}
    N = {a for a, r in byadr.items() if not r['name'].startswith('FUN_')}
    I, inotes = sibling_read(tag, byadr, R)
    for ln in inotes:
        print(ln)

    acc  = R | S | M | N | I
    resid = ALL - acc
    print(f"\n{tag}: {len(ALL)} functions;  refs actually used: {','.join(refs)}")
    print(f"  R read ................. {len(R)}")
    print(f"  I identical-.text read . {len(I)}")
    print(f"  S microread settled .... {len(S)}")
    print(f"  M cross-binary match ... {len(M)}")
    print(f"  N Ghidra FID name ...... {len(N)}")
    print(f"  accounted (R|S|M|N|I) .. {len(acc)}")
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
    print( "         not a claim about the binary. See engineering-rules.md 6 and 1.2b.")

    weak = N - M - R - S - I
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
