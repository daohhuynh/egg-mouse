#!/usr/bin/env python3
"""recmap.py <tag> [<tag> ...]  -- extract a config tool's settings-record
serializer map: which object field feeds which record byte.

    python3 Tools/ghidra-export/recmap.py cfg107
    python3 Tools/ghidra-export/recmap.py cfg100 cfg101 cfg104 cfg107 --diff

WHY THIS EXISTS.  The settings record layout is currently hand-derived from
cfg107 (notes/config-protocol.md 7.x).  A future config tool -- 1.08, 1.11 --
could move a field, and nothing in the file announces that.  Writing a moved
field is writing a wrong byte to the only mouse there is.  This tool recovers
the map mechanically from any version, so "has the layout changed?" is a
question a script answers rather than a question someone remembers to ask.

WHAT IT RECORDS.  Structure only: a record offset, the object offset that feeds
it, and the address of the instruction pair that does it.  It holds no opinion
about what any field MEANS, does not rank, and does not know the name of a
single setting.  See CLAUDE.md 7.1 for why that matters -- the script this
replaces encoded prior conclusions and handed them to everyone who read it.

THE SAFETY PROPERTY, and it is the whole point.  A partial map is more
dangerous than no map, because it looks like a complete one.  The first
prototype of this returned 108 of cfg107's 109 record offsets and did not
complain: it silently missed record 0x72, whose store uses `movb` rather than
`movzbl` (cfg107 0x4045d6), and it missed record 0x00 because its regex
required an explicit displacement.  Had either been a field that MOVED between
versions, we would have written a wrong byte and been confident.

So this tool follows CLAUDE.md 6's partition rule: the mapped set must cover
0x00..max with no holes, computed in one place, residue enumerated, and
`ok == False` on any residue.  It refuses rather than guesses.  A caller that
ignores `ok` has defeated the only thing separating this from the prototype.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

TAGS = {
    "cfg100": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe",
    "cfg101": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe",
    "cfg104": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe",
    "cfg107": "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe",
}

# A byte load from the source struct, into the low half of a scratch register.
#   0f b6 51 06    movzbl 0x6(%ecx), %edx
#   0f b6 11       movzbl (%ecx), %edx
#   8a 49 08       movb   0x8(%ecx), %cl
LOAD = re.compile(
    r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*"
    r"mov(?:zbl|b)\s+(?:0x([0-9a-f]+))?\(%e(\w\w)\), %(?:e(\w\w)|(\w)l)\s*$")

# A byte store into the destination buffer.
#   88 50 05       movb %dl, 0x5(%eax)
#   88 10          movb %dl, (%eax)
STORE = re.compile(
    r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*"
    r"movb\s+%(\w)l, (?:0x([0-9a-f]+))?\(%e(\w\w)\)\s*$")


def disassemble(path):
    out = subprocess.run(["objdump", "-d", path], cwd=ROOT,
                         capture_output=True, text=True)
    return out.stdout.splitlines()


def low8(m):
    """Name the 8-bit register a load wrote, from either capture form."""
    if m.group(4):                      # movzbl ... , %edx  -> 'd'
        return m.group(4)[0]
    return m.group(5)                   # movb   ... , %cl   -> 'c'


def pairs_in(lines):
    """Every (addr, src, dst) load/store pair, in file order."""
    out = []
    for i in range(len(lines) - 1):
        a, b = LOAD.match(lines[i]), STORE.match(lines[i + 1])
        if not (a and b):
            continue
        # STORE groups are (1) address, (2) register, (3) displacement, (4) base.
        # Getting this wrong is silent -- it compares an address string against
        # a register name, matches nothing, and reports "no pairs found".
        if low8(a) != b.group(2):        # the store must use what the load wrote
            continue
        src = int(a.group(2), 16) if a.group(2) else 0
        dst = int(b.group(3), 16) if b.group(3) else 0
        out.append((int(a.group(1), 16), src, dst))
    return out


# Any byte/word/long store into the destination buffer, however encoded. Used
# ONLY to audit the pair matcher: a store in the serializer's body that did not
# become a pair is unexplained, and unexplained is the failure this tool exists
# to make loud.
STORE_ANY = re.compile(
    r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*"
    r"mov(b|w|l)\s+%\w+, (?:0x([0-9a-f]+))?\(%eax\)\s*$")

# Longest instruction pair we expect: a 7-byte movzbl with a 32-bit
# displacement followed by a 6-byte store. The first version of this used 8,
# which split the run at cfg107 0x4043a3 -- a 32-bit-displacement load for
# record 0x22 -- and then discarded the orphan as too short. That is how a
# single record offset went missing without a word of complaint.
MAX_PAIR_GAP = 16


def extract(tag, verbose=False):
    """Recover one binary's record map. Returns a dict; check ['ok'] first.

    `tag` is one of TAGS, or a PATH to any config executable. The path form is
    what the update pipeline uses: a version nobody has read is not in TAGS by
    definition, and requiring it to be added first would mean editing this file
    before the tool that decides whether the file is safe has run.
    """
    path = TAGS.get(tag, tag)
    full = path if os.path.isabs(path) else os.path.join(ROOT, path)
    if not os.path.exists(full):
        return {"tag": tag, "ok": False, "why": "binary not present: " + path}

    lines = disassemble(full)
    ps = pairs_in(lines)
    if not ps:
        return {"tag": tag, "ok": False, "why": "no load/store pairs found at all"}

    runs, cur = [], [ps[0]]
    for p in ps[1:]:
        if p[0] - cur[-1][0] <= MAX_PAIR_GAP:
            cur.append(p)
        else:
            runs.append(cur)
            cur = [p]
    runs.append(cur)
    runs = [r for r in runs if len(r) >= 8]
    if not runs:
        return {"tag": tag, "ok": False, "why": "no run of >=8 pairs; serializer not located"}

    merged, where, collision = {}, {}, []
    for r in runs:
        for addr, src, dst in r:
            if dst in merged and merged[dst] != src:
                collision.append((dst, merged[dst], src))
            merged[dst] = src
            where[dst] = addr

    hi = max(merged)

    # THE AUDIT WINDOW IS THE FUNCTION, NOT THE RUN. Deriving it from the runs
    # is circular: a store the pair matcher failed to see is also a store that
    # moves the run's own boundary, so it lands outside the window and the
    # audit reports nothing wrong. That is exactly how the planted
    # "blind to zero-displacement stores" defect first passed -- losing record
    # 0x00's pair moved lo_addr past the very store that proved it was lost.
    #
    # So walk out to the enclosing function instead: back to the previous
    # `retl`/`int3` padding, forward to this function's `retl`.
    first = min(r[0][0] for r in runs)
    last = max(r[-1][0] for r in runs)
    idx = [(int(m.group(1), 16), i) for i, ln in enumerate(lines)
           if (m := re.match(r"^\s*([0-9a-f]+):\s", ln))]
    addr_of = dict((i, a) for a, i in idx)
    lines_by_i = {i: lines[i] for _, i in idx}
    first_i = min((i for a, i in idx if a >= first), default=0)
    last_i = max((i for a, i in idx if a <= last), default=len(lines) - 1)

    END = re.compile(r"\b(retl|int3)\b")
    lo_i = first_i
    while lo_i > 0 and not END.search(lines_by_i.get(lo_i - 1, "")):
        lo_i -= 1
    hi_i = last_i
    while hi_i < len(lines) - 1 and not END.search(lines_by_i.get(hi_i, "")):
        hi_i += 1

    lo_addr = addr_of.get(lo_i, first)
    hi_addr = addr_of.get(hi_i, last)

    # Every store to the destination buffer inside the function body, by any
    # encoding, must have become a pair. This is what separates "the serializer
    # does not write that byte" from "my parser did not see it" -- they are
    # indistinguishable from the map alone, and only one of them is safe.
    unmatched = []
    for i in range(lo_i, hi_i + 1):
        m = STORE_ANY.match(lines_by_i.get(i, ""))
        if not m:
            continue
        dst = int(m.group(3), 16) if m.group(3) else 0
        width = {"b": 1, "w": 2, "l": 4}[m.group(2)]
        if not all((dst + k) in merged for k in range(width)):
            unmatched.append((int(m.group(1), 16), dst, width))

    # Offsets with no store at all in the body are genuinely not serialised.
    # Enumerated, never silently dropped.
    unwritten = [o for o in range(hi + 1) if o not in merged]

    res = {
        "tag": tag, "map": merged, "at": where, "high": hi,
        "count": len(merged), "unwritten": unwritten, "collisions": collision,
        "unmatched": unmatched, "body": (lo_addr, hi_addr),
        "runs": [(r[0][0], len(r)) for r in runs],
        "ok": not unmatched and not collision,
    }
    if unmatched:
        res["why"] = ("%d store(s) inside the serializer body were not matched "
                      "into the map: %s. The map is INCOMPLETE and must not be "
                      "used." % (len(unmatched),
                                 ", ".join("0x%06x->rec 0x%02x/%dB" % u
                                           for u in unmatched[:8])))
    elif collision:
        res["why"] = "the same record offset is written from two sources"
    return res


def report(r, verbose=False):
    print("=== %s ===" % r["tag"])
    if "map" not in r:
        print("  REFUSED: %s" % r.get("why", "unknown"))
        return
    print("  serializer runs : %s" % ", ".join(
        "0x%06x (%d pairs)" % (a, n) for a, n in r["runs"]))
    print("  record offsets  : %d mapped, 0x00..0x%02x" % (r["count"], r["high"]))
    print("  serializer body : 0x%06x..0x%06x" % r["body"])
    if r["unwritten"]:
        print("  NOT serialised  : %s  (no store to these anywhere in the body)"
              % ", ".join("0x%02x" % o for o in r["unwritten"]))
    print("  partition       : %s" % ("COMPLETE -- every store in the body is "
                                      "accounted for" if r["ok"]
                                      else "INCOMPLETE -- " + r["why"]))
    if verbose:
        for dst in sorted(r["map"]):
            print("      record 0x%02x <- obj 0x%02x   (at 0x%06x)"
                  % (dst, r["map"][dst], r["at"][dst]))


def diff(a, b):
    """Compare two extracted maps. Returns a list of human-readable findings."""
    if not (a.get("ok") and b.get("ok")):
        return ["REFUSED: one side did not produce a complete partition, so a "
                "diff would compare a hole against a value."]
    ma, mb = a["map"], b["map"]
    out = []
    for o in sorted(set(ma) - set(mb)):
        out.append("record 0x%02x exists in %s but not in %s" % (o, a["tag"], b["tag"]))
    for o in sorted(set(mb) - set(ma)):
        out.append("record 0x%02x exists in %s but not in %s" % (o, b["tag"], a["tag"]))
    for o in sorted(set(ma) & set(mb)):
        if ma[o] != mb[o]:
            out.append("record 0x%02x is fed by obj 0x%02x in %s but obj 0x%02x in %s"
                       % (o, ma[o], a["tag"], mb[o], b["tag"]))
    return out


def main(argv):
    tags = [a for a in argv if not a.startswith("-")]
    verbose = "-v" in argv or "--verbose" in argv
    want_diff = "--diff" in argv
    if not tags:
        print(__doc__)
        print("known tags: " + ", ".join(sorted(TAGS)))
        return 2

    results = []
    for t in tags:
        if t not in TAGS:
            print("unknown tag %s; known: %s" % (t, ", ".join(sorted(TAGS))))
            return 2
        r = extract(t)
        results.append(r)
        report(r, verbose)
        print()

    rc = 0 if all(r.get("ok") for r in results) else 1

    if want_diff and len(results) > 1:
        base = results[-1]
        print("--- each version against %s ---" % base["tag"])
        for r in results[:-1]:
            d = diff(r, base)
            print("\n%s: %s" % (r["tag"], "IDENTICAL" if not d else "DIFFERS"))
            for line in d:
                print("    " + line)
        print()
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
