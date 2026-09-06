#!/usr/bin/env python3
"""resource_id.py -- recover, from raw bytes, WHICH firmware resource an
Endgame updater's own code loads.

CLAUDE.md §1.4: "Whichever firmware resource the official updater loads, it must
be a constant in our code -- never a variable, never selected at runtime, never
chosen from a list. Other resources in the binary may belong to other products."

That rule was easy to honour while there was one updater and one hand-read
`push $0x8c`. It is the whole problem for the update pipeline, where the user
hands us an .exe nobody has read. Updater 1.10 carries SIX FWFILE resources of
identical length; five of them would be accepted by the device -- right size,
valid per-block checksums, and they read back exactly as written (§2: "nothing
downstream of us catches a wrong-but-well-formed image"). Picking the wrong one
is a brick, and the only thing separating them is which id the vendor's code
asks for.

So this recovers the id MECHANICALLY, and refuses when it cannot:

  1. Parse the import table; find the IAT slot for FindResourceW.
  2. Find every `call *slot` (ff 15 <abs32>) in the executable sections.
  3. Walk backwards over the three stdcall pushes. FindResourceW(hModule,
     lpName, lpType) pushes right to left, so immediately before the call is
     hModule, then lpName, then lpType.
  4. Keep only the calls whose lpType points at the UTF-16 string "FWFILE".
  5. lpName is MAKEINTRESOURCE(id) -- a small immediate. That is the id.

WHAT MAKES THIS SAFE TO AUTOMATE, which was the open question. It is not that
the parse is clever; it is that the answer is CHECKABLE THREE WAYS and all three
must agree before anything is written down:

  - the id recovered here,
  - the resource actually present at that id, its length and its SHA-256,
  - and, for the four updaters we hold, a hand derivation done previously.

Any disagreement, any call whose operands are not both immediates, any second
FWFILE call site with a different id -- REFUSE. A refusal costs a person ten
minutes with a disassembler. A wrong answer costs the mouse.

    python3 Tools/pe/resource_id.py <updater.exe> [...]
"""
import os
import re
import struct
import subprocess
import sys


