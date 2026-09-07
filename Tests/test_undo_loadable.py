#!/usr/bin/env python3
"""The undo files this tool writes must be files this tool can read back.

This exists because they were not. `loadRecord` in Sources/egg-config/main.cpp
required byte 0 of a saved record to be 0xA0 -- a value that appears there on NO
platform. The device never sends the report-id slot at all, so on Windows the
vendor's own 0xA1 survives in it and on macOS hidapi leaves 0x00. Every file the
tool saves therefore begins 00 01 on macOS.

The consequence was not cosmetic. `egg-config factory-reset --yes` reads, saves
egg-before-reset.bin, wipes the settings, and tells the user to run
`egg-config restore egg-before-reset.bin --yes` if anything went wrong. That
restore would have refused the file. So would the vault at
~/.egg-mouse-known-good.bin, which engineering-rules.md §4.1 calls the undo. The undo
existed, was written correctly, and could not be used.

It survived the whole suite because nothing round-tripped a saved record through
the CLI -- the C++ tests call plausible() directly, which was already correct.
The gap was between the two.

The invariant, stated as an invariant rather than an example: BYTE 0 MUST NOT
AFFECT LOADABILITY, and the real structural checks must still bite.
"""
import os
import subprocess
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BIN = os.path.join(ROOT, "build", "egg-config")
CAPTURE = os.path.join(ROOT, "frames", "01-baseline-003-in-01.bin")


def real_record():
    """A genuine 1041-byte record: the vendor's captured 1040-byte reply plus
    the one trailing byte Transport leaves zero (it keeps the buffer at `want`
    and the device sends want-1). Exactly the shape RecordVault writes."""
    with open(CAPTURE, "rb") as fh:
        body = fh.read()
    assert len(body) == 1040, len(body)
    return bytearray(body + b"\x00")


def run(*args):
    return subprocess.run([BIN] + list(args), capture_output=True, text=True)


class UndoIsLoadable(unittest.TestCase):
    def setUp(self):
        for p in (BIN, CAPTURE):
            if not os.path.exists(p):
                self.skipTest("missing %s" % os.path.basename(p))
        self.tmp = tempfile.mkdtemp()

    def write(self, name, data):
        p = os.path.join(self.tmp, name)
        with open(p, "wb") as fh:
            fh.write(bytes(data))
        return p

    def test_byte_zero_does_not_affect_loadability(self):
        """0x00 is what macOS produces, 0xa1 is what the device and Windows
        produce, 0xa0 is what the broken check demanded and nothing produces.
        All three are the same record and all three must load."""
        for slot in (0x00, 0xA1, 0xA0, 0x5A):
            rec = real_record()
            rec[0] = slot
            p = self.write("slot_%02x.bin" % slot, rec)
            r = run("diff", p, p)
            self.assertEqual(r.returncode, 0,
                             "byte 0 = 0x%02x was refused:\n%s" % (slot, r.stdout))
            self.assertIn("0 of 1024 payload bytes differ", r.stdout)

    def test_the_macos_shape_specifically_round_trips_to_a_frame(self):
        """The shape the vault actually holds, all the way to the bytes restore
        would put on the wire. The frame must carry report id 0xA0 and command
        0x11 REGARDLESS of what byte 0 of the file was."""
        rec = real_record()
        rec[0] = 0x00                      # what macOS saves
        p = self.write("macos.bin", rec)
        # --unknown-bytes=preserve sends the record back untouched, so a
        # restore of a saved record must differ from it in nothing at all.
        r = run("--unknown-bytes=preserve", "dryrun", p)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("report 0xa0, command 0x11, 1041 bytes", r.stdout)
        self.assertIn("0 of 1024 payload bytes differ", r.stdout)

        # The default policy zeroes record 0x01-0x04 "as the vendor does", and
        # this captured record carries 0x80 at payload +0x01 -- so exactly one
        # byte differs, at exactly that offset. Pinned because it is the one
        # place read-modify-write and copying the vendor disagree, and a silent
        # change of default would otherwise not show up anywhere.
        r = run("dryrun", p)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("1 of 1024 payload bytes differ", r.stdout)
        self.assertIn("payload +0x0001)   80 -> 00", r.stdout)
        # and the emitted frame really does begin a0 11, not 00 11
        hexline = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("0000")]
        self.assertTrue(hexline, "no frame dump in dryrun output")
        self.assertTrue(hexline[0].split()[1].startswith("a011"),
                        "frame begins %s" % hexline[0].split()[1][:8])

    def test_a_truncated_save_is_still_refused(self):
        p = self.write("short.bin", real_record()[:1040])
        self.assertNotEqual(run("diff", p, p).returncode, 0)

    def test_a_zeroed_save_is_still_refused(self):
        """What a failed earlier session leaves behind. plausible() catches it
        by the payload being uniform, which is a real check -- unlike byte 0."""
        p = self.write("zero.bin", bytearray(1041))
        self.assertNotEqual(run("diff", p, p).returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
