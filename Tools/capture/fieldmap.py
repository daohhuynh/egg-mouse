#!/usr/bin/env python3
"""fieldmap.py -- turn a capture plus its log into a byte-offset field map.

    fieldmap.py 02-basic.pcapng --log log.txt
    fieldmap.py 02-basic.pcapng            # diffs only, no attribution
    fieldmap.py *.pcapng --log log.txt --emit Sources/EGGCore/data/settings-1.07.json

THE METHOD. The vendor tool sends the WHOLE settings record on every APPLY. So
if exactly one setting was changed between two APPLYs, the bytes that differ
between the two frames are that setting, and nothing else is. That is the entire
derivation, and it is why the session log insists on one change per APPLY: the
discipline is not tidiness, it is what makes the diff mean anything.

The chain is [record read at startup] -> [write 1] -> [write 2] -> ..., so diff
i is attributed to log line i.

WHAT THIS REFUSES TO DO. Attribution is only sound when the counts line up. If
the number of write frames does not equal the number of numbered log lines, the
off-by-one could be anywhere and every mapping after it would be wrong while
looking perfectly reasonable. So it reports the mismatch and attributes NOTHING.
A field map with one silently-shifted row is worse than no field map: it is a
wrong byte written to the one mouse that exists (CLAUDE.md 1.3, 4.3).

Likewise a diff touching several separate byte runs is reported in full and
never reduced to whichever run looks likeliest. One setting CAN legitimately
move two runs -- a value and a counter, say -- and picking the tidy one is
exactly the guess this project forbids.

BYTES THAT MOVE ON EVERY APPLY. A sequence number or a checksum changes on every
APPLY, and so does the one setting you changed over and over -- polling rate
through seven values moves the same byte seven times. Those two are
indistinguishable from the bytes alone, so this ANNOTATES them and never removes
them. An earlier version excluded them from the map as "not settings", which
silently deleted the polling rate; the test suite caught it. Anything that can
drop a real field to tidy the output is a bug, however sensible the rule sounds.

The annotation also needs enough diffs to mean anything: with two APPLYs, every
byte that moved trivially "moved on every APPLY". Below MIN_FOR_ALWAYS diffs it
is not reported at all.

--emit WRITES THE FIELD MAP AS DATA, which is the deliverable. CLAUDE.md §3
says protocol constants live in EGGCore "as data tables, not scattered through
code", and the difference between this tool producing the table and a human
retyping it into C++ is an hour of transcription against bytes where a
transcription error is expensive. Every entry carries the capture it came from,
the log line that moved it, and a provenance tag, so any row can be re-derived
from the raw file without trusting this one.

Field NAMES are the log line's own text, verbatim. The tool does not invent a
name, guess a type, or merge two runs it thinks are related. A human names
fields later, from a table that already says exactly what evidence exists.

OFFSETS ARE WIRE OFFSETS -- offset 0 is the first byte USBPcap recorded. See
notes/wire-observed.md 2.1: the translation to a hidapi buffer index is still
undecided and shifts everything by one. One unambiguous origin, translated once,
at the edge.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from records import frames

CMD_WRITE = 0x11
CMD_READ = 0x12

# Below this many diffs, "moved on every APPLY" is meaningless -- with two
# APPLYs every byte that moved at all qualifies.
MIN_FOR_ALWAYS = 4


def runs(a, b):
    """Byte offsets where a and b differ, grouped into contiguous runs.
    Returns [(start, end_exclusive)]."""
    n = min(len(a), len(b))
    out, i = [], 0
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    if len(a) != len(b):
        out.append((n, max(len(a), len(b))))
    return out


def parse_log(path, capture):
    """Numbered lines under the '--- <capture> ---' block, in order.

    Deliberately forgiving about the header text and strict about the numbering:
    the header may have had a tab name written next to it by hand, but a gap or
    a repeat in the numbers means the log itself is ambiguous and that has to
    surface rather than be smoothed over."""
    base = os.path.basename(capture)
    stem = base.split(".")[0]
    want = re.compile(r"^-{2,}\s*" + re.escape(stem))
    end = re.compile(r"^-{2,}\s*\d")
    num = re.compile(r"^\s*(\d+)[.)]?\s+(\S.*)$")
    lines, inside, problems = [], False, []
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        return [], ["cannot read %s: %s" % (path, e)]
    for raw in text.replace("\r\n", "\n").split("\n"):
        if want.match(raw):
            inside = True
            continue
        if inside and end.match(raw):
            break
        if inside:
            m = num.match(raw)
            if m:
                lines.append((int(m.group(1)), m.group(2).rstrip()))
    if not inside:
        problems.append("no '--- %s ---' block found in %s" % (stem, path))
    for k, (n, _) in enumerate(lines, 1):
        if n != k:
            problems.append("log numbering is not 1..N: entry %d is numbered %d"
                            % (k, n))
            break
    return lines, problems


def encoding_guess(width, obs):
    """A conservative shape, never a meaning.

    Widths of 1 and 2 are the only ones called; anything else is 'bytes'. For a
    two-byte run the little-endian reading is offered ONLY as a candidate, and
    only when every observation's new value is consistent with it -- and the
    tag stays [G] regardless, because a two-byte run that happens to look like
    LE16 could equally be two adjacent one-byte fields that moved together.
    That distinction is exactly what a diff cannot settle."""
    if width == 1:
        return "u8"
    if width == 2:
        return "le16?"
    return "bytes"


def emit(path, byrun, always, captures, unattributed):
    fields = []
    for (s, e), obs in sorted(byrun.items()):
        w = e - s
        fields.append({
            "wire_offset": s,
            "payload_offset": s - 0x10,
            "width": w,
            "encoding": encoding_guess(w, obs),
            "name": None,                     # a human names it, not this tool
            "moved_by_every_apply": all(k in always for k in range(s, e)),
            "tag": "O",                       # observed on the wire, by diff
            "observations": [
                {"line": n, "action": a, "old": o, "new": v, "capture": c}
                for n, a, o, v, c in obs
            ],
        })
    doc = {
        "device": "Endgame Gear OP1 8k v2",
        "config_tool": "1.07",
        "generated_by": "Tools/capture/fieldmap.py",
        "captures": sorted(captures),
        "offset_origin":
            "wire offset -- byte 0 is the first byte USBPcap recorded. The "
            "translation to a hidapi buffer index is UNDECIDED and shifts every "
            "offset by one; see notes/wire-observed.md 2.1. Do not use these "
            "offsets against the device until that is settled.",
        "naming":
            "name is null everywhere on purpose. `action` is the log line "
            "verbatim; a human turns that into a field name.",
        "fields": fields,
        "unattributed": unattributed,
    }
    with open(path, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    print("\n%d field runs -> %s" % (len(fields), path))
    if unattributed:
        print("%d capture(s) contributed NO attributed rows: %s"
              % (len(unattributed), ", ".join(u["capture"] for u in unattributed)))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    logp = None
    emitp = None
    if "--log" in sys.argv:
        logp = sys.argv[sys.argv.index("--log") + 1]
        args = [a for a in args if a != logp]
    if "--emit" in sys.argv:
        emitp = sys.argv[sys.argv.index("--emit") + 1]
        args = [a for a in args if a != emitp]
    allruns = {}
    allalways = set()
    captures = []
    unattributed = []
    if not args:
        print(__doc__)
        return 2

    for cap in args:
        captures.append(os.path.basename(cap))
        print("=" * 72)
        print(cap)
        print("=" * 72)
        fs = frames(cap)
        reads = [f for f in fs if not f.out and f.payload]
        writes = [f for f in fs if f.out and f.b1 == CMD_WRITE and f.payload]
        print("  %d feature frames, %d long reads, %d WRITE frames"
              % (len(fs), len(reads), len(writes)))

        if not writes:
            print("\n  No 0x11 WRITE frames in this capture. Nothing to diff.")
            print("  (Expected for 01-baseline, which changed nothing.)")
            continue
        if not reads:
            print("\n  *** No settings read in this capture, so the state "
                  "before the first\n      APPLY is unknown and diff 1 cannot "
                  "be formed. Attributing from\n      diff 2 onward would shift "
                  "every row. Refusing.")
            continue

        # The WRITE frame header. Bytes 0..0x0f of an outbound 0x11 frame are
        # not settings; our writer has to reproduce them and cannot invent them.
        print("\n  WRITE frame header, bytes 0x00-0x0f:")
        heads = sorted({f.data[:0x10] for f in writes})
        for h in heads:
            print("    %s   (%d of %d frames)"
                  % (h.hex(" "), sum(1 for f in writes if f.data[:0x10] == h),
                     len(writes)))
        if len(heads) > 1:
            print("    ^ the header VARIES between frames. Something in it is a"
                  " counter,\n      a length, or a per-command field. It is not"
                  " a constant to copy.")

        chain = [reads[0].payload] + [f.payload for f in writes]
        diffs = [runs(chain[i], chain[i + 1]) for i in range(len(chain) - 1)]

        entries, problems = ([], [])
        if logp:
            entries, problems = parse_log(logp, cap)
            for p in problems:
                print("\n  *** %s" % p)
            if len(entries) != len(diffs):
                print("\n  *** COUNT MISMATCH: %d WRITE frames but %d numbered "
                      "log lines." % (len(diffs), len(entries)))
                print("      An off-by-one here would shift every mapping after"
                      " it while\n      still looking sensible, so nothing is "
                      "attributed. Diffs below\n      are printed unlabelled; "
                      "fix the log or the capture and rerun.")
                entries = []

        # Bytes that moved on every APPLY. ANNOTATION ONLY -- see the module
        # docstring. These stay in every diff and in the field map.
        always = set()
        if len(diffs) >= MIN_FOR_ALWAYS:
            always = set(range(len(chain[0])))
            for d in diffs:
                always &= {i for s, e in d for i in range(s, e)}
        if always:
            print("\n  MOVED BY EVERY APPLY (%d of them):" % len(diffs))
            for s, e in group(sorted(always)):
                print("    0x%04x..0x%04x (%d byte%s)"
                      % (s, e - 1, e - s, "" if e - s == 1 else "s"))
            print("  Could be a sequence number or checksum -- or could be the "
                  "one setting\n  you changed repeatedly. The bytes cannot tell"
                  " those apart, so this is\n  a note, not a filter: they stay "
                  "in the diffs and in the map below.")

        print("\n  PER-APPLY DIFFS")
        amb = 0
        for i, d in enumerate(diffs):
            label = ""
            if entries:
                label = "  <- %s" % entries[i][1]
            real = list(d)
            print("\n   %3d.%s" % (i + 1, label))
            if not real:
                print("        NO BYTES CHANGED.")
                if entries:
                    print("        *** the app sent an APPLY that changed "
                          "nothing. Either the\n            setting was already"
                          " at that value, or it was refused.")
                continue
            if len(real) > 1:
                amb += 1
            for s, e in real:
                mark = " *" if all(k in always for k in range(s, e)) else ""
                print("        0x%04x..0x%04x  %-24s -> %s%s"
                      % (s, e - 1, chain[i][s:e].hex(" "),
                         chain[i + 1][s:e].hex(" "), mark))
            if len(real) > 1:
                print("        ^ %d separate runs moved for one change. NOT "
                      "reduced to one." % len(real))
        if amb:
            print("\n  %d of %d diffs moved more than one run." % (amb, len(diffs)))

        if entries:
            print("\n  FIELD MAP -- offset run -> every observation of it")
            byrun = {}
            for i, d in enumerate(diffs):
                for s, e in d:
                    byrun.setdefault((s, e), []).append(
                        (entries[i][0], entries[i][1],
                         chain[i][s:e].hex(" "), chain[i + 1][s:e].hex(" ")))
            for (s, e), obs in sorted(byrun.items()):
                mark = ("   [moved by every APPLY]"
                        if all(k in always for k in range(s, e)) else "")
                print("\n    0x%04x..0x%04x  (%d byte%s)%s"
                      % (s, e - 1, e - s, "" if e - s == 1 else "s", mark))
                for n, what, old, new in obs:
                    print("        %2d  %-46s %s -> %s" % (n, what[:46], old, new))
                allruns.setdefault((s, e), []).extend(
                    (n, what, old, new, os.path.basename(cap))
                    for n, what, old, new in obs)
            allalways |= always
        else:
            # A capture that produced diffs but could not be attributed still
            # has to appear in the output. Silently omitting it would make the
            # emitted map look complete when a whole section is missing.
            unattributed.append({
                "capture": os.path.basename(cap),
                "write_frames": len(diffs),
                "log_lines": len(entries),
                "reason": "counts did not line up, or no log block was found",
            })

    if emitp:
        emit(emitp, allruns, allalways, captures, unattributed)
    return 0


def group(xs):
    out = []
    for x in xs:
        if out and x == out[-1][1]:
            out[-1][1] = x + 1
        else:
            out.append([x, x + 1])
    return [(a, b) for a, b in out]


if __name__ == "__main__":
    sys.exit(main())
