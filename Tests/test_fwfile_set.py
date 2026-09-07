#!/usr/bin/env python3
"""Pin the FWFILE resource set of every updater we hold.

WHY THE WHOLE SET AND NOT JUST ID 140. Updater 1.10's `.text` is byte-identical
to 1.06's and 1.07's -- same SHA-256 -- and 1.10 still added a sixth FWFILE that
neither of the others has. So a guard keyed on code identity would have accepted
1.10 without noticing a new firmware blob had appeared inside it. The resources
move independently of the code, therefore the resources are what gets pinned.

The five wrong images are the hazard engineering-rules.md §2 describes: each is exactly
66,560 bytes, each would produce valid per-block checksums, and each would read
back exactly as written. Every check in the vendor's own protocol passes on the
wrong image. Only knowing the id separates them, so a silent change to any entry
in this table has to be loud.

    python3 Tests/test_fwfile_set.py
"""
import hashlib
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "pe"))
import fwfile  # noqa: E402

V110 = "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"
V107 = "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe"
V106 = "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe"
V104 = "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe"

# The id the vendor's own binary hardcodes: `push 0x8c` at 0x0040320c feeding
# FindResourceW, 1.04 likewise at file offset 0x373a. See
# notes/flash-wire-observed.md §5.
FLASHED_ID = 140

# id -> sha256, for updater 1.10 only. Regenerate with Tools/pe/fwfile.py.
SET_110 = {
    133: "09ba52978e0b72a4b4b65f640d177de61530636eed5fa5bf97b1ef431f5e62cf",
    135: "4fb2c174736683e6a265c60f4875dd7f8c26472c72be50d57ec00ebcb399cc6f",
    137: "9cf31ca802ae286401c8de486fdeb1dd128196302cb8103f6dafeb5aca020e92",
    140: "8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0",
    142: "42660cc0fa77e55c82baddb33cb928134f3542bbb6e966206283078e1fc4ca05",
    143: "13c13a62ea5f749a007f7e73e383223efbd6a9131dc0cd981817aa7ba65a535d",
}

IMAGE_SIZE = 66560


def path(rel):
    return os.path.join(ROOT, rel)


def have(rel):
    return os.path.exists(path(rel))


class FwfileSet(unittest.TestCase):

    def setUp(self):
        if not have(V110):
            self.skipTest("updater 1.10 not present")

    def test_the_1_10_resource_set_is_exactly_these_six(self):
        got = {n: h for n, _, _, _, h in fwfile.fwfiles(path(V110))}
        self.assertEqual(got, SET_110)

    def test_every_image_is_the_same_length_which_is_why_size_is_no_guard(self):
        for n, _, _, size, _ in fwfile.fwfiles(path(V110)):
            self.assertEqual(size, IMAGE_SIZE, "id %d" % n)

    def test_the_flasher_pins_the_id_140_hash_and_nothing_else(self):
        """Firmware.h's constant must be 140's, and must match no other id."""
        with open(path("Sources/EGGFlashCore/include/egg/Firmware.h")) as fh:
            hdr = fh.read()
        want = SET_110[FLASHED_ID]
        self.assertIn(want, hdr,
                      "Firmware.h no longer pins FWFILE/140 of updater 1.10")
        for n, h in SET_110.items():
            if n != FLASHED_ID:
                self.assertNotIn(h, hdr,
                                 "Firmware.h pins id %d, which is never flashed" % n)

    def test_id_140_differs_in_every_release_so_a_stale_exe_is_detectable(self):
        seen = {}
        for rel in (V104, V106, V107, V110):
            if not have(rel):
                continue
            fw = {n: h for n, _, _, _, h in fwfile.fwfiles(path(rel))}
            self.assertIn(FLASHED_ID, fw, "%s has no FWFILE/140" % rel)
            seen[rel] = fw[FLASHED_ID]
        self.assertGreaterEqual(len(seen), 2, "need two updaters to compare")
        self.assertEqual(len(set(seen.values())), len(seen),
                         "two releases ship the same image 140: %s" % seen)

    def test_identical_code_does_not_imply_identical_resources(self):
        """The reason this file pins resources instead of .text.

        1.06/1.07/1.10 share a .text SHA-256. If their FWFILE sets were also
        equal the distinction would be academic; they are not, and 1.10's extra
        blob is the proof.
        """
        for rel in (V106, V107):
            if not have(rel):
                self.skipTest("%s not present" % rel)
        old = {n for n, _, _, _, _ in fwfile.fwfiles(path(V106))}
        mid = {n for n, _, _, _, _ in fwfile.fwfiles(path(V107))}
        new = {n for n, _, _, _, _ in fwfile.fwfiles(path(V110))}
        self.assertEqual(old, mid)
        self.assertTrue(new - old,
                        "1.10 was expected to add an id the earlier releases lack")
        self.assertEqual(new - old, {143})

    def test_extraction_round_trips_to_the_pinned_hash(self):
        """Guard the extractor itself, not only its output table."""
        with open(path(V110), "rb") as fh:
            b = fh.read()
        for n, _, off, size, h in fwfile.fwfiles(path(V110)):
            self.assertEqual(hashlib.sha256(b[off:off + size]).hexdigest(), h)
            self.assertLessEqual(off + size, len(b), "id %d runs past EOF" % n)


