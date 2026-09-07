#!/usr/bin/env python3
"""Score the 2026-09-06 firmware-1.10 capture run.

Reports what each capture ACTUALLY did rather than asserting a step list: the owner
worked chronologically while the steps were still being edited, so a section may
predate the version of the steps that is now on disk (CAPTURE-STEPS.md).

Every prediction checked here is cited to notes/prediction-capture-1.10.md or to
notes/config-protocol.md 7.17.  Nothing in this file encodes a belief about what
an UNKNOWN byte means -- it prints unknowns as unknown (CLAUDE.md 3.1).
"""
import sys, os, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import records

REC = 0x73                      # 115-byte settings record, 0x00..0x72
BTN, BTNLEN, NBTN = 0x37, 7, 8  # eight 7-byte button entries
CPI, CPILEN, NCPI = 0x23, 5, 4  # four 5-byte CPI stages: flag,Xlo,Xhi,Ylo,Yhi

# config-protocol.md 7.17, every value an immediate in cfg107's menu handlers.
ACTION = {
    (0x00, 0x01): "MOUSE LEFT CLICK",   (0x00, 0x02): "MOUSE RIGHT CLICK",
    (0x00, 0x04): "MOUSE MIDDLE CLICK", (0x00, 0x10): "MOUSE FORWARD",
    (0x00, 0x08): "MOUSE BACK",         (0x01, 0x01): "MOUSE SCROLL UP",
    (0x01, 0xff): "MOUSE SCROLL DOWN",  (0x0c, 0x00): "CPI FIXED CPI",
    (0x09, 0xf1): "CPI LOOP",           (0xff, 0x00): "DISABLE",
    (0x20, 0xcd): "MEDIA PLAY/PAUSE",   (0x20, 0xb5): "MEDIA NEXT",
    (0x20, 0xb6): "MEDIA PREVIOUS",     (0x20, 0xe2): "MEDIA MUTE",
    (0x20, 0xe9): "MEDIA VOLUME UP",    (0x20, 0xea): "MEDIA VOLUME DOWN",
    (0x18, 0x96): "MEDIA BROWSER",      (0x18, 0x94): "MEDIA EXPLORER",
}
MEDIA_PREDICTED = {k: v for k, v in ACTION.items() if v.startswith("MEDIA")}
MODNAME = {0x01: "LCTRL", 0x02: "LSHIFT", 0x04: "LALT", 0x08: "LGUI",
           0x10: "RCTRL", 0x20: "RSHIFT", 0x40: "RALT", 0x80: "RGUI"}


def act(e):
    k = (e[0], e[1])
    if k in ACTION:
        return ACTION[k]
    if e[0] == 0x02:                       # KEYBOARD KEY: +1 modifier, +2 usage
        mods = [n for b, n in MODNAME.items() if e[1] & b] or ["none"]
        return "KEY usage=0x%02x mods=%s" % (e[2], "+".join(mods))
    return "UNKNOWN"


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def stages(r):
    return [(r[CPI + i * CPILEN], u16(r, CPI + i * CPILEN + 1),
             u16(r, CPI + i * CPILEN + 3)) for i in range(NCPI)]


def entries(r):
    return [tuple(r[BTN + i * BTNLEN: BTN + (i + 1) * BTNLEN]) for i in range(NBTN)]


