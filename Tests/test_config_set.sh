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
longest=$(printf '%s\n' "$out" | awk '{ print length }' | sort -n | tail -1)
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

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
