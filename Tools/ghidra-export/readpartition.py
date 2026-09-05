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

QUEUES COUNTED. `readqueue_fw110.json`, the five `unique_<tag>.json` sets, and
-- added 2026-09-04 -- `openqueue.json`, which is this tool's OWN output from
the run that discovered the remainder. That last one is deliberately circular
and it is safe only because the queue was actually read: it names the 4,497
bodies that belonged to no other queue, and counting them before their read
lands would turn a real hole into a clean row. Delete the file, or the read,
and the hole reappears in the next run. `readqueue_xm1r.json` is a list of
ENTRY ADDRESSES rather than hashes and is mapped through xm1r's own records.
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
    op = os.path.join(AN, "openqueue.json")
    if os.path.exists(op):
        read |= set(json.load(open(op)).keys())
    xq = os.path.join(AN, "readqueue_xm1r.json")
    if os.path.exists(xq):
        want = {e.lower() for e in json.load(open(xq))}
        for r in records("xm1r"):
            if r["entry"].lower() in want:
                h = bodyhash(r)
                if h:
                    read.add(h)
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
    print("reading-queue hashes (fw110 + five unique_<tag> + openqueue + xm1r): %d"
          % len(read))

    # IN-QUEUE MEANS QUEUED, NOT READ. Counting a queue whose read has not
    # landed turns a real hole into a clean row, so say so out loud: for every
    # queue file, how much of it is actually present in read_<tag>.json.
    #
    # A HAND READ IS A READ. handread.json holds functions read by hand and
    # written up in the notes, and the OPEN column has always honoured it --
    # but this block did not, so every hand-read function counted as
    # unharvested. On 2026-09-05 that made readqueue_fw110.json look 70 bodies
    # short when 8 of those were hand-read, recorded, and written up in
    # updater-protocol.md, 0x00403960 (the flash sequence) and 0x00403200 (the
    # FWFILE load) among them. The two sources must agree or the tool argues
    # with itself, and the alarming direction is the one that gets believed.
    readhash = set()
    hand = {}
    hp = os.path.join(AN, "handread.json")
    if os.path.exists(hp):
        with open(hp) as fh:
            hand = {k: {x.lower() for x in v} for k, v in json.load(fh).items()}
    for t in TAGS:
        done = set(hand.get(t, ()))
        rp = os.path.join(AN, f"read_{t}.json")
        if os.path.exists(rp):
            with open(rp) as fh:
                done |= {e.lower() for e in json.load(fh)}
        if not done:
            continue
        for r in records(t):
            if r["entry"].lower() in done:
                h = bodyhash(r)
                if h:
                    readhash.add(h)
    # A queue can be short for two completely different reasons and the label
    # has to say which, or the harmless one gets investigated. Added 2026-09-05
    # after fw110 sat at "*** INCOMPLETE 70 short" for a day: 37 of those were
    # bodies under 16 bytes that classify.sigs() refuses to hash, so they can
    # never match no matter how often they are read, and 8 more were hand-read
    # and recorded. Only 27 were real, and one of those was not a function.
    def status(unmatched_reps, tag_of):
        """unmatched_reps: [(tag, entry, size)] for every body not hash-matched."""
        artefact, real = 0, 0
        for rep in unmatched_reps:
            t, entry = rep[0], rep[1]
            done = set(hand.get(t, ()))
            rp2 = os.path.join(AN, f"read_{t}.json")
            if os.path.exists(rp2):
                with open(rp2) as fh2:
                    done |= {e.lower() for e in json.load(fh2)}
            if entry.lower() in done:
                artefact += 1          # read; simply not hashable/harvested
            else:
                real += 1
        if not unmatched_reps:
            return "OK"
        if real == 0:
            return "all %d short are READ but unhashable -- artefact, not a task" % artefact
        return "*** %d UNREAD (+%d read-but-unhashable)" % (real, artefact)

    print("\nQUEUE STATUS -- 'in-queue' above means QUEUED; harvested means a "
          "reader\nactually returned it (harvest_reads.py --write):")
    qf = [("readqueue_fw110.json", "hashes"), ("openqueue.json", "hashes")]
    for name, kind in qf:
        fp = os.path.join(AN, name)
        if not os.path.exists(fp):
            continue
        with open(fp) as fh:
            q = json.load(fh)
        ks = set(q.keys())
        d = len(ks & readhash)
        # TWO SHAPES. readqueue_fw110.json maps hash -> [[tag,entry,size], ...];
        # openqueue.json maps hash -> [tag,entry,size], flat. Slicing the flat
        # one as if it were nested yields ('f','w','1') and every body then
        # looks unread. Normalise instead of assuming.
        reps = []
        for h in ks - readhash:
            v = q[h]
            if not v:
                continue
            reps.append(tuple(v[:3]) if isinstance(v[0], str) else tuple(v[0])[:3])
        print("  %-26s %6d bodies, %6d harvested  %s"
              % (name, len(ks), d, status(reps, None)))
    for tag in UNIQUE:
        fp = os.path.join(AN, f"unique_{tag}.json")
        if not os.path.exists(fp):
            continue
        with open(fp) as fh:
            u = {x.lower() for x in json.load(fh)}
        rec = [r for r in records(tag) if r["entry"].lower() in u]
        hs = {bodyhash(r) for r in rec}
        hs.discard(None)
        d = len(hs & readhash)
        # ONE representative per distinct body, so real+artefact reconciles
        # with (bodies - harvested). Counting records instead double-counts
        # twins and made xm1r read 5511 against a 4909 shortfall.
        seen_h = {}
        for r in rec:
            h2 = bodyhash(r)
            if h2 and h2 not in readhash:
                seen_h.setdefault(h2, (tag, r["entry"], int(r["size"])))
        reps = list(seen_h.values())
        print("  %-26s %6d bodies, %6d harvested  %s"
              % (f"unique_{tag}.json", len(hs), d, status(reps, tag)))
    fp = os.path.join(AN, "readqueue_xm1r.json")
    if os.path.exists(fp):
        with open(fp) as fh:
            u = {x.lower() for x in json.load(fh)}
        rec = [r for r in records("xm1r") if r["entry"].lower() in u]
        hs = {bodyhash(r) for r in rec}
        hs.discard(None)
        d = len(hs & readhash)
        seen_h = {}
        for r in rec:
            h2 = bodyhash(r)
            if h2 and h2 not in readhash:
                seen_h.setdefault(h2, ("xm1r", r["entry"], int(r["size"])))
        reps = list(seen_h.values())
        print("  %-26s %6d bodies, %6d harvested  %s"
              % ("readqueue_xm1r.json", len(hs), d, status(reps, "xm1r")))
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

    # OPEN 0 DOES NOT MEAN EVERYTHING IS READ, and on 2026-09-05 it was read
    # that way. OPEN counts a body as covered when it is in a reading QUEUE;
    # a queue that was never harvested still shows OPEN 0. So print the
    # difference here rather than leaving it to be noticed in the block above,
    # and split it, because the two halves need opposite responses.
    unhashable, unread = [], []
    for tag in ("fw110",):
        qp = os.path.join(AN, f"readqueue_{tag}.json")
        if not os.path.exists(qp):
            continue
        with open(qp) as fh:
            q = json.load(fh)
        handt = set(hand.get(tag, ()))
        rp = os.path.join(AN, f"read_{tag}.json")
        if os.path.exists(rp):
            with open(rp) as fh:
                handt |= {e.lower() for e in json.load(fh)}
        for h, reps in q.items():
            if h in readhash:
                continue
            mine = [r for r in reps if r[0] == tag]
            if any(r[1].lower() in handt for r in mine):
                continue
            (unhashable if max(r[2] for r in reps) < 16 else unread).append(
                (mine[0][1] if mine else reps[0][1], max(r[2] for r in reps)))
    if unhashable or unread:
        print("\nfw110 HARVEST RESIDUE -- what OPEN cannot show:")
        print("  %4d sub-16-byte bodies. classify.sigs() will not hash these, so they"
              % len(unhashable))
        print("       can NEVER be counted harvested. Permanent artefact, not a task;")
        print("       clear them via handread.json, one address at a time.")
        print("  %4d bodies of >=16 bytes, %d bytes, GENUINELY UNREAD."
              % (len(unread), sum(s for _, s in unread)))
        if unread:
            print("       largest: %s" % ", ".join(
                "%s(%dB)" % (a, s) for a, s in sorted(unread, key=lambda x: -x[1])[:6]))

    if "--queue" in sys.argv:
        out = sys.argv[sys.argv.index("--queue") + 1]
        json.dump({k: v for k, v in sorted(openrep.items())}, open(out, "w"), indent=0)
        print("->", out)


if __name__ == "__main__":
    main()
