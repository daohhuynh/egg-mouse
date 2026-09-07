"""Tests/test_ingest_config.py -- the config-tool ingest, and proof it can fail.

The owner asked whether adapting to an uploaded config version was "genuinely
impossible, or just difficult but still possible". The answer shipped as
Tools/pe/ingest_config.py: possible, by RE-DERIVING every byte egg-config can
write from the new binary rather than assuming the layout held.

A tool that says SAFE about everything is not evidence of anything (engineering-rules.md
§6.2). So this does two things:

  1. Requires all four shipped config tools to re-derive identically -- and
     cfg100 matters most, because it is a SEPARATE CODE BASE with a different
     settings-object base (0x5dbf40 against cfg107's 0x57f210). Deriving the
     same record offsets there is what shows the locators are finding the
     protocol rather than matching one compiler's output.
  2. Plants defects and requires each to be caught.
"""
import importlib.util
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "ghidra-export"))

_spec = importlib.util.spec_from_file_location(
    "ingest_config", os.path.join(ROOT, "Tools", "pe", "ingest_config.py"))
ic = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ic)

import recmap  # noqa: E402

TAGS = ["cfg107", "cfg104", "cfg101", "cfg100"]

# Where each version keeps the settings object. Recorded because it is the
# thing that MUST differ between builds -- if these ever coincide, the base is
# being assumed somewhere rather than derived.
EXPECTED_BASE = {
    "cfg107": 0x57F210,
    "cfg104": 0x57E210,
    "cfg101": 0x57F2B0,
    "cfg100": 0x5DBF40,
}


def have(tag):
    return os.path.exists(os.path.join(ROOT, recmap.TAGS[tag]))


class EveryShippedConfigToolReDerives(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = {t: ic.analyse(t) for t in TAGS if have(t)}
        if not cls.r:
            raise unittest.SkipTest("no config binaries present")

    def test_all_of_them_are_safe_to_adopt(self):
        for tag, r in sorted(self.r.items()):
            self.assertTrue(r.get("ok"),
                            "%s: %s" % (tag, "; ".join(r["problems"])))

    def test_the_record_layout_is_identical_in_all_four(self):
        for tag, r in sorted(self.r.items()):
            self.assertEqual([], r["map_diff"], tag)
            self.assertEqual(111, r["map"]["count"], tag)
            self.assertEqual([1, 2, 3, 4], r["map"]["unwritten"], tag)

    def test_the_object_base_is_derived_and_really_does_differ(self):
        """If these ever agree, something is hard-coded that should not be."""
        for tag, r in sorted(self.r.items()):
            self.assertEqual(EXPECTED_BASE[tag], r["base"],
                             "%s: base 0x%06x" % (tag, r.get("base", 0)))
        bases = [r["base"] for r in self.r.values()]
        self.assertEqual(len(bases), len(set(bases)),
                         "two versions cannot share a settings-object base; "
                         "if they appear to, the base is not being derived")

    def test_lod_and_cpi_stage_land_on_the_same_records_everywhere(self):
        for tag, r in sorted(self.r.items()):
            self.assertEqual(0x09, r["fields"]["lod"]["rec"], tag)
            self.assertEqual(11, r["fields"]["lod"]["sites"], tag)
            self.assertEqual(0x0D, r["fields"]["cpi-stage"]["rec"], tag)
            self.assertEqual(4, r["fields"]["cpi-stage"]["sites"], tag)

    def test_the_sensor_angle_clamp_is_the_same_range_in_all_four(self):
        """Corroboration for the bound egg-config now enforces.

        encodeSensorAngle was narrowed from -128..127 to -127..127 on
        2026-09-06 from cfg107 alone. Three other versions -- one of them a
        different code base -- clamp to exactly the same range, which is a
        stronger footing than one binary. Note cfg100/101/104 use `jg` where
        cfg107 uses `jge`; the bounds are read as VALUES for that reason.
        """
        for tag, r in sorted(self.r.items()):
            self.assertEqual((-127, 127), r["clamp"], tag)


class TheRefusalActuallyWorks(unittest.TestCase):
    """§6.2: report the harness's failure rate; zero is a red flag."""

    def setUp(self):
        if not have("cfg107"):
            self.skipTest("cfg107 not present")
        self.shipped = ic.shipped_button_table

    def tearDown(self):
        ic.shipped_button_table = self.shipped

    def test_a_field_that_moved_record_is_caught(self):
        # Pretend egg-config writes LOD to record 0x0a. The binary says 0x09,
        # and adopting that silently would send a lift-off distance to whatever
        # record 0x0a means -- which is the CPI stage index.
        real = dict(ic.FIELD_EXPECTATIONS)
        try:
            ic.FIELD_EXPECTATIONS["lod"] = (set(range(0, 11)), 0x0A)
            bad = ic.analyse("cfg107")
        finally:
            ic.FIELD_EXPECTATIONS.clear()
            ic.FIELD_EXPECTATIONS.update(real)
        self.assertFalse(bad["ok"])
        self.assertTrue(any("lod MOVED" in p for p in bad["problems"]),
                        "expected a MOVED report, got: %s" % bad["problems"])

    def test_a_field_whose_value_set_changed_is_caught(self):
        # A version offering TWELVE lift-off distances instead of eleven. The
        # set comparison is equality, so this must not pass as "close enough".
        real = dict(ic.FIELD_EXPECTATIONS)
        try:
            ic.FIELD_EXPECTATIONS["lod"] = (set(range(0, 12)), 0x09)
            bad = ic.analyse("cfg107")
        finally:
            ic.FIELD_EXPECTATIONS.clear()
            ic.FIELD_EXPECTATIONS.update(real)
        self.assertFalse(bad["ok"])
        self.assertTrue(any("lod:" in p for p in bad["problems"]),
                        "expected lod to fail to locate, got: %s"
                        % bad["problems"])

    def test_a_wrong_button_byte_is_caught(self):
        def fake():
            t = self.shipped()
            t["volume-up"] = (0x20, 0xE8)     # off by one
            return t
        ic.shipped_button_table = fake
        r = ic.analyse("cfg107")
        self.assertFalse(r["ok"])
        self.assertTrue(any("volume-up" in p for p in r["problems"]),
                        "expected volume-up to fail to re-derive, got: %s"
                        % r["problems"])

    def test_a_firmware_updater_is_refused_not_analysed(self):
        exe = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe")
        if not os.path.exists(exe):
            self.skipTest("updater not present")
        r = ic.analyse(exe)
        self.assertFalse(r.get("ok"))
        self.assertTrue(r["problems"])

    def test_a_missing_file_is_refused(self):
        r = ic.analyse(os.path.join(ROOT, "no-such-config-tool.exe"))
        self.assertFalse(r.get("ok"))
        self.assertTrue(any("no such file" in p for p in r["problems"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
