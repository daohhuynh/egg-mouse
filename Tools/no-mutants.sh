#!/bin/bash
# Is a planted mutant sitting in the source tree right now?
#
# WHY THIS EXISTS. `Tests/mutants.sh` edits the real files in place, rebuilds,
# runs the suite, and restores from a backup in an EXIT trap. That trap covers
# a normal exit and a Ctrl-C. It does not cover a SIGKILL, a machine that goes
# down, or the case this project actually hit twice on 2026-09-06: a `git add`
# issued from another window WHILE the run was in flight, which staged
#
#     std::vector<std::uint8_t> back(kLargeLen, 0);  // MUTANT
#     back[1] = kReady;
#     const int st = kReady;
#
# into WritePhase.cpp -- a firmware write that reports every block verified
# without reading a single one back. It was caught by eye both times. Eyes are
# not a control.
#
# Every mutant this suite plants carries the marker `// MUTANT`, and production
# code never does. So the check is one grep, and the point is that it is a grep
# and not a habit.
#
# Run it before any commit that touches Sources/. It is also a ctest entry --
# safe there because `mutants` is RUN_SERIAL, so no other test can be running
# while the tree is deliberately mutated.
set -u
cd "$(dirname "$0")/.."

rc=0

hits=$(grep -rn "MUTANT" Sources/ 2>/dev/null || true)
if [ -n "$hits" ]; then
    rc=1
    echo "PLANTED MUTANTS ARE STILL IN THE SOURCE TREE:"
    echo "$hits"
    echo
    echo "A mutation run is in flight, was interrupted, or one was staged by"
    echo "accident. DO NOT COMMIT until this is empty."
    echo
    echo "If mutants.sh is still running (check: pgrep -f mutants.sh), just"
    echo "WAIT -- its EXIT trap restores every file it touched."
    echo
    echo "If it is not running, revert ONLY the files listed above, e.g."
    echo "  git checkout -- Sources/EGGFlashCore/src/WritePhase.cpp"
    echo "NOT \`git checkout -- Sources/\`: mutants.sh restores from a mktemp"
    echo "backup that is gone once it dies, so a blanket checkout would also"
    echo "throw away whatever uncommitted work happens to be in that tree."
fi

# The SAME failure shape, one directory over. Tests/test_auditclaims.py grades
# the citation checker by writing a deliberately-defective claim to
# notes/zzz-planted-<pid>.md and deleting it in a `finally`. A `finally` is not
# a guarantee either: kill the test and the bad note stays, where it is both a
# commit waiting to happen and a permanent red on audit_claims_strict.
notes=$(ls notes/zzz-planted-*.md 2>/dev/null || true)
if [ -n "$notes" ]; then
    rc=1
    echo
    echo "PLANTED TEST NOTES ARE STILL IN notes/:"
    echo "$notes"
    echo
    echo "If Tests/test_auditclaims.py is running, WAIT. If it is not, these"
    echo "are safe to delete -- they are generated fixtures, never content:"
    echo "  rm notes/zzz-planted-*.md"
fi

[ $rc -ne 0 ] && exit 1
echo "no planted mutants in Sources/, no planted notes in notes/"
