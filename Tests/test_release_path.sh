#!/bin/bash
# test_release_path.sh -- the NON-DEFAULT firmware release row.
#
# THE GAP THIS CLOSES, recorded in working-memory.md before it was fixed.
# `Sources/EGGFlashCore/src/FirmwareManifest.cpp` holds four rows. Every
# automated test drove kReleases[0] (1.10) and nothing else, so the code that
# selects a row, checks the .exe against it, refuses an unproven release and
# derives an approval token from the chosen image was exercised for exactly one
# input. The 1.07 path had been driven by hand once and worked; every FUTURE
# release uses it, and a regression in it would not have failed anything.
#
# Everything here is off-wire. `image` and `stream` send nothing by
# construction, and every `flash` invocation omits --confirm, so no byte can
# reach a device even if a check were broken.
#
# WHAT IS DELIBERATELY NOT ASSERTED: that flashing 1.07 would work. It has
# never been on this mouse (CLAUDE.md §5) and the tool says so itself. What is
# asserted is that the SELECTION machinery treats a non-default row exactly as
# it treats the default one, and refuses in the extra ways an unproven row
# requires.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/egg-flash
EXE110="Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"
EXE107="old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe"
EXE104="old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe"
[ -x "$BIN" ] || { echo "build $BIN first"; exit 2; }
for f in "$EXE110" "$EXE107" "$EXE104"; do
  [ -f "$f" ] || { echo "SKIP: $f not present"; exit 0; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0
check() {
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}

echo "egg-flash -- the non-default release row (1.07)"
echo

# ---------------------------------------------------------------------------
# 1. Selecting the row.
# ---------------------------------------------------------------------------
out=$("$BIN" --version 1.07 image "$EXE107" 2>&1); rc=$?
[ "$rc" = 0 ]
check "--version 1.07 with the 1.07 updater is accepted" $?
printf '%s' "$out" | grep -q "release *1\.07"
check "...and the plan names 1.07, not the default" $?
printf '%s' "$out" | grep -q "3922b14157eff4268ca28c8fe7f284ba1ec0880dae2d9137d01eb1c42e297940"
check "...with 1.07's OWN image sha256" $?
printf '%s' "$out" | grep -qi "0x0081d408"
check "...and 1.07's own whole-image checksum" $?
printf '%s' "$out" | grep -q "NOT PROVEN ON THIS DEVICE"
check "...and it says the release is not proven on this mouse" $?
printf '%s' "$out" | grep -q "FWFILE/140"
check "...and the resource id is still 140, from the row (§1.4)" $?

# The default row must NOT quietly accept a different file.
out=$("$BIN" image "$EXE107" 2>&1); rc=$?
[ "$rc" = 1 ]
check "with no --version, the 1.07 updater is REFUSED" $?
printf '%s' "$out" | grep -q "1\.07"
check "...and the refusal names 1.07 as what the file actually is" $?
printf '%s' "$out" | grep -q -- "--version 1.07"
check "...and prints the flag to re-run with" $?

out=$("$BIN" --version 1.07 image "$EXE110" 2>&1); rc=$?
[ "$rc" = 1 ]
check "--version 1.07 with the 1.10 updater is REFUSED" $?
printf '%s' "$out" | grep -q -- "--version 1.10"
check "...and names 1.10, the release that file really is" $?

out=$("$BIN" --version 1.09 image "$EXE107" 2>&1); rc=$?
[ "$rc" != 0 ]
check "a version this build does not know is refused" $?

# ---------------------------------------------------------------------------
# 2. The byte stream. Same shape, different image -- and nothing else.
# ---------------------------------------------------------------------------
"$BIN" --version 1.07 stream "$EXE107" 2>/dev/null > "$TMP/s107.txt"
"$BIN" stream "$EXE110" 2>/dev/null > "$TMP/s110.txt"

[ "$(wc -l < "$TMP/s107.txt")" = "$(wc -l < "$TMP/s110.txt")" ]
check "both releases emit the same number of lines" $?

# Every frame line is "<label> <hex>". Bytes 0-3 are report id, command,
# block index and a zero -- the part that says WHAT is being done to WHICH
# block. Bytes 4-5 of an A0 06 are the block's own checksum, so they are
# image-derived and MUST differ between releases; the payload starts at byte
# 16. So the frame-for-frame comparison is over bytes 0-3 only: it proves the
# command and index sequence is identical across releases, which is the thing
# that must not depend on which row was selected.
#
# WORTH KNOWING, because the first draft of this test got it wrong and passed
# for the wrong reason: comparing the first SIXTEEN bytes fails, and correctly
# so -- the per-block checksum lives inside them.
hdrs() { grep -E '^[a-z]+ [0-9a-f]+$' "$1" | awk '{print $1, substr($2,1,8)}'; }
diff <(hdrs "$TMP/s107.txt") <(hdrs "$TMP/s110.txt") > "$TMP/hdr.diff"
check "frame-for-frame, command and block index are identical across releases" $?

# ...and the rest is NOT, or the comparison above would prove nothing.
! diff -q "$TMP/s107.txt" "$TMP/s110.txt" > /dev/null
check "...while the full streams DO differ, so that check can fail" $?

# The per-block checksum field tracks the IMAGE, and it can be checked against
# something already known rather than against a vague "most of them differ".
#
# notes/updater-protocol.md 8.5a tabulates which 1024-byte chunks of FWFILE
# 140 change at each release step, computed from chunk SHA-256s: 1.07 -> 1.10
# changes chunks 0-28 and 64, thirty of them. The flasher's own A0 06 frames
# carry a 16-bit checksum per block, derived independently of any of that. They
# agree exactly -- same thirty blocks, same set -- which corroborates 8.5a by a
# different route, and is why this asserts the SET and not a count.
sums() { grep -E '^write [0-9a-f]+$' "$1" | awk '{print substr($2,9,4)}'; }
diffblocks=$(diff <(sums "$TMP/s107.txt" | cat -n) \
                  <(sums "$TMP/s110.txt" | cat -n) \
             | awk '/^</ {print $2-1}' | paste -sd, -)
want=$(python3 -c 'print(",".join(str(i) for i in list(range(29)) + [64]))')
[ "$diffblocks" = "$want" ]
check "the blocks whose checksum differs are exactly 8.5a's 0-28 and 64" $?

grep -c '^write ' "$TMP/s107.txt" | grep -qx 65
check "1.07 still writes exactly 65 blocks" $?
grep -q '^start a00300000000000000000000000000004108d481' "$TMP/s107.txt"
check "A0 03 declares 1.07's own checksum, little-endian" $?
grep -q '^start a0030000000000000000000000000000417dd581' "$TMP/s110.txt"
check "...and 1.10's declares 1.10's, which is a different value" $?

# ---------------------------------------------------------------------------
# 3. The unproven-release gate, and the token bound to the image (§4.2c).
# ---------------------------------------------------------------------------
cat frames/01-baseline-003-in-01.bin > "$TMP/vault.bin"
printf '\0' >> "$TMP/vault.bin"
python3 - "$EXE110" "$TMP/backup.bin" <<'PY'
import importlib.util, sys
s = importlib.util.spec_from_file_location("fwfile", "Tools/pe/fwfile.py")
m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
d = open(sys.argv[1], "rb").read()
for (n, l, o, sz, h) in m.fwfiles(sys.argv[1]):
    if n == 140:
        open(sys.argv[2], "wb").write(d[o:o + sz])
PY

flash107() { "$BIN" flash "$EXE107" --version 1.07 --vault "$TMP/vault.bin" \
                    --backup "$TMP/backup.bin" "$@" 2>&1; }

out=$(flash107); rc=$?
[ "$rc" = 1 ]
check "flashing an unproven release refuses by default" $?
printf '%s' "$out" | grep -q -- "--i-know-this-version-is-untested"
check "...and names the flag, which is deliberately not --yes" $?
printf '%s' "$out" | grep -q "never been flashed onto this mouse"
check "...and says why, in terms of this mouse" $?

out=$(flash107 --i-know-this-version-is-untested); rc=$?
printf '%s' "$out" | grep -qE -- "--confirm [0-9a-f]{8}"
check "with the flag, it reaches the plan and prints a token" $?

T107=$(printf '%s' "$out" | grep -oE -- "--confirm [0-9a-f]{8}" | head -1 | awk '{print $2}')
T110=$("$BIN" flash "$EXE110" --vault "$TMP/vault.bin" --backup "$TMP/backup.bin" 2>&1 \
       | grep -oE -- "--confirm [0-9a-f]{8}" | head -1 | awk '{print $2}')
[ -n "$T107" ] && [ -n "$T110" ] && [ "$T107" != "$T110" ]
check "the 1.07 token differs from the 1.10 token (§4.2c)" $?

# The token must be a function of the bytes, so it has to be stable.
T107b=$(flash107 --i-know-this-version-is-untested \
        | grep -oE -- "--confirm [0-9a-f]{8}" | head -1 | awk '{print $2}')
[ "$T107" = "$T107b" ]
check "...and is identical on a second run of the same plan" $?

# A stale approval must not carry over between releases.
out=$(flash107 --i-know-this-version-is-untested --confirm "$T110"); rc=$?
[ "$rc" != 0 ]
check "1.10's token is rejected for a 1.07 flash" $?

# ---------------------------------------------------------------------------
# 4. A third row, so the machinery is not fitted to one alternative.
# ---------------------------------------------------------------------------
out=$("$BIN" --version 1.04 image "$EXE104" 2>&1); rc=$?
[ "$rc" = 0 ]
check "--version 1.04 with the 1.04 updater is accepted too" $?
printf '%s' "$out" | grep -q "41d5397ec84c4f167e6dce4d05f649f425e5c38ee066b8931721898d5dc013c3"
check "...with 1.04's own image sha256" $?
out2=$("$BIN" --version 1.04 image "$EXE107" 2>&1); rc=$?
[ "$rc" = 1 ] && printf '%s' "$out2" | grep -q -- "--version 1.07"
check "...and 1.04 refuses the 1.07 file, naming 1.07" $?

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
