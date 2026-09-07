#!/bin/bash
# test_show.sh -- `egg-config show` decoded against Endgame's own screenshots.
#
# WHY THIS EXISTS, and why it is not a self-consistency check.
#
# test_config.cpp holds every decoder to its own encoder: for each field, every
# value `set` accepts must decode back to the same value. That catches a
# decoder that drifted from its encoder. It CANNOT catch a pair that agree with
# each other and both misdescribe the mouse -- a polling divisor read as a
# frequency the wrong way round would round-trip perfectly.
#
# So this test anchors the whole decoder to something outside the code:
# `frames/01-baseline-003-in-01.bin`, the device's own A1 12 reply from the
# vendor's capture, decoded and compared line by line against the sixteen
# settings visible in windows-run/screenshots/*.png. engineering-rules.md §1.1a: those
# screenshots are factory defaults, and notes/config-wire-observed.md §7 shows
# all sixteen agree with this record byte for byte. This test says the SAME
# sixteen survive the trip through `show` and come out in words.
#
# It also fixes the record's own provenance in the process: three whole-record
# diffs in §7 are zero, so this is the state the screenshots depict.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/egg-config
SRC=frames/01-baseline-003-in-01.bin
[ -x "$BIN" ] || { echo "build $BIN first"; exit 2; }
[ -f "$SRC" ]  || python3 Tools/capture/mkframes.py >/dev/null 2>&1
[ -f "$SRC" ]  || { echo "missing $SRC, and it could not be rebuilt from"; \
                    echo "windows-run/01-baseline.pcapng"; exit 2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
REC="$TMP/factory.bin"

# The capture holds 1040 bytes: the frame as Windows saw it, report id included,
# minus the one trailing pad byte a 1041-byte record carries. Rebuilt here
# rather than checked in, so the input to this test is the vendor's own file.
python3 - "$SRC" "$REC" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
assert len(d) == 1040, len(d)
open(sys.argv[2], 'wb').write(d + b'\x00')
PY

PASS=0; FAIL=0
check() {
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}
# `show` must SAY this, not merely not contradict it.
says() {
  printf '%s' "$OUT" | grep -qF -- "$2"
  check "$1" $?
}

echo "egg-config show -- the factory record, against the screenshots"
echo

OUT=$("$BIN" show --from "$REC" 2>&1)
check "show --from exits 0 on the vendor's own record" $?

# --- the sixteen, from notes/config-wire-observed.md §7 --------------------
says "Polling Rate 8000Hz            (basic.png)"            "8000 Hz"
says "LOD 1.0mm                      (basic.png)"            "(1.0mm)"
# THE NEEDLE MUST CARRY THE VALUE, not just the field name. Five of these
# grepped for "cpi-levels", "angle-snapping", "motion-sync", "force-max-fps"
# and "slamclick-filter" -- strings `show` prints for EVERY record whatever the
# byte says -- while their labels claimed "4", "unticked" and "ticked". Four of
# the five were saved by the value loop below; `cpi-levels` was checked nowhere
# at all, so the 4 in its own label was never asserted (found 2026-09-07). A
# check whose needle cannot fail is the same defect as a needle that never
# matches, and this file has now had one of each.
says "CPI Levels 4                   (CPI-levels.png)"       "cpi-levels             0x0e = 04"
says "CPI 2 is the selected radio    (basic.png)"            "stage 2 is the active one"
says "Angle Snapping unticked        (advanced-sensor.png)"  "angle-snapping         0x0a = 00"
says "Motion Sync unticked           (advanced-sensor.png)"  "motion-sync"
says "Force max Sensor fps ticked    (advanced-sensor.png)"  "force-max-fps"
says "Sensor Angle 0                 (advanced-sensor.png)"  "0 degrees"
says "Slamclick Filter ticked        (advanced-sensor.png)"  "slamclick-filter       0x06[0] = 1"
says "Downshift 'Default'            (CPI-downshift-tuning)" '"Default", item 4 of 4'
says "Smoothing 'Ripple Control Off' (smoothing-tuning.png)" '"Ripple Control Off", item 2 of 3'
says "CPI stages 400/800/1600/3200   (CPI-levels.png)"       "400 CPI"
says "'I understand...' unticked     (buttons.png)"          "0x72 = 00"
says "Sensor Glass Mode is off       (§7.25)"                "0x6f = 00"

# The inverted checkbox. This is the one a decoder is most likely to get
# backwards, and getting it backwards would read as perfectly sensible.
says "Disable LED on Lift-Off UNTICKED reads as the LED staying LIT" \
     "stays lit"

# Values, not just names, for the three booleans whose names appear either way.
for pair in "angle-snapping|0  (off)" "motion-sync|0  (off)" \
            "force-max-fps|1  (on)" "slamclick-filter|1  (on)"; do
  f=${pair%%|*}; want=${pair##*|}
  printf '%s' "$OUT" | grep -F -- "$f" | grep -qF -- "$want"
  check "$f reads '$want'" $?
done

# --- buttons and multiclick ------------------------------------------------
for pair in "right|right-click" "middle|middle-click" "forward|forward" \
            "back|back" "wheel-up|scroll-up" "wheel-down|scroll-down" \
            "cpi-button|cpi-loop" "left|left-click"; do
  b=${pair%%|*}; a=${pair##*|}
  printf '%s' "$OUT" | grep -E "^  $b " | grep -qF -- "$a"
  check "button $b is $a   (button-mapping.png)" $?
done

n=$(printf '%s' "$OUT" | grep -c "filter 8")
[ "$n" = 5 ]
check "all five multiclick sliders read 8   (buttons.png), got $n" $?

printf '%s' "$OUT" | grep -q "UNKNOWN"
check "no entry decodes to UNKNOWN" $([ $? = 1 ] && echo 0 || echo 1)

printf '%s' "$OUT" | grep -qi "handedness: right-handed"
check "the factory record is right-handed" $?

# --- it must send nothing --------------------------------------------------
printf '%s' "$OUT" | grep -qF "nothing was sent to any device"
check "--from says plainly that it opened no device" $?

# --- the machine form is the GUI's contract --------------------------------
M=$("$BIN" show --from "$REC" --machine 2>&1)
check "show --machine exits 0" $?

nf=$(printf '%s\n' "$M" | grep -c '^FIELD	')
nc=$(printf '%s\n' "$M" | grep -c '^CPI	')
nb=$(printf '%s\n' "$M" | grep -c '^BUTTON	')
nm=$(printf '%s\n' "$M" | grep -c '^MULTICLICK	')
nh=$(printf '%s\n' "$M" | grep -c '^HANDED	')
[ "$nf" = 13 ]; check "13 FIELD rows, one per settable field (got $nf)" $?
[ "$nc" = 4 ];  check "4 CPI rows (got $nc)" $?
[ "$nb" = 8 ];  check "8 BUTTON rows, all eight entries (got $nb)" $?
[ "$nm" = 5 ];  check "5 MULTICLICK rows (got $nm)" $?
[ "$nh" = 1 ];  check "1 HANDED row (got $nh)" $?

# Every FIELD row must name a field `set` knows, or the GUI would offer a row
# it cannot act on. Checked by RUNNING set, not by comparing two lists.
bad=0
while IFS=$'\t' read -r _ name loc val status _; do
  [ -n "$name" ] || { bad=$((bad+1)); continue; }
  [ -n "$loc" ] && [ -n "$val" ] || { bad=$((bad+1)); continue; }
  [ "$status" = ok ] || { bad=$((bad+1)); continue; }
  o=$("$BIN" set "$name" 2>&1); rc=$?
  # No VALUE given, so this must be a usage refusal, never "no such field".
  if printf '%s' "$o" | grep -qi "not a field\|unknown field"; then bad=$((bad+1)); fi
  [ "$rc" != 0 ] || true
done < <(printf '%s\n' "$M" | grep '^FIELD	')
[ "$bad" = 0 ]
check "every FIELD row names a real field, with a location and a value" $?

# FALSIFICATION (§6.2): the checks above must be able to fail. Feed `show` a
# record with three bytes changed and require it to report the change. A test
# that only ever sees the factory record would pass on a decoder that ignored
# its input entirely.
python3 - "$REC" "$TMP/mutated.bin" <<'PY'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
P = 0x10
d[P + 0x05] = 0x40   # polling  8000 -> 125 Hz
d[P + 0x09] = 0x0a   # lod      1.0mm -> 1.7mm
d[P + 0x08] = 0x00   # led-on-liftoff  lit -> goes out
open(sys.argv[2], 'wb').write(bytes(d))
PY
MUT=$("$BIN" show --from "$TMP/mutated.bin" 2>&1)
caught=0
printf '%s' "$MUT" | grep -qF "125 Hz"     || caught=$((caught+1))
printf '%s' "$MUT" | grep -qF "(1.7mm)"    || caught=$((caught+1))
printf '%s' "$MUT" | grep -qF "goes out"   || caught=$((caught+1))
printf '%s' "$MUT" | grep -qF "8000 Hz"    && caught=$((caught+1))
[ "$caught" = 0 ]
check "a record with three bytes changed decodes to three changed readings" $?

# And a byte no encoder can produce must be SAID to be unrecognised, not shown
# as some nearby legal value. §1.2a: a silent absence is a claim.
python3 - "$REC" "$TMP/impossible.bin" <<'PY'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
# 0x0d (cpi-stage) takes 0..3; 0x09 (lod) takes 0..10. cfg107 has a SECOND
# encoding for 0x09 in 0xc2..0xd9 that nothing has ever been seen to emit, so
# 0xc4 there is exactly the signal a decoder must not swallow.
d[0x10 + 0x0d] = 0x07
d[0x10 + 0x09] = 0xc4
open(sys.argv[2], 'wb').write(bytes(d))
PY
IMP=$("$BIN" show --from "$TMP/impossible.bin" 2>&1)
n=$(printf '%s' "$IMP" | grep -c "NOT a value")
[ "$n" = 2 ]
check "two impossible bytes are each named as impossible (got $n)" $?
# The lod ROW specifically -- the glass-mode row legitimately names the
# 0.7-1.7mm scale in prose, and grepping the whole output would match that and
# pass for the wrong reason.
printf '%s' "$IMP" | grep -E '^  lod ' | grep -q "mm)"
check "the impossible lod byte is NOT shown as a millimetre reading" \
      $([ $? = 1 ] && echo 0 || echo 1)
printf '%s' "$IMP" | grep -E '^  lod ' | grep -q "NOT a value \`set lod\`"
check "the impossible lod byte says which field could not produce it" $?

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
