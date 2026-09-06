#!/usr/bin/env python3
"""uidiff.py -- controls an OLDER config tool handles that the newest does not.

WHY. config-protocol.md 7.25: record 0x6f had a name, a caption and a live
checkbox in cfg100/101/104, and cfg107 kept only the readers. Reading cfg107
alone made it look like a device capability flag. If one withdrawn control hid a
field, others might, so "which controls did they take away?" should be a command
rather than an afternoon.

WHAT IT RECORDS. Structure only: control id, its caption from the resources, and
the set of message-map handler addresses each binary attaches to it. It holds no
opinion about what any control means and does not rank. CLAUDE.md 3.1.

THE BLIND SPOT, and it is not small (1.2a). Control IDs ARE REUSED ACROSS
DIALOGS. cfg100's 1044 is a `SCROLL DOWN` pushbutton on the button-mapping page;
cfg107's 1044 is a static label `X:` on the sensor page. Keying on the id alone
therefore reports pairs that are not the same control at all. Every row this
prints is a CANDIDATE to look at, never a finding, and the caption column is
what usually settles it. It also cannot see a control whose id changed between
versions, or one handled by a base class rather than its own map.

  python3 Tools/ghidra-export/uidiff.py                  # all older vs cfg107
  python3 Tools/ghidra-export/uidiff.py cfg104           # just that one
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CTL = re.compile(r"^\s{6}(\d+)\s+(\S+)\s+(/\S+)?\s*'(.*)'\s*$")
HND = re.compile(r"^\s{12}WM_COMMAND\s+\S+.*?\s+handler (0x[0-9a-f]+)")

# cfg107's empty handler: a single retl serving four controls (7.21). A control
# that points here is present and inert, which is a different thing from absent
# and is exactly the state 7.25's checkbox is in.
DEAD = {"0x00413d90"}


def load(tag):
    out = subprocess.run(
        [sys.executable, os.path.join(ROOT, "Tools/ghidra-export/ctlchain.py"), tag],
        capture_output=True, text=True, cwd=ROOT).stdout
    handlers = collections.defaultdict(set)
    caption = {}
    cur = None
    for line in out.splitlines():
        m = CTL.match(line)
        if m:
            cur = int(m.group(1))
            if m.group(4):
                caption.setdefault(cur, m.group(4))
            continue
        m = HND.match(line)
        if m and cur is not None:
            handlers[cur].add(m.group(1))
    return handlers, caption


def main(argv):
    olds = argv[1:] or ["cfg100", "cfg101", "cfg104"]
    new_h, new_c = load("cfg107")
    print("controls an older tool handles with real code, that cfg107 does not.")
    print("IDs are reused across dialogs -- read the caption before believing a row.\n")
    print("%-7s %-6s %-42s %-24s %s" % ("tool", "id", "caption", "older handler", "cfg107"))
    n = 0
    for tag in olds:
        h, c = load(tag)
        for cid, hs in sorted(h.items()):
            theirs = new_h.get(cid, set())
            if theirs and not theirs <= DEAD:
                continue                      # cfg107 still handles it
            cap = new_c.get(cid) or c.get(cid) or ""
            print("%-7s %-6d %-42s %-24s %s"
                  % (tag, cid, "'" + cap[:40] + "'",
                     ",".join(sorted(hs)), ",".join(sorted(theirs)) or "none"))
            n += 1
    print("\n%d candidate%s." % (n, "" if n == 1 else "s"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
