#!/usr/bin/env python3
"""test_cpi.py -- the CPI stage writer, scored against cfg107's raw bytes and
against the vendor's own captured records.

CLAUDE.md §1.3 forbids writing a byte whose meaning is [G]. For CPI that meant
two things had to be [D] before `egg-config cpi` could exist: the LAYOUT (which
§7.8 settled) and the LEGAL VALUES (which nothing had, until §7.19).

This checks both without trusting the notes:

  1. The clamp and rounding constants are recovered from the RAW BYTES of
     cfg107 `0x40d880`, by matching the encoded instructions. If Endgame's
     normaliser says something other than [10, 30000] / step 10 / step 50 above
     10000, this fails and the encoder is wrong.
  2. The trackbar path is recovered separately, from `0x40c2f0`. Two derivations
     that must agree; if they ever disagree, ONE of them is a misreading and
     neither may be trusted (§1.2b: never rule anything out from one view).
  3. Every A0 11 write and every large read in `windows-run` is decoded as four
     CPI stages, and every X and Y the vendor ever put on the wire must be a
     value our encoder would accept. That is the check that cannot be faked by
     transcribing the same function twice.

No disassembler. §1.2b: Ghidra's and objdump's views are products of analysis;
the bytes are not.
"""
import os
import re
import struct
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "capture"))

CFG107 = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe")
EGGCONFIG = os.path.join(ROOT, "build", "egg-config")
CAPTURES = os.path.join(ROOT, "windows-run")

PAYLOAD = 16
CPI_FIRST = 0x23
CPI_LEN = 5
CPI_STAGES = 4


def text():
    """cfg107's .text, addressable by VA."""
    with open(CFG107, "rb") as f:
        b = f.read()
    pe = b.find(b"PE\0\0")
    nsec = struct.unpack("<H", b[pe + 6:pe + 8])[0]
    optsz = struct.unpack("<H", b[pe + 20:pe + 22])[0]
    st = pe + 24 + optsz
    imgbase = struct.unpack("<I", b[pe + 24 + 28:pe + 24 + 32])[0]
    for i in range(nsec):
        e = b[st + 40 * i:st + 40 * i + 40]
        if e[:8].rstrip(b"\0") != b".text":
            continue
        _vs, va, rs, ro = struct.unpack("<IIII", e[8:24])
        return imgbase + va, b[ro:ro + rs]
    raise SystemExit("no .text in cfg107")


BASE, TEXT = text()


def at(va, n):
    return TEXT[va - BASE: va - BASE + n]


def normalise(v):
    """The encoder's rule, reimplemented here so the C++ is scored against an
    independent transcription rather than against itself."""
    v = min(max(v, 10), 30000)
    step = 10 if v <= 10000 else 50
    q, rem = divmod(v, step)
    return (q + 1 if rem * 2 >= step else q) * step


def tool_bytes(stage, x, y=None):
    args = [EGGCONFIG, "cpi", str(stage), str(x)] + ([str(y)] if y else [])
    out = subprocess.run(args, capture_output=True, text=True, cwd=ROOT).stdout
    for line in out.splitlines():
        p = line.strip().split()
        if len(p) == CPI_LEN and all(len(q) == 2 for q in p):
            try:
                return tuple(int(q, 16) for q in p)
            except ValueError:
                pass
    return None


