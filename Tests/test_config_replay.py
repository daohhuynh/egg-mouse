#!/usr/bin/env python3
"""egg-config's write path, replayed against the vendor's own settings writes.

The captures give 73 pairs of (record before, record after) where exactly one
setting changed and we know which. That is a much better test than any record we
could compose: for each pair, hand egg-config the BEFORE record and the same
field change the vendor made, and the result must equal the vendor's AFTER
record byte for byte.

A pass means our encoding is right AND our read-modify-write preserved every
byte we do not understand -- both halves of §4.1 and §1.3 in one assertion.

A failure is a finding, not an embarrassment: it means either the encoding is
wrong or the vendor moved more than one byte, and the second is worth knowing
about before we write to hardware.
"""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))
BIN = os.path.join(ROOT, "build", "egg-config")

# (capture, write index, field, value). The write index is into the ordered
# settings writes of that capture; the map files in Tools/capture/maps say which
# log line each one is. Only fields egg-config can actually set are listed.
CASES = [
    ("02-basic",  0, "polling", "125"),
    ("02-basic",  1, "polling", "250"),
    ("02-basic",  2, "polling", "500"),
    ("02-basic",  3, "polling", "1000"),
    ("02-basic",  4, "polling", "2000"),
    ("02-basic",  5, "polling", "4000"),
    ("02-basic",  6, "polling", "8000"),
    ("02-basic",  7, "angle-snapping", "1"),
    # the vendor's checkbox is "Disable LED on Lift-Off"; ticking it stores 0,
    # and our field is named after the byte rather than the caption.
    ("02-basic",  8, "led-on-liftoff", "0"),
    ("02-basic",  9, "cpi-levels", "1"),
    ("02-basic", 11, "cpi-levels", "3"),
    ("02-basic", 12, "cpi-levels", "4"),
    ("03-sensor", 0, "motion-sync", "1"),
    ("03-sensor", 1, "force-max-fps", "0"),
    ("03-sensor", 2, "sensor-angle", "20"),
    ("03-sensor", 3, "sensor-angle", "-45"),
    ("03-sensor", 4, "sensor-angle", "127"),
    # promoted 2026-09-05 once the capture scored them
    ("02-basic", 17, "lod", "0"),
    ("02-basic", 18, "lod", "1"),
    ("02-basic", 22, "lod", "5"),
    ("02-basic", 27, "lod", "10"),
    ("03-sensor", 5, "cpi-downshift", "1"),
    ("03-sensor", 6, "cpi-downshift", "2"),
    ("03-sensor", 7, "cpi-downshift", "3"),
    ("03-sensor", 8, "cpi-downshift", "4"),
    ("03-sensor", 9, "smoothing", "1"),
    ("03-sensor", 10, "smoothing", "2"),
    ("03-sensor", 11, "smoothing", "3"),
    ("04-buttons", 0, "slamclick-filter", "0"),
    ("06-cpi-stage", 0, "cpi-stage", "0"),
    ("06-cpi-stage", 1, "cpi-stage", "1"),
    ("06-cpi-stage", 2, "cpi-stage", "2"),
    ("06-cpi-stage", 3, "cpi-stage", "3"),
]

# Deliberately excluded, with the reason, so the exclusion is a claim and not a
# silent gap. See test_the_excluded_case_really_does_move_two_bytes.
EXCLUDED = [
    ("02-basic", 10, "cpi-levels", "2",
     "the vendor ALSO moved record 0x0d, the active CPI stage, because stage 4 "
     "was selected and reducing the count to 2 forced it down"),
]


def records(capture):
    import ingest
    p = os.path.join(ROOT, "windows-run", "%s.pcapng" % capture)
    rd = [r for _, r in ingest.settings_reads(p)]
    wr = [r for _, r in ingest.settings_writes(p)]
    return rd, wr


# The on-disk format egg-config reads and writes: 1041 bytes, report id at [0],
# fifteen header bytes, then the 1024-byte record at kPayloadOffset = 0x10.
# ingest's record already starts at that boundary, so it drops straight in.
PAYLOAD_OFFSET = 0x10
FRAME_LEN = 0x411


def as_saved(record):
    buf = bytearray(FRAME_LEN)
    buf[0] = 0xA0
    n = min(len(record), FRAME_LEN - PAYLOAD_OFFSET)
    buf[PAYLOAD_OFFSET:PAYLOAD_OFFSET + n] = record[:n]
    return bytes(buf)


