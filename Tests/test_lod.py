"""§7.25 -- record 0x6f decides what `lod` means.

Everything here is recovered from raw bytes: the three `cmpb $0x1, 0x57f23b`
sites, the eleven `.rdata` millimetre strings in the order cfg107 adds them,
and the eleven-arm jump table that stores 0x00..0x0a. Nothing is transcribed
from the notes, so a wrong claim in §7.25 fails here rather than being restated.

CLAUDE.md §1.2b: the decompiler and the xref lists are not admissible for a
negative. The "cfg107 never writes 0x57f23b" check below is an exhaustive scan
of the disassembly for the address, with its blind spot named in the test that
asserts it.
"""
import os
import re
import struct
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe")
CAPTURES = os.path.join(ROOT, "windows-run")
PAYLOAD = 16

OBJECT = 0x57F210          # the settings object, §7.3
GATE = OBJECT + 0x2B       # -> record 0x6f
LOD = OBJECT + 0x27        # -> record 0x09


def disassemble(start, stop):
    return subprocess.run(
        [os.path.join(ROOT, "Tools/ghidra-export/dis.sh"), "cfg107",
         hex(start), hex(stop)],
        capture_output=True, text=True, check=True).stdout


class SectionMap:
    """VA -> file offset. Written out rather than reached for, because the
    first attempt matched on `max(virtual, raw)` size and silently resolved an
    .rdata address into .text -- which produced twelve plausible 'strings' that
    were actually instruction bytes."""

    def __init__(self, path):
        with open(path, "rb") as fh:
            self.f = fh.read()
        pe = struct.unpack_from("<I", self.f, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.f, pe + 6)[0]
        opt = struct.unpack_from("<H", self.f, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.f, pe + 24 + 28)[0]
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + opt + 40 * i
            vs, va, rs, pr = struct.unpack_from("<IIII", self.f, o + 8)
            self.secs.append((va, vs, pr))

    def raw_off(self, va):
        r = va - self.base
        for v, vs, pr in self.secs:
            if v <= r < v + vs:
                return pr + (r - v)
        raise KeyError(hex(va))

    def wstr(self, va):
        r = va - self.base
        for v, vs, pr in self.secs:
            if v <= r < v + vs:            # VIRTUAL size only. See above.
                o = pr + (r - v)
                end = o
                while self.f[end:end + 2] != b"\0\0":
                    end += 2
                return self.f[o:end].decode("utf-16-le")
        raise KeyError(hex(va))


