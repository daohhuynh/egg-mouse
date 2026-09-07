"""§7.25 -- record 0x6f is Sensor Glass Mode, and it decides what `lod` means.

Everything here is recovered from raw bytes: the three `cmpb $0x1, 0x57f23b`
sites, the eleven `.rdata` millimetre strings in the order cfg107 adds them,
and the eleven-arm jump table that stores 0x00..0x0a. Nothing is transcribed
from the notes, so a wrong claim in §7.25 fails here rather than being restated.

engineering-rules.md §1.2b: the decompiler and the xref lists are not admissible for a
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


def disassemble_tag(tag, start, stop):
    return subprocess.run(
        [os.path.join(ROOT, "Tools/ghidra-export/dis.sh"), tag,
         hex(start), hex(stop)],
        capture_output=True, text=True, check=True).stdout


def disassemble(start, stop):
    return disassemble_tag("cfg107", start, stop)


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


class TheGateIsAWithdrawnSetting(unittest.TestCase):
    """§7.25's correction. Read from cfg107 alone, record 0x6f looks like a
    device capability flag: only ever compared, never stored. The other three
    config tools WRITE it, from a checkbox. Every number below is recovered per
    binary -- the object base from that tool's own immediate loads, the record
    mapping from recmap.py -- so none of it is cfg107's layout assumed onto the
    others."""

    # The `lod` read each tool's Advanced Sensor page performs, object +0x27.
    # Used only to CHECK the base recovered independently below.
    LOD_LOAD = {"cfg100": 0x5DBF67, "cfg101": 0x57F2D7,
                "cfg104": 0x57E237, "cfg107": 0x57F237}
    BASE = {"cfg100": 0x5DBF40, "cfg101": 0x57F2B0,
            "cfg104": 0x57E210, "cfg107": 0x57F210}

    def test_every_version_serialises_0x6f_from_object_0x2b(self):
        sys.path.insert(0, os.path.join(ROOT, "Tools/ghidra-export"))
        from recmap import extract
        for tag in ("cfg100", "cfg101", "cfg104", "cfg107"):
            r = extract(tag)
            self.assertTrue(r["ok"], tag)
            self.assertEqual(0x2B, r["map"][0x6F], tag)
            self.assertEqual(0x27, r["map"][0x09], tag)

    def test_the_recovered_base_agrees_with_the_lod_read(self):
        """base + 0x27 == the byte the page loads for `lod`. If these ever
        disagree the base is wrong and every address below is meaningless."""
        for tag, base in self.BASE.items():
            self.assertEqual(self.LOD_LOAD[tag], base + 0x27, tag)

    def test_the_older_tools_write_the_gate_and_cfg107_does_not(self):
        want = {"cfg100": (0x4134B8, 0x4134D3),
                "cfg101": (0x411764, 0x411785),
                "cfg104": (0x411824, 0x411845)}
        for tag, (on, off) in want.items():
            text = disassemble_tag(tag, 0x401000, 0x520000)
            gate = self.BASE[tag] + 0x2B
            stores = {}
            for line in text.splitlines():
                m = re.match(r"\s*([0-9a-f]+):.*movb\t\$(0x[0-9a-f]+), 0x%x$" % gate,
                             line)
                if m:
                    stores[int(m.group(1), 16)] = int(m.group(2), 16)
            self.assertEqual({on: 1, off: 0}, stores, tag)

    def test_cfg107_only_ever_compares_it(self):
        text = disassemble_tag("cfg107", 0x401000, 0x52C591)
        gate = self.BASE["cfg107"] + 0x2B
        hits = [l for l in text.splitlines() if ("0x%x" % gate) in l]
        self.assertEqual(3, len(hits))
        for l in hits:
            self.assertIn("cmpb", l, l)

    def test_the_control_is_a_checkbox_called_sensor_glass_mode(self):
        """The caption comes from the resources by way of ctlchain.py, which
        resolves a control to the class that binds it. Both spellings are
        accepted because 1.00/1.01 say `Glass Mode` and 1.04/1.07 say
        `Sensor Glass Mode` -- and a test that demanded one would fail on a
        rename rather than on a finding."""
        for tag in ("cfg100", "cfg101", "cfg104", "cfg107"):
            out = subprocess.run(
                [sys.executable,
                 os.path.join(ROOT, "Tools/ghidra-export/ctlchain.py"), tag],
                capture_output=True, text=True, cwd=ROOT).stdout
            rows = [l for l in out.splitlines()
                    if "1031" in l and "Glass Mode" in l]
            self.assertTrue(rows, "%s: no control 1031 captioned Glass Mode" % tag)
            self.assertIn("AUTOCHECKBOX", rows[0], tag)

    def test_cfg107s_handler_for_it_is_a_bare_ret(self):
        """§7.21. The control survives into 1.07 and its BN_CLICKED entry points
        at 0x413d90, which is one byte of `retl`."""
        out = subprocess.run(
            [sys.executable, os.path.join(ROOT, "Tools/ghidra-export/ctlchain.py"),
             "cfg107"], capture_output=True, text=True, cwd=ROOT).stdout
        lines = out.splitlines()
        idx = [i for i, l in enumerate(lines)
               if "1031" in l and "Sensor Glass Mode" in l]
        self.assertEqual(1, len(idx))
        window = "\n".join(lines[idx[0]:idx[0] + 3])
        self.assertIn("0x00413d90", window, window)
        body = disassemble_tag("cfg107", 0x413D90, 0x413D91)
        self.assertIn("retl", body)


@unittest.skipUnless(os.path.exists(EXE), "cfg107 not present")
class LodHasTwoWritersWithDifferentEncodings(unittest.TestCase):
    """§7.29. Two eleven-arm tables write object +0x27 in cfg107. Only the APPLY
    collector's values have ever been on the wire."""

    def arms(self, lo, hi, base):
        text = disassemble_tag("cfg107", lo, hi)
        out = {}
        for line in text.splitlines():
            m = re.match(r"\s*([0-9a-f]+):.*movb\t\$(-?0x[0-9a-f]+), %s" % base,
                         line)
            if m:
                out[int(m.group(1), 16)] = int(m.group(2), 16) & 0xFF
        return out

    def table(self, va, n):
        pe = SectionMap(EXE)
        return [struct.unpack_from("<I", pe.f, pe.raw_off(va) + 4 * i)[0]
                for i in range(n)]

    def test_the_selection_handler_stores_a_sensor_looking_ladder(self):
        arms = self.arms(0x40FA70, 0x40FB1C, r"0x57f237")
        vals = [arms[t] for t in self.table(0x40FB1C, 11)]
        self.assertEqual([0xC2, 0xC4, 0xC6, 0xC9, 0xCA, 0xCC, 0xCD,
                          0xD0, 0xD4, 0xD7, 0xD9], vals)
        self.assertEqual(sorted(vals), vals, "the ladder is monotone")

    def test_the_apply_collector_stores_the_index(self):
        arms = self.arms(0x40EC40, 0x40ECA2, r"0x27\(%edi\)")
        self.assertEqual(list(range(11)),
                         [arms[t] for t in self.table(0x40EE54, 11)])

    def test_the_two_sets_are_disjoint(self):
        """They differ by ~200, so picking the wrong one is not a near miss."""
        a = set(self.arms(0x40FA70, 0x40FB1C, r"0x57f237").values())
        b = set(self.arms(0x40EC40, 0x40ECA2, r"0x27\(%edi\)").values())
        self.assertEqual(set(), a & b)


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

    def test_the_sensor_ladder_has_never_been_on_the_wire(self):
        """§7.29. If a record ever carried 0xc2..0xd9 at 0x09 the selection
        handler's encoding would be the live one and `set lod 0..10` would be
        writing nonsense."""
        seen = {f[PAYLOAD + 0x09] for f in self.recs}
        self.assertEqual(set(), seen & set(range(0xC0, 0xE0)))

    def test_the_factory_lod_is_three_which_is_one_millimetre(self):
        import collections
        c = collections.Counter(f[PAYLOAD + 0x09] for f in self.recs)
        self.assertEqual(3, c.most_common(1)[0][0])


if __name__ == "__main__":
    unittest.main()
