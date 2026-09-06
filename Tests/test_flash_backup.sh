#!/bin/bash
# test_flash_backup.sh -- `flash` must refuse without a valid firmware backup,
# and must refuse BEFORE it sends anything.
#
# CLAUDE.md §4.2, added 2026-09-05: "Never erase without a saved copy of what is
# being erased." Until the same day, cmdFlash satisfied that itself by reading
# all 65 blocks out with A0 07 immediately before A0 03. the owner's call was that
# this is worse, not better: the vendor never sends A0 07 before A0 03, and
# inserting 65 frames into the one sequence we have a capture of trades a rule
# we wrote for a deviation from the only evidence we have. So the backup moved
# to its own `read-firmware` run and this one only CHECKS it.
#
# That trade is only sound if the check is real. This file is what makes it
# real, and it is the reason the host-side gates in cmdFlash run BEFORE the
# device is enumerated: every case below runs with no mouse attached.
#
# THE FAILURE GUARDED AGAINST: `flash` accepting something that is not a backup
# and erasing anyway. All five refusals below are silent in the byte stream --
# there is no wire evidence to catch them, which is §4.3's point about bugs that
# compile clean.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/egg-flash
EXE="Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"
[ -x "$BIN" ] || { echo "build $BIN first"; exit 2; }
[ -f "$EXE" ] || { echo "SKIP: $EXE not present"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0
check() {
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}

# A vault that IS a valid undo, so the settings gate never masks a backup
# result. Same shape as test_flash_undo.sh builds.
cat frames/01-baseline-003-in-01.bin > "$TMP/vault.bin"
printf '\0' >> "$TMP/vault.bin"
V=(--vault "$TMP/vault.bin")

# `flash` needs --confirm to do anything, and every case here omits it, so
# nothing can reach the device even if a check were broken. The exit code is the
# discriminator: 2 means "plan shown, awaiting confirmation" (the backup was
# ACCEPTED) and 1 means refused.
run() { "$BIN" flash "$EXE" "${V[@]}" "$@" 2>&1; }

echo "egg-flash flash -- the firmware backup gate (§4.2)"
echo

out=$(run); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "no backup file was named"
check "with no --backup at all, it refuses" $?
printf '%s' "$out" | grep -q "read-firmware"
check "...and names the command that makes one" $?
printf '%s' "$out" | grep -q "NOTHING WAS SENT"
check "...and says nothing was sent" $?

out=$(run --backup "$TMP/does-not-exist.bin"); rc=$?
[ "$rc" = 1 ]
check "a --backup naming a missing file is refused" $?

head -c 100 /dev/urandom > "$TMP/short.bin"
out=$(run --backup "$TMP/short.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "is 100 bytes"
check "a short file is refused, and the size is named" $?

# One byte too long. Caught only because checkBackup reads 66561 and demands
# the read return exactly 66560 -- a naive read of 66560 would accept this.
head -c 66561 /dev/urandom > "$TMP/long.bin"
out=$(run --backup "$TMP/long.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "66561 bytes"
check "a file ONE BYTE too long is refused, not truncated" $?

# The shape a failed read leaves behind: right length, no content. An A0 07 loop
# that returned empty reports and got saved anyway looks exactly like this.
python3 -c "open('$TMP/flat.bin','wb').write(b'\xff'*66560)"
out=$(run --backup "$TMP/flat.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "identical bytes"
check "66560 identical bytes is refused as a failed read" $?

python3 -c "open('$TMP/zeros.bin','wb').write(b'\x00'*66560)"
out=$(run --backup "$TMP/zeros.bin"); rc=$?
[ "$rc" = 1 ]
check "...and so is an all-zero file of the right length" $?

# The positive case. Uses the vendor's own image as a stand-in for a real
# read-back: right length, varied content, and it is a genuine firmware image.
# Accepted means rc=2 -- the plan printed, waiting for --confirm.
python3 - "$EXE" "$TMP/good.bin" <<'PY'
import sys; sys.path.insert(0, 'Tools/pe'); import fwfile
raw = open(sys.argv[1], 'rb').read()
for rid, _lang, off, size, _d in fwfile.fwfiles(sys.argv[1]):
    if rid == 140:
        open(sys.argv[2], 'wb').write(raw[off:off+size])
PY
out=$(run --backup "$TMP/good.bin"); rc=$?
[ "$rc" = 2 ]
check "a valid 66560-byte image IS accepted (rc=$rc, want 2)" $?
printf '%s' "$out" | grep -q "sha256"
check "...and its sha256 is printed, so the user can compare runs" $?

# THE POINT OF THE WHOLE CHANGE. The flash must not contain an A0 07 read-back
# before the erase any more; `stream` is the plan, so this is checkable.
"$BIN" stream "$EXE" "${V[@]}" 2>/dev/null > "$TMP/s.txt"
# Frame lines are "<label> <hex>", so match on the hex, never the label: a
# label is ours and could be renamed, while a1 3a is the byte on the wire.
first3a=$(grep -n " a13a" "$TMP/s.txt" | head -1 | cut -d: -f1)
[ "$(grep -c " a13a" "$TMP/s.txt")" = 1 ] && [ -n "$first3a" ]
check "A1 3A is emitted exactly once (line $first3a)" $?
first03=$(grep -n " a003" "$TMP/s.txt" | head -1 | cut -d: -f1)
first07=$(grep -n " a007" "$TMP/s.txt" | head -1 | cut -d: -f1)
[ -n "$first3a" ] && [ -n "$first03" ] && [ "$first3a" -lt "$first03" ]
check "A1 3A comes before the erase (3a at $first3a, 03 at $first03)" $?
[ -n "$first03" ] && [ -n "$first07" ] && [ "$first07" -gt "$first03" ]
check "NO A0 07 IS EMITTED BEFORE A0 03 (first 07 at $first07)" $?
n07=$(grep -c " a007" "$TMP/s.txt")
[ "$n07" = 65 ]
check "there are exactly 65 A0 07 frames, one per written block (got $n07)" $?
n06=$(grep -c " a006" "$TMP/s.txt")
[ "$n06" = 65 ]
check "...and exactly 65 A0 06 writes to pair with them (got $n06)" $?
# 1 enter + 1 start + 65 writes + 65 verifies + sum + complete + reset = 135.
# Counted by matching the frame hex, because `stream` prints an image header on
# stdout too and `grep -c .` would silently fold those five lines in -- which it
# did, and the total came out "right" for the wrong reason.
nall=$(grep -cE " a[01][0-9a-f]{2}" "$TMP/s.txt")
[ "$nall" = 135 ]
check "the whole plan is 135 frames (got $nall)" $?

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
