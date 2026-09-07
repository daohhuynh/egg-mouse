"""Tests/test_ingest_row.py -- the row `ingest.py` prints must COMPILE.

WHY THIS EXISTS. Tests/test_manifest.py re-derives every value in
FirmwareManifest.cpp from the .exe files by a separate implementation, to the
letter. What nothing tested was the ARTEFACT a human copies: the block of C++
`ingest.py` prints and then instructs the user to paste and rebuild.

It was wrong. `provenOnDevice` was missing from the emitted row, so the
provenance string landed in the bool's slot; clang rejects that as a narrowing
conversion, so the build broke with nothing pointing at the row. Adopting a new
Endgame release -- the one workflow this tool exists for -- did not work, and
had not since the field was added.

The check here is the strongest available and needs no .exe: take `emit_row`'s
output, wrap it in a translation unit that includes the REAL header, and
compile it. If the struct gains, loses or reorders a field, this fails.

It also pins the two things a compiler cannot see:
  - provenOnDevice is `false`, and only ever false. CLAUDE.md §5: proven means
    flashed onto the one mouse and verified, which no file can say about itself.
  - the row carries a real date, not the literal "<date>" the old version
    emitted and told the user to go and fix by hand.
"""
import importlib.util
import os
import re
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR_DIR = os.path.join(ROOT, "Sources", "EGGFlashCore", "include")


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


ingest = _load(os.path.join(ROOT, "Tools", "pe", "ingest.py"), "ingest")

# A synthetic record with every field emit_row reads. Deliberately NOT derived
# from a real .exe: *.exe is gitignored, and a test that silently skips itself
# when a file is absent is a test that measures nothing.
RECORD = {
    "path": "/somewhere/Endgame Gear OP1 8k v2 Firmware Updater 1.11.exe",
    "label": "1.11",
    "version": [1, 1, 1, 0],
    "exe_sha256": "a" * 64,
    "id": 140,
    "image": {"size": 66560, "sha256": "b" * 64},
    "checksum": 0x0081D57D,
    "resources": [{"id": 133}, {"id": 135}, {"id": 137},
                  {"id": 140}, {"id": 141}, {"id": 142}],
}


def _cxx():
    for c in ("clang++", "c++", "g++"):
        try:
            subprocess.run([c, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
            return c
        except Exception:
            continue
    return None


class IngestRowCompiles(unittest.TestCase):

    def test_row_compiles_against_the_real_struct(self):
        cxx = _cxx()
        self.assertIsNotNone(cxx, "no C++ compiler found; this test needs one")
        row = ingest.emit_row(RECORD)
        # `extern` first, then the definition -- the same shape
        # FirmwareManifest.cpp uses. Without it clang reports the array as an
        # unused const variable and -Werror turns that into the failure,
        # hiding whatever the row itself was doing wrong.
        tu = ('#include "egg/FirmwareManifest.h"\n'
              "namespace egg::fw {\n"
              "extern const Release kIngestRowUnderTest[];\n"
              "const Release kIngestRowUnderTest[] = {\n"
              "%s\n};\n}\n" % row)
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "row.cpp")
            with open(src, "w") as f:
                f.write(tu)
            # -Werror on purpose. A missing trailing field is only a WARNING
            # (-Wmissing-field-initializers), and a row that silently leaves
            # `provenance` null is exactly the shape of defect this catches.
            p = subprocess.run(
                [cxx, "-std=c++17", "-fsyntax-only", "-Wall", "-Wextra",
                 "-Werror", "-I", HDR_DIR, src],
                capture_output=True, text=True)
        self.assertEqual(
            p.returncode, 0,
            "the row ingest.py tells a user to paste does not compile:\n"
            + p.stderr + "\n--- the row ---\n" + row)

    def test_the_harness_can_fail(self):
        """§6.2: a check that cannot produce a bad result is not evidence.

        Drop the `false,` line -- the exact defect that shipped -- and the
        compile above must reject it. If this passes, the compile test proves
        nothing.
        """
        cxx = _cxx()
        self.assertIsNotNone(cxx)
        broken = ingest.emit_row(RECORD).replace("     false,\n", "")
        self.assertNotIn("false,", broken.split("0x")[-1],
                         "the plant did not actually remove the field")
        tu = ('#include "egg/FirmwareManifest.h"\n'
              "namespace egg::fw {\n"
              "extern const Release kBroken[];\n"
              "const Release kBroken[] = {\n%s\n};\n}\n" % broken)
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "broken.cpp")
            with open(src, "w") as f:
                f.write(tu)
            p = subprocess.run(
                [cxx, "-std=c++17", "-fsyntax-only", "-Wall", "-Wextra",
                 "-Werror", "-I", HDR_DIR, src],
                capture_output=True, text=True)
        self.assertNotEqual(
            p.returncode, 0,
            "a row with provenOnDevice missing COMPILED. The test above is "
            "then vacuous, and the defect it exists to catch would ship again.")

    def test_proven_is_false_and_cannot_be_anything_else(self):
        row = ingest.emit_row(RECORD)
        self.assertIn("     false,\n", row)
        self.assertNotIn("true", row)
        # And not merely absent from this one record: nothing in the source
        # can put `true` in that slot.
        with open(os.path.join(ROOT, "Tools", "pe", "ingest.py")) as f:
            src = f.read()
        body = src[src.index("def emit_row"):src.index("def _today")]
        self.assertNotIn("true", body.replace("Tools/pe", ""))

    def test_the_date_is_real(self):
        row = ingest.emit_row(RECORD)
        self.assertNotIn("<date>", row)
        self.assertRegex(row, r"// Ingested \d{4}-\d{2}-\d{2} by")

    def test_the_row_names_the_file_and_the_label(self):
        row = ingest.emit_row(RECORD)
        self.assertIn("Firmware Updater 1.11.exe", row)
        self.assertIn('{"1.11",', row)
        self.assertIn("a" * 64, row)
        self.assertIn("b" * 64, row)
        self.assertIn("0x0081d57du", row)
        self.assertIn("140,", row)
        self.assertIn("66560,", row)

    def test_the_field_count_matches_the_struct(self):
        """A belt-and-braces check the compiler already makes, kept because it
        localises the failure: it names the number of fields rather than
        printing a page of clang diagnostics."""
        with open(os.path.join(HDR_DIR, "egg", "FirmwareManifest.h")) as f:
            hdr = f.read()
        body = hdr[hdr.index("struct Release {"):]
        body = body[:body.index("\n};")]
        # One declaration per member; skip comments and blank lines.
        members = [l for l in body.split("\n")[1:]
                   if l.strip() and not l.strip().startswith("//")]
        self.assertEqual(len(members), 9,
                         "struct Release changed shape; emit_row needs the "
                         "same change: " + repr(members))

    def test_the_checklist_names_every_hand_edit(self):
        """The row is not enough to adopt a release, and only one of the other
        edits announces itself when it is forgotten. The tool has to say so."""
        with open(os.path.join(ROOT, "Tools", "pe", "ingest.py")) as f:
            src = f.read()
        for needed in ("FirmwareManifest.cpp", "test_manifest.py",
                       "provenOnDevice", "ctest"):
            self.assertIn(needed, src,
                          "ingest.py's instructions do not mention " + needed)


if __name__ == "__main__":
    unittest.main()
