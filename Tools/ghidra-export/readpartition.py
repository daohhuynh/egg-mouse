#!/usr/bin/env python3
"""readpartition.py [--queue PATH] — what has actually been READ, per binary.

The question this exists to answer is the one CLAUDE.md 6 insists be stated as a
partition: for each binary, is every sized function either covered by a reading
queue or settled mechanically, and if not, HOW MANY are neither and which.

WHY IT EXISTS. Work-list item 3's plan queued, per binary, the bodies "unique to
that binary" -- present in it and in none of the other eight. That is a sound
way to vary which portion of each binary gets read, and it has a hole that the
per-binary counts cannot show: a body shared by exactly two non-fw110 binaries
is unique to neither, so it lands in no queue at all. fw104 and cfg100 are the
same code base, and 1,639 bodies are shared by exactly those two. Nothing was
wrong with any individual number; the remainder was never computed.

METHOD, and it is deliberately blunt:

  covered  = the function's normalised body hash (classify.norm) is in the union
             of every reading queue -- fw110's readqueue plus the five
             unique_<tag> sets, mapped through their own binaries to hashes.
  settled  = microread.py settled it IN THIS BINARY. Per-binary, never pooled:
             microread names instruction shapes, and a body is only settled
             where the tool actually ran.
  open     = neither. This is the number that matters and it is printed per
             binary, with the union of distinct open bodies at the end.

FUNCTIONS UNDER 16 BYTES HAVE NO HASH. classify.py's sigs() skips them because
tiny bodies collide meaninglessly, so they can never be "covered" -- they must
be settled or read individually. Treating them as covered is how 39 of fw110's
functions disappeared into a subtraction (updater-protocol.md 6.2.5b).

.analysis/handread.json, if present, maps a tag to entry addresses read BY HAND
and written up in the notes. Those count as read. It exists because the only way
to clear a sub-16-byte function is to read it individually, and a queue that
keeps re-offering functions already read in a commit is a queue nobody trusts.

--queue PATH writes the union of open normalised bodies, each with one
representative (tag, entry, size), as a reading queue.
"""
import hashlib, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from classify import norm

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXP = os.path.join(ROOT, ".analysis", "export")
AN = os.path.join(ROOT, ".analysis")
TAGS = ("fw110", "fw107", "fw106", "fw104",
        "cfg107", "cfg104", "cfg101", "cfg100", "xm1r")
UNIQUE = ("cfg107", "cfg104", "cfg101", "cfg100", "fw104", "xm1r")


def bodyhash(r):
    if not r.get("bytes_hex") or r["size"] < 16:
        return None
    return hashlib.sha256(norm(bytes.fromhex(r["bytes_hex"]))).hexdigest()


def records(tag):
    p = os.path.join(EXP, f"{tag}.jsonl")
    return [json.loads(l) for l in open(p)] if os.path.exists(p) else []


def main():
    read = set(json.load(open(os.path.join(AN, "readqueue_fw110.json"))).keys())
    hp = os.path.join(AN, "handread.json")
    hand = ({t: {e.lower() for e in v} for t, v in json.load(open(hp)).items()}
            if os.path.exists(hp) else {})
    for tag in UNIQUE:
        p = os.path.join(AN, f"unique_{tag}.json")
        if not os.path.exists(p):
            continue
        u = {x.lower() for x in json.load(open(p))}
        for r in records(tag):
            if r["entry"].lower() in u:
                h = bodyhash(r)
                if h:
                    read.add(h)
    print("reading-queue hashes (fw110's queue + the five unique_<tag> sets): %d"
          % len(read))
    print()
    print("%-8s %7s %8s %8s %6s %7s %11s" %
          ("binary", "sized", "in-queue", "micrord", "hand", "OPEN", "open bytes"))
    openrep = {}
    for tag in TAGS:
        mp = os.path.join(AN, f"microread_{tag}.json")
        settled = ({k.lower() for k in json.load(open(mp))["settled"]}
                   if os.path.exists(mp) else set())
        n = q = m = h_ = 0
        ob = 0
        handt = hand.get(tag, set())
        for r in records(tag):
            if not r["size"]:
                continue
            n += 1
            h = bodyhash(r)
            if h and h in read:
                q += 1
            elif r["entry"].lower() in settled:
                m += 1
            elif r["entry"].lower() in handt:
                h_ += 1
            else:
                ob += r["size"]
                key = h or f"{tag}:{r['entry']}"      # unhashable: keep per-binary
                openrep.setdefault(key, (tag, r["entry"], r["size"]))
        note = "" if os.path.exists(mp) else "   (microread.py never run for this tag)"
        print("%-8s %7d %8d %8d %6d %7d %11d%s"
              % (tag, n, q, m, h_, n - q - m - h_, ob, note))
    print()
    print("distinct OPEN bodies, union over all binaries: %d" % len(openrep))
    if "--queue" in sys.argv:
        out = sys.argv[sys.argv.index("--queue") + 1]
        json.dump({k: v for k, v in sorted(openrep.items())}, open(out, "w"), indent=0)
        print("->", out)


if __name__ == "__main__":
    main()
