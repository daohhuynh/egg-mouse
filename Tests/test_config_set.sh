#!/bin/bash
# test_config_set.sh -- `egg-config set` refuses everything it should.
#
# No device required, and that is the point: every check here is about what the
# tool REFUSES, and refusal happens before a device is ever opened. §1.3 says a
# byte whose meaning is [G] must never reach the hardware, and the settable
# table is the only thing standing between a typo and exactly that.
#
# The dangerous direction is a value being ACCEPTED that should not be, so the
# assertions are written that way round: an unlisted field, a non-power-of-two
# divisor, a plausible-looking rate that does not divide 8000.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/egg-config
[ -x "$BIN" ] || { echo "build $BIN first"; exit 2; }

# frames/ is NOT in the repository: .gitignore line 16 ignores *.bin. Every byte
# of it IS committed, inside windows-run/01-baseline.pcapng, so rebuild rather
# than skip. Without this the `cat` below produced an empty file and the test
# carried on comparing nothing -- `set -uo pipefail` has no -e (found 2026-09-07).
FRAME=frames/01-baseline-003-in-01.bin
[ -f "$FRAME" ] || python3 Tools/capture/mkframes.py >/dev/null 2>&1
[ -f "$FRAME" ] || { echo "FAIL: $FRAME missing and not rebuildable from"; \
                     echo "      windows-run/01-baseline.pcapng (Tools/capture/mkframes.py)"; \
                     exit 2; }

PASS=0; FAIL=0
# want_rc <expected-rc> <substring> <args...>
want_rc() {
  local want=$1 needle=$2; shift 2
  local out rc
  out=$("$BIN" "$@" 2>&1); rc=$?
  if [ "$rc" = "$want" ] && printf '%s' "$out" | grep -qF "$needle"; then
    echo "  PASS  $* -> rc=$rc"; PASS=$((PASS+1))
  else
    echo "  FAIL  $* -> rc=$rc (wanted $want) and/or missing '$needle'"
    printf '%s\n' "$out" | sed 's/^/        | /' | head -6
    FAIL=$((FAIL+1))
  fi
}

echo "egg-config set -- refusals"
echo

# An unlisted field must never be settable, however real it sounds. These are
# genuine settings on this mouse; the point is that knowing a field EXISTS is
# not knowing which byte it is (§7.3 maps all 115 and names only a few).
#
# angle-snapping and motion-sync USED TO BE ON THIS LIST and were moved off on
# 2026-09-05 when §7.8/§7.9 derived their bytes. That is the list working as
# intended: it holds names whose byte is unknown, not names we dislike.
# lod CAME OFF this list on 2026-09-05 for the same reason: the capture showed
# eleven writes moving record 0x09 through 0..10, so its byte is no longer
# unknown. ripple-control stays -- the capture showed it moving NOTHING, which
# is a finding about the control, not a licence to guess a byte for it.
for f in ripple-control dpi cpi debounce; do
  want_rc 2 "is not a settable field" set "$f" 1
done

# Derived but DELIBERATELY withheld, each for a reason the tool must state.
# These are the dangerous ones -- a plausible name with a real byte behind it --
# so the test asserts both that they are refused AND that the refusal explains
# itself rather than pretending the field does not exist.
#
# slamclick-filter, cpi-downshift and smoothing left this list on 2026-09-05.
# All three were withheld for the SAME stated reason -- sub-byte fields need a
# read-modify-write inside a byte carrying other settings -- and that reason
# stopped applying when Settable grew mask/shift and the composition was tested
# against the vendor's own writes. The remaining two are withheld for reasons
# that have not gone away: one is a name that means the inverse of its byte, and
# the others are fields whose byte is shared or whose block is not fully known.
for f in disable-led-on-liftoff multiclick-filter button-mapping; do
  want_rc 2 "is not a settable field" set "$f" 1
done
# ...and the refusal must say WHY, not merely refuse.
want_rc 2 "SHARES its byte" set multiclick-filter 1

