#!/bin/bash
# test_flash_restore.sh -- `restore-firmware` writes back a backup, and ONLY a
# backup this tool made.
#
# the owner's call, 2026-09-06, asked as a question and answered "yes, but only its
# own backups". The gate that makes that true is a sidecar <image>.origin
# written by `read-firmware`, checked against the image's bytes at load time.
#
# WHY THIS FILE EXISTS SEPARATELY FROM test_flash.cpp. The unit tests drive
# Image::loadFromBackup directly and prove the gate works. They cannot prove the
# CLI reaches it: a verb that never calls the loader, or calls it after the
# device is touched, passes every one of them. Every case below runs with no
# mouse attached, which is the property CLAUDE.md §4.2b actually requires --
# everything that can fail happens before A1 3A.
#
# It also pins the sidecar FORMAT from the outside. test_flash.cpp round-trips
# writeProvenance against readProvenance, so a format change that broke both
# ends together would pass there; the sidecars here are written by hand from
# what the format is documented to be, so a drift shows up as a failure.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/egg-flash
EXE="Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"
[ -x "$BIN" ] || { echo "build $BIN first"; exit 2; }
# 77, not 0. CLAUDE.md 6.2: a harness that cannot produce a bad result is not
# evidence. This used to `exit 0`, so on any machine without Endgame's binaries
# -- a fresh clone, CI, anyone but the owner -- ctest printed a green pass for a test
# that had asserted nothing. CMakeLists sets SKIP_RETURN_CODE 77 so the run says
# "Skipped" instead, which is the true statement.
[ -f "$EXE" ] || { echo "SKIP: $EXE not present"; exit 77; }

