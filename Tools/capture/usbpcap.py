#!/usr/bin/env python3
"""usbpcap.py -- read a USBPcap .pcapng and pull out the HID traffic.

WHAT THIS IS FOR. The vendor's Windows tools are the only place the protocol has
ever been seen working. CLAUDE.md §2 recorded that no independent check on our
derivation existed; a capture of Endgame's own software talking to Endgame's own
hardware IS that check. This turns one into something we can diff against.

STRUCTURE, so a reader can verify rather than trust:

  pcapng   Section Header Block (0x0A0D0D0A) fixes the byte order via the magic
           0x1A2B3C4D. Interface Description Blocks (0x01) give the link type;
           Enhanced Packet Blocks (0x06) and legacy Simple Packet Blocks (0x03)
           carry the bytes. Every block is length-prefixed and length-suffixed,
           so we walk by the trailing length and never guess a size.

  USBPcap  link type 249. Each packet begins with USBPCAP_BUFFER_PACKET_HEADER:
             u16 headerLen; u64 irpId; u32 status; u16 function; u8 info;
             u16 bus; u16 device; u8 endpoint; u8 transfer; u32 dataLength
           = 27 bytes. Control transfers carry one extra byte, `stage`, making
           28. **We trust headerLen rather than the constant**, because that is
           the field that exists precisely so the layout can grow.
           info bit 0 = direction, 1 meaning device-to-host.

  HID      A feature report is a control transfer whose 8-byte SETUP is
             SET_REPORT  bmRequestType 0x21, bRequest 0x09
             GET_REPORT  bmRequestType 0xA1, bRequest 0x01
           with wValue = (reportType << 8) | reportId and reportType 3 = Feature.
           The payload arrives in the DATA/COMPLETE stage of the same IRP, which
           is why transfers are reassembled by irpId rather than read packet by
           packet.

This module decodes. It attaches no meaning to any byte and knows nothing about
this project's command numbers -- that belongs upstairs, in the tools that
compare a capture against a prediction.
"""
import struct
import sys

LINKTYPE_USBPCAP = 249

# usb.transfer
XFER_ISO, XFER_INTERRUPT, XFER_CONTROL, XFER_BULK = 0, 1, 2, 3
XFER_NAME = {0: "iso", 1: "interrupt", 2: "control", 3: "bulk", 0xFF: "irp-info"}

# control stage
STAGE_SETUP, STAGE_DATA, STAGE_STATUS, STAGE_COMPLETE = 0, 1, 2, 3

HID_REPORT_TYPE = {1: "Input", 2: "Output", 3: "Feature"}


class Packet:
    __slots__ = ("ts", "irp", "status", "function", "info", "bus", "device",
                 "endpoint", "transfer", "stage", "data")

    @property
    def from_device(self):
        return bool(self.info & 1)

    def __repr__(self):
        return ("<pkt %.6f dev=%d ep=0x%02x %s %s %dB>"
                % (self.ts, self.device, self.endpoint,
                   XFER_NAME.get(self.transfer, "?"),
                   "IN" if self.from_device else "OUT", len(self.data)))


def _blocks(buf):
    """Yield (block_type, body) walking the length-suffixed block chain."""
    endian = "<"
    o = 0
    n = len(buf)
    while o + 12 <= n:
        btype = struct.unpack_from(endian + "I", buf, o)[0]
        if btype == 0x0A0D0D0A:                       # Section Header
            magic = struct.unpack_from("<I", buf, o + 8)[0]
            endian = "<" if magic == 0x1A2B3C4D else ">"
            btype = struct.unpack_from(endian + "I", buf, o)[0]
        blen = struct.unpack_from(endian + "I", buf, o + 4)[0]
        if blen < 12 or o + blen > n:
            break
        yield endian, btype, buf[o + 8:o + blen - 4]
        o += blen


def parse_usbpcap(body, endian):
    """One USBPcap packet payload -> Packet, or None if it is too short."""
    if len(body) < 27:
        return None
    hl = struct.unpack_from(endian + "H", body, 0)[0]
    if hl < 27 or hl > len(body):
        return None
    (irp, status, function, info, bus, device, endpoint, transfer,
     dlen) = struct.unpack_from(endian + "QIHBHHBBI", body, 2)
    p = Packet()
    p.irp, p.status, p.function, p.info = irp, status, function, info
    p.bus, p.device, p.endpoint, p.transfer = bus, device, endpoint, transfer
    # `stage` only exists on control transfers, and only when the header was
    # actually made longer for it. Deriving it from headerLen rather than from
    # the transfer type keeps this correct if either side changes.
    p.stage = body[27] if (transfer == XFER_CONTROL and hl >= 28) else None
    p.data = body[hl:hl + dlen]
    return p