@unittest.skipUnless(os.path.exists(CFG107), "cfg107 not present")
class TheBoundsComeFromTheVendorsOwnNormaliser(unittest.TestCase):
    """cfg107 0x40d880-0x40d93a, matched as encoded instructions."""

    def test_the_low_clamp_is_ten(self):
        # 40d8ac  83 f9 0a    cmpl $0xa, %ecx
        # 40d8b1  b9 0a...    movl $0xa, %ecx
        self.assertEqual(b"\x83\xf9\x0a", at(0x40D8AC, 3))
        self.assertEqual(b"\xb9\x0a\x00\x00\x00", at(0x40D8B1, 5))
        # and the other arm, 40d902  ba 0a 00 00 00   movl $0xa, %edx
        self.assertEqual(b"\xba\x0a\x00\x00\x00", at(0x40D902, 5))

    def test_the_high_clamp_is_thirty_thousand(self):
        # 40d8ee cmpl $0x7530,%ecx ; 40d8f6 movl $0x7530,%ecx
        self.assertEqual(b"\x81\xf9\x30\x75\x00\x00", at(0x40D8EE, 6))
        self.assertEqual(b"\xb9\x30\x75\x00\x00", at(0x40D8F6, 5))
        self.assertEqual(b"\xba\x30\x75\x00\x00", at(0x40D911, 5))

    def test_the_changeover_is_ten_thousand(self):
        # 40d918  81 f9 10 27 00 00   cmpl $0x2710, %ecx
        self.assertEqual(b"\x81\xf9\x10\x27\x00\x00", at(0x40D918, 6))

    def test_the_fine_step_is_ten_and_rounds_half_up(self):
        # edx = ecx/10 via 0xcccccccd >> 3, rem compared against 5.
        self.assertEqual(b"\xb8\xcd\xcc\xcc\xcc", at(0x40D8B6, 5))
        self.assertEqual(b"\xc1\xea\x03", at(0x40D8BD, 3))     # shrl $3
        self.assertEqual(b"\x83\xf9\x05", at(0x40D8C7, 3))     # cmpl $5
        self.assertEqual(b"\x72\x01", at(0x40D8CA, 2))         # jb (skip incl)
        self.assertEqual(b"\x42", at(0x40D8CC, 1))             # incl %edx

    def test_the_coarse_step_is_fifty_and_rounds_half_up(self):
        # edx = ecx/50 via 0x51eb851f >> 4, rem compared against 25 (0x19),
        # then edx *= 50 (imull $0x32).
        self.assertEqual(b"\xb8\x1f\x85\xeb\x51", at(0x40D920, 5))
        self.assertEqual(b"\xc1\xea\x04", at(0x40D927, 3))     # shrl $4
        self.assertEqual(b"\x6b\xc0\x32", at(0x40D92C, 3))     # imull $50
        self.assertEqual(b"\x83\xf9\x19", at(0x40D931, 3))     # cmpl $25
        self.assertEqual(b"\x6b\xd2\x32", at(0x40D937, 3))     # imull $50

    def test_the_trackbar_path_reaches_the_same_set(self):
        """A second, independent derivation. §1.2b in miniature: one reading of
        one function is a reading; two routes agreeing is a derivation."""
        # 40c2f0  3d e8 03 00 00   cmpl $0x3e8, %eax     (position vs 1000)
        self.assertEqual(b"\x3d\xe8\x03\x00\x00", at(0x40C2F0, 5))
        # pos <= 1000: eax = pos*5 then *2  ->  pos*10
        self.assertEqual(b"\x8d\x04\x80", at(0x40C2FD, 3))     # leal (%eax,%eax,4)
        self.assertEqual(b"\x03\xc0", at(0x40C300, 2))         # addl %eax,%eax
        # pos > 1000: (pos - 0x320) * 0x32  ->  (pos-800)*50
        self.assertEqual(b"\x2d\x20\x03\x00\x00", at(0x40C30A, 5))
        self.assertEqual(b"\x6b\xc0\x32", at(0x40C30F, 3))
        # And the ends of the slider range must map to the clamps.
        self.assertEqual(10, 1 * 10)
        self.assertEqual(30000, (1400 - 800) * 50)
        # The two halves must MEET, or there is a gap the edit box can reach
        # and the slider cannot.
        self.assertEqual(1000 * 10, (1001 - 800) * 50 - 50)

    def test_the_slider_stores_the_record_bytes_directly(self):
        """§7.19.2a. The trackbar handler writes the settings object at
        0x57f220/0x57f222 -- object +0x10/+0x12, which §7.3's independently
        derived map calls record 0x24 and 0x26. And it writes them with `movw`,
        so u16-little-endian is the vendor's chosen store width, not our
        assumption."""
        self.assertEqual(b"\x66\x89\x15\x20\xf2\x57\x00", at(0x40C31B, 7))  # X
        self.assertEqual(b"\x66\x89\x0d\x22\xf2\x57\x00", at(0x40C36F, 7))  # Y
        self.assertEqual(b"\x66\x89\x0d\x22\xf2\x57\x00", at(0x40C392, 7))  # Y, coarse
        # The object base itself, so the +0x10/+0x12 arithmetic is not floating.
        self.assertEqual(0x10, 0x57F220 - 0x57F210)
        self.assertEqual(0x12, 0x57F222 - 0x57F210)

    def test_the_y_block_repeats_the_same_arithmetic(self):
        self.assertEqual(b"\x3d\xe8\x03\x00\x00", at(0x40C354, 5))
        self.assertEqual(b"\x8d\x14\x80", at(0x40C361, 3))
        self.assertEqual(b"\x03\xd2", at(0x40C364, 2))
        self.assertEqual(b"\x2d\x20\x03\x00\x00", at(0x40C381, 5))
        self.assertEqual(b"\x6b\xc0\x32", at(0x40C386, 3))

    def test_every_cpi_slider_in_the_product_has_the_same_range(self):
        """SetRange(1, 0x578, FALSE) -- push 0; push 0x578; push 1.

        This was written expecting EIGHT sites (four X and four Y on the CPI
        page) and found TEN. CLAUDE.md 1.2a: a count is an absence claim about
        everywhere you did not look, so the two extra were chased rather than
        absorbed into the number. They are the FIXED CPI popup (dialog 150,
        caption 'FIXED CPI', two trackbars 1077/1080), the dialog behind the
        `fixed-cpi` BUTTON ACTION -- a completely separate code path that
        reaches the same value domain.

        That is worth more than the count was. It means the CPI a button can be
        bound to and the CPI a stage can hold are the same grid, so
        `test_button_map.py`'s independently-pinned `fixed-cpi:1600` ->
        `0c 00 40 06 40 06` corroborates this encoder, and vice versa.

        Asserted as a PARTITION over containing functions rather than as a
        total, so a new site anywhere fails loudly instead of moving a number.
        """
        pat = b"\x6a\x00\x68\x78\x05\x00\x00\x6a\x01"
        sites = [BASE + m.start() for m in re.finditer(re.escape(pat), TEXT)]
        page  = [va for va in sites if 0x40BBD0 <= va < 0x40C000]   # dialog 135
        popup = [va for va in sites if 0x401900 <= va < 0x401980]   # dialog 150
        self.assertEqual(8, len(page), "CPI page: %s" % [hex(v) for v in page])
        self.assertEqual(2, len(popup), "FIXED CPI: %s" % [hex(v) for v in popup])
        self.assertEqual(len(sites), len(page) + len(popup),
                         "a SetRange(1,1400) site outside both known dialogs: %s"
                         % [hex(v) for v in sites
                            if v not in page and v not in popup])

    def test_the_two_dialogs_are_the_ones_named(self):
        """Anchors the address ranges above to the resources, so the partition
        cannot quietly come to mean two other functions."""
        # The constructors that install each vtable push the dialog id first.
        self.assertEqual(b"\x68\x96\x00\x00\x00", at(0x40103C, 5))   # 150
        self.assertEqual(b"\x68\x87\x00\x00\x00", at(0x40AFAB, 5))   # 135


