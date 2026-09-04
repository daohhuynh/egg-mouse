#!/usr/bin/env python3
"""dlgdump.py <tag> [<tag>...] — every RT_DIALOG control and caption, verbatim.

CLAUDE.md 1.2a: "Check the user-visible surface before concluding a capability
is missing: strings in both encodings, .rsrc dialogs and their control captions,
menus, message maps. A feature the vendor ships has a button somewhere."

Several negatives in these notes are of exactly that shape -- there is no eighth
updater command, no network capability, no fifth config command. Each was argued
from code. This prints the other half of the evidence: what the program offers
its user. A control with no explanation in the notes is a lead; a notes section
with no control behind it is a claim to re-check.

It parses DIALOG and DIALOGEX templates directly rather than through any
decompiler view, and it prints ordinal class names as their Win32 names
(#0x80 = BUTTON, and so on) because a caption alone does not say whether the
thing is a button, a label or a progress bar.

STRING TABLE. RT_STRING resources are blocks of 16 length-prefixed UTF-16
strings, id = (block-1)*16 + index. Printed too, because MFC puts command
prompts and error text there rather than in the dialog.

DLGINIT (resource type 240). A dialog template says a control is a COMBOBOX; it
does not say what is IN it. The list items live in a separate RT_DLGINIT
resource sharing the dialog's id, as records
  WORD id; WORD msg; DWORD len; BYTE data[len]
terminated by id == 0, where msg 0x0403 is CB_ADDSTRING and 0x0143 is
LB_ADDSTRING. These are the vendor's own enumerations of its own settings --
the visible half of whatever the protocol encodes as an integer -- so leaving
them undecoded would leave the user-visible surface only half read.

This records what is in the resource. It attaches no meaning to any of it.
"""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsrc

ORD = {0x80: "BUTTON", 0x81: "EDIT", 0x82: "STATIC", 0x83: "LISTBOX",
       0x84: "SCROLLBAR", 0x85: "COMBOBOX"}

# The button-style low nibble, which is what separates a push button from a
# checkbox from a radio button. Captions do not.
BS = {0: "PUSHBUTTON", 1: "DEFPUSHBUTTON", 2: "CHECKBOX", 3: "AUTOCHECKBOX",
      4: "RADIOBUTTON", 5: "3STATE", 6: "AUTO3STATE", 7: "GROUPBOX",
      8: "USERBUTTON", 9: "AUTORADIOBUTTON", 11: "OWNERDRAW"}


def _sz(b, o):
    w = struct.unpack_from("<H", b, o)[0]
    if w == 0:
        return "", o + 2
    if w == 0xFFFF:
        v = struct.unpack_from("<H", b, o + 2)[0]
        return ORD.get(v, "#0x%x" % v), o + 4
    s = []
    while True:
        c = struct.unpack_from("<H", b, o)[0]
        o += 2
        if c == 0:
            break
        s.append(chr(c))
    return "".join(s), o


def parse(b):
    ver, sig = struct.unpack_from("<HH", b, 0)
    ex = (ver == 1 and sig == 0xFFFF)
    if ex:
        style = struct.unpack_from("<I", b, 12)[0]
        cdit = struct.unpack_from("<H", b, 16)[0]
        o = 26
    else:
        style = struct.unpack_from("<I", b, 0)[0]
        cdit = struct.unpack_from("<H", b, 8)[0]
        o = 18
    _, o = _sz(b, o)                       # menu
    _, o = _sz(b, o)                       # class
    title, o = _sz(b, o)                   # caption
    if style & 0x40:                       # DS_SETFONT
        o += 2
        if ex:
            o += 6
        _, o = _sz(b, o)                   # typeface
    out = []
    for _ in range(cdit):
        o = (o + 3) & ~3
        if ex:
            st = struct.unpack_from("<I", b, o + 8)[0]
            o += 12 + 8
            cid = struct.unpack_from("<I", b, o)[0]
            o += 4
        else:
            st = struct.unpack_from("<I", b, o)[0]
            o += 8 + 8
            cid = struct.unpack_from("<H", b, o)[0]
            o += 2
        cls, o = _sz(b, o)
        cap, o = _sz(b, o)
        extra = struct.unpack_from("<H", b, o)[0]
        o += 2 + extra
        kind = BS.get(st & 0xF, "") if cls == "BUTTON" else ""
        out.append((cid, cls, kind, cap, st))
    return title, out


def dlginit(b):
    o, out = 0, []
    while o + 8 <= len(b):
        cid, msg, n = struct.unpack_from("<HHI", b, o)
        o += 8
        if cid == 0:
            break
        d = b[o:o + n]
        o += n
        out.append((cid, msg, d))
    return out


def strings(b, block):
    o, i, out = 0, 0, []
    while o + 2 <= len(b):
        n = struct.unpack_from("<H", b, o)[0]
        o += 2
        if n:
            out.append(((block - 1) * 16 + i,
                        b[o:o + 2 * n].decode("utf-16-le", "replace")))
            o += 2 * n
        i += 1
    return out


def main():
    for tag in [a for a in sys.argv[1:] if not a.startswith("--")]:
        d = rsrc.load(tag)
        base, secs = rsrc.sections(d)
        pe = struct.unpack_from("<I", d, 0x3c)[0]
        opt = struct.unpack_from("<H", d, pe + 20)[0]
        rrva = struct.unpack_from("<I", d, pe + 24 + opt - 128 + 16)[0]
        rd = rsrc.rva2off(secs, base, rrva)
        out = []
        rsrc.walk(d, secs, base, rd, rd, 0, [], out)
        print(f"\n########## {tag}")
        for path, drva, dsz, fo in out:
            if path[0] != 5 or fo is None:                 # RT_DIALOG
                continue
            title, ctrls = parse(d[fo:fo + dsz])
            print(f"\n  DIALOG {path[1]}  caption={title!r}  {len(ctrls)} controls")
            for cid, cls, kind, cap, st in ctrls:
                vis = "" if st & 0x10000000 else "  [hidden: no WS_VISIBLE]"
                dis = "  [WS_DISABLED]" if st & 0x08000000 else ""
                print(f"     {(cid - 0x100000000 if cid > 0x7fffffff else cid):>6}  {cls:<10}{('/' + kind) if kind else '':<18}"
                      f"{cap!r}{vis}{dis}")
        for path, drva, dsz, fo in out:
            if path[0] != 240 or fo is None:               # RT_DLGINIT
                continue
            recs = dlginit(d[fo:fo + dsz])
            print(f"\n  DLGINIT for dialog {path[1]}  ({len(recs)} records)")
            for cid, msg, dat in recs:
                mn = {0x403: "CB_ADDSTRING", 0x143: "LB_ADDSTRING",
                      0x401: "CB_INSERTSTRING"}.get(msg, "msg=0x%x" % msg)
                txt = dat.split(b"\0")[0].decode("latin-1")
                print(f"     {cid:>6}  {mn:<16}{txt!r}")
        n = 0
        for path, drva, dsz, fo in out:
            if path[0] != 6 or fo is None:                 # RT_STRING
                continue
            for sid, s in strings(d[fo:fo + dsz], path[1]):
                n += 1
        print(f"\n  RT_STRING: {n} non-empty strings "
              f"(pass --strings to print them)")
        if "--strings" in sys.argv:
            for path, drva, dsz, fo in out:
                if path[0] != 6 or fo is None:
                    continue
                for sid, s in strings(d[fo:fo + dsz], path[1]):
                    print(f"     {sid:>6}  {s!r}")


if __name__ == "__main__":
    main()
