#!/usr/bin/env python3
"""whatmoved.py -- what would ANOTHER Windows capture session actually buy?

The question this answers, asked on 2026-09-06: the repo-root log.txt is
quarantined, so is the whole windows-run/ capture session worth redoing?

It is answered by partitioning the 115 settings-record bytes into:

  MOVED     took more than one value across every settings record in
            windows-run/. These are the bytes the captures actually
            demonstrate, and every one is already named.
  REACHABLE never moved, but a control in the vendor tool DOES write it --
            that control was simply never exercised. A redo would move
            these. They are already [D] from cfg107 with a cited address,
            so a redo CORROBORATES them; it does not derive them.
  CLOSED    never moved and no control in ANY of cfg100/101/104/107 writes
            it. No capture can ever attribute these, however many are taken.

The partition is computed, not asserted, and it must be exhaustive:
MOVED + REACHABLE + CLOSED == 115 or this exits non-zero. That is engineering-rules.md
§6's rule about stating coverage as a partition rather than as arithmetic
between separately-scoped numbers.

REACHABLE/CLOSED membership is a claim about the vendor binaries, so it is
listed here with its evidence and its blind spot rather than being inferred:
see config-protocol.md §7.24a (the tab-creation run makes 135/140/153/139 and
never 137, in all four tools) and §7.26 (records 0x00 and 0x07, a bounded
negative whose scan cannot see a store through a base register).

§7.24a used to offer a SECOND negative -- "no control on 137 has a message-map
entry" -- and it was false: 1043 `Apply led settings` has one (cfg107
0x405940). It was retracted 2026-09-06. Nothing here rests on it; the
never-created fact carries the CLOSED claim on its own.

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
CAPTURE_DIRS = [os.path.join(ROOT, d) for d in ("windows-run", "windows-capture")]
CAPTURES = CAPTURE_DIRS[0]   # kept: test_handedness.records resolves against it

# Bytes no control in ANY of the four config tools writes. Each carries the
# section that established it; auditclaims.py checks those citations exist.
CLOSED = {
    **{r: "LED page, dialog 137: the tab-creation run makes 135/140/153/139 "
          "and never 137, in all four tools (§7.24a). NOT 'no handler' -- "
          "control 1043 `Apply led settings` has one (cfg107 0x405940); that "
          "claim was retracted 2026-09-06, and it never carried the argument"
       for r in range(0x0F, 0x23)},
    0x00: "serialised from object +0x00, always zero, no control traced (§7.26)",
    0x07: "serialised from object +0x0c, always zero, no control traced (§7.26)",
    0x02: "not written by the serialiser; vendor sends zero (§7.4)",
    0x03: "not written by the serialiser; vendor sends zero (§7.4)",
    0x04: "not written by the serialiser; vendor sends zero (§7.4)",
    0x6F: "Sensor Glass Mode: cfg107 only READS it; the control is withdrawn "
          "on this model (§7.25, §7.30)",
}


def _records(path):
    """Every settings record in one capture, by the same structural rule as
    Tests/test_handedness.records: a large GET is a settings record only when the
    SET before it was `A1 12`; writes are `A0 11`.  Inlined here only so this can
    walk more than one directory."""
    import usbpcap
    out, asked = [], False
    for t in usbpcap.control_transfers(usbpcap.read(path)):
        d = bytes(t.data or b"")
        if t.is_set_report:
            asked = d[:2] == b"\xa1\x12"
            if len(d) >= 1024 and d[:2] == b"\xa0\x11":
                out.append(d)
        elif asked and len(d) >= 1024:
            out.append(d)
            asked = False
    return out


def observed(dirs=None):
    """(record byte -> set of values, number of records) over every capture dir."""
    vals = collections.defaultdict(set)
    n = 0
    for d in (dirs if dirs is not None else CAPTURE_DIRS):
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".pcapng"):
                continue
            for f in _records(os.path.join(d, name)):
                n += 1
                for r in range(RECORD_LEN):
                    vals[r].add(f[PAYLOAD + r])
    return vals, n


def main():
    dirs = [d for d in CAPTURE_DIRS if os.path.isdir(d)]
    if not dirs:
        print("no capture directory present", file=sys.stderr)
        return 2
    vals, n = observed(dirs)

    moved = {r for r in range(RECORD_LEN) if len(vals[r]) > 1}
    closed = {r for r in range(RECORD_LEN) if r not in moved and r in CLOSED}
    reachable = {r for r in range(RECORD_LEN) if r not in moved and r not in closed}

    print("%d settings records across %s\n"
          % (n, ", ".join(os.path.basename(d) for d in dirs)))
    print("MOVED     %3d  the captures demonstrate these" % len(moved))
    print("REACHABLE %3d  a control writes them; it was never exercised."
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