def vendor_frames(capture):
    """The FULL 1041-byte SET frames, header included.

    ingest.settings_writes() slices from RECORD_BASE because everything else
    wants the record. The header is what this file could never check, and the
    header is where the report id and the command live.
    """
    import usbpcap
    p = os.path.join(ROOT, "windows-run", "%s.pcapng" % capture)
    xf = [t for t in usbpcap.control_transfers(usbpcap.read(p)) if t.is_feature]
    return [bytes(t.data) for t in xf
            if not (t.bmRequestType & 0x80)
            and len(t.data) == FRAME_LEN and t.data[1] == 0x11]


def before_after(capture, i):
    """The record the vendor started that write from, and the one it sent."""
    rd, wr = records(capture)
    before = wr[i - 1] if i > 0 else rd[0]
    return as_saved(bytes(before)), as_saved(bytes(wr[i]))


class Replay(unittest.TestCase):
    def setUp(self):
        if not os.path.exists(BIN):
            self.skipTest("build/egg-config not built")
        if not os.path.exists(os.path.join(ROOT, "windows-run")):
            self.skipTest("no captures")

    def encode(self, before, field, value, policy=None):
        """Run egg-config's OWN write-path composition over a saved record.

        `policy` is None for the compiled-in default (ConfigRecord.h
        kDefaultUnknownBytes) or "preserve"/"vendor" to pin one arm. Both arms
        are scored below, so flipping the default cannot silently lose coverage
        of the other -- that is the whole reason this parameter exists.
        """
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "in.bin"), os.path.join(d, "out.bin")
            with open(a, "wb") as f:
                f.write(before)
            cmd = [BIN, "encode", field, value, a, b]
            if policy is not None:
                cmd.insert(1, "--unknown-bytes=%s" % policy)
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0,
                             "egg-config encode failed: %s%s" % (r.stdout, r.stderr))
            with open(b, "rb") as f:
                return f.read()

    def dryrun_frame(self, before, field, value, policy):
        """The exact 1041 bytes `set` would put on the wire, from a record file.

        Parsed out of `egg-config dryrun`'s hex dump rather than a side channel,
        so this tests the output a person would actually read.
        """
        with tempfile.TemporaryDirectory() as d:
            a = os.path.join(d, "in.bin")
            with open(a, "wb") as f:
                f.write(before)
            cmd = [BIN, "--unknown-bytes=%s" % policy, "dryrun", a]
            if field:
                cmd += [field, value]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        out = bytearray()
        started = False
        for line in r.stdout.splitlines():
            if "as they would go on the wire" in line:
                started = True
                continue
            if not started:
                continue
            parts = line.split()
            if len(parts) != 2:
                break
            out += bytes.fromhex(parts[1])
        return bytes(out)

    def test_every_settable_field_reproduces_the_vendor_write(self):
        """Ours must equal the vendor's, except at record 0x01 -- see below."""
        for capture, i, field, value in CASES:
            with self.subTest(capture=capture, write=i, field=field, value=value):
                before, after = before_after(capture, i)
                got = self.encode(before, field, value)
                n = min(len(got), len(after))
                d = [k - PAYLOAD_OFFSET for k in range(n) if got[k] != after[k]]
                self.assertEqual(
                    [x for x in d if x != 0x01], [],
                    "%s write %d (%s=%s): differs from the vendor at record %s"
                    % (capture, i, field, value, [hex(x) for x in d]))

    def test_the_compiled_in_default_matches_the_recorded_decision(self):
        """Which policy is compiled in must be a visible, deliberate choice.

        The default was flipped to MatchVendor on 2026-09-05 (ConfigRecord.h
        carries the evidence and the one-line reversal). This test exists so
        that flipping it back is something a person did on purpose, not
        something that drifted -- the two explicit-policy tests above already
        prove BOTH arms work, so nothing here is about correctness. It is about
        the default being stated in two places that must agree.
        """
        before, after = before_after("02-basic", 0)
        self.assertEqual(before[PAYLOAD_OFFSET + 1], 0x80,
                         "this case must start from a record reporting 0x80, "
                         "or it cannot tell the two policies apart")
        got = self.encode(before, "polling", "125")            # no --unknown-bytes
        vendor = self.encode(before, "polling", "125", policy="vendor")
        preserve = self.encode(before, "polling", "125", policy="preserve")
        self.assertNotEqual(vendor, preserve, "the two arms produced identical "
                            "bytes, so this test cannot distinguish them")
        self.assertEqual(
            got, vendor,
            "the compiled-in default is no longer MatchVendor. If that was "
            "deliberate, update ConfigRecord.h's comment and this test together "
            "-- they are the two places the decision is written down.")
        self.assertEqual(got[PAYLOAD_OFFSET + 1], 0x00)
        self.assertEqual(after[PAYLOAD_OFFSET + 1], 0x00,
                         "and the vendor's own frame carries 0x00 there")

    def test_the_default_never_differs_from_the_vendor_outside_the_four_bytes(self):
        """True under EITHER default, so it survives the decision being changed.

        This is the assertion that used to be written as "differs only at record
        0x01", which was a statement about one particular default rather than
        about the code. Whichever arm is compiled in, the vendor's frame and
        ours may disagree only inside 0x01..0x04 and nowhere else in 1024 bytes.
        """
        for capture, i, field, value in CASES:
            before, after = before_after(capture, i)
            got = self.encode(before, field, value)
            n = min(len(got), len(after))
            d = [k - PAYLOAD_OFFSET for k in range(n) if got[k] != after[k]]
            self.assertTrue(set(d) <= {0x01, 0x02, 0x03, 0x04},
                            "%s write %d (%s=%s): differs from the vendor at %s"
                            % (capture, i, field, value, [hex(x) for x in d]))

    def test_the_whole_1041_byte_frame_matches_the_vendor_header_included(self):
        """The strongest assertion in this file, and the newest.

        Every other test here compares the 1024-byte PAYLOAD, because that is
        what `encode` writes. But a frame is 1041 bytes and the other 17 are
        where the report id and the command live -- config-protocol.md §7.2a
        derived `a0 11 00 00` plus a memset covering [2..15] from FUN_00404180,
        and until now nothing had ever compared that derivation against a
        capture. `egg-config dryrun` emits the real frame, so it can be.

        Under --unknown-bytes=vendor this must be byte-identical to what
        Endgame's own tool put on the wire. All 1041 bytes, no exclusions.
        """
        for capture, i, field, value in CASES:
            with self.subTest(capture=capture, write=i, field=field, value=value):
                frames = vendor_frames(capture)
                self.assertGreater(len(frames), i, "capture has too few frames")
                before, _ = before_after(capture, i)
                got = self.dryrun_frame(before, field, value, "vendor")
                self.assertEqual(len(got), FRAME_LEN)
                d = [k for k in range(FRAME_LEN) if got[k] != frames[i][k]]
                self.assertEqual(
                    d, [], "%s write %d (%s=%s): our frame differs from the "
                    "vendor's at wire offsets %s"
                    % (capture, i, field, value, [hex(x) for x in d]))

    def test_the_frame_header_is_what_the_derivation_says(self):
        """a0 11 then fourteen zeros, stated as a claim and checked as one."""
        before, _ = before_after("02-basic", 0)
        got = self.dryrun_frame(before, "polling", "1000", "vendor")
        self.assertEqual(got[0], 0xA0)
        self.assertEqual(got[1], 0x11)
        self.assertEqual(got[2:PAYLOAD_OFFSET], b"\x00" * (PAYLOAD_OFFSET - 2))
        self.assertEqual(got[FRAME_LEN - 1], 0x00,
                         "the 1041st byte is past the 1024-byte payload")

    def test_the_excluded_case_really_does_move_two_bytes(self):
        """The exclusion above is a claim about the vendor. Check it, or it is
        just a convenient way to make a failing case disappear."""
        for capture, i, field, value, _why in EXCLUDED:
            before, after = before_after(capture, i)
            moved = [k for k in range(min(len(before), len(after)))
                     if before[k] != after[k]]
            moved = [m - PAYLOAD_OFFSET for m in moved]
            self.assertEqual(moved, [0x0d, 0x0e],
                             "expected the vendor to move both the active stage "
                             "and the count; it moved %s" % [hex(m) for m in moved])
            # and ours moves only the one, which is exactly the divergence
            got = self.encode(before, field, value, policy="preserve")
            ours = [k - PAYLOAD_OFFSET for k in range(min(len(before), len(got)))
                    if before[k] != got[k]]
            self.assertEqual(ours, [0x0e])

    def test_matchvendor_reproduces_every_vendor_write_byte_for_byte(self):
        """--unknown-bytes=vendor must match the vendor at all 1024 bytes.

        This is the arm the OTHER test cannot check, and it is the stronger
        claim: not "we differ only where we said we would" but "we differ
        nowhere at all". If it passes, the vendor's 33 captured settings writes
        are reproducible from our code with zero divergence, and record
        0x01..0x04 is the only thing that ever stood between us and that.

        It is scored explicitly rather than left to the default so that the
        default in ConfigRecord.h is a decision with evidence on both sides,
        not a dependency of the test suite.
        """
        for capture, i, field, value in CASES:
            with self.subTest(capture=capture, write=i, field=field, value=value):
                before, after = before_after(capture, i)
                got = self.encode(before, field, value, policy="vendor")
                n = min(len(got), len(after))
                d = [k - PAYLOAD_OFFSET for k in range(n) if got[k] != after[k]]
                self.assertEqual(
                    d, [],
                    "%s write %d (%s=%s): differs from the vendor at record %s"
                    % (capture, i, field, value, [hex(x) for x in d]))

    def test_preserve_differs_from_the_vendor_only_at_record_0x01(self):
        """The other arm, pinned explicitly for the same reason.

        Identical in substance to the default-policy test above; what it adds is
        independence from which policy is compiled in as the default. Together
        the two make the default a one-line change with coverage either way.
        """
        divergent = 0
        for capture, i, field, value in CASES:
            before, after = before_after(capture, i)
            got = self.encode(before, field, value, policy="preserve")
            n = min(len(got), len(after))
            d = [k - PAYLOAD_OFFSET for k in range(n) if got[k] != after[k]]
            if before[PAYLOAD_OFFSET + 1] == 0x80:
                self.assertEqual(d, [0x01], "%s write %d differs at %s"
                                 % (capture, i, [hex(x) for x in d]))
                divergent += 1
            else:
                self.assertEqual(d, [], "%s write %d differs at %s"
                                 % (capture, i, [hex(x) for x in d]))
        # If this ever reaches zero the test above has stopped testing anything,
        # because both arms would be producing identical bytes.
        self.assertGreaterEqual(divergent, 3)

    def test_the_two_policies_differ_at_nothing_but_the_four_unknown_bytes(self):
        """Whichever default is chosen, the blast radius is those four bytes.

        A policy that reached any further would be a second, undeclared change
        to the record riding along with the first -- exactly what §1.3 forbids.
        Checked over every case rather than argued from the source.
        """
        for capture, i, field, value in CASES:
            before, _ = before_after(capture, i)
            a = self.encode(before, field, value, policy="preserve")
            b = self.encode(before, field, value, policy="vendor")
            d = [k - PAYLOAD_OFFSET for k in range(min(len(a), len(b)))
                 if a[k] != b[k]]
            self.assertTrue(set(d) <= {0x01, 0x02, 0x03, 0x04},
                            "%s write %d: the two policies differ at %s"
                            % (capture, i, [hex(x) for x in d]))

    def test_a_mistyped_policy_is_refused_rather_than_defaulted(self):
        """A typo must not silently choose one of two different byte streams."""
        before, _ = before_after("02-basic", 0)
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "in.bin"), os.path.join(d, "out.bin")
            with open(a, "wb") as f:
                f.write(before)
            for bad in ("Vendor", "preserv", "", "1", "match-vendor"):
                r = subprocess.run(
                    [BIN, "--unknown-bytes=%s" % bad, "encode",
                     "polling", "1000", a, b],
                    capture_output=True, text=True)
                self.assertEqual(r.returncode, 2,
                                 "--unknown-bytes=%r was accepted" % bad)
                self.assertFalse(os.path.exists(b),
                                 "--unknown-bytes=%r still wrote a file" % bad)

    def test_read_modify_write_preserves_everything_else(self):
        """§1.3: bytes we do not understand must come back unchanged.

        Stated as "the field byte, plus at most the four bytes the policy is
        allowed to touch, and NOTHING else in 1024" so that it holds under
        either default. Under --unknown-bytes=preserve it is exactly [0x05],
        which the arm-specific test below pins.
        """
        before, _ = before_after("02-basic", 0)
        got = self.encode(before, "polling", "1000")
        moved = [k - PAYLOAD_OFFSET for k in range(min(len(before), len(got)))
                 if before[k] != got[k]]
        self.assertIn(0x05, moved)
        self.assertTrue(set(moved) <= {0x05, 0x01, 0x02, 0x03, 0x04},
                        "moved %s" % [hex(m) for m in moved])

        # The pure read-modify-write arm moves the field byte and nothing else.
        only = self.encode(before, "polling", "1000", policy="preserve")
        moved = [k - PAYLOAD_OFFSET for k in range(min(len(before), len(only)))
                 if before[k] != only[k]]
        self.assertEqual(moved, [0x05])


if __name__ == "__main__":
    unittest.main(verbosity=2)
