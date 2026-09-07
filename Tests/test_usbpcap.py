#!/usr/bin/env python3
"""Regression tests for the USBPcap decoder.

The bug these exist for: control transfers were grouped by irpId over the whole
file. Windows reuses an IRP address the moment the previous request on it
retires -- one address carries 470 packets in 08-flash.pcapng -- so that
grouping merged hundreds of transfers into one and kept only the first payload.
131 firmware-block writes decoded as 14, and nothing anywhere said so. A
decoder that silently drops 89% of a flash is worse than no decoder.
"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Tools", "capture"))
import usbpcap  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")


def pkt(irp, stage, data, ts=0.0, from_device=False):
    p = usbpcap.Packet()
    p.ts, p.irp, p.status, p.function = ts, irp, 0, 0
    p.info = 1 if from_device else 0
    p.bus, p.device, p.endpoint = 1, 5, 0
    p.transfer, p.stage, p.data = usbpcap.XFER_CONTROL, stage, data
    return p


def setup(bmreq, breq, wvalue, wlength, payload=b""):
    return struct.pack("<BBHHH", bmreq, breq, wvalue, 0, wlength) + payload


class ReusedIrp(unittest.TestCase):
    """The whole point. Same irpId, many transfers, none may be lost."""

    def test_one_irp_carrying_many_transfers(self):
        pkts = []
        for i in range(200):
            pkts.append(pkt(0xDEAD, usbpcap.STAGE_SETUP,
                            setup(0x21, 0x09, 0x0300, 3, bytes([i, i, i]))))
            pkts.append(pkt(0xDEAD, usbpcap.STAGE_COMPLETE, b"", from_device=True))
        xf = usbpcap.control_transfers(pkts)
        self.assertEqual(len(xf), 200)
        # and each kept ITS OWN payload, not the first one 200 times
        self.assertEqual([t.data[0] for t in xf], list(range(200)))

    def test_setup_closes_the_previous_transfer_on_that_irp(self):
        """A completion that never arrives must not swallow the next request."""
        pkts = [pkt(1, usbpcap.STAGE_SETUP, setup(0xA1, 0x01, 0x0300, 8)),
                pkt(1, usbpcap.STAGE_SETUP, setup(0xA1, 0x01, 0x0300, 8)),
                pkt(1, usbpcap.STAGE_COMPLETE, b"\x01\x02", from_device=True)]
        xf = usbpcap.control_transfers(pkts)
        self.assertEqual(len(xf), 2)
        self.assertEqual(xf[0].data, b"")        # truncated, and reported as such
        self.assertEqual(xf[1].data, b"\x01\x02")

    def test_interleaved_irps_do_not_cross_contaminate(self):
        pkts = [pkt(1, usbpcap.STAGE_SETUP, setup(0xA1, 0x01, 0x0300, 2)),
                pkt(2, usbpcap.STAGE_SETUP, setup(0xA1, 0x01, 0x0300, 2)),
                pkt(2, usbpcap.STAGE_COMPLETE, b"\xbb", from_device=True),
                pkt(1, usbpcap.STAGE_COMPLETE, b"\xaa", from_device=True)]
        xf = usbpcap.control_transfers(pkts)
        self.assertEqual([t.data for t in xf], [b"\xaa", b"\xbb"])

    def test_orphan_completion_is_dropped_not_crashed(self):
        xf = usbpcap.control_transfers([pkt(9, usbpcap.STAGE_COMPLETE, b"\x01")])
        self.assertEqual(xf, [])

    def test_out_payload_rides_in_the_setup_packet(self):
        p = pkt(1, usbpcap.STAGE_SETUP, setup(0x21, 0x09, 0x0304, 4, b"\x04\x05\x06\x07"))
        t, = usbpcap.control_transfers([p])
        self.assertTrue(t.is_set_report)
        self.assertTrue(t.is_feature)
        self.assertEqual(t.report_id, 4)
        self.assertEqual(t.data, b"\x04\x05\x06\x07")


def every_capture():
    """(directory, filename) for every .pcapng in the repo, in a stable order.

    BOTH directories. This class read `windows-run` only until 2026-09-07, so
    the decoder-integrity check below -- the one guarding the bug where
    usbpcap silently dropped most of a capture -- had never been run against
    the seven firmware-1.10 files, which are the newest and least-exercised
    capture shapes in the repo.
    """
    out = []
    for d in ("windows-run", "windows-capture"):
        full = os.path.join(ROOT, d)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if name.endswith(".pcapng"):
                out.append((full, name))
    return out


class RealCaptures(unittest.TestCase):
    """Against the capture runs that exist. Skipped if they are not present."""

    def caps(self, name):
        p = os.path.join(ROOT, "windows-run", name)
        if not os.path.exists(p):
            self.skipTest("windows-run/%s not present" % name)
        return usbpcap.control_transfers(usbpcap.read(p))

    def test_both_flashes_carry_131_block_writes(self):
        for name in ("08-flash.pcapng", "09-flash-again.pcapng"):
            xf = self.caps(name)
            big = [t for t in xf if not (t.bmRequestType & 0x80) and len(t.data) == 1041]
            self.assertEqual(len(big), 131, "%s: %d block writes" % (name, len(big)))

    def test_no_transfer_loses_a_packet(self):
        """Every control packet is accounted for: attached to a transfer, or an
        orphan with no open request. Silence here is what the old bug looked
        like, so count rather than trust.

        EVERY capture, both directories. The old form checked `08-flash` alone,
        which is the single file the decoder was originally debugged against --
        the weakest possible choice for a regression gate.
        """
        caps = every_capture()
        if not caps:
            self.skipTest("no captures")
        for directory, name in caps:
            with self.subTest(capture=os.path.join(os.path.basename(directory), name)):
                pkts = usbpcap.read(os.path.join(directory, name))
                ctrl = [q for q in pkts if q.transfer == usbpcap.XFER_CONTROL]
                self.assertTrue(ctrl, "no control packets decoded at all")
                xf = usbpcap.control_transfers(pkts)
                attached = sum(len(t.packets) for t in xf)
                orphans = len(ctrl) - attached
                self.assertLess(orphans, len(ctrl) * 0.05,
                                "%d of %d control packets attached to no transfer"
                                % (orphans, len(ctrl)))

    def test_both_capture_runs_are_being_read(self):
        """A directory that vanishes from the sweep must fail, not skip."""
        dirs = {os.path.basename(d) for d, _ in every_capture()}
        self.assertEqual({"windows-run", "windows-capture"}, dirs,
                         "swept %r" % sorted(dirs))


if __name__ == "__main__":
    unittest.main(verbosity=2)
