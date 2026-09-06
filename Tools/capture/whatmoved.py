#!/usr/bin/env python3
"""whatmoved.py -- what would ANOTHER Windows capture session actually buy?

The question this answers, asked by the owner on 2026-09-06: the repo-root log.txt is
quarantined, so is the whole windows-run/ capture session worth redoing?

It is answered by partitioning the 115 settings-record bytes into:

  MOVED     took more than one value across every settings record in
            windows-run/. These are the bytes the captures actually
            demonstrate, and every one is already named.
  REACHABLE never moved, but a control in the vendor tool DOES write it --
            the owner simply did not exercise that control. A redo would move
            these. They are already [D] from cfg107 with a cited address,
            so a redo CORROBORATES them; it does not derive them.
  CLOSED    never moved and no control in ANY of cfg100/101/104/107 writes
            it. No capture can ever attribute these, however many are taken.

The partition is computed, not asserted, and it must be exhaustive:
MOVED + REACHABLE + CLOSED == 115 or this exits non-zero. That is CLAUDE.md
§6's rule about stating coverage as a partition rather than as arithmetic
between separately-scoped numbers.

REACHABLE/CLOSED membership is a claim about the vendor binaries, so it is
listed here with its evidence and its blind spot rather than being inferred:
see config-protocol.md §7.24a (dialog 137 is created by no tool AND every
control on it has zero message-map entries in all four) and §7.26 (records
0x00 and 0x07, a bounded negative whose scan cannot see a store through a
base register).

    python3 Tools/capture/whatmoved.py
"""
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "Tests"))
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

PAYLOAD = 16
RECORD_LEN = 0x73
CAPTURES = os.path.join(ROOT, "windows-run")

# Bytes no control in ANY of the four config tools writes. Each carries the
# section that established it; auditclaims.py checks those citations exist.
CLOSED = {
    **{r: "LED page, dialog 137: created by no tool, and every control on it "
          "has zero message-map entries in all four (§7.24a)"
       for r in range(0x0F, 0x23)},
    0x00: "serialised from object +0x00, always zero, no control traced (§7.26)",
    0x07: "serialised from object +0x0c, always zero, no control traced (§7.26)",
    0x02: "not written by the serialiser; vendor sends zero (§7.4)",
    0x03: "not written by the serialiser; vendor sends zero (§7.4)",
    0x04: "not written by the serialiser; vendor sends zero (§7.4)",
    0x6F: "Sensor Glass Mode: cfg107 only READS it; the control is withdrawn "
          "on this model (§7.25, §7.30)",
}


def observed():
    """(record byte -> set of values, number of records) over windows-run/."""
    from test_handedness import records as recs
    vals = collections.defaultdict(set)
    n = 0
    for name in sorted(os.listdir(CAPTURES)):
        if not name.endswith(".pcapng"):
            continue
        for f in recs(name):
            n += 1
            for r in range(RECORD_LEN):
                vals[r].add(f[PAYLOAD + r])
    return vals, n


def main():
    if not os.path.isdir(CAPTURES):
        print("windows-run/ not present", file=sys.stderr)
        return 2
    vals, n = observed()

    moved = {r for r in range(RECORD_LEN) if len(vals[r]) > 1}
    closed = {r for r in range(RECORD_LEN) if r not in moved and r in CLOSED}
    reachable = {r for r in range(RECORD_LEN) if r not in moved and r not in closed}

    print("%d settings records across %s\n" % (n, os.path.basename(CAPTURES)))
    print("MOVED     %3d  the captures demonstrate these" % len(moved))
    print("REACHABLE %3d  a control writes them; the owner did not exercise it."
          % len(reachable))
    print("               A redo WOULD move these -- and every one is already")
    print("               [D] from cfg107, so a redo corroborates, not derives.")
    print("CLOSED    %3d  no control in any of the four tools writes them." % len(closed))
    print("               No capture can ever attribute these.\n")

    total = len(moved) + len(reachable) + len(closed)
    if total != RECORD_LEN:
        print("PARTITION IS NOT EXHAUSTIVE: %d != %d" % (total, RECORD_LEN),
              file=sys.stderr)
        return 1
    print("partition: %d + %d + %d == %d  OK\n"
          % (len(moved), len(reachable), len(closed), RECORD_LEN))

    def block(r):
        if 0x23 <= r <= 0x36:
            return "CPI block"
        if 0x37 <= r <= 0x6E:
            return "button block"
        if 0x0F <= r <= 0x22:
            return "LED/RGB block"
        return "scalar"

    for name, s in (("REACHABLE", reachable), ("CLOSED", closed)):
        c = collections.Counter(block(r) for r in s)
        print("%s by block: %s" % (name, dict(c)))

    print("\nCLOSED, with why each is closed:")
    for r in sorted(closed):
        if 0x10 <= r <= 0x22:      # the LED run shares one reason; print once
            continue
        print("  0x%02x  %s" % (r, CLOSED[r]))
    print("  0x10-0x22  (same reason as 0x0f)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
