"""Tests/test_manifest.py -- the firmware manifest, checked against the .exe
files by a completely separate implementation.

THE RISK THIS COVERS. CLAUDE.md §1.4 says the firmware resource id must be a
compile-time constant, "never chosen from a list", because updater 1.10 carries
six FWFILE resources of identical length and five of them would be accepted by
the device -- right size, valid per-block checksums, reading back exactly as
written. §2 assumes the device validates nothing, so a wrong-but-well-formed
image is a brick with no downstream check at all.

Supporting several releases means the table has several rows. A wrong hash in a
row, a transposed id, a row whose `provenOnDevice` was copied from the one above
-- none of those would fail to compile, and the flasher would then refuse a good
.exe or, far worse, accept the wrong image with everything reporting success.

So every value in FirmwareManifest.cpp is re-derived here from the .exe itself,
by Python that shares no code with the C++:

  - the updater SHA-256, hashed from the file
  - the FWFILE id, recovered from the vendor's own FindResourceW call site
    (Tools/pe/resource_id.py) -- NOT read out of the resource directory, which
    is the whole distinction §1.4 turns on
  - the image size and SHA-256, from the resource at that id
  - the A0 03 whole-image checksum, summed here rather than trusted

and the structural invariants that a table cannot enforce about itself.
"""
import hashlib
import importlib.util
import os
import re
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "Sources", "EGGFlashCore", "src", "FirmwareManifest.cpp")
PE_DIR = os.path.join(ROOT, "Tools", "pe")


