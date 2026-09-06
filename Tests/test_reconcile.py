#!/usr/bin/env python3
"""test_reconcile.py -- can the scoreboard reconciler produce a bad result?

CLAUDE.md §6.2: "A harness that cannot produce a bad result is not evidence."
reconcile_scores.py passed on its first clean run, which is the shape §6.2 says
to distrust. So each case here plants ONE defect of a kind the scoreboard has
actually had -- a quoted denominator, a slug that resolves to nothing, one
prediction scored twice, a second tally in the prose -- and requires the tool to
name it. A plant that goes unnoticed is a hole in the tool, not a pass.

The plants are applied to COPIES in a temp tree; the real notes are never
written to.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOL = os.path.join("Tools", "notes", "reconcile_scores.py")
SCORES = os.path.join("notes", "prediction-scores.md")
REGISTER = os.path.join("notes", "wire-predictions.md")

FAILS = []


def ok(name, cond, detail=""):
    print(("  PASS  " if cond else "  FAIL  ") + name +
          (("  --  " + detail) if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


def run_in(tree):
    r = subprocess.run([sys.executable, os.path.join(tree, TOOL)],
                       capture_output=True, text=True, cwd=tree)
    return r.returncode, r.stdout + r.stderr


def tree_with(edits):
    """A copy of the repo's notes + tool, with `edits` applied.

    edits: {relative path: [(old, new), ...]}
    """
    d = tempfile.mkdtemp(prefix="reconcile-")
    for rel in (TOOL, SCORES, REGISTER):
        dst = os.path.join(d, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(ROOT, rel), dst)
    # The tool checks that own-file scorings name a file that exists.
    for f in ("prediction-factory-reset.md", "prediction-restore.md",
              "prediction-read-firmware.md"):
        open(os.path.join(d, "notes", f), "w").write("placeholder\n")
    for rel, subs in edits.items():
        p = os.path.join(d, rel)
        s = open(p).read()
        for old, new in subs:
            assert old in s, "plant did not apply: %r in %s" % (old[:60], rel)
            s = s.replace(old, new, 1)
        open(p, "w").write(s)
    return d


def expect_failure(name, edits, wants):
    d = tree_with(edits)
    try:
        rc, out = run_in(d)
        if rc == 0:
            ok(name, False, "the tool passed a tree with a planted defect")
            return
        missed = [w for w in wants if w not in out]
        ok(name, not missed,
           "exited 1 but did not mention " + repr(missed) + "\n" + out)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    print("reconcile_scores.py -- planted defects")

    # 0. The control. Without it, every plant below could be passing because
    #    the tool fails on everything.
    d = tree_with({})
    try:
        rc, out = run_in(d)
        ok("an unmodified tree reconciles", rc == 0, out)
    finally:
        shutil.rmtree(d, ignore_errors=True)

    # 1. The original defect: a denominator that disagrees with the register.
    #
    # The plant is written as `| 271 |` -> `| 266 |` rather than as the whole
    # tally row. The row's counts change every time a prediction is scored, and
    # the first version hardcoded them -- so scoring #24 on 2026-09-06 made this
    # plant stop applying, and `expect_failure` correctly refused to grade a
    # mutation that had not happened. A plant has to be anchored to the thing it
    # is testing (the denominator) and not to unrelated numbers beside it.
    expect_failure(
        "a quoted denominator that no longer matches the register",
        {SCORES: [("| 0 | 2 | 271 |", "| 0 | 2 | 266 |")]},
        ["tally row", "271"])

    # 2. A verdict changed without the table following it.
    expect_failure(
        "a verdict changed but the tally not updated",
        {SCORES: [("### #20 `cfg-basic-page-hidden-controls` — **CONFIRMED**",
                   "### #20 `cfg-basic-page-hidden-controls` — **REFUTED**")]},
        ["tally row"])

    # 3. A slug that resolves to nothing -- the seven-slug gap, re-planted.
    expect_failure(
        "a scored slug that is in neither the register nor the declarations",
        {SCORES: [("### CONFIRMED — `led-liftoff-inverted` semantics",
                   "### CONFIRMED — `led-liftoff-inverted-typo` semantics")]},
        ["neither a register slug"])

    # 4. One prediction scored twice under two names. This is the failure the
    #    whole declaration table exists to make impossible, and it is silent:
    #    both entries look correct on their own.
    expect_failure(
        "one prediction scored twice, under a name and its alias",
        {SCORES: [("### REFUTED — `rgb-block-tag-is-cpi-stage`",
                   "### CONFIRMED — `upd-idle-state`")]},
        ["is scored 2 times"]),

    # 5. An alias pointing at a slug that is not in the register either.
    expect_failure(
        "an alias that resolves to a non-existent register slug",
        {SCORES: [("ALIAS OF: `upd-idle-state`", "ALIAS OF: `upd-idle-statue`")]},
        ["is not a register slug either"])

    # 6. A second tally written into the prose -- how three of the four strays
    #    got in. Each looked local and harmless where it was written.
    expect_failure(
        "a second tally written into the prose",
        {SCORES: [("Also note the register numbers",
                   "Total so far: **CONFIRMED 6, REFUTED 1**.\n\n"
                   "Also note the register numbers")]},
        ["a second tally is written into the prose"])

    # 7. The register itself malformed. If the four counts disagree, no tally
    #    computed from it means anything and the tool must say so first.
    expect_failure(
        "a register entry missing its REFUTED IF",
        {REGISTER: [("- **REFUTED IF:** Ticking the box makes report byte 0x18",
                     "- **REFUTED-IF:** Ticking the box makes report byte 0x18")]},
        ["four register counts disagree"])

    # 8. An own-file scoring naming a file that is not there. A filename is a
    #    resolvable name only while the file exists.
    expect_failure(
        "an own-file scoring whose file does not exist",
        {SCORES: [("`notes/prediction-restore.md` stage A",
                   "`notes/prediction-restore-v2.md` stage A")]},
        ["that file does not exist"])

    print("\n%s (%d failure%s)" % ("FAILED" if FAILS else "all passed",
                                   len(FAILS), "" if len(FAILS) == 1 else "s"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
