#!/usr/bin/env python3
"""mkframes.py -- rebuild frames/ from the committed capture it came from.

    python3 Tools/capture/mkframes.py [--force] [--check]

WHY THIS EXISTS. Five shell tests read `frames/01-baseline-003-in-01.bin`, the
device's own 1040-byte A1 12 reply, and `.gitignore` line 16 ignores `*.bin`
under "Extracted firmware images". So the fixture is NOT in the repository, and
on a fresh clone those five `cat` it, get nothing, and carry on comparing empty
files -- `set -uo pipefail` without `-e` does not stop them. A gate that passes
because its input vanished is worse than no gate (engineering-rules.md §6.2).

The fix is not to commit the file. Every byte of it is ALREADY committed, inside
`windows-run/01-baseline.pcapng` -- and inside five other captures besides, all
identical. Extracting it on demand makes the fixture's provenance mechanical
instead of a file someone could edit: the four frames are whatever that capture
holds, and the SHA-256s below say which four.

The sums are pinned rather than merely recorded. If a future capture is dropped
in under the same name, this refuses instead of quietly re-baselining the tests
that depend on it.
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

CAPTURE = os.path.join(ROOT, "windows-run", "01-baseline.pcapng")
OUTDIR = os.path.join(ROOT, "frames")

# name -> sha256 of the bytes, taken from the files that were on disk when this
# script was written and re-derived from the capture in the same session.
EXPECT = {
    "01-baseline-000-out-02.bin":
        "e99e97ef8e9976ad8a5483f0e3b9fd556753fb2fb3f844b118b28a5e8fb584ee",
    "01-baseline-001-in-01.bin":
        "2ac29431980aa40e91acb606577d53d0d194fc33593172b4063dbbcc5984651a",
    "01-baseline-002-out-12.bin":
        "a38317af561bd01ceb7520b4d1a171e3b8a08dc32bd75bf38cfea8bf724e4009",
    "01-baseline-003-in-01.bin":
        "837a9cd2a9d3fcabc190a97bc5f02f7e80cb9da29a1cf87cf7923cff9d51f9d9",
}


def extract():
    """The four HID FEATURE transfers of the baseline conversation, in order."""
    import usbpcap
    transfers = usbpcap.control_transfers(usbpcap.read(CAPTURE))
    feat = [t for t in transfers if t.is_feature]
    out = []
    for i, t in enumerate(feat):
        data = bytes(t.data or b"")
        # The suffix is the COMMAND byte, data[1] -- 02, 01, 12, 01 -- not the
        # report id. That is how the files were named originally and changing it
        # would orphan every test that hardcodes the path.
        cmd = data[1] if len(data) > 1 else 0
        name = "01-baseline-%03d-%s-%02x.bin" % (
            i, "out" if t.is_set_report else "in", cmd)
        out.append((name, data))
    return out


def main():
    force = "--force" in sys.argv
    check = "--check" in sys.argv
    if not os.path.exists(CAPTURE):
        print("mkframes: %s is not present; cannot rebuild frames/" % CAPTURE)
        return 1
    frames = extract()
    if len(frames) != 4:
        print("mkframes: expected 4 feature transfers, found %d" % len(frames))
        return 1
    os.makedirs(OUTDIR, exist_ok=True)
    rc = 0
    for name, data in frames:
        got = hashlib.sha256(data).hexdigest()
        want = EXPECT.get(name)
        if want is None:
            print("mkframes: %s is not a name this script knows" % name)
            rc = 1
            continue
        if got != want:
            print("mkframes: REFUSING to write %s\n"
                  "  capture gives sha256 %s\n"
                  "  this script pins     %s\n"
                  "  The capture under windows-run/ is not the one the tests "
                  "were built against." % (name, got, want))
            rc = 1
            continue
        path = os.path.join(OUTDIR, name)
        if check:
            if not os.path.exists(path):
                print("missing %s" % path)
                rc = 1
            elif hashlib.sha256(open(path, "rb").read()).hexdigest() != want:
                print("differs %s" % path)
                rc = 1
            continue
        if os.path.exists(path) and not force:
            if hashlib.sha256(open(path, "rb").read()).hexdigest() == want:
                continue
        with open(path, "wb") as f:
            f.write(data)
        print("wrote %s (%d bytes)" % (path, len(data)))
    return rc


if __name__ == "__main__":
    sys.exit(main())