# The three that became settable must now actually work, and must say which
# bits they touch -- a sub-byte field that reports itself like a whole-byte one
# is how a neighbouring setting gets clobbered without anyone noticing.
want_rc 2 "0x01 bits" set slamclick-filter 1
want_rc 2 "0x0c bits" set cpi-downshift 2
want_rc 2 "0x03 bits" set smoothing 2
want_rc 2 "record 0x09" set lod 5
want_rc 2 "record 0x0d" set cpi-stage 2
# and must still reject values outside their range
want_rc 2 "is not a legal" set cpi-downshift 5
want_rc 2 "is not a legal" set smoothing 0
want_rc 2 "is not a legal" set lod 11
want_rc 2 "is not a legal" set cpi-stage 4

# The vendor's caption must be refused, and the refusal must point at the field
# named after the byte instead of just saying "no". Getting this wrong writes
# the opposite of what the user asked for, which is the whole reason for the
# rename -- so the redirect is asserted, not just the refusal.
want_rc 2 "led-on-liftoff" set disable-led-on-liftoff 1
for v in 0 1; do
  want_rc 2 "Exactly one byte changes" set led-on-liftoff "$v"
done
want_rc 2 "record 0x08" set led-on-liftoff 1
for v in 2 -1; do
  want_rc 2 "is not a legal led-on-liftoff" set led-on-liftoff "$v"
done

# Newly derived and now settable. Legal values are accepted, still refuse to
# write without --yes, and must name the byte and cite the derivation.
for f in angle-snapping motion-sync force-max-fps; do
  for v in 0 1; do
    want_rc 2 "Exactly one byte changes" set "$f" "$v"
  done
  for v in 2 -1 255; do
    want_rc 2 "is not a legal $f" set "$f" "$v"
  done
done
want_rc 2 "record 0x0a" set angle-snapping 1
want_rc 2 "record 0x0c" set motion-sync 1
want_rc 2 "record 0x71" set force-max-fps 1

# Sensor angle is signed: the vendor stores TBM_GETPOS raw, so -45 must encode
# as two's complement 0xd3 and NOT as sign-magnitude 0xad.
want_rc 2 "0xd3" set sensor-angle -45
want_rc 2 "record 0x70" set sensor-angle 20
for v in -129 128 1000; do
  want_rc 2 "is not a legal sensor-angle" set sensor-angle "$v"
done

# Rates that do not divide 8000, and rates that divide it by a non-power of two.
# 1600 is the trap: 8000/1600 = 5, a whole number that is not a legal divisor,
# and the vendor's own reader would drop it to a no-op arm rather than reject it.
for hz in 0 -1 100 300 1600 3000 7999 16000 999999; do
  want_rc 2 "is not a legal polling" set polling "$hz"
done

for n in 0 -1 5 8 99; do
  want_rc 2 "is not a legal cpi-levels" set cpi-levels "$n"
done

# Non-numeric values.
want_rc 2 "is not a number" set polling fast
want_rc 2 "is not a number" set polling 1000Hz

# Legal values must be ACCEPTED but must still refuse to write without --yes,
# and must name the byte and cite the derivation while doing it.
for hz in 125 250 500 1000 2000 4000 8000; do
  want_rc 2 "Exactly one byte changes" set polling "$hz"
done
want_rc 2 "record 0x05" set polling 1000
want_rc 2 "config-protocol.md" set polling 1000
for n in 1 2 3 4; do
  want_rc 2 "Exactly one byte changes" set cpi-levels "$n"
done

