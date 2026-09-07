"""Tests/test_ingest_errors.py -- what the two ingest tools do when the input
is wrong, which is the only time anyone reads their output closely.

WHY THIS IS A TEST AND NOT A STYLE PREFERENCE. `Sources/EGGApp/UpdatesView.swift`
shows these tools' stdout RAW. Whatever they print is the entire user interface
for adopting a new Endgame release. So:

  - a Python traceback in that pane is the interface, and it says nothing a
    person can act on;
  - a verdict that is absent from the text is absent from the GUI, no matter
    what the exit status is;
  - and an exit status of 0 on a failed check is a green light.

TWO REAL DEFECTS, both fixed 2026-09-06 and both pinned here.

1. `ingest.py` opened the .exe with no handler, so a typo'd path produced an
   OSError traceback and an unexplained exit 1.
2. `ingest_config.py` had a SILENT THIRD STATE. `ok` was computed as
   `not problems and not map_diff`, but a non-empty `map_diff` added nothing to
   `problems` -- so a config version that MOVED a record offset printed neither
   "SAFE" nor "NOT SAFE TO ADOPT" and exited 0. The single thing that tool
   exists to catch was reported by saying nothing. §1.3 is the rule it would
   have walked past: every byte egg-config writes is placed by that map.
"""
import importlib.util
import io
import contextlib
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "ghidra-export"))


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


ingest = _load(os.path.join(ROOT, "Tools", "pe", "ingest.py"), "ingest_e")
ic = _load(os.path.join(ROOT, "Tools", "pe", "ingest_config.py"), "ingest_cfg_e")

CFG107 = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe")


def run(mod, argv):
    """Call a tool's main() and capture what the GUI would show."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mod.main(argv)
    return rc, buf.getvalue()


class TheFirmwareHalfExplainsItself(unittest.TestCase):
    def test_a_missing_file_is_a_sentence_and_a_nonzero_exit(self):
        rc, out = run(ingest, ["ingest.py", "/nope/not-here.exe", "--label", "9.99"])
        self.assertEqual(rc, 1)
        self.assertIn("NOT READY TO INGEST", out)
        self.assertIn("cannot read", out)
        self.assertNotIn("Traceback", out)

    def test_it_does_not_print_findings_about_a_file_it_never_read(self):
        # The first fix printed the problem but kept the header fields, so the
        # pane said "exe SHA-256 None" and "exe size 0 bytes" -- which read as
        # facts about the file rather than the absence of one.
        rc, out = run(ingest, ["ingest.py", "/nope/not-here.exe"])
        self.assertNotIn("None", out)
        self.assertNotIn("0 bytes", out)

    def test_a_directory_is_handled_the_same_way(self):
        rc, out = run(ingest, ["ingest.py", ROOT])
        self.assertEqual(rc, 1)
        self.assertNotIn("Traceback", out)

    def test_no_arguments_prints_the_docstring_and_exits_2(self):
        rc, out = run(ingest, ["ingest.py"])
        self.assertEqual(rc, 2)
        self.assertIn("engineering-rules.md", out)


class TheConfigHalfAlwaysReachesAVerdict(unittest.TestCase):
    def setUp(self):
        if not os.path.exists(CFG107):
            self.skipTest("cfg107 not present")

    def test_a_good_target_says_SAFE_and_exits_0(self):
        rc, out = run(ic, ["ingest_config.py", "cfg107"])
        self.assertEqual(rc, 0)
        self.assertIn("SAFE:", out)
        self.assertNotIn("NOT SAFE TO ADOPT", out)

    def test_a_missing_file_says_NOT_SAFE_and_exits_1(self):
        rc, out = run(ic, ["ingest_config.py", "/nope/not-here.exe"])
        self.assertEqual(rc, 1)
        self.assertIn("NOT SAFE TO ADOPT", out)
        self.assertNotIn("Traceback", out)

    def test_a_MOVED_RECORD_MAP_is_a_problem_and_not_just_a_printed_diff(self):
        # THE DEFECT, reproduced: force the map diff to be non-empty and check
        # that it now reaches `problems`, the verdict line and the exit status.
        # No config tool we hold moves a record, so this is the only way to
        # exercise the one path the tool exists for.
        real = ic.recmap.diff
        ic.recmap.diff = lambda a, b: ["lod: record 0x09 -> 0x0a"]
        try:
            r = ic.analyse("cfg107")
            rc, out = run(ic, ["ingest_config.py", "cfg107"])
        finally:
            ic.recmap.diff = real
        self.assertFalse(r["ok"])
        self.assertTrue(any("record map DIFFERS" in p for p in r["problems"]),
                        r["problems"])
        self.assertEqual(rc, 1)
        self.assertIn("NOT SAFE TO ADOPT", out)
        self.assertIn("0x0a", out)

    def test_the_falsification_of_that_test(self):
        # With diff restored, the same target is SAFE -- so the test above is
        # measuring the patch and not something that always fails.
        r = ic.analyse("cfg107")
        self.assertTrue(r["ok"], r["problems"])

    def test_there_is_no_silent_third_state(self):
        # Belt and braces on the shape rather than on one instance: whatever
        # `analyse` returns, main() prints exactly one verdict and the exit
        # status agrees with which one it printed.
        for patch in (None, ["something moved"]):
            real = ic.recmap.diff
            if patch is not None:
                ic.recmap.diff = lambda a, b: list(patch)
            try:
                rc, out = run(ic, ["ingest_config.py", "cfg107"])
            finally:
                ic.recmap.diff = real
            safe = "SAFE:" in out and "NOT SAFE TO ADOPT" not in out
            unsafe = "NOT SAFE TO ADOPT" in out
            self.assertTrue(safe != unsafe, "verdict is ambiguous or absent")
            self.assertEqual(rc, 0 if safe else 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
