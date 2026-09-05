#!/usr/bin/env python3
"""The outbound byte stream of egg-flash against the vendor's own.

§4.3 asks for a golden file: freeze a trusted byte stream and diff every future
run against it. This is better than that. The reference is not something we
produced and then declared trusted -- it is what Endgame's updater 1.10 actually
sent to the owner's mouse on 2026-09-05, captured with USBPcap, in the run that took
the device from firmware 1.07 to 1.10 and left it working.

So this test does not ask "does the code still do what it did yesterday". It
asks "does the code send what the vendor sent". A frozen golden file cannot tell
you your derivation was wrong from the first day. This can.

Scope, stated so it is not mistaken for more: it compares the HOST-TO-DEVICE
bytes only. It says nothing about timing, about how responses are handled, or
about what happens when anything goes wrong -- none of which appears in a clean
capture. Those are the mock's job (§4.3), and the mock's failure cases are
invented, not observed.
"""
import os
import struct
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

CAPTURE = os.path.join(ROOT, "windows-run", "08-flash.pcapng")
CAPTURE2 = os.path.join(ROOT, "windows-run", "09-flash-again.pcapng")
EXE = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe")
BIN = os.path.join(ROOT, "build", "egg-flash")


def vendor_frames(path):
    """Every host-to-device HID feature report in the capture, in order.

    Decoded here rather than through Tools/capture/usbpcap.py on purpose: that
    module is the project's own code and had a bug that silently dropped most of
    this capture. A test that shares the buggy decoder with the thing it is
    testing checks nothing. This walks the pcapng and the USBPcap header
    directly, in about twenty lines.
    """
    with open(path, "rb") as fh:
        buf = fh.read()
    out, o, n = [], 0, len(buf)
    while o + 12 <= n:
        btype, blen = struct.unpack_from("<II", buf, o)
        if blen < 12 or o + blen > n:
            break
        if btype == 6:                                   # Enhanced Packet Block
            cap = struct.unpack_from("<I", buf, o + 8 + 12)[0]
            body = buf[o + 8 + 20:o + 8 + 20 + cap]
            if len(body) >= 28:
                hl = struct.unpack_from("<H", body, 0)[0]
                info = body[16]
                transfer = body[22]
                dlen = struct.unpack_from("<I", body, 23)[0]
                stage = body[27] if hl >= 28 else None
                data = body[hl:hl + dlen]
                # A host-to-device control SETUP whose buffer carries a payload
                # after the 8 setup bytes: bmRequestType 0x21, bRequest 0x09.
                if (transfer == 2 and stage == 0 and not (info & 1)
                        and len(data) > 8 and data[0] == 0x21 and data[1] == 0x09):
                    out.append(data[8:])
        o += blen
    return out


def our_frames():
    r = subprocess.run([BIN, "stream", EXE], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("egg-flash stream failed: %s" % r.stderr)
    frames = []
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and all(c in "0123456789abcdef" for c in parts[1]):
            frames.append((parts[0], bytes.fromhex(parts[1])))
    return frames


class Golden(unittest.TestCase):
    def setUp(self):
        for p in (CAPTURE, EXE, BIN):
            if not os.path.exists(p):
                self.skipTest("missing %s" % os.path.basename(p))

    def test_stream_matches_the_vendor_byte_for_byte(self):
        vendor = vendor_frames(CAPTURE)
        ours = our_frames()
        self.assertEqual(len(ours), len(vendor),
                         "we emit %d frames, the vendor sent %d"
                         % (len(ours), len(vendor)))
        for i, ((label, mine), theirs) in enumerate(zip(ours, vendor)):
            self.assertEqual(len(mine), len(theirs),
                             "frame %d (%s): length %d, vendor %d"
                             % (i, label, len(mine), len(theirs)))
            if mine != theirs:
                bad = [k for k in range(len(mine)) if mine[k] != theirs[k]]
                self.fail("frame %d (%s) differs at %d byte(s), first at %d: "
                          "ours %02x, vendor %02x"
                          % (i, label, len(bad), bad[0],
                             mine[bad[0]], theirs[bad[0]]))

    def test_the_two_vendor_flashes_sent_identical_bytes(self):
        """If they did not, there is a nonce or a timestamp in the stream and
        no fixed reference is possible. They did."""
        if not os.path.exists(CAPTURE2):
            self.skipTest("no second flash capture")
        self.assertEqual(vendor_frames(CAPTURE), vendor_frames(CAPTURE2))

    def test_the_capture_is_the_shape_we_think_it_is(self):
        """Guards the guard. If this decoder silently returned an empty list,
        every comparison above would pass vacuously."""
        v = vendor_frames(CAPTURE)
        self.assertEqual(len(v), 135)
        self.assertEqual(sum(1 for f in v if len(f) == 1041), 131)
        # four 64-byte frames: a1 3a enter, a1 08 whole-sum, a1 09 complete,
        # and a1 13 -- which goes to the RE-ENUMERATED application device, not
        # to the bootloader, and is easy to miss for exactly that reason.
        self.assertEqual(sum(1 for f in v if len(f) == 64), 4)
        self.assertEqual([f[1] for f in v if len(f) == 64], [0x3A, 0x08, 0x09, 0x13])
        self.assertEqual(sum(1 for f in v if f[0] == 0xA0 and f[1] == 0x06), 65)
        self.assertEqual(sum(1 for f in v if f[0] == 0xA0 and f[1] == 0x07), 65)


if __name__ == "__main__":
    unittest.main(verbosity=2)
