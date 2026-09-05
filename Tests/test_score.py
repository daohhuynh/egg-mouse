#!/usr/bin/env python3
"""test_score.py -- the scorer must be able to say REFUTED.

§6.2: "A harness that cannot produce a bad result is not evidence." A scorer
that only ever confirms is worse than none, because it launders a derivation
into an observation. So the cases here are weighted toward the ways it should
FAIL to confirm: a wrong value, a capture that never touched the field, and a
capture that moved the byte on unrelated lines.
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCORE = os.path.join(ROOT, "Tools", "capture", "score.py")

FAILS = []
RAN = []


def check(name, cond, out=""):
    RAN.append(name)
    print(("  PASS  " if cond else "  FAIL  ") + name)
    if not cond:
        FAILS.append(name)
        print("\n".join("        | " + l for l in out.split("\n")[:30]))


def doc(fields):
    return {"device": "x", "fields": fields}


def field(payload_offset, width, obs):
    return {"wire_offset": payload_offset + 0x10,
            "payload_offset": payload_offset, "width": width,
            "encoding": "u8", "name": None, "tag": "O",
            "moved_by_every_apply": False, "in_vendor_record": True,
            "observations": obs}


def ob(line, action, old, new):
    return {"line": line, "action": action, "old": old, "new": new,
            "capture": "02-basic.pcapng"}


def norm(s):
    """Collapse runs of spaces so assertions do not depend on column padding."""
    return " ".join(s.split())


def run(d):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(d, f)
        path = f.name
    r = subprocess.run([sys.executable, SCORE, path],
                       capture_output=True, text=True)
    os.unlink(path)
    return norm(r.stdout + r.stderr), r.returncode


print("score.py\n")

# The real prediction: record 0x05 = 8000/rate.
good = [ob(1, "polling 125", "01", "40"), ob(2, "polling 250", "40", "20"),
        ob(3, "polling 500", "20", "10"), ob(4, "polling 1000", "10", "08"),
        ob(5, "polling 2000", "08", "04"), ob(6, "polling 4000", "04", "02"),
        ob(7, "polling 8000", "02", "01")]
out, rc = run(doc([field(0x05, 1, good)]))
check("a correct polling sweep is CONFIRMED",
      "CONFIRMED polling-divisor" in out and "REFUTED" not in out, out)
check("and it still warns that nothing was refuted",
      "reason for suspicion" in out, out)

# One wrong value must refute the whole prediction, not be averaged away.
bad = list(good)
bad[3] = ob(4, "polling 1000", "10", "09")     # 0x09, not 0x08
out, rc = run(doc([field(0x05, 1, bad)]))
check("a single wrong value REFUTES the prediction",
      "REFUTED polling-divisor" in out and rc == 1, out)
check("and the refutation prints its REFUTED IF and the §7 tiebreaker",
      "REFUTED IF:" in out and "DEVICE is the tiebreaker" in out, out)
check("a refutation exits non-zero so a script cannot ignore it", rc == 1, out)

# A capture that never touched the byte must be UNTESTED, never CONFIRMED.
out, rc = run(doc([field(0x20, 1, [ob(1, "angle snapping", "00", "01")])]))
check("a capture that never moves the byte is UNTESTED, not CONFIRMED",
      "UNTESTED polling-divisor" in out and "CONFIRMED" not in out, out)

# The byte moved, but on lines that say nothing about polling. Confirming from
# that would be reading the answer off an unrelated change.
out, rc = run(doc([field(0x05, 1, [ob(1, "angle snapping", "01", "40")])]))
check("the byte moving on an unrelated line does not confirm",
      "UNTESTED polling-divisor" in out, out)

# A two-byte run must be indexed, not assumed to start at the prediction.
wide = [ob(1, "polling 1000", "ff 10", "ff 08")]
out, rc = run(doc([field(0x04, 2, wide)]))
check("a wide run is indexed to the predicted byte",
      "CONFIRMED polling-divisor" in out, out)

# ---- kind "bit": one named bit flips, the rest of the byte holds ----------
out, rc = run(doc([field(0x06, 1, [ob(1, "slamclick filter (clicked once)",
                                      "00", "01")])]))
check("a clean bit-0 flip CONFIRMS the bitfield prediction",
      "CONFIRMED slamclick-bit0" in out, out)

# The byte moved and bit 0 did change -- but so did bit 2. That is the whole
# point of the mask check: a whole-byte rewrite must not pass as a bit flip.
out, rc = run(doc([field(0x06, 1, [ob(1, "slamclick filter (clicked once)",
                                      "00", "05")])]))
check("a bit flip that also disturbs other bits is REFUTED",
      "REFUTED slamclick-bit0" in out and rc == 1, out)

# Right byte, wrong bit. 0x10 is Motion Jitter's bit, not Slamclick's.
out, rc = run(doc([field(0x06, 1, [ob(1, "slamclick filter (clicked once)",
                                      "00", "10")])]))
check("the wrong bit moving is REFUTED, not CONFIRMED",
      "REFUTED slamclick-bit0" in out, out)

# ---- kind "toggle" -------------------------------------------------------
out, rc = run(doc([field(0x0a, 1, [ob(8, "angle snapping (clicked once)",
                                      "00", "01")])]))
check("a 0->1 toggle CONFIRMS", "CONFIRMED angle-snapping" in out, out)

out, rc = run(doc([field(0x0a, 1, [ob(8, "angle snapping (clicked once)",
                                      "00", "07")])]))
check("a toggle to something outside {0,1} is REFUTED",
      "REFUTED angle-snapping" in out, out)

# ---- kind "masked": two combos sharing one byte --------------------------
ds = [ob(8, "downshift -> first", "00", "08"),     # item 1 -> field 2 (0b10<<2)
      ob(9, "downshift -> second", "08", "0c"),    # item 2 -> field 3
      ob(10, "downshift -> third", "0c", "04"),    # item 3 -> field 1
      ob(11, "downshift -> fourth", "04", "00")]   # item 4 -> field 0
out, rc = run(doc([field(0x0b, 1, ds)]))
check("the downshift remap CONFIRMS when each line lands where derived",
      "CONFIRMED cpi-downshift-remap" in out, out)

# Same lines, but the combo index is stored directly (0,1,2,3) instead of
# through the jump-table remap. This is the single most likely way for the
# derivation to be wrong, so it must refute.
ds_direct = [ob(8, "downshift -> first", "00", "00"),
             ob(9, "downshift -> second", "00", "04"),
             ob(10, "downshift -> third", "04", "08"),
             ob(11, "downshift -> fourth", "08", "0c")]
out, rc = run(doc([field(0x0b, 1, ds_direct)]))
check("an identity remap REFUTES the downshift prediction",
      "REFUTED cpi-downshift-remap" in out, out)

# A downshift line that also moves the smoothing bits refutes the claim that
# the two combos own separate halves of the byte.
ds_bleed = [ob(8, "downshift -> first", "00", "0a")]   # bit 1 moved too
out, rc = run(doc([field(0x0b, 1, ds_bleed)]))
check("bits outside the mask moving REFUTES, and says so",
      "REFUTED cpi-downshift-remap" in out
      and "BITS OUTSIDE THE MASK MOVED" in out, out)

print("\n%d/%d passed" % (len(RAN) - len(FAILS), len(RAN)))
sys.exit(1 if FAILS else 0)