class NoOtherCodePathCanNameFwfile(unittest.TestCase):
    """The absence claim of notes/flash-wire-observed.md §5, as a test.

    §1.2a: a negative claim has to state its search space. This one is a scan of
    every byte of the file for the two things that could reach a resource by
    name -- a reference to the L"FWFILE" string, and the enumeration APIs.
    """

    def setUp(self):
        if not have(V110):
            self.skipTest("updater 1.10 not present")
        with open(path(V110), "rb") as fh:
            self.b = fh.read()

    def test_the_rdata_fwfile_string_is_referenced_exactly_once(self):
        import re
        import struct
        needle = "FWFILE\0".encode("utf-16-le")
        occurrences = [m.start() for m in re.finditer(re.escape(needle), self.b)]
        # One in .rdata, one inside .rsrc's own type-name table.
        self.assertEqual(len(occurrences), 2, "unexpected FWFILE string count")
        rdata_va = struct.pack("<I", 0x5447C0)
        refs = len(re.findall(re.escape(rdata_va), self.b))
        self.assertEqual(refs, 1,
                         "L\"FWFILE\" at 0x5447c0 is named more than once")

    def test_the_one_reference_is_a_push_followed_by_push_140(self):
        import struct
        i = self.b.index(struct.pack("<I", 0x5447C0))
        self.assertEqual(self.b[i - 1], 0x68, "not a push imm32")
        self.assertEqual(self.b[i + 4], 0x68, "lpName is not a push imm32 either")
        self.assertEqual(struct.unpack_from("<I", self.b, i + 5)[0], FLASHED_ID)
        self.assertEqual(self.b[i + 9], 0x6A, "hModule is not push imm8")
        self.assertEqual(self.b[i + 10], 0x00, "hModule is not NULL")

    def test_the_resource_enumeration_apis_are_not_imported(self):
        for nm in (b"EnumResourceNamesW", b"EnumResourceNamesA",
                   b"EnumResourceTypesW", b"EnumResourceTypesA",
                   b"EnumResourceLanguagesW"):
            self.assertNotIn(nm + b"\0", self.b,
                             "%s is imported; the directory could be walked" % nm)


# ---------------------------------------------------------------------------
# The two predictions in notes/wire-predictions.md whose citation named a file
# that does not exist.
# ---------------------------------------------------------------------------
# `#13 alternate-updater-payloads` and `#14 config-tool-sends-no-firmware` both
# cited "scratchpad/fw.py". That file is not in the repo, so §1.2's rule -- a
# citation must be something someone else can go and check -- was not met, and
# the two were unverifiable as written however true they were.
#
# Fixed by re-deriving every number with the COMMITTED parser
# (Tools/pe/fwfile.py) and pinning the result here, so the citation now names a
# test that runs in ctest rather than a script nobody has. All values below
# reproduced exactly on 2026-09-06; nothing in either prediction changed.

CFG_TOOLS = [
    "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe",
    "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe",
    "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe",
    "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe",
]

