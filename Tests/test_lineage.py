"""Tests/test_lineage.py -- §8.5a's filler-chunk fingerprint must actually
DISCRIMINATE, not merely accept the image we already trust.

WHAT IT IS FOR. `Tools/pe/ingest.py` reads an Endgame updater nobody has read
and proposes a manifest row for it. At that moment there is no pinned
whole-image SHA-256 to check against -- the row being proposed is what would
create it -- so the pinned hash cannot help by construction. That is exactly
when engineering-rules.md §1.4's warning is live: "other resources in the binary may
belong to other products", and updater 1.10 carries SIX FWFILE resources of
identical length, five of which the device would accept (right size, valid
per-block checksums, and they read back exactly as written). §2 assumes the
device validates nothing, so a wrong-but-well-formed image is a brick with
nothing downstream to catch it.

§8.5a (notes/updater-protocol.md) is the one mechanical check that works here:
chunks 29-63 of every FWFILE blob are a single 1024-byte chunk repeated 35
times, and its value is constant per resource NAME across all four updaters --
21 blobs, six distinct values, zero collisions between names.

WHY THIS TEST AND NOT JUST "1.10 PASSES". A fingerprint that accepts the right
answer proves nothing on its own; engineering-rules.md §6.2 is explicit that a harness
which cannot produce a bad result is not evidence. So the test that matters is
the NEGATIVE one: every OTHER FWFILE resource in every updater we hold must be
REJECTED. Those are the five images an operator could plausibly end up holding
if a future .exe moved the id, and they are the whole reason the check exists.
"""
import hashlib
import importlib.util
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

UPDATERS = [
    ("1.10", "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"),
    ("1.07", "old-firmware-executables/"
             "Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe"),
    ("1.06", "old-firmware-executables/"
             "Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe"),
    ("1.04", "old-firmware-executables/"
             "Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe"),
]


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


ingest = _load(os.path.join(ROOT, "Tools", "pe", "ingest.py"), "ingest")
fwfile = _load(os.path.join(ROOT, "Tools", "pe", "fwfile.py"), "fwfile")


def blobs(rel):
    """Every FWFILE resource of one updater, as (id, bytes)."""
    path = os.path.join(ROOT, rel)
    with open(path, "rb") as f:
        d = f.read()
    return [(name, d[off:off + size])
            for (name, lang, off, size, sha) in fwfile.fwfiles(path)]


class TheFingerprintAcceptsOurImage(unittest.TestCase):
    def test_140_matches_in_every_updater_we_hold(self):
        for label, rel in UPDATERS:
            found = [b for (name, b) in blobs(rel) if name == 140]
            self.assertEqual(len(found), 1, "%s has no FWFILE 140" % label)
            verdict, detail = ingest.lineage(found[0])
            self.assertEqual(verdict, "match", "%s: %s" % (label, detail))

    def test_the_constant_is_the_notes_value_and_is_reachable(self):
        # Pinned separately from the code path so a typo in either one shows
        # up here rather than silently agreeing with itself.
        self.assertEqual(
            ingest.FILLER_SHA_140,
            "cefe77fb6c23f0d4cb19fc709232bed0035ba41cc1413d46688172d3e6c6ffda")
        b = [x for (n, x) in blobs(UPDATERS[0][1]) if n == 140][0]
        chunk = b[29 * 1024:30 * 1024]
        self.assertEqual(hashlib.sha256(chunk).hexdigest(),
                         ingest.FILLER_SHA_140)


class TheFingerprintRejectsEverythingElse(unittest.TestCase):
    """The half that makes it evidence. Five other resource names exist, all of
    them the same length as ours, and the device would take any of them."""

    def test_every_other_fwfile_is_rejected(self):
        seen = 0
        for label, rel in UPDATERS:
            for name, b in blobs(rel):
                if name == 140:
                    continue
                seen += 1
                verdict, detail = ingest.lineage(b)
                self.assertNotEqual(
                    verdict, "match",
                    "%s FWFILE %d passed the 140 fingerprint" % (label, name))
        # 21 blobs across four updaters, four of which are the 140s.
        self.assertEqual(seen, 17, "expected 17 non-140 blobs, saw %d" % seen)

    def test_a_flipped_byte_inside_the_filler_run_is_caught(self):
        b = bytearray([x for (n, x) in blobs(UPDATERS[0][1]) if n == 140][0])
        b[29 * 1024] ^= 0x01
        verdict, detail = ingest.lineage(bytes(b))
        self.assertEqual(verdict, "mismatch")
        # The run is compared against its own first chunk, so corrupting the
        # first chunk shows up as 35 disagreeing chunks, not as a hash miss.
        self.assertIn("differ from chunk 29", detail)

    def test_a_flipped_byte_in_a_later_filler_chunk_is_caught(self):
        b = bytearray([x for (n, x) in blobs(UPDATERS[0][1]) if n == 140][0])
        b[50 * 1024 + 7] ^= 0x80
        verdict, detail = ingest.lineage(bytes(b))
        self.assertEqual(verdict, "mismatch")
        self.assertIn("50", detail)

    def test_a_whole_replacement_filler_is_caught_by_the_hash(self):
        # Self-consistent but wrong: every chunk in the run replaced by one
        # value, so the "all equal" arm passes and only the hash can object.
        # This is the shape a different resource name actually has.
        b = bytearray([x for (n, x) in blobs(UPDATERS[0][1]) if n == 140][0])
        other = bytes(1024)
        for i in range(29, 64):
            b[i * 1024:(i + 1) * 1024] = other
        verdict, detail = ingest.lineage(bytes(b))
        self.assertEqual(verdict, "mismatch")
        self.assertIn("filler chunk is", detail)

    def test_a_wrong_size_is_unusable_not_a_mismatch(self):
        # Reported separately on purpose: "not the shape this is defined over"
        # is a different statement from "wrong lineage", and the size problem
        # is already raised by its own check.
        verdict, detail = ingest.lineage(bytes(1024))
        self.assertEqual(verdict, "unusable")


class TheGuardIsWiredIntoTheTool(unittest.TestCase):
    """A check nothing calls is a comment. This pins the call site."""

    def test_report_carries_the_verdict_for_a_real_updater(self):
        r = ingest.report(os.path.join(ROOT, UPDATERS[0][1]), "1.10")
        self.assertEqual(r["lineage"][0], "match")
        self.assertTrue(r["ok"], r["problems"])

    def test_a_mismatch_would_block_the_row(self):
        # Falsification of the wiring, not of the fingerprint: force lineage()
        # to disagree and confirm the tool REFUSES rather than warns.
        real = ingest.FILLER_SHA_140
        ingest.FILLER_SHA_140 = "00" * 32
        try:
            r = ingest.report(os.path.join(ROOT, UPDATERS[0][1]), "1.10")
        finally:
            ingest.FILLER_SHA_140 = real
        self.assertFalse(r["ok"])
        self.assertTrue(any("8.5a" in p for p in r["problems"]),
                        r["problems"])


if __name__ == "__main__":
    unittest.main()