# frames/ is NOT in the repository: .gitignore line 16 ignores *.bin. Every byte
# of it IS committed, inside windows-run/01-baseline.pcapng, so rebuild rather
# than skip. Without this the `cat` below produced an empty file and the test
# carried on comparing nothing -- `set -uo pipefail` has no -e (found 2026-09-07).
FRAME=frames/01-baseline-003-in-01.bin
[ -f "$FRAME" ] || python3 Tools/capture/mkframes.py >/dev/null 2>&1
[ -f "$FRAME" ] || { echo "FAIL: $FRAME missing and not rebuildable from"; \
                     echo "      windows-run/01-baseline.pcapng (Tools/capture/mkframes.py)"; \
                     exit 2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0
check() {
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}

# A vault that IS a valid settings undo, so the A1 13 gate never masks a
# provenance result. Same shape test_flash_undo.sh builds.
cat frames/01-baseline-003-in-01.bin > "$TMP/vault.bin"
printf '\0' >> "$TMP/vault.bin"
V=(--vault "$TMP/vault.bin")

# The stand-in for a real read-back: the vendor's own image. Right length,
# varied content, and genuinely firmware -- which also lets the token below be
# compared against `flash`'s for the same bytes.
python3 - "$EXE" "$TMP/backup.bin" <<'PY'
import sys; sys.path.insert(0, 'Tools/pe'); import fwfile
raw = open(sys.argv[1], 'rb').read()
for rid, _lang, off, size, _d in fwfile.fwfiles(sys.argv[1]):
    if rid == 140:
        open(sys.argv[2], 'wb').write(raw[off:off+size])
PY
cp "$TMP/backup.bin" "$TMP/current.bin"

# The sidecar, written by hand from the documented format -- see the header.
sidecar() {  # <image> <sha256> <size>
  printf 'egg-flash backup provenance v1\nsha256 %s\nsize %s\ndevice-pid 0x1977\nbcd-device 0x0006\nproduct Bootloader\nentry a1-3a\ntaken 2026-09-06T00:00:00Z\n' \
    "$2" "$3" > "$1.origin"
}
sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

run() { "$BIN" restore-firmware "$@" "${V[@]}" 2>&1; }

echo "egg-flash restore-firmware -- the provenance gate (the owner, 2026-09-06)"
echo

# --- it exists at all -------------------------------------------------------
# `help` exits 2 by design, and this file sets pipefail, so a bare
# `$BIN help | grep` reports 2 whatever grep found. Capture, then match.
H=$("$BIN" help 2>&1 || true)
printf '%s' "$H" | grep -q "restore-firmware"
check "the verb is documented in help" $?
printf '%s' "$H" | grep -q -- "--i-know-this-is-button-entered"
check "...and so is the entry-receipt escape hatch" $?

out=$(run); rc=$?
printf '%s' "$out" | grep -q "needs the backup to WRITE"
check "with no image named it refuses and says what it wants" $?

# --- the gate ---------------------------------------------------------------
rm -f "$TMP/backup.bin.origin"
out=$(run "$TMP/backup.bin" --backup "$TMP/current.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "no provenance file at"
check "a 66560-byte image with NO sidecar is refused" $?
printf '%s' "$out" | grep -q "read-firmware"
check "...and names the command that makes one" $?

sidecar "$TMP/backup.bin" "$(sha "$TMP/backup.bin")" 66560

# The same path for both roles. --backup means "what is on the device NOW, the
# thing about to be erased"; naming the image being restored reads as obeying
# §4.2 while obeying nothing.
out=$(run "$TMP/backup.bin" --backup "$TMP/backup.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "same file as the image to restore"
check "--backup naming the image being restored is refused" $?
printf '%s' "$out" | grep -q "NOTHING WAS SENT"
check "...and says nothing was sent" $?

# One flipped byte, sidecar untouched.
python3 - "$TMP/backup.bin" "$TMP/tampered.bin" <<'PY'
import sys
b = bytearray(open(sys.argv[1], 'rb').read())
b[40000] ^= 0x01
open(sys.argv[2], 'wb').write(b)
PY
sidecar "$TMP/tampered.bin" "$(sha "$TMP/backup.bin")" 66560   # the OLD hash
out=$(run "$TMP/tampered.bin" --backup "$TMP/current.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "has changed since it was backed up"
check "an image altered since the backup is refused" $?

printf 'some other tool v3\nsha256 %s\nsize 66560\n' "$(sha "$TMP/backup.bin")" \
  > "$TMP/backup.bin.origin"
out=$(run "$TMP/backup.bin" --backup "$TMP/current.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "not an egg-flash provenance file"
check "a sidecar written by something else is refused" $?

# --- the positive case, and it must actually pass ---------------------------
# §6.2: a gate that refuses everything is a broken loader, not a gate. If this
# fails, every refusal above is passing for the wrong reason.
sidecar "$TMP/backup.bin" "$(sha "$TMP/backup.bin")" 66560
out=$(run "$TMP/backup.bin" --backup "$TMP/current.bin"); rc=$?
printf '%s' "$out" | grep -q "^restoring " \
  && ! printf '%s' "$out" | grep -q "REFUSED"
check "a backup WITH a matching sidecar is accepted (rc=$rc)" $?
printf '%s' "$out" | grep -q "THIS IS A RESTORE"
check "...and the plan says so, rather than reading like a vendor flash" $?
printf '%s' "$out" | grep -q "origin "
check "...and prints where the provenance came from" $?
printf '%s' "$out" | grep -q "POINT OF NO RETURN"
check "...and still prints the erase warning" $?

# The gate runs before the device is looked at, which is what makes a refusal
# cost an error message instead of a latched mouse.
r=$(printf '%s' "$out" | grep -n "^restoring " | head -1 | cut -d: -f1)
q=$(printf '%s' "$out" | grep -n "^preflight" | head -1 | cut -d: -f1)
[ -n "$r" ] && { [ -z "$q" ] || [ "$r" -lt "$q" ]; }
check "...and it runs BEFORE the device is enumerated" $?

# --- the token binds the bytes (§4.2c) --------------------------------------
# Same image, same frames, same token -- whichever verb is speaking. This is
# the check that restore is `flash` with a different Image and not a second
# code path that happens to look similar.
rtok=$(printf '%s' "$out" | grep -oE -- "--confirm [0-9a-f]{8}" | head -1 | awk '{print $2}')
ftok=$("$BIN" flash "$EXE" "${V[@]}" --backup "$TMP/current.bin" 2>&1 \
        | grep -oE -- "--confirm [0-9a-f]{8}" | head -1 | awk '{print $2}')
[ -n "$rtok" ] && [ "$rtok" = "$ftok" ]
check "restoring FWFILE/140 has the SAME token as flashing it ($rtok)" $?

# A different image must produce a different one, or the token is not binding
# anything.
sidecar "$TMP/tampered.bin" "$(sha "$TMP/tampered.bin")" 66560
ttok=$(run "$TMP/tampered.bin" --backup "$TMP/current.bin" \
        | grep -oE -- "--confirm [0-9a-f]{8}" | head -1 | awk '{print $2}')
[ -n "$ttok" ] && [ "$ttok" != "$rtok" ]
check "ONE flipped byte changes the token ($rtok -> $ttok)" $?

# And a token from the other plan is refused, which is the failure §4.2c names:
# approving a dry run and then running something else.
out=$(run "$TMP/backup.bin" --backup "$TMP/current.bin" --confirm "$ttok"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "does not match this plan's token"
check "a token from a DIFFERENT image is refused" $?
printf '%s' "$out" | grep -q "NOTHING WAS SENT"
check "...and nothing was sent" $?

# --- the structural checks are not bypassed by a valid sidecar --------------
python3 -c "open('$TMP/flat.bin','wb').write(b'\xff'*66560)"
sidecar "$TMP/flat.bin" "$(sha "$TMP/flat.bin")" 66560
out=$(run "$TMP/flat.bin" --backup "$TMP/current.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "failed read"
check "66560 identical bytes are refused even WITH a valid sidecar" $?

head -c 66561 /dev/urandom > "$TMP/long.bin"
sidecar "$TMP/long.bin" "$(sha "$TMP/long.bin")" 66561
out=$(run "$TMP/long.bin" --backup "$TMP/current.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "66561 bytes"
check "a file ONE BYTE too long is refused, not truncated" $?

# --- the escape hatch parses and changes nothing off the fromBoot path ------
# The gate itself needs a mouse in the bootloader to reach, so what is checked
# here is that the flag is accepted and does not perturb the host-side run. The
# DECISION is entryGate() in EGGFlashCore, graded by Tests/mutants.sh and by
# the truth table in test_flash.cpp -- deliberately not left in a printf block
# where nothing could plant a bug in it.
sidecar "$TMP/backup.bin" "$(sha "$TMP/backup.bin")" 66560
a=$(run "$TMP/backup.bin" --backup "$TMP/current.bin" | grep -oE -- "--confirm [0-9a-f]{8}" | head -1)
b=$(run "$TMP/backup.bin" --backup "$TMP/current.bin" --i-know-this-is-button-entered \
      | grep -oE -- "--confirm [0-9a-f]{8}" | head -1)
[ -n "$a" ] && [ "$a" = "$b" ]
check "--i-know-this-is-button-entered changes no byte of the plan" $?

# AND THAT IT ACTUALLY SURVIVES ARGUMENT PARSING. The assertion above was the
# ONLY test of this flag until 2026-09-06, and an audit was right that it is
# vacuous on its own: the token is identical with and without the flag, so it
# would pass unchanged if main() dropped the flag on the floor. The gate the
# flag feeds needs a mouse in the bootloader to reach, so egg-flash now
# announces the override host-side, before the device is looked at -- which is
# what makes the CLI-to-gate wire observable with nothing plugged in.
out=$(run "$TMP/backup.bin" --backup "$TMP/current.bin" --i-know-this-is-button-entered)
printf '%s' "$out" | grep -q "^override "
check "...and the flag is ACKNOWLEDGED host-side, so it is known to have parsed" $?
out=$(run "$TMP/backup.bin" --backup "$TMP/current.bin")
! printf '%s' "$out" | grep -q "^override "
check "...and without it, no override line is printed" $?

# The same-file guard must survive a different spelling of the same path.
cp "$TMP/backup.bin" "$TMP/dup.bin"; cp "$TMP/backup.bin.origin" "$TMP/dup.bin.origin"
out=$(run "$TMP/dup.bin" --backup "$TMP/./dup.bin"); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out" | grep -q "same file as the image to restore"
check "--backup ./x and x are caught as the same file, not just x and x" $?

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
