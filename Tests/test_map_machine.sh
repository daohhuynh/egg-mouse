#!/bin/bash
# test_map_machine.sh -- `egg-config map --machine` is the GUI's source of truth
# for the button-mapping picker, so every name it emits must be one the CLI
# actually accepts.
#
# WHY THIS EXISTS. Until 2026-09-06 the app built its action menu by scraping
# the HUMAN listing, and an audit found it offering `e.g.`, `for`, `minus`,
# `grave` and `pagedown` -- continuation lines of two descriptions, read as
# actions. Five of twenty-four menu entries were guaranteed refusals, and the
# two actions that take an argument had nowhere to put one, so `key:` and
# `fixed-cpi:` bindings could not be made from the app at all.
#
# Nothing caught it because nothing ever asked "is every offered action real?".
# That is the question here, and it is asked by RUNNING each one rather than by
# comparing two lists that could drift together.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/egg-config
[ -x "$BIN" ] || { echo "build $BIN first"; exit 2; }

PASS=0; FAIL=0
check() {
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}

M=$("$BIN" map --machine 2>&1)

echo "egg-config map --machine -- the GUI's action list"
echo

nact=$(printf '%s\n' "$M" | grep -c '^ACTION')
[ "$nact" = 19 ]
check "exactly 19 actions, the closed set of notes/config-protocol.md 7.17.1 (got $nact)" $?

nbtn=$(printf '%s\n' "$M" | grep -c '^BUTTON')
[ "$nbtn" = 8 ]
check "all 8 button slots are listed, offered or not (got $nbtn)" $?

offered=$(printf '%s\n' "$M" | awk -F'\t' '$1=="BUTTON" && $3=="1"' | wc -l | tr -d ' ')
[ "$offered" = 6 ]
check "6 of them are offered -- the vendor's own page shows six rows (got $offered)" $?

printf '%s\n' "$M" | awk -F'\t' '$1=="BUTTON" && $3=="0" {print $2}' | sort | tr '\n' ' ' | grep -q "cpi-button left"
check "...and the two withheld are left and cpi-button" $?

# THE ASSERTION THE OLD SCRAPER WOULD HAVE FAILED. Every action name is run
# through the real CLI. `map <button> <action>` without --yes prints a plan and
# refuses to write; an action the parser does not know is rejected differently,
# and that difference is what this looks for.
# THE REJECTION PATTERN IS PROVEN LIVE FIRST. §6.2: a harness that cannot
# produce a bad result is not evidence -- and the first version of this test was
# exactly that. It matched on "unknown action", which the CLI never says (it
# says "`x` is not an action."), so three of the five entries the old scraper
# emitted -- minus, grave, pagedown -- would have passed BOTH this check and the
# identifier check below. The pattern is now asserted against known-bad input
# before it is trusted against real input.
REJECT='is not an action|needs an argument'
planted=0
for junk in "minus" "grave" "pagedown" "e.g." "for"; do
  o=$("$BIN" map right "$junk" 2>&1 || true)
  printf '%s' "$o" | grep -qE "$REJECT" || planted=$((planted+1))
done
[ "$planted" = 0 ]
check "the rejection pattern catches all 5 entries the old scraper emitted" $?

bad=""
while IFS=$'\t' read -r _ name _ arg; do
  case "$arg" in
    cpi) probe="$name:1600" ;;
    key) probe="$name:a" ;;
    *)   probe="$name" ;;
  esac
  out=$("$BIN" map right "$probe" 2>&1 || true)
  if printf '%s' "$out" | grep -qE "$REJECT"; then
    bad="$bad $probe"
  fi
done < <(printf '%s\n' "$M" | grep '^ACTION')
[ -z "$bad" ]
check "every action the GUI would offer is accepted by the CLI${bad:+ -- REJECTED:$bad}" $?

# And the inverse: the two that need an argument must be MARKED as needing one,
# or the app offers them bare and every selection is a refusal.
printf '%s\n' "$M" | grep -q "^ACTION	fixed-cpi	cpi	cpi"
check "fixed-cpi is marked as taking a CPI argument" $?
printf '%s\n' "$M" | grep -q "^ACTION	key	keyboard	key"
check "key is marked as taking a key argument" $?
noarg=$(printf '%s\n' "$M" | awk -F'\t' '$1=="ACTION" && $4=="none"' | wc -l | tr -d ' ')
[ "$noarg" = 17 ]
check "the other 17 take no argument (got $noarg)" $?

# Not one of these may look like prose. The five bad entries the scraper emitted
# were ordinary lowercase words, so a name check alone would not have caught
# them -- but a stray "e.g." would be caught here, and cheaply.
printf '%s\n' "$M" | grep '^ACTION' | awk -F'\t' '{print $2}' \
  | grep -qvE '^[a-z][a-z0-9-]*$' && bogus=1 || bogus=0
[ "$bogus" = 0 ]
check "every action name is a plain lowercase identifier" $?

nkey=$(printf '%s\n' "$M" | grep -c '^KEY')
[ "$nkey" -ge 40 ]
check "the key names come from the parser's own table (got $nkey)" $?
# Spot-check that a listed key really is accepted, so the list is not decorative.
out=$("$BIN" map right "key:$(printf '%s\n' "$M" | grep '^KEY' | head -1 | cut -f2)" 2>&1 || true)
! printf '%s' "$out" | grep -qE "$REJECT"
check "...and the first of them is accepted by the CLI" $?

# THE TWO LISTINGS MUST AGREE. `map --machine` builds the GUI's picker from
# kNamedKeys; `map` with no arguments is what a person reads. Until 2026-09-07
# the second was four hardcoded prose lines listing 27 of 51 names, unchanged
# since §7.32 added 21 more -- so `leftshift`, the key in the vendor's own
# KEYBOARD KEY screenshot, worked and could not be discovered from the tool.
# Every name the machine form emits must appear in the human form.
H=$("$BIN" map 2>&1)
missing=""
for k in $(printf '%s\n' "$M" | grep '^KEY' | cut -f2); do
  printf '%s' "$H" | grep -qE "(^|[ ,])$k([ ,.]|$)" || missing="$missing $k"
done
[ -z "$missing" ]
check "every --machine KEY name is in the human listing too (missing:${missing:-none})" $?

# ...and the ranges the table does NOT contain are still advertised, because
# they are parsed algorithmically and would otherwise vanish from the help the
# moment the list started being generated. That is the mistake this test caught
# in its own fix: the first generated version dropped a-z, 0-9, f1-f12, kp0-kp9.
for r in "a-z" "0-9" "f1-f12" "kp0-kp9"; do
  printf '%s' "$H" | grep -qF -- "$r"
  check "the human listing still advertises the $r range" $?
done

# A name that is NOT in the table must not appear as if it were offered.
printf '%s' "$H" | grep -qE "(^|[ ,])nosuchkey([ ,.]|$)"
check "the listing does not name a key the table lacks" $([ $? = 1 ] && echo 0 || echo 1)

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
