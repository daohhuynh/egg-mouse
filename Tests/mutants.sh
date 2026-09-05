#!/bin/bash
# mutants.sh -- does test-flash actually catch anything?
#
# CLAUDE.md §6.2: "A harness that cannot produce a bad result is not evidence.
# If the planted positives never fail and no verdict is ever rejected, the
# harness is measuring nothing. Report its failure rate; a rate of zero is a red
# flag, not a pass."
#
# test-flash passed 47/47 on its first run, which is exactly the shape §6.2 says
# to distrust. So this plants known bugs in the flasher, rebuilds, and requires
# the suite to notice. Each mutant is a bug someone could plausibly write --
# several are the vendor's own (updater-protocol.md §5.6) -- not a character
# swap.
#
# TWO CATEGORIES, and the distinction is the honest part:
#
#   kill  -- a real defect. The suite MUST fail. A survivor is a hole.
#   equiv -- provably cannot change behaviour for any reachable input, so the
#            suite must NOT fail. Calling one of these a hole would be a lie,
#            and "fixing" it would mean writing a test that asserts something
#            untrue. If an equiv mutant IS caught, that is also a finding: it
#            means the reasoning behind calling it equivalent was wrong.
#
# THREE THINGS THIS SCRIPT GOT WRONG BEFORE, all of which faked its own results:
#   1. No timeout. §4.2 forbids the write phase from ever giving up, so a
#      mutant that breaks verification retries forever. A hang was reported as
#      SURVIVED because the script simply waited.
#   2. No forced rebuild. make's mtime granularity let it run a STALE binary,
#      so results moved between runs -- 6/10 then 5/10 on identical input.
#      `touch` before every build fixes it.
#   3. Both together made the output non-reproducible, which for a script whose
#      entire job is to measure trustworthiness is the worst possible bug.
#   4. AND IT WAS STILL NON-REPRODUCIBLE (2026-09-05). The same mutant --
#      "off-by-one below kBlockFirst", the one guarding the only unrecoverable
#      failure this project has -- reported SURVIVED on one run and killed
#      (SIGABRT) on the next, from an unchanged tree. `touch` alone does not
#      guarantee the built binary actually contains the mutation: any build
#      hiccup leaves the previous test-flash in place, it passes because it is
#      the CLEAN binary, and a clean pass is indistinguishable from "the suite
#      did not catch this".
#
#      That is the worst possible direction for the error. A hole that is not
#      there gets investigated; a hole that IS there and reports as killed does
#      not. So the fix is not to trust the build: hash the binary before and
#      after, and if the mutant build is byte-identical to the clean one, the
#      mutation provably did not reach the binary and the result is BROKEN, not
#      SURVIVED. A verdict is only allowed when the thing under test demonstrably
#      changed.
set -uo pipefail
cd "$(dirname "$0")/.."

# macOS has no coreutils `timeout`.
runlimitedto() {
  local out=$1; shift
  local lim=$1; shift
  "$@" >"$out" 2>&1 & local pid=$!
  ( sleep "$lim"; kill -9 $pid 2>/dev/null ) >/dev/null 2>&1 & local w=$!
  wait $pid; local rc=$?
  kill -9 $w 2>/dev/null; wait $w 2>/dev/null
  return $rc
}

WP=Sources/EGGFlashCore/src/WritePhase.cpp
FC=Sources/EGGFlashCore/src/FlashCommands.cpp
FW=Sources/EGGFlashCore/src/Firmware.cpp
FWH=Sources/EGGFlashCore/include/egg/Firmware.h

# ONE list. Backup and restore are both derived from it, because they used to be
# two hardcoded lists and the second one drifted the moment a fourth file was
# added: a mutant editing Firmware.h applied, restore() did not know that file
# existed, and the mutation survived into every LATER mutant -- which then made
# two correctly-labelled equivalent mutants look wrongly labelled. The harness
# accused itself of a bug it did not have. Add a file here and nowhere else.
MUTABLE=("$WP" "$FC" "$FW" "$FWH")

BACKUP=$(mktemp -d)
cp "${MUTABLE[@]}" "$BACKUP/"

