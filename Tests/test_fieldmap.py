#!/usr/bin/env python3
"""test_fieldmap.py -- prove fieldmap.py's REFUSALS actually fire.

CLAUDE.md 6.2: "A harness that cannot produce a bad result is not evidence."
The valuable behaviour in fieldmap.py is not that it produces a field map. It is
that it declines to produce one when the evidence cannot support it. A refusal
path that has never been seen to trigger is an untested branch guarding the most
expensive mistake in the project, so each one is driven here with synthetic
frames rather than waited on.

Synthetic, not captured: the point is to manufacture the failures a real capture
is unlikely to contain on demand -- a miscounted log, a setting that moves two
byte runs at once, an APPLY that changes nothing, a checksum.

    python3 Tests/test_fieldmap.py
"""
import io
import os
import sys
import contextlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Tools", "capture"))
import records
import fieldmap

WIRE = 1040
HDR = bytes([0xA1, 0x11]) + bytes(14)          # plausible write header
RHDR = bytes([0xA1, 0x01]) + bytes(14)


def frame(payload, out, b1):
    f = records.Frame()
    f.src, f.seq, f.ts = "synthetic.pcapng", 0, 0.0
    f.out, f.report_id, f.b1 = out, 0xA1, b1
    f.wlen = 1041
    f.data = (HDR if out else RHDR) + payload
    return f


def build(states):
    """states[0] is the record read at startup; the rest are APPLY frames."""
    fs = [frame(states[0], False, 0x01)]
    for s in states[1:]:
        fs.append(frame(s, True, 0x11))
    return fs


def run(states, log_text, name="02-basic.pcapng"):
    fs = build(states)
    real = records.frames
    records.frames = lambda p: fs
    fieldmap.frames = lambda p: fs
    logp = os.path.join(HERE, "_tmp_log.txt")
    open(logp, "w").write(log_text)
    buf = io.StringIO()
    try:
        argv = sys.argv
        sys.argv = ["fieldmap.py", name, "--log", logp]
        with contextlib.redirect_stdout(buf):
            fieldmap.main()
    finally:
        sys.argv = argv
        records.frames = real
        fieldmap.frames = real
        os.unlink(logp)
    return buf.getvalue()


def base():
    return bytearray(WIRE)


FAILS = []
RAN = []


def check(name, cond, out=""):
    RAN.append(name)
    print(("  PASS  " if cond else "  FAIL  ") + name)
    if not cond:
        FAILS.append(name)
        print("\n".join("        | " + l for l in out.split("\n")[:40]))


LOG3 = "--- 02-basic.pcapng --- Basic\n 1  polling 125\n 2  polling 250\n 3  polling 500\n"

print("fieldmap.py refusal paths\n")

# 1. Happy path: one byte moves per APPLY, counts line up, map is produced.
s0 = base()
s1 = base(); s1[0x20] = 1
s2 = base(); s2[0x20] = 2
s3 = base(); s3[0x20] = 3
out = run([bytes(s0), bytes(s1), bytes(s2), bytes(s3)], LOG3)
check("happy path attributes three observations to 0x0020",
      "FIELD MAP" in out and out.count("0x0020..0x0020") >= 2
      and "polling 500" in out, out)

# 2. Count mismatch must refuse to attribute AT ALL.
out = run([bytes(s0), bytes(s1), bytes(s2), bytes(s3)],
          "--- 02-basic.pcapng ---\n 1  polling 125\n 2  polling 250\n")
check("count mismatch refuses the whole field map",
      "COUNT MISMATCH" in out and "FIELD MAP" not in out, out)

# 2b. A NOT PRESENT line has no APPLY behind it. It must be dropped from the
# attribution and the REST must still line up -- because the vendor tool greys
# APPLY out until something changes, so this log shape is the normal one, not
# an error. Getting this wrong shifts every later label by one.
out = run([bytes(s0), bytes(s1), bytes(s2), bytes(s3)],
          "--- 02-basic.pcapng ---\n 1  polling 125\n 2  polling 250\n"
          " 3  ripple control -- NOT PRESENT on the page\n 4  polling 500\n")
check("a NOT PRESENT line is dropped and the rest still attribute correctly",
      "NO APPLY BEHIND THESE LOG LINES" in out
      and "ripple control" in out
      and "COUNT MISMATCH" not in out
      and "polling 500" in out and "FIELD MAP" in out, out)

# 2c. And the drop must not become a way to make any log fit. Drop one line and
# the counts STILL disagree -> refuse, exactly as before.
out = run([bytes(s0), bytes(s1), bytes(s2), bytes(s3)],
          "--- 02-basic.pcapng ---\n 1  polling 125\n"
          " 2  ripple control -- NOT PRESENT\n")
check("dropping a line does not rescue a log that still miscounts",
      "COUNT MISMATCH" in out and "FIELD MAP" not in out, out)

# 2d. A byte moving OUTSIDE the vendor's 115-byte record must be announced --
# and must still appear in the map, because "the vendor never writes there" is a
# claim about the vendor, not the device (§1.2a).
o0 = base()
o1 = base(); o1[0x10 + 0x72] = 7      # last byte of the record: inside
o2 = base(); o2[0x10 + 0x72] = 7; o2[0x10 + 0x73] = 9   # one past it: outside
out = run([bytes(o0), bytes(o1), bytes(o2)],
          "--- 02-basic.pcapng ---\n 1  inside the record\n 2  one byte past it\n")
