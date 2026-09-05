#!/usr/bin/env python3
"""pidwatch.py -- print the Endgame mouse's USB identity, live. STRICTLY READ-ONLY.

Nothing here opens a HID device, claims an interface, or sends a byte. It shells
to `ioreg`, which reports what the OS already enumerated. Safe to run against a
mouse in any state, including one that is mid-recovery.

WHY IT EXISTS. CLAUDE.md §2's worst case is a device with no valid application
image. Whether that state is recoverable turns on one question: can the
bootloader be reached when the application is broken? The vendor updater has a
code path for a device already sitting in the bootloader (updater-protocol.md
§5.1 case 6, `mode == 2` at PID 0x1977), which is strong evidence such a state is
expected -- but no evidence at all about how to REACH it without a working app.

Many mice force the bootloader with a button held during enumeration. If this one
does, recovery is unconditional and the project's only real risk is closed. That
is a free experiment: hold a button, replug, watch this.

  0x1978 (6520) = application     0x1977 (6519) = bootloader

Anything else under VID 0x3367 is itself a finding -- print it, do not filter it.
"""
import re, subprocess, sys, time

VID = 0x3367
NAMES = {0x1978: "APPLICATION", 0x1977: "BOOTLOADER"}


def snapshot():
    """Every VID-0x3367 collection ioreg can see, as (pid, version, usagepage,
    usage). A set, because the device presents several collections at once and
    only their identity matters here."""
    try:
        out = subprocess.run(["ioreg", "-c", "IOHIDDevice", "-r", "-l"],
                             capture_output=True, text=True, timeout=20).stdout
    except Exception as e:
        print("ioreg failed:", e, file=sys.stderr)
        return set()
    found = set()
    for blk in out.split("+-o "):
        if f'"VendorID" = {VID}' not in blk:
            continue
        def g(k):
            m = re.search(r'"%s" = (\d+)' % k, blk)
            return int(m.group(1)) if m else None
        pid = g("ProductID")
        if pid is not None:
            found.add((pid, g("VersionNumber"), g("PrimaryUsagePage"), g("PrimaryUsage")))
    return found


def render(s):
    if not s:
        return "  (no VID 0x3367 device present)"
    out = []
    for pid, ver, up, u in sorted(s):
        tag = NAMES.get(pid, "*** UNKNOWN PID -- THIS IS A FINDING ***")
        # bcdDevice is BCD: each nibble is a decimal digit, so 0x0110 is 1.10
        # and NOT 1.16. Formatting it with %d printed "1.16" for firmware 1.10 --
        # correct for 1.04/1.06/1.07 by coincidence (low byte < 0x0a) and wrong
        # from 1.10 on, which is the version the device actually runs. hidobserve
        # .py already had this right with %x; this line did not.
        v = "?" if ver is None else "0x%04x (fw %x.%02x)" % (ver, ver >> 8, ver & 0xFF)
        out.append("  PID 0x%04x %-12s ver=%-20s usagepage=0x%02x usage=0x%02x"
                   % (pid, tag, v, up or 0, u or 0))
    return "\n".join(out)


def main():
    once = "--once" in sys.argv
    print("watching VID 0x%04x -- read-only, nothing is sent to the device\n" % VID)
    prev = None
    while True:
        cur = snapshot()
        if cur != prev:
            print(time.strftime("[%H:%M:%S]"))
            print(render(cur), flush=True)
            prev = cur
        if once:
            return
        time.sleep(0.4)


if __name__ == "__main__":
    main()
