#!/usr/bin/env python3
"""test_handedness.py -- score `egg-config handedness` against the ONE capture
in which the vendor's Left-handed Mode box was actually ticked.

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
import re
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

EGGCONFIG = os.path.join(ROOT, "build", "egg-config")
CAPTURES = os.path.join(ROOT, "windows-run")

# BOTH capture runs. windows-run/ is the firmware-1.07 session; windows-capture/
# is the 1.10 one taken 2026-09-06. Tools/capture/whatmoved.py was widened to
# read both in fca893a and the ctest gates were not, so the 26 settings records
# that cover CPI stages 3/4, FIXED CPI with X != Y, the MEDIA actions and the
# key set sat outside every automated check (found 2026-09-07).
CAPTURE_DIRS = [os.path.join(ROOT, d) for d in ("windows-run", "windows-capture")]


def every_capture():
    """(directory, filename) for every .pcapng in the repo, in a stable order."""
    out = []
    for d in CAPTURE_DIRS:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".pcapng"):
                out.append((d, name))
    return out

PAYLOAD = 16
LARGE = 0x411
BLOCK = 0x37
ENTRY = 7
MULTICLICK = [0x3D, 0x44, 0x4B, 0x52, 0x59]


def records(name, directory=None):
    """Every settings record in a capture, in order.

    Structural selection, the same rule Tests/test_cpi.py uses: a large GET is a
    settings record only when the SET before it was `A1 12`. Writes are `A0 11`.

    `directory` defaults to windows-run so existing callers are unchanged; the
    corpus-wide gates pass it explicitly.
    """
    import usbpcap
    tr = usbpcap.control_transfers(
        usbpcap.read(os.path.join(directory or CAPTURES, name)))
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
        self.assertEqual(0, rc, out)
        self.assertEqual(block(self.after).hex(), got.hex())
        self.assertIn("Nothing was sent. No device was opened.", out)

    def test_the_exit_code_distinguishes_a_preview_from_a_refusal(self):
        """`--from` exits 0. A verb with NEITHER --from nor --yes exits 2.

        THIS CHANGED ON 2026-09-07 and the change was deliberate, so the
        reasoning is here rather than in a commit message. This test asserted
        `2` with the message "a dry run must not report success", which was a
        fair instinct and the wrong code, for three reasons:

        1. **2 is a PROMPT.** Everywhere else in this tool it means "I did not
           do it, and you probably meant me to -- re-run with --yes." There is
           no --yes form of `--from`: the CLI refuses the two together, per
           verb. So the sentence 2 exists to say is not true here.
        2. **The other purely-offline verbs already exit 0.** `dryrun` and
           `encode` both do, and `--from` is the same kind of thing: a complete
           operation that did exactly what was asked and touched no device.
        3. **It collapsed a success onto a failure.** A bad button name also
           exits 2. With `--from` exiting 2 as well, nothing -- not a script,
           not the GUI, whose ToolResult.ok is `status == 0` -- could tell "here
           is your frame" from "your arguments were wrong."

        Pinned in BOTH directions, because the point is the distinction and a
        test of one half would let the other drift into agreeing with it.
        """
        save(self.before, self.tmp)

        # --from, valid: a completed offline operation.
        r = subprocess.run([EGGCONFIG, "handedness", "left", "--from", self.tmp],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(0, r.returncode, r.stdout + r.stderr)

        # --from, INVALID argument: still a refusal, and distinguishable.
        r = subprocess.run([EGGCONFIG, "handedness", "sideways", "--from", self.tmp],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(2, r.returncode,
                         "a bad argument must not look like a good preview")

        # --from together with --yes: refused, per verb. Every one of these
        # carries --from, so none of them can reach a device.
        for argv in (["handedness", "left"],
                     ["map", "middle", "browser"],
                     ["cpi", "2", "800"],
                     ["multiclick", "left", "off", "8"]):
            r = subprocess.run([EGGCONFIG] + argv + ["--from", self.tmp, "--yes"],
                               capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(2, r.returncode, " ".join(argv))
            self.assertIn("--from is offline only", r.stdout + r.stderr)

        # ...and WITHOUT --from and without --yes: still the prompt, still 2.
        #
        # `handedness` IS DELIBERATELY NOT IN THIS LIST, and the reason is the
        # point rather than an inconvenience. Its preview OPENS THE DEVICE AND
        # READS IT: §7.20 makes handedness a MOVE between button entries rather
        # than a flag, so the tool cannot say what it would write without
        # knowing what is there. `egg-config handedness left` therefore puts an
        # A1 12 on the wire, and a test that runs it is a test that talks to the
        # mouse. This suite must never do that -- engineering-rules.md §4.2a: the question
        # "what does this answer, and can it be answered without touching the
        # device?" has an answer here, and the answer is the three verbs below.
        #
        # (Found the honest way, 2026-09-07: the first version of this test DID
        # include handedness, and it exited 1 -- Device::open found no openable
        # interface and returned before any frame, so nothing went out. It was a
        # near miss, not a hit, and the fix is structural rather than a promise
        # to remember.)
        for argv in (["map", "middle", "browser"],
                     ["cpi", "2", "800"],
                     ["multiclick", "left", "off", "8"]):
            r = subprocess.run([EGGCONFIG] + argv,
                               capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(2, r.returncode,
                             "%s with no --yes must stay a prompt" % " ".join(argv))
            self.assertNotIn("Nothing was sent", r.stdout,
                             "the bare preview is the SHORT one; the whole-frame "
                             "form is what --from is for")

    def test_all_four_run_verbs_preview_offline_and_send_nothing(self):
        """The §4.3 dry run, for the four verbs that write a RUN of bytes.

        Each must print a whole 1041-byte frame from a file. `dryrun` cannot
        express any of them -- it takes a FIELD and a value -- so before
        2026-09-07 the four newest write paths, the ones most likely to be got
        wrong, were the four with no way to show what they would send.
        """
        save(self.before, self.tmp)
        # `left`, not `right`: self.before IS right-handed, so `right` is a
        # no-op that returns before the frame is ever built. That case is worth
        # asserting too, and it is -- separately, below.
        for argv in (["handedness", "left"],
                     ["map", "middle", "browser"],
                     ["cpi", "3", "1200", "2400"],
                     ["multiclick", "left", "off", "12"]):
            with self.subTest(verb=" ".join(argv)):
                r = subprocess.run([EGGCONFIG] + argv + ["--from", self.tmp],
                                   capture_output=True, text=True, cwd=ROOT)
                self.assertEqual(0, r.returncode, r.stdout + r.stderr)
                self.assertIn("Nothing was sent. No device was opened.", r.stdout)
                hexlines = [l for l in r.stdout.splitlines()
                            if re.match(r"^  [0-9a-f]{4}  [0-9a-f]+$", l)]
                self.assertEqual(33, len(hexlines),
                                 "expected the whole 1041-byte frame, got %d "
                                 "hex lines" % len(hexlines))
                body = "".join(l.split()[1] for l in hexlines)
                self.assertEqual(0x411 * 2, len(body))
                self.assertTrue(body.startswith("a011"),
                                "the frame must carry report 0xa0, command 0x11")

    def test_an_offline_no_op_still_says_it_was_offline(self):
        """A --from run that changes nothing must not read like a device run.

        `handedness right` on a record that is already right-handed returns
        before any frame is built, so it never reaches the shared preview. The
        sentence it prints on its own is true of BOTH paths, and only one of
        them left the mouse alone.
        """
        save(self.before, self.tmp)
        r = subprocess.run([EGGCONFIG, "handedness", "right", "--from", self.tmp],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(0, r.returncode, r.stdout + r.stderr)
        self.assertIn("Already right-handed", r.stdout)
        self.assertIn("Nothing was sent. No device was opened.", r.stdout)
        self.assertNotIn("  0000  ", r.stdout, "a no-op prints no frame")

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
        for directory, name in every_capture():      # both runs, 2026-09-07
            for f in records(name, directory):
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
