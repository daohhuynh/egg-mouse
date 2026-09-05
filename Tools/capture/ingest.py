#!/usr/bin/env python3
"""ingest.py -- a capture plus an explicit line map becomes a fieldmap document.

WHAT THIS DOES. Each APPLY in the config tool emits one settings write. Diffing
consecutive writes gives, for each APPLY, exactly which bytes moved and from
what to what. That is the observation; score.py is where it meets a prediction.

WHY THE MAP IS A SEPARATE FILE AND WHY IT IS EXPLICIT. The obvious thing is to
pair write N with log line N. That is wrong and quietly so: in 02-basic, six of
the 36 numbered lines produced no write at all (a control that does not exist, a
dropdown with fewer options than the log allowed for, four radio buttons that
were never found). Auto-pairing would have shifted every label after the first
gap and produced a table that looks complete and is wrong from line 9 onward.

So the map is written by hand, and this tool refuses to run unless it accounts
for every line as a partition (CLAUDE.md §6): mapped lines plus explicitly
unmapped lines must equal the total, computed here in one place, with the
residue named. A line left out is an error, not a default.

FRAMING, all [O] from windows-run/ (see notes/flash-wire-observed.md §1):

  write buffer, 1041 bytes  [0]=0xA0 report id, [1]=0x11 command, record at [16]
  read buffer,  1040 bytes  [0]=0xA1 status,    [1]=0x01,         record at [16]

**ONE origin, both directions: the bytes as transferred.** The record starts at
offset 16 either way, and cfg107 agrees -- its write builder and its read parser
both use frame+0x10.

An earlier version of this file described a "one-byte stagger" between the
directions. That was an artefact of measuring the two directions from different
origins (excluding the write's report-id byte but including the read's leading
status byte), not a property of the protocol. An independent check refuted it on
2026-09-05. The real asymmetry is elsewhere and is worth knowing: the device
OMITS the report-id byte from GET_REPORT responses and returns wLength-1 bytes,
so a read buffer is one byte shorter and its [0] is a status byte rather than
the report id it was asked for.

  python3 Tools/capture/ingest.py windows-run/02-basic.pcapng \
          Tools/capture/maps/02-basic.json  > /tmp/02.json
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbpcap  # noqa: E402

CMD_WRITE_SETTINGS = 0x11
CMD_READ_SETTINGS = 0x12
RECORD_BASE = 16          # both directions, measured on the transferred bytes


def settings_writes(path):
    xf = [t for t in usbpcap.control_transfers(usbpcap.read(path)) if t.is_feature]
    out = []
    for t in xf:
        if t.bmRequestType & 0x80:
            continue
        b = t.data
        if len(b) == 1041 and b[1] == CMD_WRITE_SETTINGS:
            out.append((t.ts, b[RECORD_BASE:]))
    return out


def settings_reads(path):
    xf = [t for t in usbpcap.control_transfers(usbpcap.read(path)) if t.is_feature]
    out = []
    for t in xf:
        if not (t.bmRequestType & 0x80):
            continue
        b = t.data
        if len(b) == 1040 and b[1] == 0x01 and b[0] not in (0x50, 0x51):
            out.append((t.ts, b[RECORD_BASE:]))
    return out


def runs(a, b):
    """Contiguous stretches where two records differ, as (offset, width)."""
    diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    out = []
    for i in diff:
        if out and i == out[-1][0] + out[-1][1]:
            out[-1] = (out[-1][0], out[-1][1] + 1)
        else:
            out.append((i, 1))
    return out


def hexs(b):
    return " ".join("%02x" % c for c in b)


def build(capture, mapping):
    wr = settings_writes(capture)
    rd = settings_reads(capture)
    entries = mapping["writes"]

    if len(entries) != len(wr):
        raise SystemExit(
            "map lists %d writes, capture has %d. Fix the map -- do NOT let this\n"
            "slide, a length mismatch means every label after the gap is wrong."
            % (len(entries), len(wr)))

    # Partition check. Every numbered line in the log section is either mapped
    # to a write or explicitly listed as producing none. Computed in one place,
    # residue enumerated, per §6.
    total = set(str(k) for k in mapping["log_lines"])
    mapped = set(str(e["line"]) for e in entries)
    nowrite = set(str(k) for k in mapping.get("no_write_lines", {}))
    residue = total - mapped - nowrite
    overlap = mapped & nowrite
    if residue or overlap:
        raise SystemExit("log lines unaccounted for: %s; claimed both ways: %s"
                         % (sorted(residue), sorted(overlap)))

    fields = {}
    states = []
    baseline = rd[0][1] if rd else None
    prev = baseline
    obs_count = 0
    for (ts, rec), ent in zip(wr, entries):
        # The absolute record after this write, not just what changed. A diff
        # cannot answer "what is byte X now" when byte X did not move, and a
        # prediction about an unchanged byte is not thereby unfalsifiable --
        # it is just not answerable from diffs. Both views are emitted.
        states.append({"line": str(ent["line"]), "action": ent["action"],
                       "record": hexs(rec)})
        if prev is None:
            prev = rec
            continue
        for off, width in runs(prev, rec):
            f = fields.setdefault((off, width),
                                  {"payload_offset": off, "width": width,
                                   "observations": []})
            f["observations"].append({
                "line": str(ent["line"]),
                "action": ent["action"],
                "old": hexs(prev[off:off + width]),
                "new": hexs(rec[off:off + width]),
            })
            obs_count += 1
        prev = rec

    return {
        "capture": os.path.basename(capture),
        "note": ("record offsets are relative to the serialized settings record,"
                 " which begins at offset %d of the transferred bytes in BOTH"
                 " directions" % RECORD_BASE),
        "writes": len(wr),
        "reads": len(rd),
        "observations": obs_count,
        "unmapped_log_lines": mapping.get("no_write_lines", {}),
        "states": states,
        "fields": sorted(fields.values(), key=lambda f: f["payload_offset"]),
    }


def merge(docs):
    """Several captures into one document. Observations keep their capture name
    so a line number is never ambiguous across sections."""
    fields = {}
    for d in docs:
        for f in d["fields"]:
            k = (f["payload_offset"], f["width"])
            tgt = fields.setdefault(k, {"payload_offset": k[0], "width": k[1],
                                        "observations": []})
            for ob in f["observations"]:
                ob = dict(ob)
                ob["capture"] = d["capture"]
                tgt["observations"].append(ob)
    return {
        "capture": ", ".join(d["capture"] for d in docs),
        "note": docs[0]["note"],
        "writes": sum(d["writes"] for d in docs),
        "reads": sum(d["reads"] for d in docs),
        "observations": sum(d["observations"] for d in docs),
        "unmapped_log_lines": {d["capture"]: d["unmapped_log_lines"] for d in docs},
        "states": [dict(st, capture=d["capture"])
                   for d in docs for st in d.get("states", [])],
        "fields": sorted(fields.values(), key=lambda f: f["payload_offset"]),
    }


def main():
    args = sys.argv[1:]
    if len(args) < 2 or len(args) % 2:
        sys.exit(__doc__)
    docs = []
    for i in range(0, len(args), 2):
        with open(args[i + 1], encoding="utf-8") as f:
            mapping = json.load(f)
        docs.append(build(args[i], mapping))
    doc = docs[0] if len(docs) == 1 else merge(docs)
    json.dump(doc, sys.stdout, indent=1)
    print()


if __name__ == "__main__":
    main()
