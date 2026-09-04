#!/usr/bin/env python3
"""harvest_reads.py — turn workflow read journals into `.analysis/read_<tag>.json`.

`coverage.py` asks each binary "is every exported function accounted for", and
its R (read) evidence set comes from `read_<tag>.json`. Nothing was writing that
file after a reading wave, so cfg107/cfg100/xm1r showed hundreds or thousands of
UNACCOUNTED functions that had, in fact, been read. That is a silent under-count
in the direction that looks like more work rather than less, which is the safe
direction but still wrong.

WHAT COUNTS AS READ, and the second clause is the load-bearing one:

  1. the function's own address was returned by a reader, in the binary it was
     read from; and
  2. **its normalised body hash was read anywhere.** The whole reading plan is
     built on reading one instance of each distinct normalised body -- that is
     what "4,817 distinct bodies covering 5,232 functions" means -- so a twin in
     another binary is covered by construction or the plan never made sense.

Clause 2 is sound for the purpose and NOT sound for the device question.
classify.norm masks absolute VAs, so two functions with one hash can reference
completely different globals; one could touch a HID slot where its twin touches
a string. That is precisely why the device question is settled by `closure.py`
per binary, mechanically, and never by anything a reader said (CLAUDE.md 6.2).
This tool feeds coverage accounting only.

SOURCES. Every `journal.jsonl` under the session's workflow transcript dirs,
paired with the batch directory whose `batch_NNN.json` files name each id's tag.
Ids are matched to batches rather than guessed, because an entry address is not
unique across binaries.

  harvest_reads.py <batchdir>:<journal> [<batchdir>:<journal> ...] [--write]

Without --write it prints what it would do and changes nothing.
"""
import hashlib, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from classify import norm

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXP = os.path.join(ROOT, ".analysis", "export")
AN = os.path.join(ROOT, ".analysis")
TAGS = ("fw110", "fw107", "fw106", "fw104",
        "cfg107", "cfg104", "cfg101", "cfg100", "xm1r")


def _a(x):
    """Addresses arrive as 0x401000, 0x00401000 or an int. Compare as integers;
    scoring the packaging instead of the address is a mistake this repo has
    already made once (verify_read.py's norm())."""
    if x is None:
        return None
    if isinstance(x, int):
        return x
    x = str(x).strip().lower()
    try:
        return int(x, 16)
    except ValueError:
        return None


def bodyhash(r):
    if not r.get("bytes_hex") or r["size"] < 16:
        return None
    return hashlib.sha256(norm(bytes.fromhex(r["bytes_hex"]))).hexdigest()


def main():
    pairs = [a for a in sys.argv[1:] if ":" in a and not a.startswith("--")]
    if not pairs:
        sys.exit(__doc__)

    # id -> tag, from the batch inputs
    idtag = {}
    for spec in pairs:
        bd = spec.rsplit(":", 1)[0]
        for name in sorted(os.listdir(bd)):
            if not re.fullmatch(r"batch_\d{3}\.json", name):
                continue
            for fn in json.load(open(os.path.join(bd, name))):
                idtag.setdefault(_a(fn["id"]), set()).add(fn.get("tag"))

    reported = set()
    for spec in pairs:
        jp = spec.rsplit(":", 1)[1]
        if not os.path.exists(jp):
            print(f"  (no journal {jp})")
            continue
        n = 0
        for line in open(jp):
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
                fid = _a(f.get("id"))
                if fid is not None:
                    reported.add(fid)
                    n += 1
        print(f"  {jp}: {n} function results")

    # hashes actually read, via the binary each id came from
    recs = {t: {_a(r["entry"]): r for r in
                (json.loads(l) for l in open(os.path.join(EXP, f"{t}.jsonl")))}
            for t in TAGS if os.path.exists(os.path.join(EXP, f"{t}.jsonl"))}
    readhash = set()
    direct = {t: set() for t in recs}
    for fid in reported:
        # A batch generated before the `tag` field existed leaves {None} here,
        # and {None} is TRUTHY -- so an `or recs` fallback silently attributes
        # 4,343 of fw110's read bodies to nothing at all. Drop the Nones first.
        cand = {t for t in (idtag.get(fid) or ()) if t in recs}
        for t in (cand or recs):
            r = recs.get(t, {}).get(fid)
            if not r:
                continue
            direct[t].add(fid)
            h = bodyhash(r)
            if h:
                readhash.add(h)
    print(f"\ndistinct normalised bodies read: {len(readhash)}")

    print("\n%-8s %8s %10s %10s" % ("binary", "direct", "by-hash", "total"))
    for t in TAGS:
        if t not in recs:
            continue
        out = set(direct[t])
        for e, r in recs[t].items():
            h = bodyhash(r)
            if h and h in readhash:
                out.add(e)
        print("%-8s %8d %10d %10d" % (t, len(direct[t]), len(out) - len(direct[t]), len(out)))
        if "--write" in sys.argv:
            p = os.path.join(AN, f"read_{t}.json")
            prev = ({("0x%08x" % _a(v)) for v in json.load(open(p))}
                    if os.path.exists(p) else set())
            json.dump(sorted(prev | {"0x%08x" % e for e in out}), open(p, "w"))
    if "--write" in sys.argv:
        print("\nwrote .analysis/read_<tag>.json (union with whatever was there)")
    else:
        print("\n(dry run; pass --write to update .analysis/read_<tag>.json)")


if __name__ == "__main__":
    main()
