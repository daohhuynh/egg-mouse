"""Tests/test_citations.py -- every byte egg-config can write, checked against
the vendor binary it was derived from.

CLAUDE.md 1.2 requires a [D] claim to cite an address in an .exe. Nothing has
ever enforced that. The button table in ConfigRecord.cpp carries a cfg107
address on each of its nineteen rows, the settable-field table carries one on
each of its thirteen -- thirty-two citations that were checked once, by eye,
by a session that is gone, and never again.

This checks them from RAW BYTES, on every run.

WHY RAW BYTES AND NOT A DISASSEMBLER (CLAUDE.md 1.2b: derived views are evidence,
never ground truth). objdump's linear sweep desynchronises after data embedded in
code. It desynchronises in exactly this region: it renders 0x40865a as part of a
bogus `addb %cl, -0x7d(%eax)` and hides the `movb $0x2, %cl` that sets the
keyboard action type. A byte-pattern search has no such failure mode, and anyone
can reproduce it with `xxd` and `grep`.

WHAT THIS PROVES, and it is a strong claim: for each action our tool offers, the
vendor's own handler stores THE SAME BYTES into the settings object. Not a
similar value, not a value consistent with a capture -- the identical immediate,
at the address the table cites. If someone edits a byte in our table, or edits an
address, or transcribes a new version's table wrongly, this fails and names the
row.

IT USED TO SAY "THE SAME TWO BYTES", AND IT MEANT IT. Until 2026-09-07 this file
checked +0 and +1 and stopped, while defining OBJ_PAYLOAD = 0x57F240 and never
using it. A button entry is seven bytes. BROWSER and EXPLORER store 0x01 into the
+2..+3 word -- their HID usage is u16 LE across +1..+2 -- and our encoder wrote
zero there, unnoticed, because nothing looked. The +2..+5 checks below exist so
that the question "does our table reproduce the vendor's whole entry?" is decided
by the bytes rather than by which columns someone remembered to compare.

WHAT IT DOES NOT PROVE. That the address is the handler for the MENU ITEM we
named it after. Nothing in the bytes says "this is volume up" -- that came from
the string table and the capture. So a swap of two labels whose bytes we also
swapped would pass. Tests/test_button_map.py covers that from the other side, by
requiring the vendor's captured wire bytes to be reproducible.
"""
import os
import re
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG107 = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe")
SRC = os.path.join(ROOT, "Sources", "EGGConfigCore", "src", "ConfigRecord.cpp")

# The settings OBJECT (not the record cache at 0x57f340). Button entry k lives
# at 0x57f23e + 8k: +0 action type, +1 the discriminated second byte, +2 payload.
OBJ_ACTION = 0x57F23E
OBJ_B1 = 0x57F23F
OBJ_PAYLOAD = 0x57F240


def load_text():
    """cfg107's .text as (bytes, virtual address of byte 0)."""
    with open(CFG107, "rb") as f:
        data = f.read()
    i = data.find(b".text\x00")
    if i < 0:
        raise AssertionError("no .text section header in cfg107")
    vsize, rva, rawsize, rawoff = struct.unpack_from("<IIII", data, i + 8)
    # PE optional header: ImageBase at offset 0x1c from the start of the
    # optional header, which follows the 20-byte COFF header after "PE\0\0".
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    base = struct.unpack_from("<I", data, pe + 4 + 20 + 0x1C)[0]
    return data[rawoff:rawoff + rawsize], base + rva


TEXT, TEXT_VA = load_text(), 0
TEXT, TEXT_VA = TEXT[0], TEXT[1]


def at(va, n):
    o = va - TEXT_VA
    if o < 0 or o + n > len(TEXT):
        return b""
    return TEXT[o:o + n]