# The listing must show every settable field with a citation, and must not
# quietly grow: a field with no cite is a protocol claim with no evidence.
# Count only the SETTABLE section. The withheld list below it also names record
# offsets -- on purpose, so a user is told which byte they are being refused --
# and counting those as settable fields is how this check started lying.
listing=$("$BIN" set 2>&1 | sed -n '1,/deliberately NOT settable/p')
n_fields=$(printf '%s\n' "$listing" | grep -c "record 0x")
# A citation is any pointer into the notes. Two of the newer fields cite
# notes/config-wire-observed.md rather than config-protocol.md, because what
# justifies them is the capture rather than the disassembly -- so matching only
# the older filename would have counted a properly cited field as uncited.
n_cites=$(printf '%s\n' "$listing" | grep -cE "config-protocol\.md|config-wire-observed\.md")
if [ "$n_fields" -eq "$n_cites" ] && [ "$n_fields" -gt 0 ]; then
  echo "  PASS  every settable field carries a citation ($n_fields)"; PASS=$((PASS+1))
else
  echo "  FAIL  $n_fields settable fields but $n_cites citations"; FAIL=$((FAIL+1))
fi

# --------------------------------------------------------------------- §7.25
# THE CAPABILITY GATE, OFFLINE. `dryrun` prints the frame and the approval token
# §4.2c binds a write to, so it must refuse a gated field for the same reason
# `set` does -- otherwise the tool hands out an approval for a write it will not
# perform. Exercised against a REAL captured record with one byte poked, so the
# only thing invented is the byte whose meaning is the whole point.
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cat frames/01-baseline-003-in-01.bin > "$TMP/plain.bin"
printf '\0' >> "$TMP/plain.bin"            # the device sends N-1; pad to 1041
cp "$TMP/plain.bin" "$TMP/gated.bin"
# record 0x6f lives at payload+0x6f = file offset 16 + 111 = 127.
printf '\x01' | dd of="$TMP/gated.bin" bs=1 seek=127 count=1 conv=notrunc 2>/dev/null

want_rc 0 "record 0x09" dryrun "$TMP/plain.bin" lod 5
want_rc 8 "REFUSED"     dryrun "$TMP/gated.bin" lod 5
want_rc 8 "0x6f"        dryrun "$TMP/gated.bin" lod 5

# THE GUI'S PARSING CONTRACT, tested from the PRODUCING end for the first time.
#
# `reportGates` prints "  %-20s  %s" and EGGApp/Commands.swift parseGates splits
# on the FIRST DOUBLE SPACE. `motion-jitter-filter` is exactly 20 characters, so
# a single space would collapse the separator and the app would read the whole
# line as a name with no reason. Until 2026-09-06 the parser was tested against
# synthetic text and the producer against nothing, because only `read` printed
# this and `read` needs a mouse. `dryrun` prints it from a file now, so both
# ends are driven by the same real output.
out=$("$BIN" dryrun "$TMP/gated.bin" 2>&1 || true)
if printf '%s' "$out" | grep -qE '^  lod {2,}[^ ]'; then
  echo "  PASS  the gate line separates name from reason by >= 2 spaces"
  PASS=$((PASS+1))
else
  echo "  FAIL  the gate line's separator collapsed -- EGGApp cannot parse it"
  FAIL=$((FAIL+1))
fi
# The name column must be wide enough for the LONGEST field name, or the
# separator collapses for that one field only -- which is how this would ship.
longest_field=$("$BIN" set 2>&1 | sed -n '1,/deliberately NOT settable/p' \
  | grep -oE '^  [a-z][a-z0-9-]+ +record' | awk '{print length($1)}' | sort -n | tail -1)
gate_pad=$(printf '%s' "$out" | grep -E '^  lod ' | sed -E 's/^  (lod +)[^ ].*/\1/' | tr -d '\n' | wc -c)
if [ "${gate_pad:-0}" -ge "$((longest_field + 2))" ]; then
  echo "  PASS  the gate name column ($gate_pad) fits the longest field name ($longest_field) plus a separator"
  PASS=$((PASS+1))
else
  echo "  FAIL  gate name column $gate_pad is too narrow for a ${longest_field}-char name"
  FAIL=$((FAIL+1))
fi
# An UNGATED record must print the "none" form, or the app would disable
# every field on a perfectly good mouse.
want_rc 0 "fields this device will NOT accept: none" dryrun "$TMP/plain.bin"