def read(path):
    """Every USBPcap packet in the file, in order, with timestamps in seconds."""
    with open(path, "rb") as fh:
        buf = fh.read()
    tsresol = {}          # interface index -> ticks per second
    iface = 0
    out = []
    for endian, btype, body in _blocks(buf):
        if btype == 0x00000001:                       # Interface Description
            link = struct.unpack_from(endian + "H", body, 0)[0]
            # options follow snaplen at +8; if_tsresol is option code 9
            res, o = 6, 8
            while o + 4 <= len(body):
                code, olen = struct.unpack_from(endian + "HH", body, o)
                o += 4
                if code == 0:
                    break
                if code == 9 and olen >= 1:
                    res = body[o]
                o += (olen + 3) & ~3
            tsresol[iface] = res
            if link != LINKTYPE_USBPCAP:
                print("warning: interface %d link type %d, expected %d (USBPcap)"
                      % (iface, link, LINKTYPE_USBPCAP), file=sys.stderr)
            iface += 1
        elif btype == 0x00000006:                     # Enhanced Packet
            ifid, tsh, tsl, cap, orig = struct.unpack_from(endian + "IIIII", body, 0)
            res = tsresol.get(ifid, 6)
            div = float(10 ** res) if res < 0x80 else float(2 ** (res & 0x7F))
            p = parse_usbpcap(body[20:20 + cap], endian)
            if p:
                p.ts = ((tsh << 32) | tsl) / div
                out.append(p)
    return out


class Transfer:
    """One control transfer, reassembled from its stages by irpId."""
    __slots__ = ("ts", "device", "bmRequestType", "bRequest", "wValue",
                 "wIndex", "wLength", "data", "packets")

    @property
    def report_id(self):
        return self.wValue & 0xFF

    @property
    def report_type(self):
        return (self.wValue >> 8) & 0xFF

    @property
    def is_set_report(self):
        return self.bmRequestType == 0x21 and self.bRequest == 0x09

    @property
    def is_get_report(self):
        return self.bmRequestType == 0xA1 and self.bRequest == 0x01

    @property
    def is_feature(self):
        return (self.is_set_report or self.is_get_report) and self.report_type == 3

    def __repr__(self):
        kind = ("SET_REPORT" if self.is_set_report else
                "GET_REPORT" if self.is_get_report else
                "bmReq=0x%02x bReq=0x%02x" % (self.bmRequestType, self.bRequest))
        return ("<%s %s id=0x%02x %dB @%.3f>"
                % (kind, HID_REPORT_TYPE.get(self.report_type, "?"),
                   self.report_id, len(self.data), self.ts))


def control_transfers(packets):
    """Reassemble control transfers.

    Keyed on irpId, but NOT over the whole file. Windows reuses an IRP address
    as soon as the previous request on it completes -- in 08-flash.pcapng a
    single address carries 470 packets -- so grouping by irpId alone silently
    merges hundreds of transfers into one and DROPS every payload but the
    first. That is how 131 firmware-block writes once decoded as 14.

    A SETUP stage therefore *closes* whatever was open on that irpId and starts
    a new transfer. Later stages of the same irpId attach to whichever transfer
    is currently open on it. Order is preserved by emitting on open, not on
    close, so a transfer whose completion never arrives is still reported --
    truncated, which is a finding, rather than absent, which is a lie.
    """
    open_on_irp = {}
    out = []
    for p in packets:
        if p.transfer != XFER_CONTROL:
            continue
        if p.stage == STAGE_SETUP and len(p.data) >= 8:
            t = Transfer()
            t.packets = [p]
            t.ts = p.ts
            t.device = p.device
            (t.bmRequestType, t.bRequest, t.wValue, t.wIndex,
             t.wLength) = struct.unpack_from("<BBHHH", p.data, 0)
            # OUT (SET_REPORT): USBPcap puts the report in the SAME packet,
            # right after the 8 setup bytes, so dataLength is 8 + wLength.
            # IN (GET_REPORT): the report arrives in a later stage.
            t.data = p.data[8:] if len(p.data) > 8 else b""
            open_on_irp[p.irp] = t
            out.append(t)
            continue
        t = open_on_irp.get(p.irp)
        if t is None:
            continue
        t.packets.append(p)
        if not t.data and p.data:
            t.data = p.data
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        pkts = read(path)
        xf = control_transfers(pkts)
        feat = [t for t in xf if t.is_feature]
        print("\n##### %s" % path)
        print("  %d packets, %d control transfers, %d HID FEATURE reports"
              % (len(pkts), len(xf), len(feat)))
        devs = {}
        for p in pkts:
            devs.setdefault(p.device, 0)
            devs[p.device] += 1
        print("  packets per USB device address: %s"
              % ", ".join("dev%d=%d" % kv for kv in sorted(devs.items())))
        kinds = {}
        for p in pkts:
            k = XFER_NAME.get(p.transfer, str(p.transfer))
            kinds[k] = kinds.get(k, 0) + 1
        print("  transfer types: %s" % kinds)
        if feat:
            print("  feature reports:")
            for t in feat[:40]:
                b1 = t.data[1] if len(t.data) > 1 else 0
                print("    %.3f dev%-3d %-11s id=0x%02x len=%-5d b1=0x%02x"
                      % (t.ts, t.device,
                         "SET_REPORT" if t.is_set_report else "GET_REPORT",
                         t.report_id, len(t.data), b1))


if __name__ == "__main__":
    main()