def store_to(va, window, target):
    """Find the first byte store to `target` in [va, va+window).

    Returns ('imm', value) for `movb $imm, target(,%reg,8)`, ('reg', name) for
    `movb %r8, target(,%reg,8)`, or None. The SIB form is what the vendor uses
    throughout -- entry index scaled by 8 -- so the encodings are fixed:

        c6 04 <sib> <disp32> <imm8>     movb $imm, disp(,%index,8)
        88 <modrm> <sib> <disp32>       movb %r8,  disp(,%index,8)
    """
    blob = at(va, window)
    disp = struct.pack("<I", target)
    for m in re.finditer(re.escape(disp), blob):
        i = m.start()
        if i >= 3 and blob[i - 3] == 0xC6 and blob[i - 2] == 0x04:
            return ("imm", blob[i + 4])
        if i >= 3 and blob[i - 3] == 0x88:
            return ("reg", "%s%02x" % ("r", blob[i - 2]))
    return None


def word_store_to(va, window, target):
    """The source register of the first `movw %r16, target(,%reg,8)` in range.

    Encoding, fixed throughout the vendor's handlers:

        66 89 <modrm> <sib> <disp32>     movw %r16, disp(,%index,8)

    modrm has mod=00 and rm=100 (SIB follows); the source register is the reg
    field. Returns the register number, or None.
    """
    blob = at(va, window)
    disp = struct.pack("<I", target)
    for m in re.finditer(re.escape(disp), blob):
        i = m.start()
        if i >= 4 and blob[i - 4] == 0x66 and blob[i - 3] == 0x89:
            modrm = blob[i - 2]
            if (modrm & 0xC0) == 0 and (modrm & 7) == 4:
                return (modrm >> 3) & 7
    return None


def reg_holds(va, window_before, reg, want):
    """Was `reg` loaded with `want` in the `window_before` bytes before `va`?

    Zero is reached by `xor r,r` (33 /r or 31 /r with mod=11 and reg==rm); any
    other value by `movl $imm32, r32` (b8+r). Both forms are what the vendor's
    compiler emits here, and both are checked from raw bytes rather than from a
    disassembler that desynchronises in this region (CLAUDE.md 1.2b).
    """
    blob = at(va - window_before, window_before + 24)
    if want == 0:
        for op in (0x33, 0x31):
            for m in re.finditer(re.escape(bytes([op])), blob):
                j = m.start() + 1
                if j < len(blob):
                    modrm = blob[j]
                    if modrm >= 0xC0 and ((modrm >> 3) & 7) == (modrm & 7) \
                            and (modrm & 7) == reg:
                        return True
        return False
    return bytes([0xB8 + reg]) + struct.pack("<I", want) in blob


def parse_button_table():
    """The shipping table, straight out of the C++ source."""
    with open(SRC, encoding="utf-8") as f:
        src = f.read()
    rows = []
    for m in re.finditer(
            r'\{"([a-z0-9-]+)",\s*"([a-z]+)",\s*'
            r'(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*'
            r'ButtonPayload::(\w+),\s*"cfg107 ((?:0x[0-9a-f]+/?)+)"\s*\}', src):
        rows.append({
            "name": m.group(1), "group": m.group(2),
            "b0": int(m.group(3), 16), "b1": int(m.group(4), 16),
            "b2": int(m.group(5), 16),
            "payload": m.group(6),
            "addrs": [int(a, 16) for a in m.group(7).split("/")],
        })
    return rows