check("a diff past the vendor record is announced but still mapped",
      "OUTSIDE THE VENDOR RECORD" in out and "0x0083..0x0083" in out
      and "FIELD MAP" in out and "one byte past it" in out, out)

# 3. A setting that moves two separate runs is reported as two, never reduced.
t1 = base(); t1[0x20] = 1; t1[0x40] = 9
out = run([bytes(s0), bytes(t1)],
          "--- 02-basic.pcapng ---\n 1  something\n")
check("two-run diff is reported in full, not reduced",
      "2 separate runs moved" in out
      and "0x0020..0x0020" in out and "0x0040..0x0040" in out, out)

# 3b. REGRESSION. A setting changed over and over moves the same byte every
# time. An earlier rule called that a checksum and deleted it from the map.
r = [base()]
for k in range(1, 8):
    x = base(); x[0x20] = k; r.append(x)
out = run([bytes(x) for x in r],
          "--- 02-basic.pcapng ---\n" + "".join(
              " %d  polling step %d\n" % (k, k) for k in range(1, 8)))
check("a repeatedly-changed setting survives into the field map",
      "FIELD MAP" in out and out.split("FIELD MAP")[1].count("0x0020") > 0, out)

# 4. A byte moving on EVERY apply is annotated -- and still mapped. Needs at
# least MIN_FOR_ALWAYS diffs before the annotation means anything, so four.
cs = [base()]
for k in range(1, 5):
    x = base()
    x[0x20 + k] = 1           # a different setting each time
    x[0x100] = k * 7          # moves every time -> checksum-shaped
    cs.append(x)
out = run([bytes(x) for x in cs],
          "--- 02-basic.pcapng ---\n" + "".join(
              " %d  setting %d\n" % (k, k) for k in range(1, 5)))
check("byte moving on every APPLY is annotated but NEVER dropped",
      "MOVED BY EVERY APPLY" in out and "0x0100..0x0100" in out
      and out.split("FIELD MAP")[1].count("0x0100") > 0, out)

# 5. An APPLY that changes nothing is flagged, not silently mapped.
out = run([bytes(s0), bytes(s1), bytes(s1), bytes(s3)], LOG3)
check("no-op APPLY is flagged as already-set-or-refused",
      "NO BYTES CHANGED" in out and "changed nothing" in out, out)

# 6. No startup read -> diff 1 cannot be formed -> refuse rather than shift.
fs = [frame(bytes(s1), True, 0x11), frame(bytes(s2), True, 0x11)]
records.frames = fieldmap.frames = lambda p: fs
buf = io.StringIO()
argv = sys.argv
sys.argv = ["fieldmap.py", "02-basic.pcapng"]
with contextlib.redirect_stdout(buf):
    fieldmap.main()
sys.argv = argv
check("missing startup read refuses instead of shifting every row",
      "Refusing" in buf.getvalue(), buf.getvalue())

# 7. A varying write header must be announced, not treated as a constant.
fs = build([bytes(s0), bytes(s1), bytes(s2)])
fs[2].data = bytes([0xA1, 0x11, 0x02]) + bytes(13) + fs[2].data[16:]
records.frames = fieldmap.frames = lambda p: fs
buf = io.StringIO()
argv = sys.argv
sys.argv = ["fieldmap.py", "02-basic.pcapng"]
with contextlib.redirect_stdout(buf):
    fieldmap.main()
sys.argv = argv
check("varying write header is announced",
      "header VARIES" in buf.getvalue(), buf.getvalue())

# 8. --emit produces the deliverable, and an unattributable capture still
#    appears in it. A map that silently omits a whole section reads as complete
#    when it is not.
import json
out = os.path.join(HERE, "_tmp_map.json")
fs = build([bytes(s0), bytes(s1), bytes(s2), bytes(s3)])
records.frames = fieldmap.frames = lambda p: fs
logp = os.path.join(HERE, "_tmp_log2.txt")
open(logp, "w").write(LOG3)
argv = sys.argv
sys.argv = ["fieldmap.py", "02-basic.pcapng", "--log", logp, "--emit", out]
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    fieldmap.main()
sys.argv = argv
doc = json.load(open(out))
check("--emit writes a field map with one run and three observations",
      len(doc["fields"]) == 1 and len(doc["fields"][0]["observations"]) == 3
      and doc["fields"][0]["wire_offset"] == 0x20
      and doc["fields"][0]["payload_offset"] == 0x10, json.dumps(doc)[:400])
check("--emit refuses to name fields",
      all(f["name"] is None for f in doc["fields"]))
check("--emit records the offset-origin caveat",
      "UNDECIDED" in doc["offset_origin"])

# now the same capture with a MISCOUNTED log: nothing may be attributed, and
# the capture must still be listed as unattributed rather than vanish.
open(logp, "w").write("--- 02-basic.pcapng ---\n 1  only one line\n")
sys.argv = ["fieldmap.py", "02-basic.pcapng", "--log", logp, "--emit", out]
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    fieldmap.main()
sys.argv = argv
doc = json.load(open(out))
check("a miscounted capture yields no fields but IS listed as unattributed",
      doc["fields"] == [] and len(doc["unattributed"]) == 1
      and doc["unattributed"][0]["write_frames"] == 3, json.dumps(doc)[:400])
os.unlink(out); os.unlink(logp)

# Counted, not hardcoded. A literal total silently stops tracking the moment a
# check is added, and reports a number that is no longer about this file.
print("\n%d/%d passed" % (len(RAN) - len(FAILS), len(RAN)))
sys.exit(1 if FAILS else 0)