# `diff` -- untested until 2026-09-06, which is worse than it sounds. It is
# offline and read-only, so it looked harmless, but it is the command a person
# runs to decide whether a restore is safe: a diff that under-reports makes a
# destructive restore look benign. Four cases, including both refusals.
want_rc 0 "0 of 1024 payload bytes differ" diff "$TMP/plain.bin" "$TMP/plain.bin"
want_rc 0 "1 of 1024 payload bytes differ" diff "$TMP/plain.bin" "$TMP/gated.bin"
# The byte it names must be the one that was actually changed: record 0x6f,
# which is payload +0x6f and wire 0x7f. Naming the wrong offset is the failure
# that would still print a plausible-looking line.
want_rc 0 "wire 0x007f" diff "$TMP/plain.bin" "$TMP/gated.bin"
want_rc 0 "00 -> 01"    diff "$TMP/plain.bin" "$TMP/gated.bin"
# ...and it is direction-sensitive, or "what will change" is a coin flip.
want_rc 0 "01 -> 00"    diff "$TMP/gated.bin" "$TMP/plain.bin"
printf '\x00\x01\x02' > "$TMP/tiny.bin"
want_rc 1 "Refusing"    diff "$TMP/tiny.bin" "$TMP/plain.bin"
want_rc 1 "cannot open" diff "$TMP/does-not-exist.bin" "$TMP/plain.bin"

# THE REFUSAL MUST STILL SAY EVERYTHING, and must be readable while it does.
# The reason is a paragraph, and it used to print as one 430-character line --
# the shape people skip, which for the most important message this tool emits
# is a real defect. It is wrapped now, so check both halves of that: nothing
# lost off either end, and no line wider than a terminal.
out=$("$BIN" dryrun "$TMP/gated.bin" lod 5 2>&1 || true)
if printf '%s' "$out" | grep -qF "SENSOR GLASS MODE is on" &&
   printf '%s' "$out" | grep -qF "config-protocol.md"; then
  echo "  PASS  the wrapped refusal keeps its first words and its last"
  PASS=$((PASS+1))
else
  echo "  FAIL  the wrapped refusal lost an end"; FAIL=$((FAIL+1))
fi
# Measure ONLY the refusal paragraph, not the whole output: `dryrun` also
# prints the gate section, whose long line is deliberately unwrapped because
# the GUI parses it. Measuring everything made this test fail for the one
# reason it must not care about.
longest=$(printf '%s\n' "$out" | sed -n '/^REFUSED/,$p' \
          | awk '{ print length }' | sort -n | tail -1)
if [ "${longest:-0}" -le 80 ]; then
  echo "  PASS  no refusal line exceeds 80 columns (longest $longest)"
  PASS=$((PASS+1))
else
  echo "  FAIL  a refusal line is $longest columns wide"; FAIL=$((FAIL+1))
fi

# The gate is per FIELD. Everything else still works on the same record, or the
# guard is a blanket refusal wearing a reason.
want_rc 0 "record 0x05" dryrun "$TMP/gated.bin" polling 1000

# `encode` writes a file `restore` would later put on the wire. Same gate, and
# the output file must NOT appear.
rm -f "$TMP/out.bin"
want_rc 8 "REFUSED" encode lod 5 "$TMP/gated.bin" "$TMP/out.bin"
if [ ! -e "$TMP/out.bin" ]; then
  echo "  PASS  encode refused and wrote no file"; PASS=$((PASS+1))
else
  echo "  FAIL  encode refused but wrote $TMP/out.bin anyway"; FAIL=$((FAIL+1))
fi
rm -f "$TMP/out.bin"
if "$BIN" encode lod 5 "$TMP/plain.bin" "$TMP/out.bin" >/dev/null 2>&1 \
   && [ -s "$TMP/out.bin" ]; then
  echo "  PASS  encode still works on an ungated record"; PASS=$((PASS+1))
else
  echo "  FAIL  encode refused an ungated record, or wrote nothing"; FAIL=$((FAIL+1))