class TheButtonTableMatchesTheBinary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = parse_button_table()

    def test_the_table_was_actually_found(self):
        # A regex that silently matches nothing would make every test below
        # vacuously pass. ConfigRecord.cpp ships nineteen actions.
        self.assertEqual(19, len(self.rows),
                         "parsed %d rows: %s" %
                         (len(self.rows), [r["name"] for r in self.rows]))

    def test_every_cited_address_is_inside_cfg107_text(self):
        for r in self.rows:
            for a in r["addrs"]:
                self.assertTrue(at(a, 8), "%s cites 0x%06x, outside .text"
                                % (r["name"], a))

    def test_each_action_type_byte_is_the_immediate_the_vendor_stores(self):
        """The load-bearing check on +0."""
        for r in self.rows:
            found = None
            for a in r["addrs"]:
                found = store_to(a, 80, OBJ_ACTION)
                if found:
                    break
            self.assertIsNotNone(
                found, "%s: no store to the action byte within 80 bytes of %s"
                % (r["name"], ["0x%06x" % a for a in r["addrs"]]))
            kind, val = found
            if kind == "imm":
                self.assertEqual(
                    r["b0"], val,
                    "%s: table says action type 0x%02x, cfg107 stores 0x%02x"
                    % (r["name"], r["b0"], val))
            else:
                # A register-sourced store. The value must still be pinned, so
                # require the immediate load to be findable in the run-up --
                # this is how `key` (0x02 at 0x40865a) and the five mouse
                # actions (0x00 via a zeroed register) are covered.
                self.assertTrue(
                    self.value_is_loaded_nearby(r["addrs"][0], r["b0"]),
                    "%s: action byte comes from a register and no load of "
                    "0x%02x was found in the preceding 0x200 bytes"
                    % (r["name"], r["b0"]))

    def test_each_second_byte_is_the_immediate_the_vendor_stores(self):
        for r in self.rows:
            if r["payload"] == "Key":
                # +1 is the HID modifier bitfield, built at run time by the
                # `orb` chain at 0x40866f-0x408689 and copied from 0x57f335.
                # There is no immediate to compare, by design.
                continue
            found = None
            for a in r["addrs"]:
                found = store_to(a, 80, OBJ_B1)
                if found:
                    break
            self.assertIsNotNone(found, "%s: no store to +1" % r["name"])
            kind, val = found
            if kind == "imm":
                self.assertEqual(
                    r["b1"], val,
                    "%s: table says +1 = 0x%02x, cfg107 stores 0x%02x"
                    % (r["name"], r["b1"], val))
            else:
                self.assertTrue(
                    self.value_is_loaded_nearby(r["addrs"][0], r["b1"]),
                    "%s: +1 comes from a register and no load of 0x%02x was "
                    "found nearby" % (r["name"], r["b1"]))

    def value_is_loaded_nearby(self, va, want):
        """An 8- or 32-bit immediate load of `want` into any GPR, or -- for
        zero -- a self-xor, within 0x200 bytes before the cited address."""
        blob = at(va - 0x200, 0x200 + 16)
        if want == 0:
            for op in (b"\x33", b"\x31"):          # xorl r,r either direction
                for m in re.finditer(re.escape(op), blob):
                    if m.start() + 1 < len(blob):
                        modrm = blob[m.start() + 1]
                        if modrm >= 0xC0 and ((modrm >> 3) & 7) == (modrm & 7):
                            return True
        for reg in range(8):
            if bytes([0xB0 + reg, want]) in blob:            # movb $imm8, r8
                return True
            if bytes([0xB8 + reg]) + struct.pack("<I", want) in blob:
                return True                                   # movl $imm32, r32
        return False

    def test_the_media_actions_are_hid_consumer_page_usages(self):
        """External corroboration, and the only check here with an outside
        source. These eight values are published HID Consumer Page usages; the
        vendor storing exactly them is why §7.16 could close the >8-bit
        question rather than guess at a vendor index table."""
        HID_CONSUMER = {
            "play-pause": 0xCD, "next": 0xB5, "previous": 0xB6, "mute": 0xE2,
            "volume-up": 0xE9, "volume-down": 0xEA,
            # Browser/Explorer are 0x0196 and 0x0194 -- u16 LE across +1..+2,
            # so +1 carries the low byte and +2 carries 0x01. Their action type
            # is 0x18 rather than the 0x20 the six single-byte ones use. The
            # high byte was assumed to vanish until 2026-09-07; it does not.
            "browser": 0x96, "explorer": 0x94,
        }
        for r in self.rows:
            if r["group"] == "media":
                self.assertIn(r["name"], HID_CONSUMER, "unlisted media action")
                self.assertEqual(HID_CONSUMER[r["name"]], r["b1"], r["name"])

    def test_the_plus_2_byte_is_the_word_the_vendor_stores(self):
        """+2..+3 -- the check that did not exist until 2026-09-07.

        Every handler with a fixed payload writes a 16-bit word to OBJ_PAYLOAD
        from a register it has just loaded. Seventeen load zero with `xor`;
        BROWSER and EXPLORER load 1 with `movl $0x1`. The table's b2 column must
        be the low byte of whichever word the vendor stores.
        """
        for r in self.rows:
            if r["payload"] != "None":
                continue          # FixedCpi computes it; Key does not use it.
            found = None
            for a in r["addrs"]:
                reg = word_store_to(a, 96, OBJ_PAYLOAD)
                if reg is not None:
                    found = (a, reg)
                    break
            self.assertIsNotNone(
                found, "%s: no movw to the +2..+3 word within 96 bytes of %s"
                % (r["name"], ["0x%06x" % a for a in r["addrs"]]))
            a, reg = found
            self.assertTrue(
                reg_holds(a + 96, 96, reg, r["b2"]),
                "%s: table says +2 = 0x%02x, but the register the vendor "
                "stores into +2..+3 near 0x%06x was not loaded with it"
                % (r["name"], r["b2"], a))

    def test_no_action_declares_a_plus_2_the_vendor_zeroes(self):
        """The same claim from the other side, so a table edit cannot pass by
        also editing the address it cites. Exactly two rows may be non-zero,
        and they are the two whose HID usage does not fit in one byte."""
        nonzero = sorted(r["name"] for r in self.rows if r["b2"] != 0)
        self.assertEqual(["browser", "explorer"], nonzero,
                         "rows with a non-zero +2: %s" % nonzero)
        by = {r["name"]: r for r in self.rows}
        self.assertEqual(0x01, by["browser"]["b2"])
        self.assertEqual(0x01, by["explorer"]["b2"])

    def test_the_plus_4_word_is_zero_for_every_fixed_payload_action(self):
        """+4..+5 belongs to FIXED CPI's Y value and to nothing else. If a
        handler ever stopped zeroing it, our encoder -- which always writes
        zeros there for a None action -- would be wrong again in the same way.
        """
        for r in self.rows:
            if r["payload"] != "None":
                continue
            found = None
            for a in r["addrs"]:
                reg = word_store_to(a, 120, OBJ_PAYLOAD + 2)
                if reg is not None:
                    found = (a, reg)
                    break
            self.assertIsNotNone(found, "%s: no store to +4..+5" % r["name"])
            a, reg = found
            self.assertTrue(
                reg_holds(a + 120, 120, reg, 0),
                "%s: the +4..+5 word near 0x%06x is not stored from a zeroed "
                "register" % (r["name"], a))

    def test_the_two_browser_actions_use_a_different_action_type(self):
        # If this ever equalises, someone has "tidied" the table and broken the
        # thing that makes a 16-bit usage fit in one byte.
        by = {r["name"]: r for r in self.rows}
        self.assertEqual(0x18, by["browser"]["b0"])
        self.assertEqual(0x18, by["explorer"]["b0"])
        self.assertEqual(0x20, by["mute"]["b0"])