@unittest.skipUnless(os.path.exists(EXE), "cfg107 not present")
class TheGateIsReadOnlyAndGovernsTheLodCombo(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.text = disassemble(0x401000, 0x52C591)
        cls.pe = SectionMap(EXE)
        cls.pe_off = staticmethod(cls.pe.raw_off)

    def test_the_gate_is_compared_to_one_at_exactly_three_sites(self):
        want = "cmpb\t$0x1, 0x%x" % GATE
        at = [int(l.split(":")[0].strip(), 16)
              for l in self.text.splitlines() if want in l]
        self.assertEqual([0x40EC42, 0x40EEB6, 0x40F1E5], sorted(at))

    def test_no_store_to_the_gate_by_absolute_address(self):
        """A BOUNDED negative (§1.2a). Search space: cfg107 .text, every
        instruction mentioning 0x57f23b. Encoding forms NOT covered: a store
        through a base register, e.g. `movb %al, 0x2b(%ecx)`. Those exist and
        are unenumerable, so this test says 'no absolute store', never
        'never written'."""
        hits = [l for l in self.text.splitlines() if "0x%x" % GATE in l]
        self.assertEqual(3, len(hits), "\n".join(hits))
        for l in hits:
            self.assertIn("cmpb", l, "a non-compare reference appeared: " + l)

    def test_the_eleven_entry_list_is_the_millimetre_scale(self):
        """The strings cfg107 pushes to CB_ADDSTRING, in emission order."""
        block = disassemble(0x40EF2A, 0x40F100)
        vas = [int(m, 16) for m in
               re.findall(r"pushl\t\$(0x557f[0-9a-f]{2})", block)]
        seen, order = set(), []
        for va in vas:
            if va not in seen:
                seen.add(va)
                order.append(va)
        self.assertEqual(11, len(order), [hex(v) for v in order])
        self.assertEqual(
            ["0.7mm", "0.8mm", "0.9mm", "1.0mm", "1.1mm", "1.2mm",
             "1.3mm", "1.4mm", "1.5mm", "1.6mm", "1.7mm"],
            [self.pe.wstr(v) for v in order])

    def test_the_two_entry_list_uses_a_string_the_eleven_never_does(self):
        block = disassemble(0x40EED5, 0x40EF2A)
        vas = [int(m, 16) for m in
               re.findall(r"pushl\t\$(0x557f[0-9a-f]{2})", block)]
        self.assertEqual(["1.0mm", "2.0mm"],
                         [self.pe.wstr(v) for v in vas])

    def arms(self):
        """Every `movb $imm, 0x27(%edi)` in the handler, by address.

        Written this way because the first version scanned the range after the
        jump table and found nine arms, not eleven -- the compiler SHARES the
        arms for 1 and 2 with the two-entry ladder above the table, so they sit
        outside any range that starts at the table. A scan that reports nine
        and is asserted against eleven is a caught bug; one asserted against
        "at least eight" would have shipped."""
        block = disassemble(0x40EC40, 0x40ECA2)
        out = {}
        for line in block.splitlines():
            m = re.match(r"\s*([0-9a-f]+):.*movb\t\$(0x[0-9a-f]+), 0x27\(%edi\)",
                         line)
            if m:
                out[int(m.group(1), 16)] = int(m.group(2), 16)
        return out

    def test_the_jump_table_maps_each_index_to_its_own_value(self):
        """The eleven-entry scale, read out of the table rather than assumed:
        `jmpl *0x40ee54(,%eax,4)` at 0x40ec67. index == stored value, so `lod`
        IS the position in the 0.7mm..1.7mm list."""
        arms = self.arms()
        targets = []
        for i in range(11):
            targets.append(struct.unpack_from(
                "<I", self.pe.f, self.pe_off(0x40EE54) + 4 * i)[0])
        self.assertEqual([0x40EC6E, 0x40EC56, 0x40EC5C, 0x40EC9E, 0x40EC74,
                          0x40EC7A, 0x40EC80, 0x40EC86, 0x40EC8C, 0x40EC92,
                          0x40EC98], targets)
        self.assertEqual(list(range(11)), [arms[t] for t in targets])

    def test_the_two_arm_ladder_reuses_the_arms_for_one_and_two(self):
        """Under the gate, index 0 -> 1 and index 1 -> 2. Same two stores the
        table uses for its indices 1 and 2 -- which is exactly why `1.0mm`
        means 1 here and 3 there."""
        ladder = disassemble(0x40EC4E, 0x40EC56)
        self.assertIn("je\t0x40ec56", ladder)
        self.assertIn("je\t0x40ec5c", ladder)
        arms = self.arms()
        self.assertEqual(1, arms[0x40EC56])
        self.assertEqual(2, arms[0x40EC5C])

    def test_the_enable_site_greys_the_combo_when_the_gate_is_set(self):
        block = disassemble(0x40F1E5, 0x40F212)
        self.assertIn("jne", block)
        self.assertIn("calll\t0x41b642", block)   # CWnd::EnableWindow

    def test_enablewindow_is_what_0x41b642_actually_is(self):
        body = disassemble(0x41B642, 0x41B65D)
        self.assertIn("calll\t*0x52d884", body)
        imports = subprocess.run(["rabin2", "-i", EXE],
                                 capture_output=True, text=True).stdout
        slot = [l for l in imports.splitlines() if "0x0052d884" in l]
        self.assertTrue(slot and "EnableWindow" in slot[0], slot)


@unittest.skipUnless(os.path.isdir(CAPTURES), "captures not present")
class OnThisMouseTheGateIsZero(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from test_handedness import records
        cls.recs = [f for n in sorted(os.listdir(CAPTURES))
                    if n.endswith(".pcapng") for f in records(n)]

    def test_every_record_reports_gate_zero(self):
        self.assertEqual(82, len(self.recs))
        self.assertEqual({0}, {f[PAYLOAD + 0x6F] for f in self.recs})

    def test_the_lod_values_observed_are_the_eleven_entry_scale(self):
        seen = {f[PAYLOAD + 0x09] for f in self.recs}
        self.assertEqual(set(range(0x0B)), seen,
                         "02-basic swept all eleven; a gap means a lost capture")

    def test_the_factory_lod_is_three_which_is_one_millimetre(self):
        import collections
        c = collections.Counter(f[PAYLOAD + 0x09] for f in self.recs)
        self.assertEqual(3, c.most_common(1)[0][0])


if __name__ == "__main__":
    unittest.main()
