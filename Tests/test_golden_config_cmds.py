#!/usr/bin/env python3
"""egg-config's fixed command frames against the vendor's own bytes.

The counterpart of test_golden_vendor.py, and it closes a gap that mattered.
That test pins the flasher's whole 135-frame stream against 08-flash.pcapng, and
test_config_replay.py pins the A0 11 write frames against 73 captured writes --
but until this file existed NOTHING pinned the three short commands, and
windows-run/07-factory-reset.pcapng was referenced by no test at all.

A1 13 destroys the user's settings and is the only command in egg-config that
does. It was the least-checked frame in the repository.

The reference is again not something we produced: it is what Endgame's config
tool 1.07 and their updater 1.10 actually sent to the owner's mouse on 2026-09-05.

Scope, so it is not mistaken for more: host-to-device bytes only. It says
nothing about what the device DOES with them -- see CLAUDE.md 1.2, the meaning
of A1 13 is still [G] and only the frame is [O].
"""
import os
import struct
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
RESET = os.path.join(ROOT, "windows-run", "07-factory-reset.pcapng")
FLASH = os.path.join(ROOT, "windows-run", "08-flash.pcapng")
BIN = os.path.join(ROOT, "build", "egg-config")


def vendor_out_frames(path):
    """Host-to-device HID feature reports, in order.

    Decoded here rather than through Tools/capture/usbpcap.py deliberately: that
    module is the project's own code and has had a bug that silently dropped
    most of a capture. A test that shares a decoder with the thing it checks
    checks nothing. Same twenty lines as test_golden_vendor.py, on purpose --
    two tests agreeing through one decoder would be one test.
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
                if (transfer == 2 and stage == 0 and not (info & 1)
                        and len(data) > 8 and data[0] == 0x21 and data[1] == 0x09):
                    out.append(data[8:])
        o += blen
    return out


def our_frames():
    r = subprocess.run([BIN, "frames"], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("egg-config frames failed: %s" % r.stderr)
    out = {}
    for line in r.stdout.splitlines():
        parts = line.split()
        if parts and all(c in "0123456789abcdef" for c in parts[-1]):
            out[parts[-1][:4]] = bytes.fromhex(parts[-1])
    return out


class GoldenConfigCommands(unittest.TestCase):
    def setUp(self):
        for p in (RESET, BIN):
            if not os.path.exists(p):
                self.skipTest("missing %s" % os.path.basename(p))
        self.vendor = vendor_out_frames(RESET)
        self.ours = our_frames()

    def test_the_capture_is_the_shape_we_think_it_is(self):
        """Guards the guard. An empty list would make everything below vacuous."""
        self.assertEqual(len(self.vendor), 5)
        self.assertEqual([f[1] for f in self.vendor], [0x02, 0x12, 0x13, 0x02, 0x12])
        for f in self.vendor:
            self.assertEqual(len(f), 64)
            self.assertEqual(f[0], 0xA1)

    def test_we_emit_exactly_three_fixed_frames(self):
        self.assertEqual(sorted(self.ours), ["a102", "a112", "a113"])

    def test_every_frame_matches_the_vendor_byte_for_byte(self):
        for f in self.vendor:
            key = "%02x%02x" % (f[0], f[1])
            self.assertIn(key, self.ours, "vendor sent %s, we cannot build it" % key)
            self.assertEqual(self.ours[key], f,
                             "%s differs from the vendor's frame" % key)

    def test_the_frames_are_a_report_id_an_opcode_and_zeros(self):
        """Stated as an invariant rather than an example: any non-zero byte
        past the opcode is a payload we did not derive and must not send."""
        for key, f in self.ours.items():
            self.assertEqual(len(f), 64, key)
            self.assertTrue(all(b == 0 for b in f[2:]),
                            "%s carries payload bytes at %s"
                            % (key, [i for i in range(2, 64) if f[i]]))

    def test_a1_13_also_matches_the_one_the_updater_sent(self):
        """The updater sends A1 13 as the last step of every flash. If the
        config tool's reset and the updater's final command are the same bytes,
        then whatever A1 13 does, this mouse has already survived it twice."""
        if not os.path.exists(FLASH):
            self.skipTest("no flash capture")
        from_flash = [f for f in vendor_out_frames(FLASH)
                      if len(f) == 64 and f[1] == 0x13]
        self.assertEqual(len(from_flash), 1)
        self.assertEqual(self.ours["a113"], from_flash[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
