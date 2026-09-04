#!/usr/bin/env python3
"""verify_read.py <batchdir> <journal.jsonl> — check agent readers against the bytes.

CLAUDE.md 1.6 says agents are useful, not authoritative, and the completion
policy says a claim has to be checkable by someone who does not trust the person
making it. A reading pass over thousands of function bodies is worthless if its
error rate is unknown, so this recomputes -- from the same disassembly the agent
was shown -- the two fields that CAN be checked mechanically, and reports the
disagreement rate.

WHAT IS CHECKED
  ids        every id returned must exist in the batch, and every function in
             the batch must come back. Invented ids and silent omissions are the
             two failures that would make a coverage claim false rather than
             merely inaccurate, so they are counted separately from field errors.
  calls      every direct call/jmp target printed in the body
  imports    every indirect call through an absolute address

WHAT IS NOT CHECKED, and this is the honest limit: `summary`, `category`,
`vendor_specific` and `settings_relevant` are judgements. Nothing here validates
them. A 0% mechanical error rate is evidence the reader looked at the right
bytes; it is NOT evidence the summary is right. Do not let this tool's output be
quoted as if it were.

PLANTED POSITIVES (`--plants PATH`). CLAUDE.md 6.2: a harness that cannot
produce a bad result is not evidence, so each run seeds batches with functions
that ARE device-facing, unlabelled, and the miss rate on them is the measured
false-negative rate for the judgement fields. PATH is a JSON object mapping the
planted id to {"tag", "batch", and optionally "expect": [field, ...]} -- the
default expectation is `firmware_relevant`. The report prints per-plant hit or
miss and the rate; **a rate of zero misses across a small plant set is a weak
result, not a strong one**, and the tool says so rather than letting the reader
of its output forget it.

Plant only functions whose device nature is visible IN THEIR OWN DISASSEMBLY.
fw110's plant at 0x004012a0 failed because the honest answer needed the reader
to know that `calll *0x51b1d4` resolves to HidD_SetFeature, which symbol-free
disassembly cannot show. That was a bad question, not a bad reader.

The journal is the workflow's own record (`journal.jsonl` under the run's
transcript directory); results are read from it rather than from the workflow's
return value, which is capped at 4,096 elements.
"""
import json, os, re, sys
from collections import Counter

# Anchored deliberately. Two extractor bugs were found the first time this ran
# and both inflated the apparent error rate, i.e. they would have libelled the
# readers:
#   * `calll *0x34(%eax)` is a VTABLE call, not an absolute import slot. An
#     unanchored `\*(0x[0-9a-f]+)` matches the displacement and manufactures
#     "import slots" 0x8, 0x34, 0x208 ... IND therefore requires end-of-operand.
#   * `jmp 0x45f17f` inside a function body is a local branch, not a call. The
#     schema said "call/jmp target" and the readers reasonably heard "call", so
#     the two are counted separately and only CALL is scored.
CALL = re.compile(r'\b(?:calll|call)\s+(-?0x[0-9a-fA-F]+)\s*(?:<[^>]*>)?\s*$')
JMP = re.compile(r'\b(?:jmp|jmpl)\s+(-?0x[0-9a-fA-F]+)\s*(?:<[^>]*>)?\s*$')
IND = re.compile(r'\b(?:calll|call|jmp|jmpl)\s+\*(-?0x[0-9a-fA-F]+)\s*$')


def truth(fn):
    """Recompute the mechanical fields from the disassembly text."""
    calls, imps, jmps = set(), set(), set()
    for line in fn["disasm"]:
        body = line.split('#')[0].rstrip()
        m = IND.search(body)
        if m:
            imps.add(m.group(1).lower())
            continue
        m = CALL.search(body)
        if m:
            calls.add(m.group(1).lower())
            continue
        m = JMP.search(body)
        if m:
            jmps.add(m.group(1).lower())
    return calls, imps, jmps


# Readers report the address in whatever shape the field description suggested.
# The xm1r prompt's example was written as `calll *0x66f534`, and readers copied
# the whole instruction, which made 257 functions "disagree" when every one of
# them had the right address. Pull the last hex literal out of whatever arrived
# rather than scoring the packaging.
ADDR = re.compile(r'(-?0x[0-9a-fA-F]+)')


def norm(xs):
    out = set()
    for x in xs or []:
        x = str(x).strip().lower().lstrip('*')
        m = ADDR.findall(x)
        if m:
            x = m[-1]
        if x.startswith('0x'):
            try:
                out.add(hex(int(x, 16)))
            except ValueError:
                out.add(x)
        else:
            out.add(x)
    return out