def report(path):
    print("=" * 78)
    print(os.path.basename(path))
    print("=" * 78)
    fr = records.frames(path)
    cmds = [("-->%02x" % f.cmd) if f.out else "<--" for f in fr]
    outs = [f for f in fr if f.out and f.cmd is not None]
    print("  %d feature frames; outbound commands in order:" % len(fr))
    print("    " + " ".join("%02x" % f.cmd for f in outs))
    for c, n in [(0x02, "A1 02 info"), (0x12, "A1 12 read"), (0x11, "A0 11 WRITE"),
                 (0x13, "A1 13 FACTORY RESET")]:
        k = sum(1 for f in outs if f.cmd == c)
        if k:
            print("      %-22s x%d" % (n, k))

    recs = [(f, f.payload[:REC]) for f in fr if len(f.payload) >= REC]
    if not recs:
        print("  no full settings record in this capture\n")
        return
    prev = None
    for f, r in recs:
        tag = ("WRITE  A0 11" if (f.out and f.cmd == 0x11)
               else "read   host<-mouse" if not f.out else "cmd %02x" % f.cmd)
        if prev is None:
            print("\n  [seq %d] %s  -- BASELINE" % (f.seq, tag))
            for i, (fl, x, y) in enumerate(stages(r)):
                print("      CPI%d flag=%02x  X=%-6d Y=%-6d %s"
                      % (i + 1, fl, x, y, "" if (x == y) == (fl == 0) else "<-- FLAG DISAGREES WITH X==Y"))
            for i, e in enumerate(entries(r)):
                print("      btn%d %s  %s" % (i, bytes(e).hex(" "), act(e)))
            prev = r
            continue
        d = [i for i in range(REC) if r[i] != prev[i]]
        if not d:
            continue
        print("\n  [seq %d] %s  -- %d byte(s) changed" % (f.seq, tag, len(d)))
        for i in d:
            note = ""
            if 0x01 <= i <= 0x04 and prev[i] == 0x80 and r[i] == 0x00:
                note = "vendor zeroes 0x01-0x04 on write (7.4) -- not a setting"
            elif BTN <= i < BTN + NBTN * BTNLEN:
                note = "button %d  +%d" % ((i - BTN) // BTNLEN, (i - BTN) % BTNLEN)
            elif CPI <= i < CPI + NCPI * CPILEN:
                note = "CPI stage %d  %s" % ((i - CPI) // CPILEN + 1,
                                             "flag Xlo Xhi Ylo Yhi".split()[(i - CPI) % CPILEN])
            print("      0x%02x  %02x -> %02x   %s" % (i, prev[i], r[i], note))
        for b in {(i - BTN) // BTNLEN for i in d if BTN <= i < BTN + NBTN * BTNLEN}:
            e = entries(r)[b]
            print("      => btn%d now %s  %s" % (b, bytes(e).hex(" "), act(e)))
            if e[0] == 0x0c:
                print("         FIXED CPI  +2/+3 = %-6d   +4/+5 = %-6d"
                      % (u16(r, BTN + b * BTNLEN + 2), u16(r, BTN + b * BTNLEN + 4)))
        for s in {(i - CPI) // CPILEN for i in d if CPI <= i < CPI + NCPI * CPILEN}:
            fl, x, y = stages(r)[s]
            ok = (x == y) == (fl == 0)
            print("      => CPI%d flag=%02x X=%d Y=%d   %s"
                  % (s + 1, fl, x, y, "flag matches X==Y" if ok else "*** FLAG vs X==Y MISMATCH ***"))
        prev = r
    print()


def summary(paths):
    print("=" * 78)
    print("PREDICTION SCORING")
    print("=" * 78)
    written = {}
    for p in paths:
        prev = None
        for f in records.frames(p):
            r = f.payload[:REC]
            if len(r) < REC:
                continue
            if f.out and f.cmd == 0x11 and prev is not None:
                # An ACTION change is (+0,+1) moving.  Comparing whole entries is
                # wrong: 04-buttons.pcapng only ever moves +6, the multiclick
                # filter, leaving the action untouched.
                # 7.20: Left-handed Mode has NO record byte of its own -- it
                # rewrites entries 0 and 1 together and is inferred from them.
                # Record 0x01 is NOT the signal: 7.4 has the vendor zeroing
                # 0x01-0x04 on the first write of every capture.
                moved = [i for i in range(NBTN)
                         if entries(r)[i][:2] != entries(prev)[i][:2]]
                hand = set(moved) == {0, 1}
                for i in moved:
                    e = entries(r)[i]
                    # KEYBOARD KEY carries its usage in +2, so (+0,+1) alone
                    # collapses distinct keys onto one bucket.
                    key = tuple(e[:3]) if e[0] == 0x02 else (e[0], e[1])
                    written.setdefault(key, []).append(
                        (os.path.basename(p), f.seq, i, bytes(e).hex(" "), hand))
                for i in range(NBTN):
                    e, o = entries(r)[i], entries(prev)[i]
                    if e[0] == 0x02 and e[:2] == o[:2] and e[2] != o[2]:
                        written.setdefault(tuple(e[:3]), []).append(
                            (os.path.basename(p), f.seq, i, bytes(e).hex(" "), False))
            prev = r
    print("\nMEDIA actions written (prediction-capture-1.10.md 5, from 7.17):")
    for k, name in sorted(MEDIA_PREDICTED.items()):
        hits = written.get(k, [])
        print("  %-22s %02x %02x   %s" % (name, k[0], k[1],
              "HIT   %s seq%d btn%d" % hits[0][:3] if hits else "not seen"))
    unknown = [k for k in written if k not in ACTION and k[0] != 0x02]
    if unknown:
        print("\n  *** action pairs written that 7.17 does not name: %s"
              % ", ".join("%02x %02x" % k for k in sorted(unknown)))
    print("\nKEYBOARD KEY entries written (usage lives in +2, so keyed on +0..+2):")
    for k, v in sorted(written.items()):
        if k[0] == 0x02 and len(k) == 3:
            mods = [n for b, n in MODNAME.items() if k[1] & b] or ["none"]
            for src, seq, b, hexs, _h in v:
                print("  %s   usage=0x%02x mods=%-6s  %s seq%d btn%d"
                      % (hexs, k[2], "+".join(mods), src, seq, b))
    print("\nMOUSE actions whose (+0,+1) was ever CHANGED by a write:")
    print("  (a write moving BOTH entry 0 and entry 1 and nothing else is the")
    print("   Left-handed Mode MOVE of 7.20, which rewrites them programmatically")
    print("   -- NOT a menu pick.  Record 0x01 is not the signal: 7.4 has the")
    print("   vendor zeroing 0x01-0x04 on the first write of every capture.)")
    for k, name in sorted((k, v) for k, v in ACTION.items() if v.startswith("MOUSE")):
        hits = written.get(k, [])
        picks = [h for h in hits if not h[4]]
        swaps = [h for h in hits if h[4]]
        if picks:
            w = "MENU PICK  %s seq%d btn%d" % picks[0][:3]
        elif swaps:
            w = "only via handedness swap (%s seq%d)" % swaps[0][:2]
        else:
            w = "never changed -- default only"
        print("  %-22s %02x %02x   %s" % (name, k[0], k[1], w))


if __name__ == "__main__":
    args = sys.argv[1:]
    paths = []
    for a in args:
        paths += sorted(glob.glob(os.path.join(a, "*.pcapng"))) if os.path.isdir(a) else [a]
    if not paths:
        sys.exit("usage: score110.py <folder-or-files...>")
    for p in paths:
        report(p)
    summary(paths)
