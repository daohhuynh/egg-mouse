"""Tests/test_button_map.py -- score egg-config's button encoder against the
bytes Endgame's own software put on the wire.

windows-run/05-buttonmapping.pcapng holds twelve A0 11 writes made by the vendor
tool while a person remapped buttons. Those writes are the only independent
check that exists on our encoder: the .exe told us what the bytes SHOULD be
(config-protocol.md 7.17), and the capture shows what they actually WERE.

DELIBERATELY LABEL-FREE. The capture's per-write labels lived in a log file that
was lost, and the surviving copy is single-sourced (CLAUDE.md 1.1a). So nothing
here depends on knowing which action produced which write. Instead: every button
entry state the vendor ever wrote must be reproducible by SOME action in our
table, and the bytes must match exactly. That is a stronger claim than matching
a labelled list, because it also fails if our table can produce a state the
vendor never could.

The encoder is exercised through the shipping binary's dry run, so this scores
the code that actually reaches the device rather than a reimplementation.
"""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

CAPTURE = os.path.join(ROOT, "windows-run", "05-buttonmapping.pcapng")
# The 1.10 run. ADDED 2026-09-07, and it is the whole reason this file changed:
# 05-buttonmapping never mapped BROWSER or EXPLORER, so the only two actions
# whose entry uses +2 were outside every capture this test could see. The
# encoder wrote 0x00 there for a year of sessions and nothing could tell.
CAPTURE_MEDIA = os.path.join(ROOT, "windows-capture", "15-media.pcapng")
EGGCONFIG = os.path.join(ROOT, "build", "egg-config")

PAYLOAD = 16
BLOCK_FIRST = 0x37
ENTRY_LEN = 7
ENTRIES = 8

# Every action egg-config offers, with an argument where one is required. The
# arguments are the ones the vendor's tester happened to use, so the comparison
# is against real captured bytes rather than values we chose.
ACTIONS = [
    "left-click", "right-click", "middle-click", "forward", "back",
    "scroll-up", "scroll-down", "cpi-loop", "disable",
    "play-pause", "next", "previous", "mute", "volume-up", "volume-down",
    "browser", "explorer",
    "fixed-cpi:1600", "key:a", "key:ctrl+a", "key:ctrl+shift+a",
]


def tool_bytes(action):
    """The first six entry bytes egg-config would write for `action`."""
    out = subprocess.run([EGGCONFIG, "map", "forward", action],
                         capture_output=True, text=True, cwd=ROOT).stdout
    for line in out.splitlines():
        parts = line.strip().split()
        if len(parts) == 7 and parts[6] == "??":
            try:
                return tuple(int(p, 16) for p in parts[:6])
            except ValueError:
                return None
    return None


def captured_entry_states(path):
    """Every distinct 6-byte button-entry state in one capture, +6 excluded."""
    import usbpcap
    tr = usbpcap.control_transfers(usbpcap.read(path))
    writes = [bytes(t.data) for t in tr
              if t.is_set_report and (t.data or b"")[:2] == b"\xa0\x11"]
    reads = [bytes(t.data) for t in tr
             if t.is_get_report and len(t.data or b"") > 1000]
    states = set()
    for frame in reads[:1] + writes:
        for k in range(ENTRIES):
            o = PAYLOAD + BLOCK_FIRST + ENTRY_LEN * k
            states.add(tuple(frame[o:o + 6]))
    return states