restore() {
  local f
  for f in "${MUTABLE[@]}"; do cp "$BACKUP/$(basename "$f")" "$f"; done
  touch "${MUTABLE[@]}"
}
trap 'restore; cmake --build build --target test-flash >/dev/null 2>&1; rm -rf "$BACKUP"' EXIT

KILLED=0; HOLES=0; EQUIV_OK=0; EQUIV_BAD=0; BROKEN=0

# The clean binary's hash. Any mutant build equal to this did not take effect.
restore
cmake --build build --target test-flash >/dev/null 2>&1 || {
  echo "the CLEAN tree does not build -- nothing below would mean anything"; exit 2; }
CLEAN_HASH=$(shasum -a 256 ./build/test-flash | cut -d' ' -f1)
./build/test-flash >/dev/null 2>&1 || {
  echo "the CLEAN tree does not PASS its own tests -- fix that first"; exit 2; }

mutate() {  # <kill|equiv> <name> <file> <old|||new>
  local kind="$1" name="$2" file="$3" expr="$4"
  restore
  if ! python3 - "$file" "$expr" <<'PY'
import sys
p, expr = sys.argv[1], sys.argv[2]
s = open(p).read()
old, new = expr.split("|||")
if old not in s:
    sys.exit(3)
open(p, "w").write(s.replace(old, new, 1))
PY
  then echo "  ????      $name -- mutation did not apply"; BROKEN=$((BROKEN+1)); return; fi
  # Delete the OBJECT FILES, not just the binary.
  #
  # ROOT CAUSE, found 2026-09-05 and worth naming so nobody "simplifies" this
  # back: make compares mtimes at ONE-SECOND granularity, and a mutate ->
  # build -> test cycle here takes well under a second. `touch` sets the source
  # to the current second, the object was written in that same second, and make
  # concludes the object is up to date. So the rebuild silently does not
  # recompile, leaves an object built from clean source, links a
  # "mutant" binary that contains no mutation, and the suite then passes -- and
  # a pass is reported as SURVIVED, i.e. as a hole in the tests. That is the
  # single worst direction for this script to be wrong in, because a hole that
  # is not real gets investigated while the reverse would not.
  #
  # This was diagnosed, not guessed: with the mutation demonstrably in the
  # source (grep for MUTANT) the suite still printed "all passed (0 failures)"
  # and, specifically, "inv 3 end-to-end: every index the device saw is in
  # [0x34,0x74]" PASSED under a mutation that makes deviceIndex(0) return 0x33.
  #
  # Comparing the binary's hash against a clean build does NOT catch it: these
  # builds are not reproducible, and identical source produced three different
  # hashes across five rebuilds. So hashing proves nothing either way and the
  # only reliable move is to make a stale object impossible.
  #
  # The same bug bit Tests/test_config_set.sh's self-check within the hour, so
  # it is a property of this repo's build, not of one script.
  rm -f ./build/test-flash
  find build -name '*.o' -path '*EGGFlashCore*' -delete 2>/dev/null
  touch "$file"
  if ! cmake --build build --target test-flash >/dev/null 2>&1 \
     || [ ! -x ./build/test-flash ]; then
    echo "  ????      $name -- did not compile"; BROKEN=$((BROKEN+1)); return
  fi
  local rc=0
  local log; log=$(mktemp)
  runlimitedto "$log" 90 ./build/test-flash || rc=$?
  # Grade on the SUITE'S OWN VERDICT as well as the exit code, and shout when
  # they disagree. The exit code alone made this script unreadable: it cannot
  # tell "the suite passed" from "the suite never ran".
  local nfail; nfail=$(grep -c '^  FAIL' "$log" 2>/dev/null); nfail=${nfail:-0}
  local verdict; verdict=$(grep -Eo '(all passed|FAILED) \([0-9]+ failure' "$log" 2>/dev/null | head -1)
  # The exit code and the suite's own verdict must agree. If they ever do not,
  # the run is not evidence about the mutant at all and must not be graded.
  if { [ $rc -eq 0 ] && [ "$nfail" -gt 0 ]; } \
  || { [ $rc -eq 1 ] && [ "$nfail" -eq 0 ]; }; then
    echo "  ????      $name -- exit code $rc disagrees with the suite's own"
    echo "            count of $nfail failures. Not graded; the harness is wrong."
    BROKEN=$((BROKEN+1)); rm -f "$log"; return
  fi
  rm -f "$log"
  local how="test failures"
  [ $rc -ge 128 ] && how="hung or crashed, signal $((rc-128))"
  if [ "$kind" = kill ]; then
    if [ $rc -eq 0 ]; then
      echo "  SURVIVED  $name   <-- A HOLE. the suite does not catch this."
      HOLES=$((HOLES+1))
    else
      echo "  killed    $name   ($how)"
      KILLED=$((KILLED+1))
    fi
  else
    if [ $rc -eq 0 ]; then
      echo "  equiv-ok  $name   (survives, as it must)"
      EQUIV_OK=$((EQUIV_OK+1))
    else
      echo "  UNEXPECTED $name   <-- called equivalent but the suite killed it."
      echo "             The reasoning behind that label is wrong. Investigate."
      EQUIV_BAD=$((EQUIV_BAD+1))
    fi
  fi
}