fi

# ---------------------------------------------------------------------------
# `-v` cannot change the byte stream.
# ---------------------------------------------------------------------------
# The GUI applies -v to EVERY command it runs (ToolRunner.verbose), which is
# only defensible if -v is a pure logging flag. That is a claim about the code,
# so it gets checked against the frames themselves rather than against the
# parser: `dryrun` prints the exact 1041-byte frame a field change would send,
# and the hex must be identical with and without -v.
#
# Checked for several fields, including two sub-byte ones, because a flag that
# altered composition would show up on those first.
vdiffs=0
for spec in "polling 1000" "lod 5" "cpi-downshift 2" "slamclick-filter 0" \
            "sensor-angle -30"; do
  set -- $spec
  a=$("$BIN" dryrun "$TMP/plain.bin" "$1" "$2" 2>/dev/null \
      | grep -E '^ +[0-9a-f]{4} +[0-9a-f]+$' || true)
  b=$("$BIN" -v dryrun "$TMP/plain.bin" "$1" "$2" 2>/dev/null \
      | grep -E '^ +[0-9a-f]{4} +[0-9a-f]+$' || true)
  [ -n "$a" ] || vdiffs=$((vdiffs+1))
  [ "$a" = "$b" ] || vdiffs=$((vdiffs+1))
done
if [ "$vdiffs" = 0 ]; then
  echo "  PASS  -v does not change the frame for any of 5 fields"; PASS=$((PASS+1))
else
  echo "  FAIL  -v changed the emitted frame ($vdiffs discrepancies)"; FAIL=$((FAIL+1))
fi

# And the falsification: the comparison above must be able to fail. Two
# DIFFERENT field changes must produce different frames, or the grep is
# matching nothing and the check is vacuous.
#
# THIS IS NOT HYPOTHETICAL. When the -v check was written on 2026-09-06 its
# grep was `^[0-9a-f]{4}:`, and `dryrun` prints `  0000  a011...` -- two
# leading spaces, no colon. So it matched nothing, both sides were empty, and
# "-v does not change the frame" was true of a comparison between two empty
# strings. The check below is what said so. A falsification that has never
# fired is decoration; this one earned its place within a day of being written.
c=$("$BIN" dryrun "$TMP/plain.bin" polling 1000 2>/dev/null | grep -E '^ +[0-9a-f]{4} +[0-9a-f]+$')
d=$("$BIN" dryrun "$TMP/plain.bin" polling 500  2>/dev/null | grep -E '^ +[0-9a-f]{4} +[0-9a-f]+$')
if [ -n "$c" ] && [ "$c" != "$d" ]; then
  echo "  PASS  the frame comparison can tell two frames apart"; PASS=$((PASS+1))
else
  echo "  FAIL  the frame comparison cannot distinguish 1000 Hz from 500 Hz, "
  echo "        so the -v check above proved nothing"; FAIL=$((FAIL+1))
fi

# ---------------------------------------------------------------------------
# The recovery text reaches the user, and it is the SHARED one.
# ---------------------------------------------------------------------------
# There used to be two constants called kRecoveryProcedure -- the long one
# egg-flash prints and a short one this tool printed -- saying the same things
# in different words. A guard added elsewhere made both wrong at once, and the
# second was found by the compiler, not by a check. They are collapsed now
# (egg::kButtonEntrySteps / kButtonEntryReflashNote in Protocol.h), and
# test_flash.cpp asserts the composition. This is the other half: that the
# text actually reaches stdout, which no unit test can see.
say() {
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}
h=$("$BIN" help 2>&1)
printf '%s' "$h" | grep -q "Hold LEFT and RIGHT mouse buttons together"
say "egg-config help prints the button-entry steps" $?
printf '%s' "$h" | grep -q -- "--i-know-this-is-button-entered"
say "...and names the flag a button-entered bootloader needs" $?
! printf '%s' "$h" | grep -q "run this command again"
say "...and NOT the flasher-only 'run this command again'" $?

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
