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
import re
import struct
import subprocess
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
RESET = os.path.join(ROOT, "windows-run", "07-factory-reset.pcapng")
FLASH = os.path.join(ROOT, "windows-run", "08-flash.pcapng")
BIN = os.path.join(ROOT, "build", "egg-config")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_handedness import every_capture      # noqa: E402  -- both capture dirs


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
        for tok in line.split():
            if len(tok) >= 4 and all(c in "0123456789abcdef" for c in tok):
                out[tok[:4]] = bytes.fromhex(tok)
                break
    return out


def our_delays():
    r = subprocess.run([BIN, "frames"], capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        m = re.search(r"^(A[01] [0-9A-F]{2}).*wait=(\d+)ms", line)
        if m:
            out[m.group(1).lower().replace(" ", "")] = int(m.group(2))
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


class Delays(unittest.TestCase):
    """The wait before the first status read is half the protocol, and it is the
    half egg-config did not implement.

    It applied one 100 ms delay to every command. The vendor times each command
    separately -- Sleep(50) for A1 02, Sleep(80) for A1 12, Sleep(1100) for
    A1 13, a caller-supplied word for A0 11 -- and the wire shows each gap is
    that Sleep plus 10-15 ms of transfer. For A1 13 we read eleven times sooner
    than any vendor code path, and our whole poll window shut at 1100 ms, which
    is exactly where the config tool's FIRST read lands.

    The reference here is measured from the captures, so this test fails if the
    constants drift away from what Endgame's tools actually did.
    """

    def setUp(self):
        if not os.path.exists(BIN):
            self.skipTest("no egg-config")
        self.delays = our_delays()

    def observed(self, opcode):
        """min SET->next-GET gap for this opcode over every capture.

        BOTH capture directories since 2026-09-07. This swept `windows-run/`
        only, so the seven firmware-1.10 captures -- which are the ONLY vendor
        traffic we have from the firmware currently on the mouse -- could not
        lower a single one of these bounds. The direction of the miss is the bad
        one: this test passes when our delay is >= the vendor's shortest
        observed gap, so evidence it cannot see can only make us look safer
        than we are.
        """
        gaps = []
        for directory, name in every_capture():
            ev = timed_frames(os.path.join(directory, name))
            for i, e in enumerate(ev):
                if e[0] != "SET" or e[3] != opcode:
                    continue
                for j in range(i + 1, len(ev)):
                    if ev[j][0] == "GET":
                        gaps.append((ev[j][1] - e[1]) * 1000)
                        break
                    if ev[j][0] == "SET":
                        break
        return gaps

    def test_both_capture_runs_contribute_samples(self):
        """A directory that contributes nothing is indistinguishable from one
        that is not being read, and that is precisely how this test spent a day
        blind to `windows-capture/`."""
        per_dir = {}
        for directory, name in every_capture():
            ev = timed_frames(os.path.join(directory, name))
            n = sum(1 for e in ev if e[0] == "SET")
            per_dir[os.path.basename(directory)] = \
                per_dir.get(os.path.basename(directory), 0) + n
        for d in ("windows-run", "windows-capture"):
            self.assertGreater(per_dir.get(d, 0), 0,
                               "%s contributed no SET frames: %r" % (d, per_dir))

    def test_every_delay_is_no_shorter_than_the_vendor_waited(self):
        """The safe direction is slower. A delay shorter than the vendor's puts
        a read on the device inside a window no capture covers."""
        if not os.path.isdir(os.path.join(ROOT, "windows-run")):
            self.skipTest("no captures")
        for key, opcode in (("a102", 0x02), ("a112", 0x12), ("a113", 0x13),
                            ("a011", 0x11)):
            gaps = self.observed(opcode)
            if not gaps:
                continue
            ours = self.delays[key]
            # our delay + transfer overhead must not undercut the fastest gap
            # the vendor was ever observed to allow, by more than that overhead
            self.assertGreaterEqual(
                ours + 20, min(gaps),
                "%s: we wait %d ms, the vendor's shortest observed gap is "
                "%.1f ms over %d samples" % (key, ours, min(gaps), len(gaps)))

    def test_a1_13_specifically(self):
        """Pinned on its own because it is the destructive one and because 100
        ms -- the old value -- passes no reasonable version of the test above."""
        self.assertEqual(self.delays["a113"], 1100)   # cfg107 0x4047af, push 0x44c

    def test_the_delays_are_not_all_the_same(self):
        """The defect was a single delay applied to everything. If these ever
        collapse to one value again, that is the same bug returning."""
        self.assertGreater(len(set(self.delays.values())), 1)


def timed_frames(path):
    """(dir, timestamp, reportId, opcode) for every feature-report transfer."""
    with open(path, "rb") as fh:
        buf = fh.read()
    out, o, n = [], 0, len(buf)
    while o + 12 <= n:
        btype, blen = struct.unpack_from("<II", buf, o)
        if blen < 12 or o + blen > n:
            break
        if btype == 6:
            th, tl = struct.unpack_from("<II", buf, o + 12)
            ts = ((th << 32) | tl) / 1e6
            cap = struct.unpack_from("<I", buf, o + 20)[0]
            body = buf[o + 28:o + 28 + cap]
            if len(body) >= 28:
                hl = struct.unpack_from("<H", body, 0)[0]
                info, transfer = body[16], body[22]
                dlen = struct.unpack_from("<I", body, 23)[0]
                stage = body[27] if hl >= 28 else None
                data = body[hl:hl + dlen]
                if (transfer == 2 and stage == 0 and not (info & 1)
                        and len(data) > 9 and data[0] == 0x21 and data[1] == 0x09):
                    out.append(("SET", ts, data[8], data[9]))
                elif transfer == 2 and (info & 1) and dlen in (63, 1040):
                    out.append(("GET", ts, None, None))
        o += blen
    return out


if __name__ == "__main__":
    unittest.main(verbosity=2)
