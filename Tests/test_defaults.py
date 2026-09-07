#!/usr/bin/env python3
"""test_defaults.py -- cfg107's host default record vs the device's own.

`config-protocol.md` §7.2b calls `0x413db0` "the host default record, and why it
is NOT the wire record". That is right about what it is and says nothing about
whether it AGREES with the device. This answers that, and the answer turns out
to be a strong independent check on §7.3's entire 111-entry object->record map:

    every mapped byte agrees, except record 0x71.

Two sources that never touched each other -- a Windows binary's compiled-in
defaults, and the bytes an OP1 8k v2 reported after `A1 13` -- landing on the
same 111 values is not something a wrong map could produce.

WHY THE INSTRUCTIONS ARE DECODED HERE rather than transcribed. The first pass at
this was a hand-written table of stores. It stopped at 0x413e4f and MISSED the
whole tail of the button block, which the function keeps writing to 0x413f8a. A
hand transcription that stops early does not look wrong; it looks complete.

So this walks the bytes with a tiny register machine, and -- the property that
makes it trustworthy -- **it fails on any opcode it does not understand.** It
cannot skip a store it failed to decode, because not decoding is an error rather
than a silence.

Its blind spots, stated (§1.2a): it models only the forms this function uses,
follows no branches, and assumes `%eax` holds the object base throughout (true
here -- nothing reloads it). It is a decoder for one function, not an emulator.
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
CAPTURES = os.path.join(ROOT, "windows-run")
NOTES = os.path.join(ROOT, "notes", "config-protocol.md")

FN_START, FN_END = 0x413DB0, 0x413F8A
PAYLOAD = 16

DIS = os.path.join(ROOT, "Tools", "ghidra-export", "dis.sh")

# Store forms this function uses, as objdump prints them. `%eax` holds the
# object base throughout (nothing in the function reloads it).
# objdump separates mnemonic and operands with TABs and may append a
# `# imm = 0x..` comment. The first version anchored on spaces and on
# end-of-line, matched 21 of 130 stores, and the coverage assertion below is
# what caught it -- a decoder that quietly parses a sixth of a function would
# otherwise have "agreed with the device" on a handful of bytes and proved
# nothing.
STORE = re.compile(
    r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*"
    r"mov([bwl])\s+(\$?\S+?|%[a-z]{2,3}),\s*"
    r"(?:(-?0x[0-9a-f]+)|)\(%eax\)\s*(?:#.*)?$")
SETREG = re.compile(
    r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*"
    r"(mov[bwl]|xor[bwl])\s+(\S+?),\s*(%[a-z]{2,3})\s*(?:#.*)?$")

WIDTH = {"b": 1, "w": 2, "l": 4}
# Sub-register names mapped to the 32-bit register they live in. The 16-bit
# row was missing at first, so every `movw %dx, ...` -- which is how the four
# CPI defaults are written -- was reported as an untracked store.
LOW = {"%al": "%eax", "%cl": "%ecx", "%dl": "%edx", "%bl": "%ebx",
       "%ah": "%eax", "%ch": "%ecx", "%dh": "%edx", "%bh": "%ebx",
       "%ax": "%eax", "%cx": "%ecx", "%dx": "%edx", "%bx": "%ebx",
       "%si": "%esi", "%di": "%edi", "%bp": "%ebp", "%sp": "%esp"}
HIGH8 = {"%ah", "%ch", "%dh", "%bh"}


def decode():
    """Walk the function as objdump renders it.

    objdump, not a hand-written length table. The first version of this carried
    its own instruction lengths, got `8b` wrong (it is 2 bytes or 3 depending on
    the modrm) and desynchronised into 87 undecoded bytes. Instruction lengths
    are a solved problem and re-solving them badly is how a decoder invents
    stores that are not there.

    §1.2b says a disassembler's view is evidence and not ground truth, and that
    is respected two ways: a desynchronised sweep would produce nonsense rather
    than a false agreement with the device, and the values that matter most are
    additionally asserted against RAW BYTES in Tests/test_cpi.py.
    """
    out = subprocess.run(["bash", DIS, "cfg107", hex(FN_START), hex(FN_END + 1)],
                         capture_output=True, text=True, cwd=ROOT).stdout
    reg = {}
    stores, unknown = {}, []
    body = False
    for line in out.splitlines():
        if not re.match(r"^\s*[0-9a-f]+:\s", line):
            continue
        body = True
        m = STORE.match(line)
        if m:
            _at, w, src, disp = m.group(1), m.group(2), m.group(3), m.group(4)
            off = int(disp, 16) if disp else 0
            width = WIDTH[w]
            if src.startswith("$"):
                raw = src[1:].rstrip(",")
                val = int(raw, 16) if raw.startswith(("0x", "-0x")) else int(raw)
                val &= (1 << (8 * width)) - 1
            else:
                key = LOW.get(src, src)
                if key not in reg:
                    unknown.append("%s: store of untracked %s" % (line.strip(), src))
                    continue
                val = reg[key]
                if src in HIGH8:
                    val >>= 8
                val &= (1 << (8 * width)) - 1
            for k in range(width):
                stores[off + k] = (val >> (8 * k)) & 0xFF
            continue
        m = SETREG.match(line)
        if m:
            op, src, dst = m.groups()
            key = LOW.get(dst, dst)
            if op.startswith("xor") and src == dst:
                reg[key] = 0
            elif op.startswith("mov") and src.startswith("$"):
                raw = src[1:].rstrip(",")
                v = int(raw, 16) if raw.startswith(("0x", "-0x")) else int(raw)
                if dst in HIGH8:
                    reg[key] = (reg.get(key, 0) & ~0xFF00) | ((v & 0xFF) << 8)
                elif dst in LOW and op == "movb":
                    reg[key] = (reg.get(key, 0) & ~0xFF) | (v & 0xFF)
                elif dst in LOW and op == "movw":
                    reg[key] = (reg.get(key, 0) & ~0xFFFF) | (v & 0xFFFF)
                else:
                    reg[key] = v & 0xFFFFFFFF
            elif op.startswith("mov") and src in reg:
                reg[key] = reg[src]
            else:
                reg.pop(key, None)          # clobbered by something untracked
            continue
        # Any other instruction that writes a register we track must invalidate
        # it, or a stale value could be stored later. Cheap and conservative:
        for r in list(reg):
            if line.rstrip().endswith(r) or (r in LOW.values()
                                             and line.rstrip().endswith(
                                                 [k for k, v in LOW.items()
                                                  if v == r][0])):
                reg.pop(r, None)
    if not body:
        unknown.append("objdump produced no instructions")
    return stores, unknown


def obj_to_record():
    """§7.3's map, read out of the notes rather than restated here."""
    import re
    rows = re.findall(
        r"^\| `0x([0-9a-f]{2})` \| `0x([0-9a-f]{3})` \| `0x([0-9a-f]{2})` \|",
        open(NOTES).read(), re.M)
    return {int(o, 16): int(r, 16) for r, _w, o in rows}