echo "Mutation testing EGGFlashCore"
echo

# --- Real defects. All of these must be killed. ----------------------------

# Trust the write's own status and never compare the read-back bytes. Caught
# only by a corruption that preserves the 16-bit sum, which is why that fault
# had to be added to the mock -- before it, the two guards were redundant
# against every fault available and NEITHER was being measured.
mutate kill "skips read-back byte comparison" "$WP" \
'            const bool bytesMatch =
                std::memcmp(back.data() + 16, src, kBlockSize) == 0;|||            const bool bytesMatch = true;  // MUTANT'

# The vendor's own bug, §5.6: decide the rewrite worked by reading byte 1 of
# the SOURCE IMAGE rather than the device response.
mutate kill "copies the vendor §5.6 source-pointer bug" "$WP" \
'            if (bytesMatch && sumMatch) break;|||            if (src[1] == 0x01) break;  // MUTANT'

# Off-by-one on the first index: writes one block BELOW the application region.
# The only outcome that is not recoverable by holding LEFT+RIGHT at plug-in.
mutate kill "off-by-one below kBlockFirst" "$FW" \
'    return static_cast<std::uint8_t>(kBlockFirst + i);|||    return static_cast<std::uint8_t>(kBlockFirst + i - 1);  // MUTANT'

mutate kill "removes the block-index range guard" "$FC" \
'    if (deviceIndex < kBlockFirst || deviceIndex > kBlockLast)
        throw std::out_of_range(
            "block index " + std::to_string(deviceIndex) + " is outside [" +|||    if (false)
        throw std::out_of_range(
            "block index " + std::to_string(deviceIndex) + " is outside [" +'

mutate kill "per-block checksum not truncated to 16 bits" "$FC" \
'    const std::uint16_t sum = blockChecksum(payload, payloadLen);|||    std::uint32_t s32 = 0; for (std::size_t k = 0; k < payloadLen; ++k) s32 += payload[k];
    const std::uint16_t sum = static_cast<std::uint16_t>(s32 >> 4);  // MUTANT'

# What a decompiler-only reading of FUN_00401890 produces.
mutate kill "start command declares a zero checksum" "$FC" \
'    f[17] = static_cast<std::uint8_t>(wholeChecksum & 0xFF);|||    f[17] = 0;  // MUTANT: what the decompiled C produces'

# Updaters 1.04, 1.06 and 1.07 each ship a DIFFERENT FWFILE 140, every one of
# them 66560 bytes and 65 blocks. Only the hash separates them.
mutate kill "accepts an image with the wrong SHA-256" "$FW" \
'    if (sha != kExpectedSha256) {|||    if (false) {  // MUTANT'

mutate kill "whole-image checksum truncated to 16 bits" "$FW" \
'    std::uint32_t sum = 0;
    for (std::uint8_t b : image) sum += b;
    return sum;|||    std::uint16_t sum = 0;
    for (std::uint8_t b : image) sum = static_cast<std::uint16_t>(sum + b);
    return sum;  // MUTANT'

# CLAUDE.md §4.3 lists four invariants to assert, and the fourth is "the
# firmware resource identifier is never anything but the single hardcoded
# constant". Nothing measured it until now. Updater 1.10 carries six FWFILE
# resources, all 66,560 bytes, all 65 blocks; id 142 is the nastiest wrong
# answer because it changes on exactly the releases 140 does, which is the
# pattern that invites "the updater must use both". It does not -- one
# `push $0x8c` at 0x0040320c names 140 and nothing else can name FWFILE at all
# (notes/flash-wire-observed.md §5).
#
# Picking 142 yields a right-sized, right-shaped image that would produce valid
# per-block checksums and read back exactly as written. §2: "nothing downstream
# of us catches a wrong-but-well-formed image." So the SHA-256 pin is the ONLY
# thing between this mutant and flashing another product's firmware.
mutate kill "extracts FWFILE 142 instead of 140" "$FWH" \
'inline constexpr std::uint16_t kResourceName = 140;|||inline constexpr std::uint16_t kResourceName = 142;  // MUTANT'

# --- Equivalent mutants. These MUST survive. -------------------------------

# Comparing 1024 bytes is strictly stronger than comparing their 16-bit sum.
# With the byte comparison intact, removing the checksum comparison cannot
# admit a wrong image -- any corruption it would catch, memcmp catches too.
# It is defence in depth against a bug in our own memcmp, and nothing else, so
# no behavioural test can distinguish it. Recorded rather than papered over.
# Mutants the CAPTURE taught us to fear. Each is an offset that still produces a
# well-formed frame with a valid checksum, so every check the DEVICE performs
# passes on it. Only a fixed reference catches this class, which is why
# Tests/test_flash.cpp now pins the layout against the bytes the vendor sent.
mutate kill "block count moved to frame [15]" "$FC" \
'    f[16] = blockCount;|||    f[15] = blockCount;  // MUTANT'

mutate kill "whole-image sum shifted down two bytes" "$FC" \
'    f[17] = static_cast<std::uint8_t>(wholeChecksum & 0xFF);|||    f[15] = static_cast<std::uint8_t>(wholeChecksum & 0xFF);  // MUTANT'

mutate kill "read-back compared at the write offset, not the response offset" "$WP" \
'                std::memcmp(back.data() + 16, src, kBlockSize) == 0;|||                std::memcmp(back.data() + 15, src, kBlockSize) == 0;  // MUTANT'

mutate kill "device checksum read from the index field" "$WP" \
'                static_cast<std::uint16_t>(back[6] | (back[7] << 8));|||                static_cast<std::uint16_t>(back[4] | (back[5] << 8));  // MUTANT'

mutate equiv "removes the per-block checksum comparison" "$WP" \
'            const bool sumMatch = devSum == blockChecksum(src, kBlockSize);|||            const bool sumMatch = true;  // MUTANT (equivalent)'

# The write command carries a 16-bit index at [2..3] while the read carries 8
# bits at [2] (§3.4). Every index this tool can emit is in [0x34,0x74], so the
# high byte is always zero and dropping it changes no reachable frame. We keep
# it because reproducing the vendor exactly is free; we do not pretend a test
# could tell the difference.
mutate equiv "write index loses its high byte" "$FC" \
'    f[3] = static_cast<std::uint8_t>((deviceIndex >> 8) & 0xFF);|||    // MUTANT (equivalent): high byte dropped'

echo
echo "killed $KILLED of $((KILLED+HOLES)) real defects; $EQUIV_OK of $((EQUIV_OK+EQUIV_BAD)) equivalent mutants behaved as predicted"
[ "$BROKEN" -gt 0 ] && echo "$BROKEN mutants failed to apply or compile -- they measured nothing."
FAIL=0
[ "$HOLES" -gt 0 ] && { echo "$HOLES HOLE(S). Each is a bug the suite would not notice."; FAIL=1; }
[ "$EQUIV_BAD" -gt 0 ] && { echo "$EQUIV_BAD equivalent mutant(s) were killed -- the label is wrong."; FAIL=1; }
[ "$BROKEN" -gt 0 ] && FAIL=1
[ "$KILLED" -eq 0 ] && { echo "ZERO real defects killed. §6.2: a red flag, not a pass."; FAIL=1; }
[ "$FAIL" -eq 0 ] && echo "The suite can produce a bad result, which is what makes its passes mean anything."
exit $FAIL
