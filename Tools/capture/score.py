#!/usr/bin/env python3
"""score.py -- check the derived predictions against what a capture observed.

WHY THIS IS A SEPARATE TOOL. engineering-rules.md §7.1 deleted a Ghidra extraction script
because it "scored functions for specific hardcoded constants and
pattern-matched a specific command shape, so it carried the prior conclusions
inside it and anyone reading it inherited them". A differ that knows which byte
is the polling rate has the same defect: it will find that byte.

So `fieldmap.py` observes and names nothing, `predictions.json` states what the
binary says without seeing a capture, and this file is the only place the two
are compared. Either can be read alone without learning the other's answer.

WHAT IT DOES NOT DO. It never edits either input, never "adjusts" a prediction
to fit, and reports REFUTED as loudly as CONFIRMED -- an area with no
refutations deserves suspicion, not celebration (§6.2).

  python3 Tools/capture/score.py settings-1.07.json
"""
import json
import re
import sys
import os


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def observations_at(doc, offset):
    """Every observation whose run covers this record offset."""
    out = []
    for fld in doc.get("fields", []):
        po = fld.get("payload_offset")
        if po is None:
            continue
        if po <= offset < po + fld.get("width", 1):
            for ob in fld.get("observations", []):
                out.append((fld, ob))
    return out


def byte_at(hexstr, idx):
    """One byte out of fieldmap's space-separated hex run, or None."""
    if not isinstance(hexstr, str):
        return None
    parts = hexstr.split()
    if not (0 <= idx < len(parts)):
        return None
    try:
        return int(parts[idx], 16)
    except ValueError:
        return None


def number_in(text):
    m = re.search(r"(-?\d+)", text or "")
    return m.group(1) if m else None


def score_bitwise(p, relevant, off):
    """kind "bit" / "toggle": the log line carries no number, so the claim is
    about the SHAPE of the change rather than a value. "bit" says one named bit
    of one byte flips and every other bit of that byte holds; "toggle" says the
    whole byte flips between 0 and 1. Both are refutable by a capture in which
    the byte does not move at all, which is the failure mode that matters --
    a checkbox we mapped to the wrong byte."""
    rows, ok, bad = [], 0, 0
    for fld, ob in relevant:
        idx = off - fld["payload_offset"]
        o, n = byte_at(ob.get("old"), idx), byte_at(ob.get("new"), idx)
        if o is None or n is None:
            rows.append("  ?  line %s %-28s -> record 0x%02x not in this run"
                        % (ob.get("line"), ob.get("action"), off))
            continue
        if p["kind"] == "bit":
            m = 1 << p["bit"]
            good = bool((o ^ n) == m)
            what = ("bit %d flipped %d->%d, rest of the byte held"
                    % (p["bit"], (o & m) != 0, (n & m) != 0)) if good else (
                    "0x%02x -> 0x%02x, xor 0x%02x, wanted xor 0x%02x"
                    % (o, n, o ^ n, m))
        else:
            good = (o != n) and o in (0, 1) and n in (0, 1)
            what = "%d -> %d" % (o, n) if good else (
                   "0x%02x -> 0x%02x, not a 0/1 toggle" % (o, n))
        ok, bad = ok + bool(good), bad + (not good)
        rows.append("  %s  line %s %-28s -> %s"
                    % ("ok" if good else "XX", ob.get("line"),
                       ob.get("action"), what))
    return ("REFUTED" if bad else "CONFIRMED" if ok else "UNTESTED"), rows


def score_masked(p, relevant, off):
    """kind "masked": a sub-field of one byte, keyed by LOG LINE NUMBER rather
    than by a number in the text -- combo items are named by position, not by
    value, so there is no number to extract. `mask` and `shift` say which bits;
    `expect_by_line` says what each numbered line should put there.

    This also checks the bits OUTSIDE the mask held still, because the claim
    being tested is as much "these two combos share a byte and each owns its own
    nibble" as it is "the remap is 0->2, 1->0, 2->1"."""
    rows, ok, bad = [], 0, 0
    mask, sh = p["mask"], p.get("shift", 0)
    for fld, ob in relevant:
        idx = off - fld["payload_offset"]
        o, n = byte_at(ob.get("old"), idx), byte_at(ob.get("new"), idx)
        line = str(ob.get("line"))
        want = p["expect_by_line"].get(line)
        if o is None or n is None or want is None:
            rows.append("  ?  line %s %-28s -> no prediction for this line"
                        % (line, ob.get("action")))
            continue
        got = (n & mask) >> sh
        held = (o & ~mask & 0xFF) == (n & ~mask & 0xFF)
        good = (got == want) and held
        ok, bad = ok + bool(good), bad + (not good)
        rows.append("  %s  line %s %-28s -> field %d, predicted %d%s"
                    % ("ok" if good else "XX", line, ob.get("action"), got, want,
                       "" if held else
                       "; BITS OUTSIDE THE MASK MOVED 0x%02x->0x%02x" % (o, n)))
    return ("REFUTED" if bad else "CONFIRMED" if ok else "UNTESTED"), rows