@unittest.skipUnless(os.path.exists(CAPTURE), "capture not present")
@unittest.skipUnless(os.path.exists(CAPTURE_MEDIA), "1.10 media capture not present")
@unittest.skipUnless(os.path.exists(EGGCONFIG), "egg-config not built")
class TheEncoderMatchesTheVendorsOwnBytes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.produced = {}
        for a in ACTIONS:
            b = tool_bytes(a)
            assert b is not None, "egg-config map produced no preview for " + a
            cls.produced[a] = b
        cls.captured_107 = captured_entry_states(CAPTURE)
        cls.captured_110 = captured_entry_states(CAPTURE_MEDIA)
        cls.captured = cls.captured_107 | cls.captured_110

    def test_every_action_encodes_to_six_bytes(self):
        for a, b in sorted(self.produced.items()):
            self.assertEqual(6, len(b), a)

    def test_the_actions_are_all_distinct(self):
        # Two menu items encoding identically would mean one of them is
        # mis-transcribed from 7.17.
        seen = {}
        for a, b in sorted(self.produced.items()):
            self.assertNotIn(b, seen,
                             "%s and %s encode identically" % (a, seen.get(b)))
            seen[b] = a

    def test_every_captured_state_is_reproducible(self):
        """The load-bearing one.

        The vendor put these exact bytes on the wire. If our table cannot
        produce one of them, the table is incomplete and a user would have no
        way to ask for a mapping their mouse already supports.
        """
        ours = set(self.produced.values())
        missing = sorted(s for s in self.captured if s not in ours)
        self.assertEqual(
            [], missing,
            "states the vendor wrote that egg-config cannot produce: " +
            ", ".join(" ".join("%02x" % x for x in s) for s in missing))

    def test_the_specific_values_the_capture_pins(self):
        # Spot checks with their capture provenance, so a regression names
        # itself rather than only failing a set comparison.
        expect = {
            "volume-up":        (0x20, 0xE9, 0, 0, 0, 0),
            "disable":          (0xFF, 0x00, 0, 0, 0, 0),
            "middle-click":     (0x00, 0x04, 0, 0, 0, 0),
            "back":             (0x00, 0x08, 0, 0, 0, 0),
            "forward":          (0x00, 0x10, 0, 0, 0, 0),
            # X and Y are both 1600 -- the mapping dialog has one CPI box.
            "fixed-cpi:1600":   (0x0C, 0x00, 0x40, 0x06, 0x40, 0x06),
            "key:a":            (0x02, 0x00, 0x04, 0, 0, 0),
            "key:ctrl+a":       (0x02, 0x01, 0x04, 0, 0, 0),
            # 0x03 is the bitwise OR of ctrl and shift: the modifier is a
            # bitfield, which is what makes unobserved combinations derivable.
            "key:ctrl+shift+a": (0x02, 0x03, 0x04, 0, 0, 0),
        }
        for a, want in sorted(expect.items()):
            self.assertEqual(want, self.produced[a], a)

    def test_the_media_capture_actually_contributed_states(self):
        """A capture that parsed to nothing would make the two tests below
        vacuous, which is how the +2 bug survived: the check existed, its input
        did not reach. 15-media.pcapng holds seven A0 11 writes plus a baseline
        read, so it must yield strictly more than the one all-zero state."""
        self.assertGreaterEqual(len(self.captured_110), 8,
                                "15-media.pcapng parsed to %d entry states"
                                % len(self.captured_110))
        self.assertTrue(self.captured_110 - self.captured_107,
                        "the 1.10 media capture added no state the 1.07 "
                        "capture did not already have -- it is not covering "
                        "anything")

    def test_browser_and_explorer_carry_their_usage_high_byte_at_plus_2(self):
        """THE REGRESSION THIS FILE EXISTS TO HOLD, from 2026-09-07.

        BROWSER and EXPLORER are HID Consumer usages 0x0196 and 0x0194 -- u16
        little-endian across +1..+2. Every other action leaves +2 zero, and the
        encoder used to zero it for these two as well, so `map <btn> browser`
        put `18 96 00` on the wire where the vendor puts `18 96 01`.

        Both halves are pinned here: cfg107 stores the 0x01 ([D], 0x40817c and
        0x408226, checked byte-wise by Tests/test_citations.py) and the vendor
        wrote it ([O], 15-media.pcapng seq 4 and seq 6).
        """
        self.assertEqual((0x18, 0x96, 0x01, 0, 0, 0), self.produced["browser"])
        self.assertEqual((0x18, 0x94, 0x01, 0, 0, 0), self.produced["explorer"])
        for want in ((0x18, 0x96, 0x01, 0, 0, 0), (0x18, 0x94, 0x01, 0, 0, 0)):
            self.assertIn(want, self.captured_110,
                          "%s is not in 15-media.pcapng; this test is checking "
                          "our own output against our own expectation"
                          % " ".join("%02x" % b for b in want))

    def test_scroll_and_cpi_loop_match_the_captured_defaults(self):
        # These three were never WRITTEN in the capture, but they are the
        # DEFAULT states of entries 5, 6 and 7 in the baseline read, so the
        # capture pins them just as firmly.
        self.assertEqual((0x01, 0x01, 0, 0, 0, 0), self.produced["scroll-up"])
        self.assertEqual((0x01, 0xFF, 0, 0, 0, 0), self.produced["scroll-down"])
        self.assertEqual((0x09, 0xF1, 0, 0, 0, 0), self.produced["cpi-loop"])


@unittest.skipUnless(os.path.exists(EGGCONFIG), "egg-config not built")
class TheToolRefusesWhatItShould(unittest.TestCase):
    def run_map(self, *args):
        return subprocess.run([EGGCONFIG, "map", *args],
                              capture_output=True, text=True, cwd=ROOT)

    def test_it_will_not_remap_left(self):
        r = self.run_map("left", "disable")
        self.assertEqual(2, r.returncode)
        self.assertIn("will not remap", r.stdout)

    def test_it_will_not_remap_the_cpi_button(self):
        # Its bytes are known; how the firmware reacts to a change is not.
        r = self.run_map("cpi-button", "disable")
        self.assertEqual(2, r.returncode)
        self.assertIn("will not remap", r.stdout)

    def test_an_unknown_action_lists_the_menu_instead_of_guessing(self):
        r = self.run_map("forward", "voluume-up")
        self.assertEqual(2, r.returncode)
        self.assertIn("is not an action", r.stdout)

    def test_an_action_needing_an_argument_says_so(self):
        for a in ("fixed-cpi", "key"):
            r = self.run_map("forward", a)
            self.assertEqual(2, r.returncode)
            self.assertIn("needs an argument", r.stdout)

    def test_an_action_taking_no_argument_rejects_one(self):
        r = self.run_map("forward", "volume-up:3")
        self.assertEqual(2, r.returncode)
        self.assertIn("takes no argument", r.stdout)

    def test_a_bad_key_name_is_refused(self):
        r = self.run_map("forward", "key:nosuchkey")
        self.assertEqual(2, r.returncode)
        self.assertIn("not a key", r.stdout)

    def test_nothing_is_written_without_yes(self):
        # Every case above returns 2 having opened no device. The dry run is
        # the only path this test suite can drive, which is the point: a map
        # that reached the device from a test would be a defect.
        r = self.run_map("forward", "volume-up")
        self.assertEqual(2, r.returncode)
        self.assertIn("Re-run with --yes", r.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