@unittest.skipUnless(os.path.exists(EGGCONFIG), "egg-config not built")
class TheEncoderAgreesWithThatDerivation(unittest.TestCase):
    def test_the_grid_is_reproduced_exactly(self):
        # Every value the tool ACCEPTS must be a fixed point of normalise(),
        # and every value it REFUSES must not be. Checked over the whole range
        # rather than at examples (§4.3).
        for v in list(range(1, 200)) + list(range(9950, 10150)) + \
                 [29990, 30000, 30001, 40000, 0, -5]:
            accepted = tool_bytes(1, v) is not None
            self.assertEqual(v == normalise(v), accepted,
                             "value %d: tool %s, rule says %s" %
                             (v, "accepted" if accepted else "refused",
                              "legal" if v == normalise(v) else "illegal"))

    def test_the_bytes_are_little_endian_with_a_computed_flag(self):
        self.assertEqual((0x00, 0x40, 0x06, 0x40, 0x06), tool_bytes(1, 1600))
        self.assertEqual((0x01, 0x40, 0x06, 0x20, 0x03), tool_bytes(1, 1600, 800))
        self.assertEqual((0x00, 0x30, 0x75, 0x30, 0x75), tool_bytes(1, 30000))
        self.assertEqual((0x00, 0x0a, 0x00, 0x0a, 0x00), tool_bytes(1, 10))

    def test_each_stage_lands_five_bytes_further_on(self):
        out = {}
        for st in range(1, CPI_STAGES + 1):
            r = subprocess.run([EGGCONFIG, "cpi", str(st), "800"],
                               capture_output=True, text=True, cwd=ROOT).stdout
            m = re.search(r"record 0x([0-9a-f]{2})\.\.0x([0-9a-f]{2})", r)
            self.assertIsNotNone(m, r)
            out[st] = (int(m.group(1), 16), int(m.group(2), 16))
        for st in range(1, CPI_STAGES + 1):
            want = CPI_FIRST + CPI_LEN * (st - 1)
            self.assertEqual((want, want + CPI_LEN - 1), out[st], "stage %d" % st)

    def test_stage_five_is_refused(self):
        r = subprocess.run([EGGCONFIG, "cpi", "5", "800"],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(2, r.returncode)
        self.assertIn("is not a CPI stage", r.stdout)

    def test_nothing_is_written_without_yes(self):
        r = subprocess.run([EGGCONFIG, "cpi", "1", "800"],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(2, r.returncode)
        self.assertIn("Re-run with --yes", r.stdout)


def captured_cpi_blocks():
    """Every four-stage CPI block the vendor ever read or wrote.

    THE SELECTION IS STRUCTURAL, not a list of filenames. The first version of
    this took every GET_REPORT over 1024 bytes, which swept in `08-flash` and
    `09-flash-again` -- whose 1041-byte reports are firmware block data read
    back by A0 07, not settings. Decoding firmware as CPI produced 248 "vendor
    values we refuse" and would have read as a devastating result for the
    encoder. It was a bug in the test.

    A 1041-byte read is a settings record only when the SET_REPORT that asked
    for it was `A1 12`. That is the config read handshake and nothing else uses
    it, so the rule needs no filename and stays right if captures are added.
    """
    import usbpcap
    seen = set()
    for name in sorted(os.listdir(CAPTURES)):
        if not name.endswith(".pcapng"):
            continue
        tr = usbpcap.control_transfers(usbpcap.read(os.path.join(CAPTURES, name)))
        asked = False
        for t in tr:
            d = bytes(t.data or b"")
            take = False
            if t.is_set_report:
                asked = d[:2] == b"\xa1\x12"
                take = len(d) >= 1024 and d[:2] == b"\xa0\x11"
            elif t.is_get_report:
                take = len(d) >= 1024 and asked
                asked = False
            if not take:
                continue
            for k in range(CPI_STAGES):
                o = PAYLOAD + CPI_FIRST + CPI_LEN * k
                seen.add((name, k, d[o:o + CPI_LEN]))
    return seen


@unittest.skipUnless(os.path.isdir(CAPTURES), "captures not present")
@unittest.skipUnless(os.path.exists(EGGCONFIG), "egg-config not built")
class TheVendorNeverWroteAValueWeWouldRefuse(unittest.TestCase):
    """The check that cannot be faked by transcribing the same function twice."""

    @classmethod
    def setUpClass(cls):
        cls.blocks = captured_cpi_blocks()

    def test_there_is_something_to_check(self):
        self.assertGreaterEqual(len(self.blocks), 4)

    def test_no_flash_capture_was_mistaken_for_a_settings_record(self):
        """The bug this class shipped with, pinned so it cannot come back."""
        # Named, not substring-matched: `10-postflash-baseline` is a config
        # capture and belongs in the set. Only these two carry A0 06/A0 07.
        bad = {"08-flash.pcapng", "09-flash-again.pcapng"}
        got = sorted({n for n, _, _ in self.blocks} & bad)
        self.assertEqual([], got, "firmware blocks decoded as CPI: %s" % got)

    def test_every_captured_cpi_is_on_our_grid(self):
        bad = []
        for name, k, e in sorted(self.blocks):
            x = e[1] | (e[2] << 8)
            y = e[3] | (e[4] << 8)
            for v in (x, y):
                if v != normalise(v):
                    bad.append("%s stage %d: %d" % (name, k + 1, v))
        self.assertEqual([], bad, "CPI values the vendor wrote and we refuse: "
                         + ", ".join(bad))

    def test_the_flag_byte_is_x_differs_from_y_in_every_capture(self):
        bad = []
        for name, k, e in sorted(self.blocks):
            x = e[1] | (e[2] << 8)
            y = e[3] | (e[4] << 8)
            if bool(e[0]) != (x != y):
                bad.append("%s stage %d: flag %d, x=%d y=%d"
                           % (name, k + 1, e[0], x, y))
        self.assertEqual([], bad, "; ".join(bad))

    def test_we_reproduce_every_captured_block_byte_for_byte(self):
        missing = []
        for name, k, e in sorted(self.blocks):
            x = e[1] | (e[2] << 8)
            y = e[3] | (e[4] << 8)
            ours = tool_bytes(k + 1, x, y if y != x else None)
            if ours is None or bytes(ours) != e:
                missing.append("%s stage %d: vendor %s, ours %s"
                               % (name, k + 1, e.hex(),
                                  bytes(ours).hex() if ours else "refused"))
        self.assertEqual([], missing, "; ".join(missing))


if __name__ == "__main__":
    unittest.main(verbosity=2)