# id 140 of each updater: (sha256, 32-bit sum, first 16 bytes).
PAYLOAD_140 = {
    V110: ("8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0",
           0x0081D57D, "29C4D0EA8AE8D61CEF8DAB9E7803F7D9"),
    V107: ("3922b14157eff4268ca28c8fe7f284ba1ec0880dae2d9137d01eb1c42e297940",
           0x0081D408, "CA774E197B457109B2FC214FD83BEAD7"),
    V106: ("9f01d732913fcfc6799d699d19a92878c97a5f4dc76ed18b4a307125b35514dd",
           0x0081F40B, "4931DA122BEF4AA4454678462ED79252"),
    V104: ("41d5397ec84c4f167e6dce4d05f649f425e5c38ee066b8931721898d5dc013c3",
           0x0081F627, "4931DA122BEF4AA4454678462ED79252"),
}


class PredictionThirteenAlternateUpdaterPayloads(unittest.TestCase):
    """Every updater's own FWFILE/140 -- the A0 03 checksum identifies which
    .exe was run, from the capture alone, with no filesystem access."""

    def test_all_four_payloads_match_what_the_prediction_states(self):
        for rel, (sha, sum32, first16) in PAYLOAD_140.items():
            if not have(rel):
                self.skipTest("%s not present" % rel)
            with open(path(rel), "rb") as f:
                b = f.read()
            got = [x for x in fwfile.fwfiles(path(rel)) if x[0] == FLASHED_ID]
            self.assertEqual(len(got), 1, rel)
            name, lang, off, size, h = got[0]
            img = b[off:off + size]
            self.assertEqual(size, IMAGE_SIZE, rel)
            self.assertEqual(size % 1024, 0, rel)
            self.assertEqual(size // 1024, 65, rel)
            self.assertEqual(h, sha, rel)
            self.assertEqual(sum(img) & 0xFFFFFFFF, sum32, rel)
            self.assertEqual(img[:16].hex().upper(), first16, rel)

    def test_1_06_and_1_04_share_a_first_block_but_not_an_image(self):
        # Stated in the prediction as "first16 identical to 1.06's", and it is
        # the reason first-bytes are not an identity check: only the whole-image
        # sum separates those two.
        self.assertEqual(PAYLOAD_140[V106][2], PAYLOAD_140[V104][2])
        self.assertNotEqual(PAYLOAD_140[V106][0], PAYLOAD_140[V104][0])
        self.assertNotEqual(PAYLOAD_140[V106][1], PAYLOAD_140[V104][1])


class PredictionFourteenConfigToolsCannotFlash(unittest.TestCase):
    """engineering-rules.md §3's two-executable split assumes the config tool has no flash
    path. If it had one, the contradictory-quit-semantics argument would need
    revisiting -- so this is load-bearing, not trivia."""

    def test_each_config_tool_has_74_leaves_and_no_FWFILE(self):
        needle = "FWFILE".encode("utf-16-le")
        for rel in CFG_TOOLS:
            if not have(rel):
                self.skipTest("%s not present" % rel)
            b, leaves = fwfile.resources(path(rel))
            self.assertEqual(len(leaves), 74, rel)
            self.assertEqual(len(fwfile.fwfiles(path(rel))), 0, rel)
            self.assertFalse(any(l[4] == IMAGE_SIZE for l in leaves),
                             "%s has a 66,560-byte resource" % rel)
            self.assertEqual(b.count(needle), 0,
                             "%s contains the UTF-16 string FWFILE" % rel)

    def test_the_updaters_would_fail_that_same_check(self):
        # §6.2: the check has to be able to fail. Run it against the binaries
        # that DO carry firmware and confirm it objects.
        for rel in (V110, V104):
            if not have(rel):
                self.skipTest("%s not present" % rel)
            b, leaves = fwfile.resources(path(rel))
            self.assertNotEqual(len(leaves), 74, rel)
            self.assertGreater(len(fwfile.fwfiles(path(rel))), 0, rel)
            self.assertGreater(b.count("FWFILE".encode("utf-16-le")), 0, rel)


if __name__ == "__main__":
    unittest.main(verbosity=2)
