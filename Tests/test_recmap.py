"""Tests/test_recmap.py -- the settings-record serializer extractor.

Two things are being protected here, and they are different in kind.

1. THE EXTRACTOR AGREES WITH THE HAND DERIVATION. Every fact checked below was
   written into notes/config-protocol.md by reading cfg107 by hand, before this
   extractor existed. If the mechanical map reproduces them, the extractor is
   reading the binary the way a person did; if it drifts, one of the two is
   wrong and the disagreement is the finding.

2. THE EXTRACTOR REFUSES WHEN IT CANNOT SEE EVERYTHING. This is the one that
   actually protects the mouse. `recmap` exists so that "has the layout changed
   in a new config tool?" is answered by a script -- and a script that returns a
   PARTIAL map is worse than no script, because a partial map looks complete.
   The first prototype silently returned 108 of 111 offsets. So there is a
   deliberately broken parser below, and the test asserts it FAILS. A harness
   that cannot produce a bad result is not evidence (CLAUDE.md 6.2).
"""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "ghidra-export"))

import recmap  # noqa: E402

# record offset -> object offset, all from notes/config-protocol.md.
HAND_DERIVED = {
    0x72: 0x08,   # 7.11  the "I understand..." acknowledgement
    0x3d: 0x34,   # 7.11  LEFT multiclick / SPDT-1, one byte for both
    0x44: 0x3c,   # 7.11  RIGHT multiclick / SPDT-2
    0x4b: 0x44,   # 7.11  MIDDLE multiclick
    0x52: 0x4c,   # 7.11  FORWARD multiclick
    0x59: 0x54,   # 7.11  BACK multiclick
    0x6f: 0x2b,   # 7.11  unattributed, but its source is known
    0x08: 0x26,   # 7.8   led-on-liftoff (stores the INVERSE of the tick)
    0x70: 0x2c,   # 7.9   sensor angle
}

# 7.11: eight seven-byte button entries from record 0x37, object stride 8.
BUTTON_ENTRIES = {0x37 + 7 * k: 0x2e + 8 * k for k in range(8)}

ALL_VERSIONS = ["cfg100", "cfg101", "cfg104", "cfg107"]


def available(tag):
    return os.path.exists(os.path.join(ROOT, recmap.TAGS[tag]))


class ExtractorAgreesWithTheHandDerivation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not available("cfg107"):
            raise unittest.SkipTest("cfg107 not present")
        cls.r = recmap.extract("cfg107")

    def test_the_partition_is_complete(self):
        self.assertTrue(
            self.r["ok"],
            "cfg107 must yield a complete partition; got: %s"
            % self.r.get("why", ""))
        self.assertEqual([], self.r["unmatched"])
        self.assertEqual([], self.r["collisions"])

    def test_records_0x01_to_0x04_are_not_serialised(self):
        # Protocol.h calls these kRecordUnknownFirst..kRecordUnknownLast. The
        # claim is that the config tool never writes them -- an ABSENCE claim,
        # so it is only worth anything because the extractor scanned the whole
        # serializer body rather than failing to find a pattern (CLAUDE.md 1.2a).
        self.assertEqual([0x01, 0x02, 0x03, 0x04], self.r["unwritten"])

    def test_it_reproduces_every_hand_derived_offset(self):
        for rec, obj in sorted(HAND_DERIVED.items()):
            self.assertEqual(
                obj, self.r["map"].get(rec),
                "record 0x%02x: notes/ say obj 0x%02x" % (rec, obj))

    def test_it_reproduces_the_eight_button_entries(self):
        for rec, obj in sorted(BUTTON_ENTRIES.items()):
            self.assertEqual(
                obj, self.r["map"].get(rec),
                "button entry at record 0x%02x should come from obj 0x%02x"
                % (rec, obj))

    def test_the_record_runs_to_0x72(self):
        self.assertEqual(0x72, self.r["high"])
        self.assertEqual(111, self.r["count"])


class TheLayoutIsStableAcrossEveryShippedVersion(unittest.TestCase):
    def test_all_four_config_tools_have_the_same_record_map(self):
        have = [t for t in ALL_VERSIONS if available(t)]
        if len(have) < 2:
            self.skipTest("need at least two config binaries")
        maps = {t: recmap.extract(t) for t in have}
        for t, r in maps.items():
            self.assertTrue(r["ok"], "%s did not produce a complete map: %s"
                            % (t, r.get("why", "")))
        base = maps[have[-1]]
        for t in have[:-1]:
            self.assertEqual(
                [], recmap.diff(maps[t], base),
                "%s and %s disagree about the record layout" % (t, base["tag"]))


class TheRefusalActuallyWorks(unittest.TestCase):
    """CLAUDE.md 6.2: report the harness's failure rate; zero is a red flag.

    These plant a defect in the extractor and require it to be caught. If these
    ever pass trivially, the completeness check has stopped checking.
    """

    def setUp(self):
        if not available("cfg107"):
            self.skipTest("cfg107 not present")
        self.gap = recmap.MAX_PAIR_GAP
        self.store = recmap.STORE

    def tearDown(self):
        recmap.MAX_PAIR_GAP = self.gap
        recmap.STORE = self.store

    def test_a_too_tight_run_gap_is_caught_not_silently_tolerated(self):
        # This is the ACTUAL first-prototype bug: a gap of 8 splits the run at
        # cfg107 0x4043a3, where record 0x22's load uses a 32-bit displacement,
        # and the orphaned pair is then dropped for being too short.
        recmap.MAX_PAIR_GAP = 8
        r = recmap.extract("cfg107")
        self.assertFalse(
            r["ok"],
            "a gap of 8 loses record 0x22 and the extractor MUST refuse")
        self.assertIn("not matched", r.get("why", ""))

    def test_a_parser_blind_to_movb_loads_is_caught(self):
        # The other half of the prototype bug: record 0x72's store is fed by a
        # `movb` load, not `movzbl`. A parser that only knows movzbl loses it.
        import re
        recmap.STORE = re.compile(
            r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*"
            r"movb\s+%(\w)l, (?:0x([0-9a-f]+))\(%e(\w\w)\)\s*$")  # no zero-disp
        r = recmap.extract("cfg107")
        self.assertFalse(
            r["ok"],
            "a parser that cannot see the zero-displacement store MUST refuse")

    def test_diff_refuses_to_compare_an_incomplete_map(self):
        # A hole compared against a value reads as "identical" if the diff is
        # naive. It must not be.
        good = recmap.extract("cfg107")
        bad = dict(good, ok=False, why="planted")
        out = recmap.diff(bad, good)
        self.assertEqual(1, len(out))
        self.assertIn("REFUSED", out[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
