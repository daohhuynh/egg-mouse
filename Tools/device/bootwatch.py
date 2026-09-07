#!/usr/bin/env python3
"""bootwatch.py -- millisecond-resolution watch of the Endgame mouse's USB
identity across a replug. STRICTLY READ-ONLY.

Nothing here opens a HID device, claims an interface, or sends a byte. It shells
to `ioreg`, which reports what the OS has already enumerated. Safe against a
mouse in any state.

WHY IT EXISTS, and what it can and cannot decide
------------------------------------------------
working-memory.md records an open question that is the last unclosed piece of
the project's recovery argument:

    is the bootloader entry (hold LEFT+RIGHT, plug in) checked by the BOOTLOADER
    or by the APPLICATION? If the application does it, a broken app breaks the
    recovery path.

The observable difference: if the APPLICATION owns the check, the device must
boot the app far enough to run it. If the app initialises USB before checking,
the mouse enumerates as PID 0x1978 first and only then resets into PID 0x1977 --
a transient that a fast enough poll can catch. If the BOOTLOADER owns the check,
0x1977 is the only identity that ever appears.

    SEEING 0x1978 BEFORE 0x1977 IS CONCLUSIVE -- the application owns it.
    NOT SEEING IT IS NOT PROOF OF ANYTHING (engineering-rules.md §1.2a).

The blind spot, stated because a negative result here is exactly the kind of
absence-claim §1.2a warns about: firmware that checks the buttons BEFORE
bringing up USB would never enumerate as 0x1978, so the application could own
the check and this tool would never see it. A negative run means "not observed
by a ~100 Hz poll, which cannot see a check that precedes USB init" -- never
"the bootloader owns it".

The second signal is timing, and it is weaker but free: an app-then-reset path
has to enumerate twice, so time-to-0x1977 on a button plug should be measurably
longer than time-to-0x1978 on a normal plug. Compare the two across several
runs. Hand-timed replugs are noisy; treat a difference of tens of ms as nothing
and hundreds of ms as worth a second look.

WHY THIS QUERY. `ioreg -c IOHIDDevice -r -l` is ~150 ms per call and dumps
800 KB, far too slow to catch a transient. `-c IOUSBHostDevice -r -d1 -l` is
~9 ms and 3 KB and still carries idVendor/idProduct/bcdDevice and the product
string, so it polls ~100x faster with no loss of what we need.

    python3 Tools/device/bootwatch.py             # until Ctrl-C
    python3 Tools/device/bootwatch.py --seconds 30
"""
import argparse, re, subprocess, sys, time

VID = 0x3367
NAMES = {0x1978: "APPLICATION", 0x1977: "BOOTLOADER"}
CMD = ["ioreg", "-c", "IOUSBHostDevice", "-r", "-d1", "-l"]


def snapshot():
    """Every VID-0x3367 USB device ioreg can see right now, as a frozenset of
    (pid, bcdDevice, product-name). Empty set means absent."""
    try:
        out = subprocess.run(CMD, capture_output=True, text=True, timeout=5).stdout
    except Exception:
        return None                      # transient ioreg failure: not a state
    found = set()
    for blk in out.split("+-o "):
        m = re.search(r'"idVendor" = (\d+)', blk)
        if not m or int(m.group(1)) != VID:
            continue
        def g(k):
            mm = re.search(r'"%s" = (\d+)' % k, blk)
            return int(mm.group(1)) if mm else None
        ms = re.search(r'"USB Product Name" = "([^"]*)"', blk)
        found.add((g("idProduct"), g("bcdDevice"), ms.group(1) if ms else None))
    return frozenset(found)


def render(s):
    if not s:
        return "ABSENT"
    parts = []
    for pid, ver, name in sorted(s, key=lambda t: (t[0] or 0)):
        tag = NAMES.get(pid, "*** UNKNOWN PID -- A FINDING ***")
        # bcdDevice is BCD: 0x0110 is 1.10, not 1.16.
        v = "?" if ver is None else "0x%04x (fw %x.%02x)" % (ver, ver >> 8, ver & 0xFF)
        parts.append("PID 0x%04x %s ver=%s name=%r" % (pid, tag, v, name))
    return " | ".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="stop after N seconds (default: run until Ctrl-C)")
    a = ap.parse_args()

    print(__doc__.split("WHY IT EXISTS")[0].strip())
    print("polling %d Hz -- read-only, nothing is sent to the device" %
          round(1 / 0.010))
    print("plug/unplug now. Ctrl-C to stop.\n")

    t0 = time.perf_counter()
    prev, timeline = None, []
    try:
        while True:
            cur = snapshot()
            if cur is not None and cur != prev:
                t = time.perf_counter() - t0
                timeline.append((t, cur))
                print("[%8.3fs] %s" % (t, render(cur)), flush=True)
                prev = cur
            if a.seconds and time.perf_counter() - t0 > a.seconds:
                break
            time.sleep(0.010)
    except KeyboardInterrupt:
        print()

    print("\n--- TIMELINE (%d transitions) ---" % len(timeline))
    saw_app_then_boot = False
    for i, (t, s) in enumerate(timeline):
        gap = "" if i == 0 else "  (+%.3fs)" % (t - timeline[i - 1][0])
        print("  %8.3fs  %s%s" % (t, render(s), gap))
    pids = [tuple(sorted(p for p, _, _ in s)) for _, s in timeline if s]
    flat = [p for tup in pids for p in tup]
    for i in range(len(flat) - 1):
        if flat[i] == 0x1978 and 0x1977 in flat[i + 1:]:
            saw_app_then_boot = True
    print()
    if saw_app_then_boot:
        print("  RESULT: 0x1978 (APPLICATION) was seen BEFORE 0x1977 (BOOTLOADER).")
        print("  That is CONCLUSIVE: the application participates in bootloader entry,")
        print("  so a broken application may break the recovery path. Record as [O].")
    elif 0x1977 in flat:
        print("  RESULT: 0x1977 (BOOTLOADER) appeared with no preceding 0x1978.")
        print("  This is CONSISTENT with the bootloader owning the button check, and")
        print("  is NOT proof of it -- a check that runs before USB init would look")
        print("  identical. Tag [O] for what was seen, never [O] for the conclusion.")
    else:
        print("  RESULT: bootloader never appeared. Nothing is concluded.")


if __name__ == "__main__":
    main()
