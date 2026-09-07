#!/bin/bash
# test_flash_undo.sh -- egg-flash must say whether a settings undo exists.
#
# working-memory.md, open gaps: "A1 13 is both the Factory Reset button and the
# updater's last command. If it means the same in both places, flashing wipes
# the user's settings. The flasher must save a blob first and say so."
#
# That is [G] and untestable without flashing, so §2's rule applies: build as if
# the bad reading were true. Compounding it, no config write has been observed to
# survive a power cycle and no persist command has been seen -- so settings lost
# to a flash may not be recoverable from the device at all.
#
# The failure this guards against is SILENCE. A tool that flashes without
# mentioning the risk is indistinguishable from one where the risk does not
# exist, and the byte stream looks identical either way.
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
check() {  # check <description> <condition-rc>
  if [ "$2" = 0 ]; then echo "  PASS  $1"; PASS=$((PASS+1))
  else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi
}

echo "egg-flash -- the settings undo (A1 13)"
echo

# 2>&1 >/dev/null keeps ONLY stderr: the warning is deliberately not on stdout
# (see reportSettingsUndo), so grepping stdout would silently pass forever.
MISSING=$("$BIN" image "$EXE" --vault "$TMP/absent.bin" 2>&1 >/dev/null)
printf '%s' "$MISSING" | grep -q "NO SETTINGS UNDO EXISTS"; check "with no blob, it says so loudly" $?
printf '%s' "$MISSING" | grep -q "A1 13"; check "...and names the command that causes it" $?
printf '%s' "$MISSING" | grep -q "egg-config read"; check "...and says how to create one" $?

# A file of the right length counts; a short one does not. An interrupted
# earlier run leaves exactly the second shape, and treating it as an undo is
# how someone flashes believing they are covered.
# CORRECTED 2026-09-05. This used to build the "good" blob with
#   head -c 1041 /dev/zero
# and require it to be announced as an undo. That is the defect, not the
# contract: an all-zero file is not a settings record and `egg-config restore`
# refuses it, so the tool was promising an undo that could not be loaded --
# found by adversarial audit the same day, alongside the byte-0 defect that made
# every genuinely-saved record unloadable too. The vault now answers "is this
# loadable", not "is this 1041 bytes", so the blob here has to be a real record.
# It is the vendor's own captured reply plus the trailing byte our transport
# leaves zero, which is exactly the shape RecordVault writes.
cat frames/01-baseline-003-in-01.bin > "$TMP/good.bin"
printf '\0' >> "$TMP/good.bin"
"$BIN" image "$EXE" --vault "$TMP/good.bin" 2>&1 >/dev/null | grep -q "undo present"
check "a real saved record is recognised as an undo" $?

head -c 1041 /dev/zero > "$TMP/zeros.bin"
"$BIN" image "$EXE" --vault "$TMP/zeros.bin" 2>&1 >/dev/null | grep -q "NO SETTINGS UNDO"
check "a full-length but IMPLAUSIBLE blob is NOT accepted as an undo" $?

head -c 40 /dev/zero > "$TMP/short.bin"
"$BIN" image "$EXE" --vault "$TMP/short.bin" 2>&1 >/dev/null | grep -q "NO SETTINGS UNDO"
check "a truncated blob is NOT accepted as an undo" $?

# A dry-run warns twice -- once before the frames and once after, because the
# top of a 65-block dry-run scrolls off and A1 13 is the last frame printed.
n=$("$BIN" dryrun "$EXE" --vault "$TMP/absent.bin" 2>&1 >/dev/null | grep -c "NO SETTINGS UNDO")
[ "$n" = 2 ]
check "a dry-run warns both before and after the frames (got $n)" $?
# ...and stdout is still nothing but the dry-run itself.
"$BIN" dryrun "$EXE" --vault "$TMP/absent.bin" 2>/dev/null | grep -q "SETTINGS UNDO"
[ $? -ne 0 ]
check "the warning never appears on stdout" $?

# THE PROPERTY THAT MATTERS MOST: none of this changes a single outgoing byte.
# A safety message that perturbs the byte stream would be a bug of exactly the
# kind §4.3 says compiles clean in any language.
A=$("$BIN" stream "$EXE" --vault "$TMP/absent.bin" 2>/dev/null | grep -c .)
"$BIN" stream "$EXE" --vault "$TMP/absent.bin" 2>/dev/null > "$TMP/a.txt"
"$BIN" stream "$EXE" --vault "$TMP/good.bin"   2>/dev/null > "$TMP/b.txt"
cmp -s "$TMP/a.txt" "$TMP/b.txt"
check "the emitted byte stream is identical with and without an undo" $?
[ "$A" -gt 100 ]
check "...and that stream was actually non-empty ($A lines)" $?

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