class TheSettableFieldCitationsResolve(unittest.TestCase):
    """The thirteen writable fields. Weaker than the button check -- their
    citations name a mnemonic rather than an immediate -- so this verifies what
    can be verified from bytes: that each cited address is real, and that the
    two message-constant citations name the constant the code actually pushes.
    """

    def setUp(self):
        with open(SRC, encoding="utf-8") as f:
            self.src = f.read()

    def cites(self):
        for m in re.finditer(r'"config-protocol\.md §[\d.]+, cfg107 ([^"]*)"',
                             self.src):
            yield m.group(1), [int(a, 16) for a in
                               re.findall(r"0x([0-9a-f]{6})", m.group(1))]

    def test_every_writable_field_cites_an_address(self):
        """CLAUDE.md 1.2, enforced rather than assumed.

        This is what the audit actually caught: `lod` and `cpi-stage` were
        writable, shipping, and cited NOTHING -- their ranges came from counting
        writes in a capture and from a log that is now quarantined (1.1a).
        Both are derived and cited now. This test is what stops it recurring.
        """
        blk = self.src[self.src.index("const Settable kSettable[]"):
                       self.src.index("const std::size_t kSettableCount")]
        rows = re.split(r"\n    \{\"", blk)[1:]
        self.assertGreaterEqual(len(rows), 12, "parsed only %d rows" % len(rows))
        for r in rows:
            name = r.split('"')[0]
            self.assertTrue(
                re.search(r"cfg1\d\d[^\"]*?0x[0-9a-f]{6}", r),
                "settable field %r cites no cfg1xx address -- CLAUDE.md 1.2 "
                "says a [D] claim must cite one, and every byte this table can "
                "write reaches a mouse there is only one of" % name)

    def test_every_field_citation_is_inside_cfg107_text(self):
        for text, addrs in self.cites():
            for a in addrs:
                self.assertTrue(at(a, 4), "0x%06x outside .text (%s)" % (a, text))

    def test_the_polling_bound_check_is_where_the_note_says(self):
        # §7.5: "bound check 0x413a7d cmpl $0x3f on (byte[2]-1)".
        # 83 f8 3f = cmpl $0x3f, %eax
        self.assertEqual(b"\x83\xf8\x3f", at(0x413A7D, 3))
        # and the jump-table index byte array it guards
        self.assertEqual(b"\x0f\xb6\x90", at(0x413A86, 3))   # movzbl 0x413c04(%eax)
        self.assertEqual(0x413C04, struct.unpack("<I", at(0x413A89, 4))[0])

    def test_the_two_checkbox_stores_really_are_setcc(self):
        # §7.8 cites `setne` at 0x40ecb8 and `sete` at 0x40ecd2. 0f 95 = setne,
        # 0f 94 = sete. Getting these backwards is what the led-on-liftoff
        # inversion is, so the distinction is load-bearing rather than cosmetic.
        self.assertEqual(b"\x0f\x95", at(0x40ECB8, 2), "0x40ecb8 should be setne")
        self.assertEqual(b"\x0f\x94", at(0x40ECD2, 2), "0x40ecd2 should be sete")

    def test_the_lod_range_is_eleven_stores_of_0x00_to_0x0a(self):
        """§7.18. The field that had no citation at all until the audit."""
        # cmpl $0xa, %eax ; ja  -- the bound check, eleven items
        self.assertEqual(b"\x83\xf8\x0a", at(0x40EC62, 3))
        self.assertEqual(0x77, at(0x40EC65, 1)[0], "0x40ec65 should be `ja`")
        # The eleven arms, in jump-table order rather than numeric order.
        arms = {0x40EC56: 0x01, 0x40EC5C: 0x02, 0x40EC6E: 0x00, 0x40EC74: 0x04,
                0x40EC7A: 0x05, 0x40EC80: 0x06, 0x40EC86: 0x07, 0x40EC8C: 0x08,
                0x40EC92: 0x09, 0x40EC98: 0x0A, 0x40EC9E: 0x03}
        for va, want in sorted(arms.items()):
            # c6 47 27 <imm> = movb $imm, 0x27(%edi)   -- object 0x27 -> rec 0x09
            self.assertEqual(b"\xc6\x47\x27", at(va, 3),
                             "0x%06x is not a store to object 0x27" % va)
            self.assertEqual(want, at(va + 3, 1)[0], "0x%06x" % va)
        self.assertEqual(set(range(0, 11)), set(arms.values()),
                         "the eleven arms must cover exactly 0..10")

    def test_the_second_lod_encoding_is_recorded_and_disjoint(self):
        """§7.18's caveat, asserted so it cannot be quietly forgotten.

        A second CB_GETCURSEL at 0x40fa95-0x40fb0d writes 0xc2..0xd9 into the
        SAME object byte. Never observed on this device, which is not the same
        as absent (1.2a). The two ranges must stay disjoint -- that disjointness
        is the whole reason a stray value would be recognisable.
        """
        other = set()
        for va in (0x40FA95, 0x40FAA1, 0x40FAAD, 0x40FAB9, 0x40FAC5, 0x40FAD1,
                   0x40FADD, 0x40FAE9, 0x40FAF5, 0x40FB01, 0x40FB0D):
            self.assertEqual(b"\xc6\x05", at(va, 2))
            self.assertEqual(0x57F237, struct.unpack("<I", at(va + 2, 4))[0],
                             "0x%06x should target object 0x27" % va)
            other.add(at(va + 6, 1)[0])
        self.assertEqual(11, len(other))
        self.assertEqual(set(), other & set(range(0, 11)),
                         "the two LOD encodings have started to overlap; a "
                         "stray value is no longer recognisable")

    def test_the_lod_list_length_is_conditional_on_record_0x6f(self):
        # cmpb $0x1, 0x57f23b -- object 0x2b, which recmap maps to record 0x6f.
        self.assertEqual(b"\x80\x3d", at(0x40EC42, 2))
        self.assertEqual(0x57F23B, struct.unpack("<I", at(0x40EC44, 4))[0])
        self.assertEqual(0x01, at(0x40EC48, 1)[0])

    def test_the_cpi_stage_radio_group_stores_0_to_3(self):
        """§7.18. The other field the audit found uncited."""
        arms = {0x40EDFD: 0, 0x40EE17: 1, 0x40EE31: 2, 0x40EE4C: 3}
        for va, want in sorted(arms.items()):
            # c6 47 0a <imm> = movb $imm, 0xa(%edi)   -- object 0x0a -> rec 0x0d
            self.assertEqual(b"\xc6\x47\x0a", at(va, 3), "0x%06x" % va)
            self.assertEqual(want, at(va + 3, 1)[0], "0x%06x" % va)
        # Each arm is guarded by a BM_GETCHECK (0xf0) on its own control.
        for va, ctl in ((0x40EE01, 0x1468), (0x40EE1B, 0x14DC), (0x40EE35, 0x1550)):
            self.assertIn(struct.pack("<I", 0xF0), at(va, 24),
                          "no BM_GETCHECK near 0x%06x" % va)
            self.assertIn(struct.pack("<I", ctl), at(va, 8),
                          "control 0x%04x not read at 0x%06x" % (ctl, va))

    def test_the_sensor_angle_is_clamped_to_a_symmetric_127(self):
        """§7.16. The clamp that `encodeSensorAngle` must not exceed.

        This is the check that would have caught the -128 the encoder used to
        accept: the vendor clamps with SIGNED compares to +/-127, so 0x80 is a
        byte its own UI cannot produce.
        """
        self.assertEqual(b"\x83\xf8\x81", at(0x411F62, 3), "cmpl $-0x7f, %eax")
        self.assertEqual(0x7D, at(0x411F65, 1)[0],
                         "0x411f65 must be `jge` (0x7d), not `jae` (0x73) -- "
                         "an unsigned form would mean this is not a signed field")
        self.assertEqual(0xFFFFFF81,
                         struct.unpack("<I", at(0x411F6D, 4))[0], "low clamp -127")
        self.assertEqual(b"\xb8\x7f\x00\x00\x00", at(0x411F71, 5), "high clamp 127")
        self.assertEqual(0x7E, at(0x411F7C, 1)[0], "0x411f7c must be `jle` (0x7e)")
        # and the truncated byte lands on object 0x2c -> record 0x70
        self.assertEqual(b"\x88\x15", at(0x411F8A, 2))
        self.assertEqual(0x57F23C, struct.unpack("<I", at(0x411F8C, 4))[0])

    def test_the_encoder_agrees_with_that_clamp(self):
        src = self.src
        m = re.search(r"bool encodeSensorAngle\(long deg, std::uint8_t& out\) \{"
                      r"\s*if \(deg < (-?\d+) \|\| deg > (\d+)\)", src)
        self.assertIsNotNone(m, "encodeSensorAngle's bound check has moved")
        self.assertEqual((-127, 127), (int(m.group(1)), int(m.group(2))),
                         "encodeSensorAngle must match the vendor's clamp at "
                         "cfg107 0x411f62-0x411f7e; anything wider emits a byte "
                         "the vendor's UI cannot produce (§1.3)")

    def test_the_sensor_angle_is_stored_raw(self):
        # §7.9: TBM_GETPOS at 0x411b19, raw byte store at 0x411b25. TBM_GETPOS
        # is 0x400. If the vendor clamped, there would be a cmp between these.
        self.assertIn(struct.pack("<I", 0x400), at(0x411B19, 12),
                      "0x411b19 does not mention TBM_GETPOS (0x400)")
        self.assertEqual(0x88, at(0x411B25, 1)[0], "0x411b25 should be a movb")


if __name__ == "__main__":
    unittest.main(verbosity=2)