def score_somewhere_in(doc, p):
    """kind "somewhere_in": we know which RECORD RANGE a field lives in but not
    which byte of it. The claim is that ONE offset in that range takes the
    predicted value on every listed line -- the same offset each time.

    That last clause is what makes this worth scoring. "Some byte somewhere took
    0x01" is nearly unfalsifiable over a seven-byte range; "one fixed offset took
    0x00 then 0x01 then 0x03, in that order, on those three lines" is not. On
    success the offset is reported, which is the thing we actually wanted."""
    lo, hi = p["record_first"], p["record_last"]
    want = p["expect_by_line"]
    # ABSOLUTE record state, not the diff. A diff only reports bytes that moved,
    # so a byte that already held the predicted value reads as absent and the
    # claim comes back REFUTED for want of evidence rather than against it --
    # which is exactly what happened here first time round. The states list is
    # the full record after each write, so every offset in the range has a value
    # on every line. This gives the test MORE to fail against, not less: a wrong
    # offset that used to be merely unreported is now positively contradicted.
    seen = {}
    for st in doc.get("states", []):
        if p["match_log"].lower() in (st.get("action") or "").lower():
            seen[str(st.get("line"))] = st
    lines = [l for l in want if l in seen]
    if not lines:
        return "UNTESTED", ["no observation in records 0x%02x-0x%02x on any line"
                            " mentioning %r" % (lo, hi, p["match_log"])]
    hits = []
    for off in range(lo, hi + 1):
        if all(byte_at(seen[l].get("record"), off) == want[l] for l in lines):
            hits.append(off)
    rows = ["  lines used: %s   (of %d predicted)" % (", ".join(sorted(lines)), len(want))]
    if len(lines) < len(want):
        rows.append("  ?  only %d of %d lines are in this capture -- a partial test"
                    % (len(lines), len(want)))
    for l in sorted(lines):
        st = seen[l]
        run = " ".join("%02x" % byte_at(st.get("record"), o)
                       for o in range(lo, hi + 1))
        rows.append("     line %-3s %-26s range reads %s, wanted %02x somewhere"
                    % (l, st.get("action"), run, want[l]))
    if hits:
        rows.insert(0, "  ok  a single offset satisfies every line: %s"
                       % ", ".join("record 0x%02x" % h for h in hits))
        return "CONFIRMED", rows
    rows.insert(0, "  XX  NO single offset in 0x%02x-0x%02x takes the predicted"
                   " value on every line" % (lo, hi))
    return "REFUTED", rows


def score_one(doc, p):
    if p.get("kind") == "somewhere_in":
        v, rows = score_somewhere_in(doc, p)
        return v, rows, []
    off = p["record_offset"]
    hits = observations_at(doc, off)
    rows, verdict = [], None

    # A prediction with no match_log is keyed by line number instead (the
    # masked ones -- combo items are named by position, so there is no word to
    # match on). Then every observation at this offset is in scope and
    # expect_by_line does the selecting.
    key = p.get("match_log")
    relevant = hits if not key else [
        (f, o) for f, o in hits if key.lower() in (o.get("action") or "").lower()]

    if not hits:
        return "UNTESTED", ["no observation in this capture touches record 0x%02x" % off], []
    if not relevant:
        return ("UNTESTED",
                ["record 0x%02x moved, but on no line mentioning %r --"
                 " the capture does not test this" % (off, p["match_log"])], [])

    kind = p.get("kind", "value")
    if kind == "somewhere_in":
        v, rows = score_somewhere_in(doc, p)
        return v, rows, []
    if kind in ("bit", "toggle"):
        v, rows = score_bitwise(p, relevant, off)
        return v, rows, []
    if kind == "masked":
        v, rows = score_masked(p, relevant, off)
        return v, rows, []

    ok = bad = 0
    for fld, ob in relevant:
        key = number_in(ob.get("action"))
        want = p["expect_values"].get(key)
        # fieldmap emits a run's value as a hex string ("08", or "90 01" for a
        # two-byte run), so pick the byte this prediction is actually about
        # rather than assuming the run is one byte wide.
        got = byte_at(ob.get("new"), off - fld["payload_offset"])
        if want is None:
            rows.append("  ?  line %s %-28s -> %s   (no prediction for %r)"
                        % (ob.get("line"), ob.get("action"), got, key))
            continue
        good = (got == want)
        ok, bad = ok + bool(good), bad + (not good)
        rows.append("  %s  line %s %-28s -> got %s, predicted %s"
                    % ("ok" if good else "XX", ob.get("line"),
                       ob.get("action"), got, want))
    if bad:
        verdict = "REFUTED"
    elif ok:
        verdict = "CONFIRMED"
    else:
        verdict = "UNTESTED"
    return verdict, rows, []


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    doc = load(sys.argv[1])
    preds = load(os.path.join(here, "predictions.json"))["predictions"]

    print("=" * 72)
    print("scoring %d derived predictions against %s" % (len(preds), sys.argv[1]))
    print("=" * 72)
    tally = {}
    for p in preds:
        verdict, rows, _ = score_one(doc, p)
        tally[verdict] = tally.get(verdict, 0) + 1
        print("\n%-10s %s" % (verdict, p["id"]))
        print("           %s" % p["claim"])
        print("           cite: %s" % p["cite"])
        for r in rows:
            print("         " + r)
        if verdict == "REFUTED":
            print("           REFUTED IF: %s" % p["refuted_if"])
            print("           ^ the derivation is wrong, the capture is"
                  " mislabelled, or the\n             behaviour is"
                  " version-dependent. The DEVICE is the tiebreaker (§7).")

    print("\n" + "-" * 72)
    print("  ".join("%s %d" % kv for kv in sorted(tally.items())))
    if tally.get("REFUTED", 0) == 0 and tally.get("CONFIRMED", 0):
        print("\nNo refutations. §6.2: that is a reason for suspicion, not"
              " celebration --\ncheck that the capture actually exercised these"
              " fields and that each\nprediction could have failed.")
    return 1 if tally.get("REFUTED") else 0


if __name__ == "__main__":
    sys.exit(main())
