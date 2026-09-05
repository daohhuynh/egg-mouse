#!/usr/bin/env python3
"""hidobserve.py [--vid 0x3367] [--all] [--save DIR] -- what the device IS.

STRICTLY READ-ONLY. It shells out to `ioreg`, which reads the I/O Registry and
nothing else. **No HID device is opened, no report is sent, no byte reaches the
mouse.** That is the whole point: CLAUDE.md §5 lists seven things that cannot be
settled without the hardware, and four of them are answerable by looking at what
macOS already knows, before any of our code touches anything.

It answers, as [O]:

  * every HID collection the device presents -- usage page, usage, and the
    report sizes macOS computed
  * the RAW report descriptor per collection, parsed into report IDs and the
    exact byte length of every input, output and feature report
  * ProductID, i.e. which PID appears in normal operation
  * VersionNumber (bcdDevice), which is where the vendor's updater reads the
    firmware version from -- so the version comes free, with no protocol at all

WHY THE DESCRIPTOR MATTERS MORE THAN ANYTHING ELSE HERE. Every report length in
notes/ is [D] -- traced to an immediate in the vendor's .exe, never observed.
`updater-protocol.md` records that the vendor never validates report lengths, so
a disagreement between what their code sends and what the device declares would
be invisible on Windows and fatal for us. This is the only way to check it, and
it is free.

Nothing in this file interprets a value as protocol. It records structure.
"""
import json, os, plistlib, re, subprocess, sys

MAIN = {0x80: "Input", 0x90: "Output", 0xB0: "Feature",
        0xA0: "Collection", 0xC0: "EndCollection"}


def parse_descriptor(b):
    """Walk the HID report descriptor. Returns (reports, log).

    reports[kind][report_id] = total bits. kind is Input/Output/Feature.
    Report ID 0 means the descriptor declares no IDs, so reports are unnumbered.
    """
    g = {"usage_page": 0, "report_id": 0, "report_size": 0, "report_count": 0}
    stack, out, log, depth = [], {}, [], 0
    i = 0
    while i < len(b):
        pfx = b[i]
        if pfx == 0xFE:                                  # long item
            sz = b[i + 1]
            i += 3 + sz
            continue
        size = pfx & 0x03
        size = 4 if size == 3 else size
        typ = (pfx >> 2) & 0x03
        tag = pfx & 0xFC
        data = int.from_bytes(b[i + 1:i + 1 + size], "little") if size else 0
        if typ == 0:                                     # Main
            if tag in (0x80, 0x90, 0xB0):
                bits = g["report_size"] * g["report_count"]
                k = MAIN[tag]
                out.setdefault(k, {}).setdefault(g["report_id"], 0)
                out[k][g["report_id"]] += bits
                log.append("%s%s  id=%d  %d x %d bits = %d"
                           % ("  " * depth, k, g["report_id"],
                              g["report_count"], g["report_size"], bits))
            elif tag == 0xA0:
                log.append("%sCollection 0x%02x" % ("  " * depth, data))
                depth += 1
            elif tag == 0xC0:
                depth = max(0, depth - 1)
                log.append("%sEndCollection" % ("  " * depth))
        elif typ == 1:                                   # Global
            name = {0x04: "usage_page", 0x84: "report_id", 0x74: "report_size",
                    0x94: "report_count", 0x14: "logical_min",
                    0x24: "logical_max"}.get(tag)
            if name:
                g[name] = data
                if name in ("usage_page", "report_id"):
                    log.append("%s%s = 0x%x" % ("  " * depth, name, data))
            elif tag == 0xA4:
                stack.append(dict(g))
            elif tag == 0xB4 and stack:
                g = stack.pop()
        elif typ == 2:                                   # Local
            if tag == 0x08:
                log.append("%susage = 0x%x" % ("  " * depth, data))
        i += 1 + size
    return out, log


def devices():
    """Every IOHIDDevice, as plists. `ioreg -a` is read-only by construction."""
    x = subprocess.run(["ioreg", "-c", "IOHIDDevice", "-r", "-l", "-a"],
                       capture_output=True)
    if not x.stdout:
        return []
    try:
        top = plistlib.loads(x.stdout)
    except Exception as e:
        print("could not parse ioreg output:", e, file=sys.stderr)
        return []
    out, stack = [], list(top if isinstance(top, list) else [top])
    while stack:
        n = stack.pop()
        if not isinstance(n, dict):
            continue
        if "VendorID" in n or "ReportDescriptor" in n:
            out.append(n)
        for c in n.get("IORegistryEntryChildren", []) or []:
            stack.append(c)
    return out


def main():
    a = sys.argv[1:]
    vid = 0x3367
    if "--vid" in a:
        vid = int(a[a.index("--vid") + 1], 0)
    want_all = "--all" in a
    save = a[a.index("--save") + 1] if "--save" in a else None

    found = []
    for d in devices():
        if not want_all and d.get("VendorID") != vid:
            continue
        found.append(d)

    if not found:
        print("No HID device with VendorID 0x%04x is attached." % vid)
        print("Nothing was sent to any device; this tool only reads ioreg.")
        return 1

    print("### %d HID collection(s) with VendorID 0x%04x\n" % (len(found), vid))
    dump = []
    for d in found:
        rd = d.get("ReportDescriptor")
        rd = bytes(rd) if rd is not None else b""
        ver = d.get("VersionNumber")
        print("--- %r  (%r)" % (d.get("Product"), d.get("Manufacturer")))
        for k in ("VendorID", "ProductID", "VersionNumber", "PrimaryUsagePage",
                  "PrimaryUsage", "MaxInputReportSize", "MaxOutputReportSize",
                  "MaxFeatureReportSize", "Transport", "LocationID",
                  "SerialNumber", "CountryCode"):
            if k in d:
                v = d[k]
                print("    %-22s %s%s" % (k, v,
                      "  (0x%x)" % v if isinstance(v, int) and v > 9 else ""))
        if isinstance(ver, int):
            # The vendor's updater displays bcdDevice as a version; record both
            # readings without choosing, per CLAUDE.md §1.2.
            print("    %-22s BCD %x.%02x   raw/100 %.2f"
                  % ("VersionNumber reads as", ver >> 8, ver & 0xFF, ver / 100))
        print("    %-22s %d bytes" % ("ReportDescriptor", len(rd)))
        if rd:
            print("      " + rd.hex())
            reps, log = parse_descriptor(rd)
            print("\n    PARSED -- report lengths in BYTES, id 0 = unnumbered:")
            for kind in ("Input", "Output", "Feature"):
                for rid, bits in sorted(reps.get(kind, {}).items()):
                    n = (bits + 7) // 8
                    print("      %-8s id 0x%02x  %4d bits = %3d bytes payload"
                          "  (+1 for the id on the wire = %d)"
                          % (kind, rid, bits, n, n + 1))
            print("\n    ITEM WALK:")
            for l in log:
                print("      " + l)
        print()
        dump.append({k: (list(v) if isinstance(v, bytes) else v)
                     for k, v in d.items()
                     if k not in ("IORegistryEntryChildren",)})
    if save:
        os.makedirs(save, exist_ok=True)
        p = os.path.join(save, "hidobserve_%04x.json" % vid)
        json.dump(dump, open(p, "w"), indent=1, default=str)
        print("raw properties saved to", p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