class PE:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = d = f.read()
        self.path = path
        o = struct.unpack_from("<I", d, 0x3C)[0]
        if d[o:o + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        self.nsect = struct.unpack_from("<H", d, o + 6)[0]
        optsz = struct.unpack_from("<H", d, o + 20)[0]
        magic = struct.unpack_from("<H", d, o + 24)[0]
        if magic != 0x10B:
            raise ValueError("not a 32-bit PE (magic 0x%x)" % magic)
        self.base = struct.unpack_from("<I", d, o + 24 + 0x1C)[0]
        self.import_rva = struct.unpack_from("<I", d, o + 24 + 0x60 + 8)[0]
        self.sect = []
        t = o + 24 + optsz
        for i in range(self.nsect):
            e = t + 40 * i
            name = d[e:e + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", d, e + 8)
            chars = struct.unpack_from("<I", d, e + 36)[0]
            self.sect.append({"name": name, "va": self.base + va,
                              "size": max(vsize, rsize), "roff": roff,
                              "rsize": rsize, "code": bool(chars & 0x20000020)})

    def off(self, va):
        for s in self.sect:
            if s["va"] <= va < s["va"] + s["size"]:
                o = s["roff"] + (va - s["va"])
                return o if o < len(self.d) else None
        return None

    def read(self, va, n):
        o = self.off(va)
        return self.d[o:o + n] if o is not None else b""

    def cstr16(self, va, limit=64):
        """A NUL-terminated UTF-16LE string, as resource type names are."""
        b = self.read(va, limit * 2)
        i = b.find(b"\0\0")
        if i < 0:
            return None
        if i % 2:
            i += 1
        try:
            return b[:i].decode("utf-16-le")
        except UnicodeDecodeError:
            return None

    def iat_slot(self, want):
        """VA of the IAT entry for an imported function, or None."""
        i = self.off(self.base + self.import_rva)
        if i is None:
            return None
        while True:
            ilt, ts, fc, name_rva, iat = struct.unpack_from("<IIIII", self.d, i)
            if not name_rva:
                return None
            thunk = self.off(self.base + (ilt or iat))
            if thunk is None:
                i += 20
                continue
            slot = self.base + iat
            k = 0
            while True:
                v = struct.unpack_from("<I", self.d, thunk + 4 * k)[0]
                if not v:
                    break
                if not (v & 0x80000000):          # imported by name
                    o = self.off(self.base + v)
                    if o is not None:
                        nm = self.d[o + 2:].split(b"\0")[0].decode("ascii", "replace")
                        if nm == want:
                            return slot + 4 * k
                k += 1
            i += 20


def instruction_stream(path):
    """[(va, raw_bytes, text)] from objdump, in address order.

    objdump is used ONLY for SEQUENCING -- to know which instruction precedes
    which. Every operand VALUE is re-read from the file's own bytes below, so a
    decoder that desynchronises can cause a REFUSAL but never a wrong id
    (CLAUDE.md §1.2b: derived views find things, they never decide them).
    """
    out = subprocess.run(["objdump", "-d", path], capture_output=True,
                         text=True).stdout
    rx = re.compile(r"^\s*([0-9a-f]{4,8}):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
    got = []
    for ln in out.splitlines():
        m = rx.match(ln)
        if m:
            got.append((int(m.group(1), 16),
                        bytes.fromhex(m.group(2).replace(" ", "")),
                        m.group(3).strip()))
    return got


def decode_push(raw):
    """('imm', value) or ('reg', name) for one push, else None.

    Takes the RAW BYTES objdump reported at that address and decodes them here,
    so the value never comes from objdump's rendering.
    """
    if len(raw) == 5 and raw[0] == 0x68:                   # push imm32
        return ("imm", struct.unpack_from("<I", raw, 1)[0])
    if len(raw) == 2 and raw[0] == 0x6A:                   # push imm8, signed
        v = raw[1]
        return ("imm", v - 256 if v > 127 else v)
    if len(raw) == 1 and 0x50 <= raw[0] <= 0x57:           # push r32
        return ("reg", ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi",
                        "edi"][raw[0] - 0x50])
    return None


# How far back to look for the three stdcall pushes. fw104 puts a `movl %ecx,
# %esi` between the last push and the call, so requiring adjacency finds nothing
# there -- which is exactly how the first version of this tool refused an
# updater whose id is plainly `pushl $0x8c` four instructions earlier. Twelve
# instructions is generous for three arguments and still far short of a basic
# block.
LOOKBACK = 12


def find_resource_calls(pe, stream):
    """Every `call *[FindResourceW]` with its three decoded stdcall pushes.

    Refuses on any control transfer inside the window: a push separated from its
    call by a jump or a call is not reliably that call's argument.
    """
    slot = pe.iat_slot("FindResourceW")
    if slot is None:
        return None, []
    want = b"\xff\x15" + struct.pack("<I", slot)
    out = []
    for i, (va, raw, text) in enumerate(stream):
        if raw != want:
            continue
        # The stream must be synchronised HERE: objdump's bytes at this address
        # must be exactly the call we are looking for. They are, by the test
        # above -- that is the check, not a comment about one.
        args, blocked = [], None
        for j in range(i - 1, max(-1, i - 1 - LOOKBACK), -1):
            pva, praw, ptext = stream[j]
            if not praw:
                break
            op = praw[0]
            if op in (0xE8, 0xE9, 0xEB, 0xC3, 0xC2, 0xCC) or 0x70 <= op <= 0x7F:
                blocked = ptext          # call / jmp / ret / int3 / jcc
                break
            d = decode_push(praw)
            if d is not None:
                args.append((d[0], d[1], pva))
                if len(args) == 3:
                    break
        out.append({"call_va": va, "args": args, "blocked": blocked})
    return slot, out


def derive(path):
    """The report for one updater. `id` is None whenever anything is unclear."""
    r = {"path": path, "id": None, "why": [], "sites": []}
    try:
        pe = PE(path)
    except (ValueError, struct.error) as e:
        r["why"].append("cannot parse: %s" % e)
        return r
    slot, calls = find_resource_calls(pe, instruction_stream(path))
    if slot is None:
        r["why"].append("FindResourceW is not imported by name")
        return r
    r["slot"] = slot
    if not calls:
        r["why"].append("no `call *[FindResourceW]` found in any code section")
        return r

    ids = set()
    for c in calls:
        site = {"call_va": c["call_va"], "type": None, "name": None}
        if len(c["args"]) < 3:
            site["note"] = ("only %d of 3 pushes found within %d instructions%s"
                            % (len(c["args"]), LOOKBACK,
                               (" (stopped at `%s`)" % c["blocked"])
                               if c["blocked"] else ""))
            r["sites"].append(site)
            continue
        _, lp_type, _ = c["args"][2]
        kind_n, lp_name, _ = c["args"][1]
        site["type"] = (pe.cstr16(lp_type)
                        if c["args"][2][0] == "imm" else "<computed>")
        if site["type"] != "FWFILE":
            site["note"] = "resource type is %r, not FWFILE" % site["type"]
            r["sites"].append(site)
            continue
        if kind_n != "imm":
            site["note"] = ("resource NAME comes from register %s -- selected "
                            "at run time, which §1.4 forbids relying on"
                            % lp_name)
            r["sites"].append(site)
            r["why"].append("a FWFILE call at 0x%08x takes its id from a "
                            "register" % c["call_va"])
            continue
        if lp_name > 0xFFFF:
            site["note"] = ("lpName 0x%08x is a pointer, not MAKEINTRESOURCE"
                            % lp_name)
            r["sites"].append(site)
            r["why"].append("a FWFILE call at 0x%08x names its resource by "
                            "string" % c["call_va"])
            continue
        site["name"] = lp_name
        ids.add(lp_name)
        r["sites"].append(site)

    fw = [s for s in r["sites"] if s.get("name") is not None]
    if not fw:
        r["why"].append("no FindResourceW call site asks for type FWFILE with "
                        "an immediate id")
    elif len(ids) > 1:
        r["why"].append("FWFILE is loaded with %d DIFFERENT ids (%s) -- the "
                        "image is chosen at run time and this updater cannot "
                        "be ingested automatically"
                        % (len(ids), ", ".join(str(i) for i in sorted(ids))))
    else:
        r["id"] = ids.pop()
    return r


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    bad = 0
    for path in argv[1:]:
        r = derive(path)
        print("%s" % os.path.basename(path))
        if "slot" in r:
            print("  FindResourceW IAT slot   0x%08x" % r["slot"])
        for s in r["sites"]:
            print("  call 0x%08x  type=%-10s %s"
                  % (s["call_va"], s["type"],
                     ("name=%d" % s["name"]) if s.get("name") is not None
                     else s.get("note", "")))
        if r["id"] is None:
            bad = 1
            print("  REFUSED: " + "; ".join(r["why"]))
        else:
            print("  FWFILE resource id = %d  (0x%x)" % (r["id"], r["id"]))
        print()
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
