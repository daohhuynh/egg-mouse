#!/usr/bin/env python3
"""test_handedness.py -- score `egg-config handedness` against the ONE capture
in which the owner actually ticked the vendor's Left-handed Mode box.

config-protocol.md §7.20 derives the transform from cfg107 `0x408b00`. That is
`[D]`. `04-buttons.pcapng` line 1 is the same change performed by the vendor's
own software on the physical mouse, which is `[O]`. This is the join: our
encoder, given `01-baseline`'s record, must produce exactly the button block
`04-buttons` contains -- byte for byte, with nothing else moved.

It is worth more than it looks. §7.14 described left-handed mode as "swapping
the LEFT and RIGHT entries' masks", which is what one capture of two default
entries looks like. §7.20 found the mechanism is a MOVE plus a reset to
left-click. Both descriptions predict the same bytes HERE, so this capture
cannot tell them apart -- and that is exactly why the C++ invariants in
Tests/test_config.cpp carry the cases the capture does not reach (a remapped
button, applying twice, byte +6). A test that cannot fail for the right reason
has to say so out loud.
"""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

EGGCONFIG = os.path.join(ROOT, "build", "egg-config")
CAPTURES = os.path.join(ROOT, "windows-run")

PAYLOAD = 16
LARGE = 0x411
BLOCK = 0x37
ENTRY = 7
MULTICLICK = [0x3D, 0x44, 0x4B, 0x52, 0x59]


def records(name):
    """Every settings record in a capture, in order.

    Structural selection, the same rule Tests/test_cpi.py uses: a large GET is a
    settings record only when the SET before it was `A1 12`. Writes are `A0 11`.
    """
    import usbpcap
    tr = usbpcap.control_transfers(usbpcap.read(os.path.join(CAPTURES, name)))
    out, asked = [], False
    for t in tr:
        d = bytes(t.data or b"")
        if t.is_set_report:
            asked = d[:2] == b"\xa1\x12"
            if len(d) >= 1024 and d[:2] == b"\xa0\x11":
                out.append(d)
        elif t.is_get_report:
            if len(d) >= 1024 and asked:
                out.append(d)
            asked = False
    return out


def block(frame):
    o = PAYLOAD + BLOCK
    return frame[o:o + 2 * ENTRY]


def save(frame, path):
    d = bytes(frame)
    with open(path, "wb") as f:
        f.write(d + b"\0" * (LARGE - len(d)))


def plan(path, want):
    """The two entries `handedness <want>` would write, parsed from the dry run."""
    r = subprocess.run([EGGCONFIG, "handedness", want, "--from", path],
                       capture_output=True, text=True, cwd=ROOT)
    out = []
    for line in r.stdout.splitlines():
        if "->" not in line or "entry" not in line:
            continue
        after = line.split("->")[1].strip()
        parts = after.split()
        if len(parts) == ENTRY:
            out.append(bytes(int(p, 16) for p in parts))
    return (b"".join(out) if len(out) == 2 else None), r.stdout, r.returncode


