"""Tests/test_adoption.py -- the config-tool adoption record must be true.

`notes/config-adoption.md` says which configuration tools have been vetted.
It is hand-pasted from `Tools/pe/ingest_config.py`'s output, so it can go stale
or be mistyped in exactly the way a file nobody re-runs always can. This
re-derives every row from the .exe it names.

WHY IT MATTERS THAT THIS IS CHECKED RATHER THAN TRUSTED. CLAUDE.md §1.7's
whole complaint about prose is that "a number written in prose survives a
context compaction while the computation behind it does not, so it returns
looking like established fact". An adoption record is that failure mode with a
safety consequence attached: a row saying cfg1.08 was vetted, when it was not,
is worse than no row, because it stops anybody vetting it.
"""
import hashlib
import importlib.util
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "ghidra-export"))
DOC = os.path.join(ROOT, "notes", "config-adoption.md")


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


ic = _load(os.path.join(ROOT, "Tools", "pe", "ingest_config.py"), "ingest_cfg_a")

ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*([^|]+?)\s*\|\s*([0-9-]+)\s*\|\s*"
                 r"([0-9a-f]{16})\.\.\.\s*\|\s*(\d+) offsets, high 0x([0-9a-f]{2})"
                 r"\s*\|\s*([^|]+?)\s*\|\s*$")

SEARCH_DIRS = ["", "old-config-executables"]


def rows():
    out = []
    with open(DOC) as f:
        for line in f:
            m = ROW.match(line.rstrip("\n"))
            if m:
                out.append(m.groups())
    return out


def locate(name):
    for d in SEARCH_DIRS:
        p = os.path.join(ROOT, d, name)
        if os.path.exists(p):
            return p
    return None


class TheRecordParses(unittest.TestCase):
    def test_there_are_rows(self):
        self.assertGreaterEqual(len(rows()), 4, "no adoption rows parsed")

    def test_no_row_still_says_label_it(self):
        # ingest_config.py emits "(label it)" for the version column on
        # purpose: the marketing label is not mechanically recoverable. A row
        # pasted without filling it in is an unfinished row.
        for name, label, *_ in rows():
            self.assertNotIn("label it", label, name)

    def test_every_named_file_exists(self):
        for name, *_ in rows():
            self.assertIsNotNone(locate(name), "%s is listed but not present"
                                 % name)


class EveryRowReDerives(unittest.TestCase):
    def test_the_hash_prefix_matches_the_file(self):
        for name, label, date, sha16, count, high, ref in rows():
            p = locate(name)
            with open(p, "rb") as f:
                got = hashlib.sha256(f.read()).hexdigest()
            self.assertTrue(got.startswith(sha16),
                            "%s: recorded %s..., file is %s..."
                            % (name, sha16, got[:16]))

    def test_the_record_map_matches_the_binary(self):
        for name, label, date, sha16, count, high, ref in rows():
            r = ic.analyse(locate(name))
            self.assertTrue(r.get("ok"),
                            "%s is listed as adopted but does not re-derive: %s"
                            % (name, r["problems"]))
            self.assertEqual(r["map"]["count"], int(count), name)
            self.assertEqual(r["map"]["high"], int(high, 16), name)

    def test_a_wrong_hash_would_be_caught(self):
        # §6.2: the check must be able to fail. Same comparison, one nibble
        # changed in the recorded value.
        name = rows()[0][0]
        recorded = rows()[0][3]
        wrong = ("f" if recorded[0] != "f" else "0") + recorded[1:]
        with open(locate(name), "rb") as f:
            got = hashlib.sha256(f.read()).hexdigest()
        self.assertFalse(got.startswith(wrong))


if __name__ == "__main__":
    unittest.main(verbosity=2)