def _load(name):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(PE_DIR, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


rid = _load("resource_id")
fwfile = _load("fwfile")

# Where each release's .exe lives. Deliberately NOT derived from the label:
# the filenames are inconsistent ("1.10.exe", "v1.04.exe") and a rule that
# guessed them would be one more thing to be wrong.
EXE = {
    "1.10": "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe",
    "1.07": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe",
    "1.06": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe",
    "1.04": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe",
}

ROW = re.compile(
    r'\{"([^"]+)",\s*'                      # label
    r'\{(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\},\s*'   # version quadruple
    r'"([0-9a-f]{64})",\s*'                 # updater sha256
    r'(\d+),\s*'                            # resource id
    r'(\d+),\s*'                            # image size
    r'"([0-9a-f]{64})",\s*'                 # image sha256
    r'0x([0-9a-f]{8})u,\s*'                 # whole-image checksum
    r'(true|false),',                       # provenOnDevice
    re.S)


def rows():
    with open(SRC, encoding="utf-8") as f:
        src = f.read()
    return [{"label": m.group(1),
             "version": tuple(int(m.group(i)) for i in (2, 3, 4, 5)),
             "exe_sha": m.group(6),
             "id": int(m.group(7)),
             "size": int(m.group(8)),
             "img_sha": m.group(9),
             "checksum": int(m.group(10), 16),
             "proven": m.group(11) == "true"} for m in ROW.finditer(src)]


class TheTableIsStructurallySound(unittest.TestCase):
    def setUp(self):
        self.rows = rows()

    def test_the_rows_were_actually_parsed(self):
        # A regex that matches nothing makes every test below vacuous, so the
        # count has to be checked against something.
        #
        # IT USED TO BE THE LITERAL 4, AND THAT WAS A SILENT HAND-EDIT. Adding
        # a release meant remembering to bump a number in this file, and
        # forgetting did not announce itself: the suite failed with "parsed 5
        # rows" and read as though the manifest were wrong rather than the
        # test. Tools/pe/ingest.py had to carry a checklist item about it.
        #
        # Counted a SECOND, INDEPENDENT WAY instead: every row ends with a
        # `0x........u,` whole-image checksum on its own line, which the row
        # regex does not rely on. If the two disagree, either the regex is
        # matching nothing or the table has a malformed row -- both of which
        # are what the literal was there to catch, and neither of which
        # requires anyone to remember anything.
        with open(SRC, encoding="utf-8") as f:
            src = f.read()
        checksums = re.findall(r"^\s*0x[0-9a-f]{8}u,\s*$", src, re.M)
        self.assertEqual(len(checksums), len(self.rows),
                         "the row regex found %d rows but there are %d "
                         "whole-image checksum lines: %s"
                         % (len(self.rows), len(checksums),
                            [r["label"] for r in self.rows]))
        self.assertGreaterEqual(len(self.rows), 4,
                                "the four updaters this project holds must all "
                                "be in the manifest; parsed %s"
                                % [r["label"] for r in self.rows])

    def test_every_row_has_an_exe_mapping_in_this_file(self):
        # The OTHER hand-edit ingest.py cannot make. This one does announce
        # itself -- but as a setUp failure in an unrelated class, which reads
        # like an infrastructure problem rather than an instruction. Named here
        # so the message says what to do.
        missing = [r["label"] for r in self.rows if r["label"] not in EXE]
        self.assertEqual([], missing,
                         "manifest rows %s have no .exe path in this file's EXE "
                         "map. Add one line per release; the filenames are "
                         "inconsistent, so it cannot be derived from the label."
                         % missing)

    def test_exactly_one_release_claims_to_be_proven_on_the_device(self):
        """The claim that can only be true once, and cannot be checked by a
        machine -- so it is the one most likely to be copied down a column."""
        proven = [r["label"] for r in self.rows if r["proven"]]
        self.assertEqual(["1.10"], proven,
                         "exactly one row may say provenOnDevice, and it is the "
                         "firmware every [O] in this project came from")

    def test_the_proven_release_is_first_so_primaryRelease_is_right(self):
        # primaryRelease() returns kReleases[0] and the default flash target is
        # whatever it returns. If the proven row moves, the default silently
        # becomes an untested firmware.
        self.assertTrue(self.rows[0]["proven"],
                        "kReleases[0] must be the proven release; "
                        "primaryRelease() returns it and flash defaults to it")

    def test_labels_and_hashes_are_unique(self):
        for key in ("label", "exe_sha", "img_sha"):
            vals = [r[key] for r in self.rows]
            self.assertEqual(len(vals), len(set(vals)), "duplicate %s" % key)

    def test_every_image_is_sixty_five_whole_blocks(self):
        # FlashPlan's block arithmetic was derived against this shape. A
        # release with a different size needs that arithmetic re-derived, not
        # a manifest row.
        for r in self.rows:
            self.assertEqual(66560, r["size"], r["label"])
            self.assertEqual(0, r["size"] % 1024, r["label"])


class EveryRowMatchesItsExecutable(unittest.TestCase):
    """Re-derives each row from the .exe, independently of the C++."""

    def setUp(self):
        self.rows = rows()
        for r in self.rows:
            if r["label"] not in EXE:
                self.fail("manifest row %s has no .exe mapping in this test; "
                          "add one when ingesting a release" % r["label"])

    def each(self):
        for r in self.rows:
            p = os.path.join(ROOT, EXE[r["label"]])
            if not os.path.exists(p):
                continue
            yield r, p

    def test_the_updater_hash_is_the_hash_of_that_file(self):
        n = 0
        for r, p in self.each():
            with open(p, "rb") as f:
                self.assertEqual(hashlib.sha256(f.read()).hexdigest(),
                                 r["exe_sha"], r["label"])
            n += 1
        self.assertGreater(n, 0, "no updater .exe present to check against")

    def test_the_resource_id_is_the_one_the_vendors_code_loads(self):
        """The load-bearing one, and the reason §1.4 survives a manifest.

        This does NOT read the resource directory. It finds the vendor's own
        FindResourceW call, decodes its stdcall pushes from raw bytes, and
        requires the immediate to equal the row's id.
        """
        for r, p in self.each():
            got = rid.derive(p)
            self.assertIsNotNone(
                got["id"],
                "%s: the id could not be recovered from the binary (%s). A row "
                "whose id cannot be re-derived must not stay in the manifest."
                % (r["label"], "; ".join(got["why"])))
            self.assertEqual(got["id"], r["id"],
                             "%s: manifest says FWFILE/%d, the binary's own "
                             "FindResourceW call asks for %d"
                             % (r["label"], r["id"], got["id"]))

    def test_the_image_hash_and_size_match_that_resource(self):
        for r, p in self.each():
            blobs = {name: (off, size, sha)
                     for (name, lang, off, size, sha) in fwfile.fwfiles(p)}
            self.assertIn(r["id"], blobs,
                          "%s has no FWFILE/%d" % (r["label"], r["id"]))
            off, size, sha = blobs[r["id"]]
            self.assertEqual(size, r["size"], r["label"])
            self.assertEqual(sha, r["img_sha"], r["label"])

    def test_the_whole_image_checksum_is_the_sum_of_every_byte(self):
        # [D] FUN_00403580: the four accumulators are a 4-way unroll of one sum.
        # This is the value A0 03 declares, so a wrong one is a value we WRITE.
        for r, p in self.each():
            with open(p, "rb") as f:
                d = f.read()
            blobs = {name: (off, size)
                     for (name, lang, off, size, sha) in fwfile.fwfiles(p)}
            off, size = blobs[r["id"]]
            self.assertEqual(sum(d[off:off + size]) & 0xFFFFFFFF, r["checksum"],
                             r["label"])

    def test_the_version_quadruple_is_the_files_own(self):
        import struct
        for r, p in self.each():
            with open(p, "rb") as f:
                d = f.read()
            m = re.search(re.escape(struct.pack("<I", 0xFEEF04BD)), d)
            self.assertIsNotNone(m, "%s has no VS_FIXEDFILEINFO" % r["label"])
            a, b, c, e = struct.unpack_from("<HHHH", d, m.start() + 8)
            self.assertEqual((b, a, e, c), r["version"], r["label"])

    def test_the_other_resources_are_NOT_what_gets_flashed(self):
        """§1.4's actual danger, made explicit.

        1.10 carries six FWFILE resources of identical length. Five of them
        would pass every structural check the flasher makes. The ONLY thing
        separating them is which id the vendor's code asks for -- so this
        asserts that the row's image hash belongs to that id and to no other,
        and that the alternatives really are indistinguishable by size.
        """
        for r, p in self.each():
            blobs = fwfile.fwfiles(p)
            self.assertGreater(len(blobs), 1,
                               "%s: expected several FWFILE resources" % r["label"])
            same_size = [n for (n, l, o, s, h) in blobs if s == r["size"]]
            self.assertGreater(
                len(same_size), 1,
                "%s: if only one resource has the right size then size alone "
                "would identify the image and this test is not testing what it "
                "claims" % r["label"])
            others = [h for (n, l, o, s, h) in blobs if n != r["id"]]
            self.assertNotIn(r["img_sha"], others,
                             "%s: the flashed image is byte-identical to another "
                             "resource" % r["label"])


class RefusingAnExeNamesWhatItActuallyIs(unittest.TestCase):
    """`egg-flash` selects the row from --version, then requires the .exe to
    hash to it. When it does not, and the file IS another release this build
    knows, the refusal must say which one.

    WHY IT IS TESTED HERE. This is the only place the .exe path map lives, and
    the behaviour is exactly "the manifest's own hash lookup, used". Until
    2026-09-06 `releaseForUpdaterSha` had no caller anywhere in Sources/ while
    `egg-flash help` claimed the .exe was "identified BY ITS OWN SHA-256", so
    naming a 1.06 updater without `--version 1.06` produced a hash mismatch and
    no hint that this binary held the answer.

    IT SELECTS NOTHING, and that is asserted below: the refusal is still a
    refusal, the exit status is still non-zero, and the user still has to type
    --version. CLAUDE.md §1.4 is untouched.
    """

    def setUp(self):
        self.bin = os.path.join(ROOT, "build", "egg-flash")
        if not os.path.exists(self.bin):
            self.skipTest("build/egg-flash not present")
        self.rows = rows()
        self.primary = self.rows[0]["label"]

    def _run(self, path, *extra):
        return subprocess.run([self.bin, "image", path] + list(extra),
                              capture_output=True, text=True, cwd=ROOT)

    def test_a_known_release_named_without_version_is_identified(self):
        n = 0
        for r in self.rows:
            if r["label"] == self.primary:
                continue          # the default row: it would simply be accepted
            p = os.path.join(ROOT, EXE[r["label"]])
            if not os.path.exists(p):
                continue
            out = self._run(p)
            self.assertIn("REFUSED", out.stdout,
                          "%s: naming a non-default release without --version "
                          "must still be refused" % r["label"])
            self.assertIn("That file IS a release this build knows: %s"
                          % r["label"], out.stdout,
                          "%s: the refusal did not say what the file actually "
                          "is:\n%s" % (r["label"], out.stdout))
            self.assertIn("--version %s" % r["label"], out.stdout)
            n += 1
        self.assertGreater(
            n, 0,
            "no non-default updater .exe present, so this test checked nothing")

    def test_an_unknown_file_gets_no_suggestion(self):
        """The hint must not fire on a file that is not a release at all.

        A suggestion naming some release for an arbitrary .exe would be worse
        than no suggestion: it is precisely the "guessing a resource id" shape
        CLAUDE.md §2 says nothing downstream of us would catch.
        """
        candidates = [os.path.join(ROOT, "XM1r_Flash_Upgrade_1.9.46.exe")]
        candidates += [os.path.join(ROOT, EXE[r["label"]]) for r in self.rows]
        alien = None
        for c in candidates[:1]:
            if os.path.exists(c):
                alien = c
        if alien is None:
            # Fall back to any file that is certainly not an updater.
            alien = os.path.join(ROOT, "README.md")
        out = self._run(alien)
        self.assertIn("REFUSED", out.stdout)
        self.assertNotIn("That file IS a release this build knows", out.stdout,
                         "the hint fired for a file that is not a release:\n"
                         + out.stdout)

    def test_the_right_version_is_accepted(self):
        """The falsification for the two above: if every .exe were refused
        whatever was typed, the assertions on REFUSED would pass for the wrong
        reason."""
        n = 0
        for r in self.rows:
            p = os.path.join(ROOT, EXE[r["label"]])
            if not os.path.exists(p):
                continue
            out = self._run(p, "--version", r["label"])
            self.assertNotIn("REFUSED", out.stdout,
                             "%s refused its own .exe:\n%s"
                             % (r["label"], out.stdout))
            self.assertIn(r["img_sha"], out.stdout)
            n += 1
        self.assertGreater(n, 0, "no updater .exe present to check against")


if __name__ == "__main__":
    unittest.main(verbosity=2)
