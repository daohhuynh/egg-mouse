#!/usr/bin/env python3
"""score.py -- check the derived predictions against what a capture observed.

WHY THIS IS A SEPARATE TOOL. CLAUDE.md §7.1 deleted a Ghidra extraction script
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


def score_one(doc, p):
    off = p["record_offset"]
    hits = observations_at(doc, off)
    rows, verdict = [], None

    relevant = [(f, o) for f, o in hits
                if p["match_log"].lower() in (o.get("action") or "").lower()]

    if not hits:
        return "UNTESTED", ["no observation in this capture touches record 0x%02x" % off], []
    if not relevant:
        return ("UNTESTED",
                ["record 0x%02x moved, but on no line mentioning %r --"
                 " the capture does not test this" % (off, p["match_log"])], [])

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