def main():
    batchdir, journal = sys.argv[1], sys.argv[2]
    # An entry address is NOT unique across binaries -- cfg101 and cfg104 share
    # most of theirs -- and the agents only ever see and report `id`. So a
    # reported answer is scored against EVERY candidate with that id and counts
    # as correct if it matches any one of them. The ambiguous count is printed
    # so the allowance is visible rather than silent.
    byid = {}
    for name in sorted(os.listdir(batchdir)):
        # Agents write scratch files into the batch directory -- read6 gained
        # ann.py, out.json, calls_033.json and, fatally for a prefix-only test,
        # batch_000.txt. Require the exact shape the generator emits. (No batch
        # input was ever overwritten: every batch_*.json in all four directories
        # shares a single mtime, the generator's.)
        if not re.fullmatch(r"batch_\d{3}\.json", name):
            continue
        for fn in json.load(open(os.path.join(batchdir, name))):
            byid.setdefault(fn["id"].lower(), []).append(fn)

    reported = {}
    for line in open(journal):
        try:
            rec = json.loads(line)
        except ValueError:
            continue
        if rec.get("type") != "result":
            continue
        val = rec.get("value") or rec.get("result") or {}
        if isinstance(val, str):
            try:
                val = json.loads(val)
            except ValueError:
                continue
        for f in (val or {}).get("functions", []) or []:
            fid = str(f.get("id", "")).strip().lower()
            if fid:
                reported.setdefault(fid, f)

    stats = Counter()
    bad_calls, bad_imps = [], []
    invented = [fid for fid in reported if fid not in byid]
    missing = [fid for fid in byid if fid not in reported]

    ambiguous = sum(1 for v in byid.values() if len(v) > 1)
    for fid, f in reported.items():
        cands = byid.get(fid)
        if not cands:
            continue
        rc, ri = norm(f.get("calls")), norm(f.get("imports_used"))
        best = None
        for fn in cands:
            tc, ti, tj = (norm(x) for x in truth(fn))
            # a reported "call" that is really a local jmp target is tolerated:
            # the schema asked for both, so it is not an invention.
            score = len(rc - tc - tj) + len(tc - rc) + len(ri - ti) + len(ti - ri)
            if best is None or score < best[0]:
                best = (score, tc, ti, tj)
        _, tc, ti, tj = best
        stats["call_truth"] += len(tc)
        stats["imp_truth"] += len(ti)
        extra_c, miss_c = rc - tc - tj, tc - rc
        extra_i, miss_i = ri - ti, ti - ri
        if extra_c or miss_c:
            stats["call_fn_wrong"] += 1
            stats["call_extra"] += len(extra_c)
            stats["call_missed"] += len(miss_c)
            if len(bad_calls) < 20:
                bad_calls.append((fid, sorted(extra_c), sorted(miss_c)))
        if extra_i or miss_i:
            stats["imp_fn_wrong"] += 1
            stats["imp_extra"] += len(extra_i)
            stats["imp_missed"] += len(miss_i)
            if len(bad_imps) < 20:
                bad_imps.append((fid, sorted(extra_i), sorted(miss_i)))

    n = len(byid)
    print(f"batch functions .......... {sum(len(v) for v in byid.values())}"
          f"  ({n} distinct ids, {ambiguous} shared by >1 binary)")
    print(f"reported by agents ....... {len(reported)}")
    print(f"INVENTED ids ............. {len(invented)}  {invented[:10]}")
    print(f"MISSING (never reported) . {len(missing)}  {missing[:10]}")
    print(f"call targets in truth .... {stats['call_truth']}")
    print(f"  functions disagreeing .. {stats['call_fn_wrong']}"
          f"  extra {stats['call_extra']}  missed {stats['call_missed']}")
    print(f"import slots in truth .... {stats['imp_truth']}")
    print(f"  functions disagreeing .. {stats['imp_fn_wrong']}"
          f"  extra {stats['imp_extra']}  missed {stats['imp_missed']}")
    for fid, extra, miss in bad_calls[:10]:
        print(f"   calls  {fid} extra={extra} missed={miss}")
    for fid, extra, miss in bad_imps[:10]:
        print(f"   imps   {fid} extra={extra} missed={miss}")
    if "--plants" in sys.argv:
        pp = json.load(open(sys.argv[sys.argv.index("--plants") + 1]))
        print("\nPLANTED POSITIVES (CLAUDE.md 6.2) -- measured false-negative rate")
        hit = seen = 0
        for pid, meta in sorted(pp.items()):
            fid = pid.strip().lower()
            f = reported.get(fid)
            want = meta.get("expect") or ["firmware_relevant"]
            if f is None:
                print(f"   {pid}  batch {meta.get('batch')}  NOT REPORTED AT ALL")
                seen += 1
                continue
            seen += 1
            got = [w for w in want if f.get(w)]
            ok = len(got) == len(want)
            hit += ok
            print(f"   {pid}  batch {meta.get('batch'):>3}  "
                  f"{'HIT ' if ok else 'MISS'}  "
                  f"want={want} got={{{', '.join(w + '=' + str(f.get(w)) for w in want)}}}  "
                  f"category={f.get('category')!r} confidence={f.get('confidence')!r}")
        if seen:
            print(f"   plants {hit}/{seen} detected; miss rate "
                  f"{100.0 * (seen - hit) / seen:.0f}%")
            if hit == seen:
                print("   NOTE: a clean sweep over a plant set this small is weak "
                      "evidence. It bounds nothing below a miss rate of roughly "
                      f"1-in-{seen}; it is not proof the readers do not miss.")

    print("\nNOTE: summary/category/vendor_specific/settings_relevant are NOT "
          "checked here and cannot be. See the docstring.")


if __name__ == "__main__":
    main()
