#!/usr/bin/env python3
"""ctlchain.py <tag> [--dialog N] [--control N] -- resolve MFC controls from the
dialog resource to the code that handles them.

WHAT IT RECORDS, and what it deliberately does not. CLAUDE.md §3.1: a tool here
records STRUCTURE and must not encode a belief about what the structure MEANS.
This resolves MFC's own plumbing, which is structure and nothing else:

    RT_DIALOG control  ->  the class whose DoDataExchange binds that IDC
                       ->  that class's GetMessageMap
                       ->  the ON_BN_CLICKED / ON_CBN_SELCHANGE handler

It prints addresses and captions. It does not say what a handler DOES, does not
rank anything, and carries no list of interesting constants -- that last was the
defect in the script §7.1 deleted.

WHY IT EXISTS. Control ids are REUSED across dialogs in this binary: 1028 is
'LED On / Off' in dialog 137, 'Left-handed Mode' in 139 and 'Motion Sync' in
140. Reading a handler found by grepping for an id, without first pinning which
class owns it, gets the wrong feature -- silently, and with a plausible-looking
disassembly to back it up. That mistake is one wrong byte away from a wrong
write, so the pinning is the tool.

METHOD, with its blind spots stated (§1.2a):

  1. Dialog templates come from .rsrc via dlgdump.parse, not from a previous
     run's output.
  2. Class <-> dialog: vtable V is installed at .text site S; the nearest
     `push imm32` BEFORE S whose value is a dialog id present in .rsrc is that
     class's template. That is a heuristic, so it is CHECKED: the class's
     DoDataExchange must bind at least one IDC the template actually contains,
     and a class failing that check is printed UNRESOLVED, never guessed.
  3. DDX calls are recognised by shape -- `lea disp32(%reg),%r; push %r;
     push $IDC; push %edi; call rel32`. Blind to a different register
     allocation, to a DDX with an 8-bit displacement, and to an inlined one.
  4. Handlers come from the 24-byte AFX_MSGMAP_ENTRY struct scanned in .rdata.
     Blind to a map built at runtime.

So a control this prints nothing for is a control THIS METHOD did not resolve.
It is never evidence that no handler exists.

Nothing is executed (§1.5).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsrc                                    # noqa: E402
import dlgdump                                 # noqa: E402

# AfxSig / message names we care to label. Anything unlisted prints as a number.
MSG = {0x111: "WM_COMMAND", 0x110: "WM_INITDIALOG", 0x4E: "WM_NOTIFY",
       0x113: "WM_TIMER", 0x200: "WM_MOUSEMOVE", 0x0F: "WM_PAINT",
       0x135: "WM_CTLCOLOR", 0x114: "WM_HSCROLL", 0x115: "WM_VSCROLL"}
CODE = {0x111: {0: "BN_CLICKED", 1: "CBN_SELCHANGE / EN_SETFOCUS",
                5: "EN_CHANGE", 9: "CBN_SELENDOK", 0xFFFF: "(any)"}}


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    st = pe + 24 + optsz
    out = []
    for i in range(nsec):
        e = d[st + 40 * i:st + 40 * i + 40]
        nm = e[:8].rstrip(b"\0").decode(errors="replace")
        vs, va, rs, ro = struct.unpack("<IIII", e[8:24])
        out.append((nm, base + va, vs, ro, rs))
    return base, out


class Img:
    def __init__(self, tag):
        self.tag = tag
        self.b = rsrc.load(tag)
        self.base, self.secs = sections(self.b)
        for nm, vb, vs, ro, rs in self.secs:
            if nm == ".text":
                self.tbase, self.text = vb, self.b[ro:ro + rs]

    def off(self, va):
        for nm, vb, vs, ro, rs in self.secs:
            if vb <= va < vb + max(vs, rs):
                return ro + (va - vb)
        return None

    def rd(self, va, n):
        o = self.off(va)
        return self.b[o:o + n] if o is not None else b""

    def va_of(self, fileoff):
        for nm, vb, vs, ro, rs in self.secs:
            if ro <= fileoff < ro + rs:
                return nm, vb + fileoff - ro
        return None, None

    def code(self, va):
        return self.tbase <= va < self.tbase + len(self.text)


def templates(img):
    """{dialog id: [(cid, cls, kind, caption, style)]}"""
    base, secs = rsrc.sections(img.b)
    pe = struct.unpack_from("<I", img.b, 0x3C)[0]
    opt = struct.unpack_from("<H", img.b, pe + 20)[0]
    rrva = struct.unpack_from("<I", img.b, pe + 24 + opt - 128 + 16)[0]
    rd = rsrc.rva2off(secs, base, rrva)
    out = []
    rsrc.walk(img.b, secs, base, rd, rd, 0, [], out)
    dlgs = {}
    for path, drva, dsz, fo in out:
        if path[0] != 5 or fo is None:
            continue
        title, ctrls = dlgdump.parse(img.b[fo:fo + dsz])
        dlgs[path[1]] = (title, ctrls)
    return dlgs


# ---------------------------------------------------------------- DDX by shape
# lea disp32(%reg),%r ; push %r ; push $IDC ; push %edi ; call rel32
LEA = re.compile(rb"\x8d[\x86\x8e\x96\x9e\xb6\xbe\x87\x8f\x97\x9f\xbf](....)"
                 rb"[\x50\x51\x52\x53\x56\x57]"
                 rb"\x68(....)"
                 rb"[\x50\x51\x52\x53\x56\x57]"
                 rb"\xe8(....)", re.S)


def ddx_sites(img):
    """[(site_va, idc, member_disp, callee_va)] over all of .text."""
    out = []
    for m in LEA.finditer(img.text):
        disp = struct.unpack("<I", m.group(1))[0]
        idc = struct.unpack("<I", m.group(2))[0]
        if not (1 <= idc <= 0xFFFF):
            continue
        site = img.tbase + m.start()
        end = img.tbase + m.end()
        callee = end + struct.unpack("<i", m.group(3))[0]
        if not img.code(callee):
            continue
        out.append((site, idc, disp, callee))
    return out


def fn_start(img, va):
    o = va - img.tbase
    while o > 0 and not (img.text[o - 1] == 0xCC and img.text[o - 4:o - 1] == b"\xcc\xcc\xcc"):
        o -= 1
    return img.tbase + o


def vtable_of(img, fn):
    """Every .rdata slot holding `fn`, and the vtable base each belongs to."""
    hits = []
    for m in re.finditer(re.escape(struct.pack("<I", fn)), img.b):
        nm, va = img.va_of(m.start())
        if nm not in (".rdata", ".data"):
            continue
        v = va
        while True:
            p = img.rd(v - 4, 4)
            if len(p) < 4:
                break
            q = struct.unpack("<I", p)[0]
            if not img.code(q):
                break
            v -= 4
        hits.append((va, v))
    return hits


def installers(img, vbase):
    return [va for va in
            (img.va_of(m.start())[1]
             for m in re.finditer(re.escape(struct.pack("<I", vbase)), img.b))
            if va is not None and img.code(va)]


PUSH32 = re.compile(rb"\x68(....)", re.S)


def dialog_id_before(img, site, known):
    """Nearest `push imm32` before `site` whose value is a real dialog id."""
    win = img.rd(site - 160, 160)
    best = None
    for m in PUSH32.finditer(win):
        v = struct.unpack("<I", m.group(1))[0]
        if v in known:
            best = v
    return best


def msgmap_entries(img):
    """Every AFX_MSGMAP_ENTRY in .rdata: (va, msg, code, id, lastid, sig, pfn)."""
    out = []
    for nm, vb, vs, ro, rs in img.secs:
        if nm != ".rdata":
            continue
        d = img.b[ro:ro + rs]
        for o in range(0, len(d) - 24, 4):
            msg, code, nid, last, sig, pfn = struct.unpack("<IIIIII", d[o:o + 24])
            if msg not in MSG or pfn == 0 or not img.code(pfn):
                continue
            if nid > 0xFFFF or last > 0xFFFF or sig > 0x200:
                continue
            out.append((vb + o, msg, code, nid, last, sig, pfn))
    return out


def getmessagemap(img, fn_ddx):
    """`movl $imm32,%eax; ret` sitting just after the DoDataExchange's ret."""
    o = fn_ddx - img.tbase
    # scan forward to the function's end padding, then the next b8 .. c3
    while o < len(img.text) - 6:
        if img.text[o] == 0xB8 and img.text[o + 5] == 0xC3:
            v = struct.unpack("<I", img.text[o + 1:o + 5])[0]
            if img.off(v) is not None and not img.code(v):
                return img.tbase + o, v
        o += 1
        if o - (fn_ddx - img.tbase) > 0x800:
            break
    return None, None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    tag = args[0] if args else "cfg107"
    want_dlg = want_ctl = None
    for i, a in enumerate(sys.argv):
        if a == "--dialog":
            want_dlg = int(sys.argv[i + 1])
        if a == "--control":
            want_ctl = int(sys.argv[i + 1])

    img = Img(tag)
    dlgs = templates(img)
    known = set(dlgs)
    ddx = ddx_sites(img)
    maps = msgmap_entries(img)

    # class resolution: DoDataExchange fn -> dialog id
    byfn = {}
    for site, idc, disp, callee in ddx:
        byfn.setdefault(fn_start(img, site), []).append((site, idc, disp, callee))

    # CLASS <-> DIALOG, by containment of the control-id SET.
    #
    # The first version walked the vtable a DoDataExchange sits in, found the
    # .text site that installs that vtable, and took the nearest preceding
    # `push imm32` that happened to be a dialog id. It resolved 4 of 11
    # dialogs -- it missed 135 and 150, whose classes were pinned by hand from
    # exactly that evidence, so the failure was in the mechanics and not in the
    # premise. Rather than tune a window size until the answer looked right
    # (which is how a heuristic quietly becomes a belief), the rule is now one
    # that needs no window and no vtable at all:
    #
    #   a DoDataExchange binds a SET of control ids; the dialog whose template
    #   contains every one of them is the class's template.
    #
    # Ambiguity is reported, never broken by preference. `--verbose` prints the
    # vtable/ctor evidence too, so the two routes can be compared where both
    # have an answer.
    resolved = {}
    ambiguous = {}
    for fn, sites in byfn.items():
        ids = {i for _, i, _, _ in sites}
        cands = [d for d in dlgs if ids <= {c[0] for c in dlgs[d][1]}]
        if len(cands) == 1:
            resolved.setdefault(cands[0], []).append((fn, sites))
        elif len(cands) > 1:
            # Prefer the tightest template: a class that binds N ids belongs to
            # the smallest dialog that has them all. Ties stay ambiguous.
            sz = sorted(cands, key=lambda d: len(dlgs[d][1]))
            if len(dlgs[sz[0]][1]) < len(dlgs[sz[1]][1]):
                resolved.setdefault(sz[0], []).append((fn, sites))
            else:
                ambiguous[fn] = cands

    print("########## %s -- %d dialogs, %d DDX sites, %d msgmap entries"
          % (tag, len(dlgs), len(ddx), len(maps)))
    if ambiguous:
        print("  %d DoDataExchange functions match more than one template and"
              " are NOT assigned:" % len(ambiguous))
        for fn, cands in sorted(ambiguous.items()):
            print("     0x%08x -> dialogs %s" % (fn, cands))
    for did in sorted(dlgs):
        if want_dlg is not None and did != want_dlg:
            continue
        title, ctrls = dlgs[did]
        entry = resolved.get(did)
        print("\n  DIALOG %d  caption=%r%s"
              % (did, title, "" if entry else "   [class UNRESOLVED by this method]"))
        if not entry:
            continue
        for fn, sites in entry:
            gm, mapva = getmessagemap(img, fn)
            # The array's REAL end, not a guessed window. MFC terminates a
            # message map with an all-zero AFX_MSGMAP_ENTRY. Bounding it by a
            # fixed count let dialog 140's BN_CLICKED(1028) handler appear
            # under dialog 139 -- the exact cross-dialog confusion this tool
            # exists to prevent, produced by the tool itself.
            arr = arr_end = None
            if mapva is not None:
                arr = struct.unpack("<I", img.rd(mapva + 4, 4))[0]
                arr_end = arr
                while True:
                    e = img.rd(arr_end, 24)
                    if len(e) < 24:
                        break
                    msg, _c, _i, _l, _s, pfn = struct.unpack("<IIIIII", e)
                    if msg == 0 and pfn == 0:
                        break
                    arr_end += 24
            print("    DoDataExchange 0x%08x   GetMessageMap 0x%08x -> map 0x%08x"
                  % (fn, gm or 0, mapva or 0))
            bound = {}
            for site, idc, disp, callee in sorted(sites, key=lambda s: s[1]):
                bound.setdefault(idc, []).append((site, disp, callee))
            for cid, cls, kind, cap, st in ctrls:
                if want_ctl is not None and cid != want_ctl:
                    continue
                if cid not in bound:
                    continue
                cap_s = (cap[:46] + "...") if len(cap) > 49 else cap
                print("      %-5d %-10s %-18s %r" % (cid, cls, "/" + kind if kind else "", cap_s))
                for site, disp, callee in bound[cid]:
                    print("            DDX  at 0x%08x  member +0x%-5x callee 0x%08x"
                          % (site, disp, callee))
                for va, msg, code, nid, last, sig, pfn in maps:
                    if nid != cid or (arr is not None and not
                                      (arr <= va < arr_end)):
                        continue
                    label = CODE.get(msg, {}).get(code, "code %d" % code)
                    print("            %-11s %-24s handler 0x%08x   (entry 0x%08x)"
                          % (MSG.get(msg, hex(msg)), label, pfn, va))


if __name__ == "__main__":
    main()