@unittest.skipUnless(os.path.isdir(CAPTURES), "captures not present")
@unittest.skipUnless(os.path.exists(EGGCONFIG), "egg-config not built")
class ScoredAgainstTheVendorsOwnLeftHandedRecord(unittest.TestCase):
    # `05-buttonmapping.pcapng`, NOT `04-buttons`. config-protocol.md §7.14
    # says "this capture's baseline differs from 01-baseline" without naming it,
    # and §7.14 is the section about `05-buttonmapping`. The first version of
    # this test read `04-buttons` -- whose button block is right-handed in all
    # sixteen of its records -- and failed. Checked, not assumed: of the eleven
    # captures, `05-buttonmapping` is the only one containing `0002 / 0001`.
    @classmethod
    def setUpClass(cls):
        cls.recs = records("05-buttonmapping.pcapng")
        cls.before, cls.after = cls.recs[0], cls.recs[1]
        cls.tmp = os.path.join(os.environ.get("TMPDIR", "/tmp"), "egg-hand.bin")

    def test_the_pair_really_is_the_handedness_change(self):
        self.assertEqual(b"\x00\x01", block(self.before)[:2])
        self.assertEqual(b"\x00\x02", block(self.before)[ENTRY:ENTRY + 2])
        self.assertEqual(b"\x00\x02", block(self.after)[:2])
        self.assertEqual(b"\x00\x01", block(self.after)[ENTRY:ENTRY + 2])

    def test_we_reproduce_it_byte_for_byte(self):
        save(self.before, self.tmp)
        got, out, rc = plan(self.tmp, "left")
        self.assertIsNotNone(got, out)
        self.assertEqual(2, rc, "a dry run must not report success")
        self.assertEqual(block(self.after).hex(), got.hex())

    def test_the_vendor_moved_exactly_two_bytes_and_so_do_we(self):
        """`0x38` and `0x3f` -- the `+1` mask of each entry.

        `0x01` also differs and is NOT a setting: record 0 is a READ, in which
        the device reports `80 00 00 00` at `0x01`..`0x04`, and record 1 is a
        WRITE, in which the vendor sends `00 00 00 00` (§7.4). Excluding it is
        the read-vs-write difference, not an exception carved to make the test
        pass -- `egg-config --unknown-bytes` exists for exactly this byte.
        """
        a = self.before[PAYLOAD:PAYLOAD + 0x73]
        b = self.after[PAYLOAD:PAYLOAD + 0x73]
        moved = {i for i in range(len(a)) if a[i] != b[i]} - {1, 2, 3, 4}
        self.assertEqual({0x38, 0x3F}, moved,
                         "moved: %s" % sorted(hex(i) for i in moved))
        # And our plan must change those same two and no others.
        save(self.before, self.tmp)
        got, out, _ = plan(self.tmp, "left")
        self.assertIsNotNone(got, out)
        old = block(self.before)
        ours = {BLOCK + i for i in range(2 * ENTRY) if old[i] != got[i]}
        self.assertEqual({0x38, 0x3F}, ours,
                         "we would move: %s" % sorted(hex(i) for i in ours))

    def test_already_in_that_state_writes_nothing(self):
        save(self.before, self.tmp)
        r = subprocess.run([EGGCONFIG, "handedness", "right", "--from", self.tmp],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(0, r.returncode)
        self.assertIn("Nothing to write", r.stdout)

    def test_a_record_in_neither_state_is_refused(self):
        d = bytearray(self.before) + bytearray(LARGE - len(self.before))
        d[PAYLOAD + BLOCK + 1] = 0x04            # left is now middle-click
        with open(self.tmp, "wb") as f:
            f.write(bytes(d))
        r = subprocess.run([EGGCONFIG, "handedness", "left", "--from", self.tmp],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(2, r.returncode)
        self.assertIn("neither handedness state", r.stdout)

    def test_the_capture_cannot_distinguish_move_from_swap(self):
        """Stated, not assumed. Both entries are at their defaults here, so a
        swap and a move-plus-reset agree; the C++ invariants carry the rest."""
        b = block(self.before)
        self.assertEqual(b"\x00\x01\x00\x00\x00\x00", b[:6])
        self.assertEqual(b"\x00\x02\x00\x00\x00\x00", b[ENTRY:ENTRY + 6])


@unittest.skipUnless(os.path.isdir(CAPTURES), "captures not present")
@unittest.skipUnless(os.path.exists(EGGCONFIG), "egg-config not built")
class TheMulticlickAndSpdtBytesAreOBSERVED(unittest.TestCase):
    """This class was written asserting that no capture had ever moved these
    bytes, so only the default 8 would appear. That was wrong, and wrong in the
    best direction: `04-buttons.pcapng` exercises them thoroughly.

    Chronologically, per button, across its sixteen records:

        left     8 8 8 0 25 12 12 12 12 12 f0 f1 8 8 8 8
        right    8 8 8 8  8  8 25 25 25 25 25 25 25 f0 f1 8
        middle   8 8 8 8  8  8  8 25 25 25 25 25 25 25 25 25
        forward  8 8 8 8  8  8  8  8 25 25 25 25 25 25 25 25
        back     8 8 8 8  8  8  8  8  8 25 25 25 25 25 25 25

    So §7.22 is not `[D]` alone. The filter range, the two GX bytes, AND the
    asymmetry that only left and right can carry them are all `[O]` -- and the
    asymmetry is the fact `egg-config multiclick` refuses on, which makes this
    the test that matters most in the file.
    """

    @classmethod
    def setUpClass(cls):
        cls.seen = {k: [] for k in range(len(MULTICLICK))}
        for name in sorted(os.listdir(CAPTURES)):
            if not name.endswith(".pcapng"):
                continue
            for f in records(name):
                for k, at in enumerate(MULTICLICK):
                    cls.seen[k].append(f[PAYLOAD + at])

    def test_they_are_the_seventh_byte_of_each_button_entry(self):
        for k, at in enumerate(MULTICLICK):
            self.assertEqual(BLOCK + ENTRY * k + 6, at)

    def test_the_gx_bytes_appear_on_LEFT_AND_RIGHT_ONLY(self):
        """The asymmetry §7.22.3 rests on, observed rather than inferred."""
        for k in range(len(MULTICLICK)):
            gx = {v for v in self.seen[k] if v in (0xF0, 0xF1)}
            if k < 2:
                self.assertEqual({0xF0, 0xF1}, gx,
                                 "button %d never showed both GX bytes" % k)
            else:
                self.assertEqual(set(), gx,
                                 "button %d carried a GX byte: %s" % (k, gx))

    def test_every_numeric_value_ever_seen_is_inside_the_derived_range(self):
        for k in range(len(MULTICLICK)):
            nums = sorted({v for v in self.seen[k] if v not in (0xF0, 0xF1)})
            self.assertTrue(nums, "button %d has no numeric observation" % k)
            self.assertLessEqual(max(nums), 25,
                                 "button %d exceeded the 0..25 range: %s" % (k, nums))
            self.assertGreaterEqual(min(nums), 0)

    def test_the_range_endpoints_were_actually_reached(self):
        """0 and 25 both observed, so `SetRange(0, 0x19)` is not just derived."""
        left = set(self.seen[0])
        self.assertIn(0, left)
        self.assertIn(25, left)

    def test_the_default_is_eight(self):
        """cfg107 0x405f9f TBM_SETPOS(8) and 0x413e45/0x413e4f, on the wire."""
        for k in range(len(MULTICLICK)):
            self.assertEqual(8, self.seen[k][0],
                             "button %d did not start at 8" % k)

    def test_our_encoder_accepts_every_value_the_vendor_ever_wrote(self):
        modes = {0xF0: "gx-safe", 0xF1: "gx-speed"}
        bad = []
        for k in range(len(MULTICLICK)):
            for v in sorted(set(self.seen[k])):
                mode = modes.get(v, "off")
                args = [EGGCONFIG, "multiclick",
                        ["left", "right", "middle", "forward", "back"][k], mode]
                if mode == "off":
                    args.append(str(v))
                out = subprocess.run(args, capture_output=True, text=True,
                                     cwd=ROOT).stdout
                if "would write 0x%02x" % v not in out:
                    bad.append("button %d value 0x%02x: %s"
                               % (k, v, out.splitlines()[0] if out else "(no output)"))
        self.assertEqual([], bad, "; ".join(bad))


if __name__ == "__main__":
    unittest.main(verbosity=2)