def factory_record():
    import usbpcap
    tr = usbpcap.control_transfers(
        usbpcap.read(os.path.join(CAPTURES, "07-factory-reset.pcapng")))
    asked = False
    for t in tr:
        d = bytes(t.data or b"")
        if t.is_set_report:
            asked = d[:2] == b"\xa1\x12"
        elif t.is_get_report:
            if asked and len(d) >= 1024:
                return d
            asked = False
    return None


@unittest.skipUnless(os.path.exists(CFG107), "cfg107 not present")
class TheDecoderIsHonestAboutWhatItRead(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stores, cls.unknown = decode()

    def test_nothing_was_silently_skipped(self):
        self.assertEqual([], self.unknown[:20],
                         "%d undecoded items" % len(self.unknown))

    def test_it_reached_the_end_of_the_button_block(self):
        """The hand transcription this replaces stopped at 0x413e4f and missed
        everything past object +0x3c. The block runs to +0x74."""
        self.assertIn(0x64, self.stores)
        self.assertIn(0x6C, self.stores)
        self.assertGreaterEqual(len(self.stores), 95)


@unittest.skipUnless(os.path.exists(CFG107), "cfg107 not present")
@unittest.skipUnless(os.path.isdir(CAPTURES), "captures not present")
class TheHostDefaultsAgreeWithTheDevice(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stores, _ = decode()
        cls.map = obj_to_record()
        cls.dev = factory_record()

    def test_the_pieces_are_all_here(self):
        self.assertEqual(111, len(self.map), "§7.3's map is not 111 rows")
        self.assertIsNotNone(self.dev, "07-factory-reset has no settings record")

    def test_exactly_one_mapped_byte_disagrees(self):
        diff = {}
        for obj, val in sorted(self.stores.items()):
            rec = self.map.get(obj)
            if rec is None:
                continue
            got = self.dev[PAYLOAD + rec]
            if got != val:
                diff[rec] = (val, got)
        self.assertEqual({0x71: (0x00, 0x01)}, diff,
                         "host vs device: %s"
                         % {hex(k): (hex(a), hex(b)) for k, (a, b) in diff.items()})

    def test_that_one_byte_is_force_max_fps(self):
        """Record 0x71 is the ONE factory default firmware 1.10 changed, which
        working-memory records from a different route entirely."""
        self.assertEqual(0x00, self.stores[0x2D])
        self.assertEqual(0x01, self.dev[PAYLOAD + 0x71])

    def test_the_agreement_is_broad_enough_to_mean_something(self):
        covered = [o for o in self.stores if o in self.map]
        # 91 of §7.3's 111 mapped bytes. The other 20 this function never
        # writes -- they are left at the caller's zero fill, so there is nothing
        # here to compare them against. Pinned at 91 rather than "some" so that
        # a decoder regression shows up as a failure and not as a quieter pass.
        self.assertEqual(91, len(covered),
                         "only %d mapped bytes compared" % len(covered))


@unittest.skipUnless(os.path.isdir(CAPTURES), "captures not present")
class OnlyNamedBytesEverMove(unittest.TestCase):
    """§7.24. The completeness claim, as a gate rather than a paragraph.

    If a new capture moves a byte nothing names, that is a new field and this
    test is how the project finds out -- rather than the claim quietly going
    stale in a notes file.
    """

    @classmethod
    def setUpClass(cls):
        import collections
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from test_handedness import records as recs, every_capture
        cls.vals = collections.defaultdict(set)
        cls.n = 0
        cls.per_dir = collections.Counter()
        # EVERY capture in the repo, not just windows-run. This gate's own
        # docstring calls itself the standing check on new captures, and until
        # 2026-09-07 it could not see windows-capture/ at all -- so dropping a
        # new capture into that directory would not have failed ctest, which is
        # the one thing the gate exists to do.
        for directory, name in every_capture():
            for f in recs(name, directory):
                cls.n += 1
                cls.per_dir[os.path.basename(directory)] += 1
                for r in range(0x73):
                    cls.vals[r].add(f[PAYLOAD + r])

    NAMED = {0x05, 0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x70, 0x71, 0x72}

    def test_the_corpus_is_the_whole_corpus(self):
        """Both capture runs must contribute, and the corpus must not shrink.

        This used to be `assertEqual(82, ...)`, a number that described
        windows-run alone. A floor plus a per-directory check is the right shape
        for a gate whose whole purpose is that ADDING a capture is noticed: a
        new file raises the total and must not break the test, while a lost
        capture or a directory that stopped being read still fails.
        """
        for d in ("windows-run", "windows-capture"):
            self.assertGreater(
                self.per_dir[d], 0,
                "%s contributed no settings records. Either the directory is "
                "gone or this gate has stopped reading it -- both make the "
                "checks below vacuous for that run. Got: %s"
                % (d, dict(self.per_dir)))
        self.assertGreaterEqual(
            self.n, 108,
            "the corpus was 108 settings records on 2026-09-07 (82 in "
            "windows-run, 26 in windows-capture) and is now %d. Regenerate "
            "with `python3 Tools/capture/whatmoved.py | head -1`, which counts "
            "the same way." % self.n)

    def test_every_byte_that_moved_is_named_cpi_or_button(self):
        stray = []
        for r, vs in sorted(self.vals.items()):
            if len(vs) < 2:
                continue
            if r in self.NAMED or 0x23 <= r <= 0x36 or 0x37 <= r <= 0x6E:
                continue
            if r == 0x01:            # §7.4, read 0x80 vs write 0x00
                self.assertEqual({0x00, 0x80}, vs)
                continue
            stray.append("0x%02x: %s" % (r, sorted(hex(v) for v in vs)))
        self.assertEqual([], stray,
                         "unnamed bytes moved -- these are new fields: %s"
                         % "; ".join(stray))

    def test_the_led_run_is_constant_and_has_the_shape_7_24a_describes(self):
        want = [(0xFF, 0xFF, 0x00), (0x00, 0x00, 0xFF),
                (0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00)]
        for k in range(4):
            o = 0x0F + 5 * k
            got = []
            for i in range(5):
                self.assertEqual(1, len(self.vals[o + i]),
                                 "record 0x%02x moved" % (o + i))
                got.append(next(iter(self.vals[o + i])))
            self.assertEqual(list(want[k]), got[:3], "entry %d triple" % k)
            self.assertEqual(0x01, got[3], "entry %d separator" % k)
            self.assertEqual(k + 1, got[4], "entry %d index" % k)


if __name__ == "__main__":
    unittest.main(verbosity=2)
