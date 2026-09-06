#!/usr/bin/env python3
"""test_keymap.py -- `egg-config map key:` must reach every key cfg107 can.

config-protocol.md §7.32. cfg107 converts a captured Windows VK to a HID usage
in one function. Four ranges are handled inline (a-z, digits, F1-F12, keypad
digits) and EVERYTHING ELSE goes through a jump table: a 220-byte index at
`0x0040a370` selected by `VK - 3`, then `jmpl *0x0040a2b0(,%index,4)`.

This test rebuilds that table FROM THE RAW BYTES of the .exe and asserts that
`egg-config` can produce every usage in it. It is deliberately not a list of
keys copied into a test file -- a hand-copied list is the thing it exists to
catch. When this was first written it failed with 21 missing usages, including
HID 0xE1, which is `Left Shift` -- the very key sitting in the KEYBOARD KEY box
of `windows-run/screenshots/button-mapping-keyboard-key-popup.png`.

Blind spot, stated (§1.2a): this reads ONE jump table reached from ONE
comparison. A VK handled before the table, or a second converter elsewhere,
would not appear here. The four inline ranges are checked separately below
precisely because they never reach the table.
"""
import os
import struct
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG107 = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe")
EGG = os.path.join(ROOT, "build", "egg-config")

IDX_TABLE = 0x0040A370      # movzbl 0x40a370(%ecx), %ecx  at 0x0040a185
JMP_TABLE = 0x0040A2B0      # jmpl   *0x40a2b0(,%ecx,4)    at 0x0040a18c
IDX_COUNT = 0xDC            # cmpl $0xdb at 0x0040a179, ja -> default
VK_BIAS = 3                 # addl $-0x3 at 0x0040a176


def _sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + opt + i * 40
        vs, va, rs, pr = struct.unpack_from("<IIII", d, o + 8)
        out.append((va, vs, rs, pr))
    return base, out


def vendor_usages():
    """Every HID usage the jump table can return, from the bytes."""
    d = open(CFG107, "rb").read()
    base, secs = _sections(d)

    def off(va):
        r = va - base
        for sva, vs, rs, pr in secs:
            # virtual size decides ownership; max(virtual, raw) is a bug
            if sva <= r < sva + max(vs, 1) and r - sva < rs:
                return pr + (r - sva)
        return None

    idx = d[off(IDX_TABLE):off(IDX_TABLE) + IDX_COUNT]
    jo = off(JMP_TABLE)
    targets = [struct.unpack_from("<I", d, jo + 4 * i)[0]
               for i in range(max(idx) + 1)]
    out = {}
    for i, b in enumerate(idx):
        o = off(targets[b])
        # every real arm is `movl $imm32, %eax ; retl`; anything else is the
        # shared default, which means "this VK has no mapping"
        if d[o] == 0xB8 and d[o + 5] == 0xC3:
            out[i + VK_BIAS] = struct.unpack_from("<I", d, o + 1)[0]
    return out


def encode(spec):
    """The usage byte `map middle <spec>` would write, or None."""
    r = subprocess.run([EGG, "map", "middle", spec],
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) == 7 and p[0] == "02":      # `02 <mods> <usage> ...`
            return int(p[2], 16)
    return None


@unittest.skipUnless(os.path.exists(CFG107), "cfg107 not present")
@unittest.skipUnless(os.path.exists(EGG), "egg-config not built")
class OurKeyTableReachesEveryKeyTheVendorCan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vendor = vendor_usages()

    def test_the_table_was_actually_found(self):
        """A parse that silently found nothing would make every other test pass."""
        self.assertGreater(len(self.vendor), 40,
                           "only %d VKs decoded -- the jump table did not parse"
                           % len(self.vendor))

    def test_every_vendor_usage_is_reachable(self):
        want = set(self.vendor.values())
        got = set()
        for spec in self._candidate_names():
            u = encode("key:" + spec)
            if u is not None:
                got.add(u)
        missing = sorted(want - got)
        self.assertEqual([], missing,
                         "cfg107 can map these usages and egg-config cannot: %s"
                         % [hex(m) for m in missing])

    @staticmethod
    def _candidate_names():
        for c in "abcdefghijklmnopqrstuvwxyz0123456789":
            yield c
        for n in range(1, 13):
            yield "f%d" % n
        for n in range(10):
            yield "kp%d" % n
        yield from ("enter", "escape", "esc", "backspace", "tab", "space",
                    "minus", "equal", "leftbracket", "rightbracket", "backslash",
                    "semicolon", "quote", "grave", "comma", "period", "slash",
                    "capslock", "insert", "home", "pageup", "delete", "end",
                    "pagedown", "right", "left", "down", "up",
                    "printscreen", "prtsc", "scrolllock", "pause", "break",
                    "numlock", "kp-slash", "kp-star", "kp-minus", "kp-plus",
                    "kp-dot", "menu", "application",
                    "kb-volume-up", "kb-volume-down",
                    "leftctrl", "leftshift", "leftalt", "leftwin",
                    "rightctrl", "rightshift", "rightalt", "rightwin")

    def test_the_inline_ranges_agree_with_the_vendors_arithmetic(self):
        """The four ranges never reach the jump table, so nothing above checks
        them. cfg107: 0x40a130 VK-0x3d, 0x40a149 VK-0x13, 0x40a156 VK-0x36,
        0x40a163 VK-8, and 0x40a13a / 0x40a16d for the two zeros."""
        for i, c in enumerate("abcdefghijklmnopqrstuvwxyz"):
            self.assertEqual(0x04 + i, encode("key:" + c), c)
        for i in range(1, 10):
            self.assertEqual(0x1E + i - 1, encode("key:%d" % i))
        self.assertEqual(0x27, encode("key:0"))
        for n in range(1, 13):
            self.assertEqual(0x3A + n - 1, encode("key:f%d" % n))
        for n in range(1, 10):
            self.assertEqual(0x59 + n - 1, encode("key:kp%d" % n))
        self.assertEqual(0x62, encode("key:kp0"))

    def test_left_shift_specifically(self):
        """The key in the vendor's own screenshot, and the one that proved the
        table was short."""
        self.assertEqual(0xE1, encode("key:leftshift"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
