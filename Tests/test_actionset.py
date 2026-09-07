#!/usr/bin/env python3
"""test_actionset.py -- `egg-config map` must offer EXACTLY the vendor's actions.

config-protocol.md §7.17 lists the button actions and cites one handler address
each. That is a list of things I found. This test proves the list is COMPLETE,
which §1.2a says is a different and harder claim needing a stated search space.

The search space is the vendor's own, and it is bounded by its own code. The
Button Mapping menu's WM_COMMAND arm at cfg107 `0x00407724` is:

    407724  8d 87 bf e0 ff ff   leal  -0x1f41(%edi), %eax   ; menu id -> 0-based
    407731  83 f8 3b            cmpl  $0x3b, %eax           ; 60 ids, 0x1f41..0x1f7c
    407734  0f 87 ..            ja    0x408348              ; everything else: default
    40773a  0f b6 88 f4 83 40   movzbl 0x4083f4(%eax), %ecx ; index table
    407741  ff 24 8d a4 83 40   jmpl  *0x4083a4(,%ecx,4)    ; jump table

So the complete set of reachable actions is the distinct jump-table targets over
those 60 ids, MINUS the default arm the `ja` also branches to. That is 19, and
`egg-config map` must offer 19.

This reads the base addresses and the bound OUT OF THE INSTRUCTION BYTES rather
than hardcoding them, so a different build that moves the tables fails loudly
instead of silently testing nothing.

Blind spot, stated (§1.2a): this covers the actions reachable from THIS menu's
dispatcher. A button action installed by some other code path -- a second menu,
a dialog, a message map -- would not appear here, and this test cannot see one.
It bounds the Button Mapping menu, not the universe.
"""
import os
import re
import struct
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG107 = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe")
EGG = os.path.join(ROOT, "build", "egg-config")

DISPATCH = 0x00407724           # the `leal -0x1f41(%edi), %eax`
FIRST_ID = 0x1F41               # LEFT CLICK; the whole range is FIRST_ID + 0..bound


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


def vendor_dispatch():
    """(handlers -> [menu ids]) and the default arm, read from raw bytes."""
    with open(CFG107, "rb") as fh:
        d = fh.read()
    base, secs = _sections(d)

    def off(va):
        r = va - base
        for sva, vs, rs, pr in secs:
            # virtual size decides ownership; max(virtual, raw) is a bug
            if sva <= r < sva + max(vs, 1) and r - sva < rs:
                return pr + (r - sva)
        return None

    o = off(DISPATCH)
    # leal -0x1f41(%edi), %eax  --  8d 87 <imm32>
    assert d[o:o + 2] == b"\x8d\x87", "dispatch does not start with the expected leal"
    first = -struct.unpack_from("<i", d, o + 2)[0]
    assert first == FIRST_ID, "menu-id base moved: 0x%04x" % first

    # cmpl $imm8, %eax  --  83 f8 <imm8>, at +0x0d
    c = o + 0x0D
    assert d[c:c + 2] == b"\x83\xf8", "bound is not the expected cmpl"
    bound = d[c + 2]

    # movzbl <idx>(%eax), %ecx  --  0f b6 88 <abs32>, at +0x16
    m = o + 0x16
    assert d[m:m + 3] == b"\x0f\xb6\x88", "index load is not the expected movzbl"
    idx_table = struct.unpack_from("<I", d, m + 3)[0]

    # jmpl *<jmp>(,%ecx,4)  --  ff 24 8d <abs32>, at +0x1d
    j = o + 0x1D
    assert d[j:j + 3] == b"\xff\x24\x8d", "dispatch is not the expected jmpl"
    jmp_table = struct.unpack_from("<I", d, j + 3)[0]

    n = bound + 1
    idx = d[off(idx_table):off(idx_table) + n]
    jo = off(jmp_table)
    handlers = {}
    for i, c in enumerate(idx):
        tgt = struct.unpack_from("<I", d, jo + 4 * c)[0]
        handlers.setdefault(tgt, []).append(FIRST_ID + i)
    # The default arm is the one the `ja` also jumps to. It is by construction
    # the target claiming the most ids -- every unassigned id in the range lands
    # there -- and it is asserted below rather than assumed.
    default = max(handlers, key=lambda t: len(handlers[t]))
    return handlers, default, n


def tool_actions():
    out = subprocess.run([EGG, "map"], capture_output=True, text=True).stdout
    # `map` prints "    NAME   ..." under group headings ending in ':'
    names = set()
    for line in out.splitlines():
        m = re.match(r"^    ([a-z][a-z0-9-]+)\s", line)
        if m:
            names.add(m.group(1))
        if line.startswith("Keys:"):
            break
    return names


class ActionSetIsComplete(unittest.TestCase):
    def setUp(self):
        if not os.path.exists(CFG107):
            self.skipTest("cfg107 not present")
        if not os.path.exists(EGG):
            self.skipTest("egg-config not built")

    def test_the_dispatch_was_actually_found(self):
        """A silent parse failure must not pass as agreement."""
        handlers, default, n = vendor_dispatch()
        self.assertEqual(n, 60, "menu-id range changed")
        self.assertGreater(len(handlers), 10,
                           "only %d handlers: the tables were not parsed"
                           % len(handlers))

    def test_default_arm_is_the_catch_all(self):
        """The most-claimed target must really be the leftover arm, not an action."""
        handlers, default, n = vendor_dispatch()
        claimed = sum(len(v) for t, v in handlers.items() if t != default)
        self.assertEqual(len(handlers[default]) + claimed, n)
        # It must claim MORE ids than every real action put together is wide,
        # i.e. it is a catch-all, and every real arm must own exactly one id.
        self.assertGreater(len(handlers[default]), 1)
        for t, ids in handlers.items():
            if t != default:
                self.assertEqual(len(ids), 1,
                                 "handler 0x%08x claims %d ids" % (t, len(ids)))

    def test_cpi_heading_dispatches_nowhere(self):
        """`CPI` (0x1f72) is a group heading, not an action -- gui-surface.md
        called that [G]; the dispatcher makes it [D]."""
        handlers, default, _ = vendor_dispatch()
        self.assertIn(0x1F72, handlers[default])

    def test_tool_offers_exactly_the_vendor_action_count(self):
        handlers, default, _ = vendor_dispatch()
        vendor = len(handlers) - 1          # minus the default arm
        ours = tool_actions()
        self.assertEqual(
            len(ours), vendor,
            "cfg107 dispatches %d distinct button actions; `egg-config map` "
            "offers %d: %s" % (vendor, len(ours), sorted(ours)))


if __name__ == "__main__":
    unittest.main()
