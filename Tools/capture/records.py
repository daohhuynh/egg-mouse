#!/usr/bin/env python3
"""records.py -- pull the ordered exchange list out of one or more captures.

usbpcap.py decodes USB. This adds exactly one layer of meaning, the one the
baseline capture established as [O] (notes/wire-observed.md §2):

    payload[0] is the report id.
    payload[1] is the COMMAND on an outbound frame and the STATUS on an
    inbound one.

Those are two different fields that happen to share an offset, and collapsing
them into one "cmd" column made the baseline's status byte 0x01 show up as an
unknown command on the first run of this tool. Direction decides which it is.
Nothing else here knows what any command means. Command 0x11 and 0x12 are named
in --summary output for readability and the names come from notes; the
extraction does not depend on them, and an unrecognised command is printed with
its number rather than dropped. That matters because a capture containing a
command we have never seen is the single most interesting thing a capture can
contain, and a tool that filters to a known list would hide it (§1.2a).

    records.py 02-basic.pcapng                 # one line per exchange
    records.py 02-basic.pcapng --summary       # counts by command
    records.py *.pcapng --json out.json        # machine-readable, for fieldmap
    records.py 02-basic.pcapng --dump DIR      # one .bin per frame

BYTE OFFSETS ARE WIRE OFFSETS. Offset 0 is the first byte USBPcap recorded. See
notes/wire-observed.md §2.1: whether that is the full API buffer or buffer[1:]
is undecided, and the entire map shifts by one between the two readings. Keeping
one unambiguous origin is the point -- translate at the edge, once, when the
device settles it.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbpcap

# For display only. Never used to filter.
KNOWN = {
    0x02: "small-query",
    0x11: "WRITE-settings",
    0x12: "read-settings",
    0x13: "factory-reset?",
    0x03: "bootloader-start?",
    0x06: "write-block?",
    0x07: "read-block?",
    0x08: "image-checksum?",
    0x09: "bootloader-complete?",
    0x3A: "enter-bootloader?",
}


class Frame:
    """One feature-report transfer, with the two bytes we can name."""
    __slots__ = ("src", "seq", "ts", "out", "report_id", "b1", "wlen", "data")

    @property
    def cmd(self):
        """Outbound only. An inbound frame has a status here, not a command."""
        return self.b1 if self.out else None

    @property
    def status(self):
        return None if self.out else self.b1

    @property
    def name(self):
        return KNOWN.get(self.b1, "") if self.out else "status"

    @property
    def payload(self):
        """The 1024-byte settings body, if this frame is long enough to have
        one. Offset 0x10 is the vendor's own copy offset, [D]; on a 1040-byte
        frame that leaves exactly 1024, which is the arithmetic agreeing."""
        return self.data[0x10:] if len(self.data) >= 0x410 else b""

    def line(self):
        d, lbl = ("-->", "cmd") if self.out else ("<--", "sts")
        return ("%-22s %4d  %s  id=0x%02x %s=0x%02x %-20s wLen=%-5d got=%-5d %s"
                % (os.path.basename(self.src), self.seq, d, self.report_id,
                   lbl, self.b1, self.name, self.wlen, len(self.data),
                   self.data[:8].hex(" ")))


def frames(path):
    out = []
    for i, t in enumerate(usbpcap.control_transfers(usbpcap.read(path))):
        if not t.is_feature or len(t.data) < 2:
            continue
        f = Frame()
        f.src, f.seq, f.ts = path, len(out), t.ts
        f.out = t.is_set_report
        f.report_id, f.b1 = t.data[0], t.data[1]
        f.wlen, f.data = t.wLength, t.data
        out.append(f)
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    paths = [a for a in args if a.endswith(".pcapng") or a.endswith(".pcap")]
    rest = [a for a in args if a not in paths]

    allf = []
    for p in paths:
        try:
            fs = frames(p)
        except Exception as e:                      # a truncated capture is a
            print("!! %s: %s" % (p, e))             # fact about the capture,
            continue                                # not a reason to stop
        if not fs:
            print("!! %s: no HID feature reports at all" % p)
        allf.extend(fs)

    if "--summary" in flags:
        by = {}
        for f in allf:
            by.setdefault((f.out, f.b1), []).append(f)
        print("%-3s %-4s %-24s %5s  %s"
              % ("dir", "byte1", "meaning", "count", "sizes seen"))
        for (o, c), fs in sorted(by.items(), key=lambda kv: (not kv[0][0], kv[0][1])):
            sizes = sorted({len(f.data) for f in fs})
            meaning = (KNOWN.get(c, "UNKNOWN COMMAND -- read it") if o
                       else "status")
            print("%-3s 0x%02x %-24s %5d  %s"
                  % ("-->" if o else "<--", c, meaning, len(fs), sizes))
        unk = sorted({f.b1 for f in allf if f.out and f.b1 not in KNOWN})
        if unk:
            print("\n*** OUTBOUND commands not in the known list: %s"
                  % ", ".join("0x%02x" % c for c in unk))
            print("    That is the interesting case, not an error. Read them.")
        sts = sorted({f.b1 for f in allf if not f.out})
        if sts != [0x01]:
            print("\n*** inbound status bytes seen: %s  (0x01 = ready)"
                  % ", ".join("0x%02x" % c for c in sts))
    else:
        for f in allf:
            print(f.line())

    if "--dump" in flags:
        d = rest[0] if rest else "frames"
        os.makedirs(d, exist_ok=True)
        for f in allf:
            n = "%s-%03d-%s-%02x.bin" % (
                os.path.basename(f.src).split(".")[0], f.seq,
                "out" if f.out else "in", f.b1)
            open(os.path.join(d, n), "wb").write(f.data)
        print("\n%d frames -> %s/" % (len(allf), d))

    if "--json" in flags:
        o = rest[-1] if rest else "records.json"
        json.dump([{"src": f.src, "seq": f.seq, "ts": f.ts, "out": f.out,
                    "report_id": f.report_id, "b1": f.b1, "wlen": f.wlen,
                    "hex": f.data.hex()} for f in allf],
                  open(o, "w"), indent=1)
        print("\n%d frames -> %s" % (len(allf), o))
    return 0


if __name__ == "__main__":
    sys.exit(main())
