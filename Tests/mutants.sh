#!/bin/bash
# mutants.sh -- do test-flash and test-config actually catch anything?
#
# CLAUDE.md §6.2: "A harness that cannot produce a bad result is not evidence.
# If the planted positives never fail and no verdict is ever rejected, the
# harness is measuring nothing. Report its failure rate; a rate of zero is a red
# flag, not a pass."
#
# test-flash passed 47/47 on its first run, which is exactly the shape §6.2 says
# to distrust. test-config then passed 52/52 on ITS first run, for the same
# reason and with the same lack of evidence behind it. So this plants known bugs
# in both, rebuilds, and requires the relevant suite to notice. Each mutant is a
# bug someone could plausibly write -- several are the vendor's own
# (updater-protocol.md §5.6) -- not a character swap.
#
# The target is derived from the file being mutated: anything under
# EGGConfigCore is graded by test-config, everything else by test-flash. One
# harness, because the three bugs this script has had were all in the harness
# and having two copies of it would mean finding each of them twice.
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
CS=Sources/EGGConfigCore/src/ConfigSession.cpp
CR=Sources/EGGConfigCore/src/ConfigRecord.cpp
CV=Sources/EGGConfigCore/src/RecordVault.cpp
MB=Sources/EGGFlashCore/src/MockBootloader.cpp
# Added 2026-09-05 with the read-back and the approval token. It was NOT here
# when those were written, so the first mutation run after they landed said
# nothing about them -- recorded as a gap in working-memory.md before it was
# fixed, per §1.7. FlashPlan.cpp holds a property §4.2 depends on: the read-back
# runs BEFORE erase, where abort is always correct, so it must give up rather
# than spin the way driveToVerifiedImage deliberately does.
FP=Sources/EGGFlashCore/src/FlashPlan.cpp

# Added 2026-09-06, closing a gap that had been recorded in working-memory.md
# since the file was written: it holds the reconnect budget and the
# Disconnected-vs-SendFailed distinction that driveToVerifiedImage branches on,
# in the post-erase phase that by design cannot give up -- and NOTHING graded a
# line of it, because nothing but a mouse in bootloader mode could reach one.
#
# It is gradeable now because its DECISIONS were pulled out as pure functions
# (HidBootloaderLink.h, same date). What is still ungraded is attach(), open(),
# and the two bodies that call those functions: they need hardware, and no
# mutant below pretends otherwise.
HL=Sources/EGGFlashCore/src/HidBootloaderLink.cpp

# ONE list. Backup and restore are both derived from it, because they used to be
# two hardcoded lists and the second one drifted the moment a fourth file was
# added: a mutant editing Firmware.h applied, restore() did not know that file
# existed, and the mutation survived into every LATER mutant -- which then made
# two correctly-labelled equivalent mutants look wrongly labelled. The harness
# accused itself of a bug it did not have. Add a file here and nowhere else.
MUTABLE=("$WP" "$FC" "$FW" "$FWH" "$CS" "$CR" "$CV" "$FP" "$MB" "$HL")

BACKUP=$(mktemp -d)
cp "${MUTABLE[@]}" "$BACKUP/"

restore() {
  local f
  for f in "${MUTABLE[@]}"; do cp "$BACKUP/$(basename "$f")" "$f"; done
  touch "${MUTABLE[@]}"
}
trap 'restore; cmake --build build >/dev/null 2>&1; rm -rf "$BACKUP"' EXIT

# Which suite grades a mutation, and which object files must die for the
# rebuild to be real. Derived from the path so that adding a mutant never means
# remembering to say which target it belongs to -- the drift that already cost
# this script one false result when MUTABLE was two hardcoded lists.
target_for() {
  case "$1" in
    Sources/EGGConfigCore/*) echo "test-config" ;;
    *)                       echo "test-flash" ;;
  esac
}
objdir_for() {
  case "$1" in
    Sources/EGGConfigCore/*) echo "EGGConfigCore" ;;
    *)                       echo "EGGFlashCore" ;;
  esac
}

KILLED=0; HOLES=0; EQUIV_OK=0; EQUIV_BAD=0; BROKEN=0

# The clean binary's hash. Any mutant build equal to this did not take effect.
restore
cmake --build build >/dev/null 2>&1 || {
  echo "the CLEAN tree does not build -- nothing below would mean anything"; exit 2; }
for t in test-flash test-config; do
  ./build/$t >/dev/null 2>&1 || {
    echo "the CLEAN tree does not PASS $t -- fix that first"; exit 2; }
done

mutate() {  # <kill|equiv> <name> <file> <old|||new>
  local kind="$1" name="$2" file="$3" expr="$4"
  # HARNESS BUG #5, 2026-09-05, and the worst one yet because it MANUFACTURES
  # "killed" verdicts rather than losing them. MockBootloader.cpp was given a
  # variable ($MB) and mutants, and was never added to MUTABLE -- which is the
  # only list restore() walks. So its mutation was never reverted, every
  # subsequent mutant ran against a suite that was ALREADY FAILING, and all of
  # them were graded killed for free. The run reported 43/43 and meant nothing
  # from that point on. Two mutants labelled equivalent were reported as
  # "killed", which is the only reason it was noticed at all.
  #
  # A file not in MUTABLE is now fatal, not silent. The check is one line; the
  # bug cost a full 10-minute run and would have been invisible in any run
  # whose equivalent mutants all happened to come first.
  local m found=0
  for m in "${MUTABLE[@]}"; do [ "$m" = "$file" ] && found=1; done
  if [ $found -eq 0 ]; then
    echo "FATAL: $file is mutated but is not in MUTABLE, so restore() will"
    echo "       never revert it and every later verdict would be garbage."
    exit 2
  fi
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
  local target; target=$(target_for "$file")
  local objdir; objdir=$(objdir_for "$file")
  rm -f "./build/$target"
  find build -name '*.o' -path "*$objdir*" -delete 2>/dev/null
  touch "$file"
  if ! cmake --build build --target "$target" >/dev/null 2>&1 \
     || [ ! -x "./build/$target" ]; then
    echo "  ????      $name -- did not compile"; BROKEN=$((BROKEN+1)); return
  fi
  local rc=0
  local log; log=$(mktemp)
  runlimitedto "$log" 90 "./build/$target" || rc=$?
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


# --- HidBootloaderLink's decisions. ----------------------------------------
# Every one of these is a bug that would only ever show up on the physical
# mouse, after the erase, with no way to abort.

# The distinction the post-erase loop branches on, backwards: a departed device
# is reported as a send failure, so the phase retries into a handle the kernel
# has torn down instead of reconnecting. It would spin until the budget in
# driveToVerifiedImage ran out, on a device with no valid application.
mutate kill "send: Disconnected and SendFailed swapped" "$HL" \
'            return devicePresent ? Io::SendFailed : Io::Disconnected;|||            return devicePresent ? Io::Disconnected : Io::SendFailed;  // MUTANT'

mutate kill "recv: Disconnected and RecvFailed swapped" "$HL" \
'            return devicePresent ? Io::RecvFailed : Io::Disconnected;|||            return devicePresent ? Io::Disconnected : Io::RecvFailed;  // MUTANT'

# BadStatus means "the device answered, and resp[1] was not 0x01". The bytes are
# valid and the status is the caller's to judge. Dropping it here turns every
# not-yet-ready answer into an indistinguishable failure, which is exactly the
# information WritePhase's retry logic runs on.
mutate kill "recv: BadStatus collapsed into a failure" "$HL" \
'        case egg::Outcome::BadStatus:
        case egg::Outcome::BusyTimeout:|||        case egg::Outcome::BusyTimeout:  // MUTANT: BadStatus arm removed'

# A short read accepted as Ok. Downstream that is a block whose read-back
# comparison runs over bytes the device never sent.
mutate kill "recv: length no longer decides Ok" "$HL" \
'            return got == want ? Io::Ok : Io::ShortRead;|||            return Io::Ok;  // MUTANT'

# Off-by-one on the reconnect budget: one fewer attempt than the constant says.
# Harmless-looking, and it is the difference between the log saying what the
# header promises and the log being wrong about the only number a human has to
# go on while the mouse is not a mouse.
mutate kill "reconnect: budget loses its last attempt" "$HL" \
'    for (unsigned waited = 0; waited <= budgetMs; waited += stepMs) {|||    for (unsigned waited = 0; waited < budgetMs; waited += stepMs) {  // MUTANT'

# Sleep before the first try. Costs a full step on every reconnect where the
# device never actually left -- inside the loop that has no timeout.
mutate kill "reconnect: sleeps before trying" "$HL" \
'        if (tryAttach()) {|||        sleep(stepMs);
        if (tryAttach()) {  // MUTANT: a step wasted before the first try'

# The elapsed time reported as the budget. A human reading "reconnected after
# 20000 ms" when it took 400 concludes the device is failing.
mutate kill "reconnect: reports the budget instead of the elapsed time" "$HL" \
'            note("reconnected to the bootloader after " +
                 std::to_string(waited) + " ms");|||            note("reconnected to the bootloader after " +
                 std::to_string(budgetMs) + " ms");  // MUTANT'

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

# ---- FlashPlan.cpp: the read-back, which is what makes an erase reversible ---

# THE property. readApplicationRegion runs before A0 03, where §4.2 says abort
# is always correct. Removing the attempt cap turns it into an infinite loop
# against a device that refuses A0 07 -- and the test that catches this is the
# one driving a mock with nothing preloaded, i.e. the OTHER answer to the open
# question about whether the bootloader serves reads outside a flash session.
mutate kill "read-back retries forever instead of giving up" "$FP" \
'    for (unsigned attempt = 0; attempt < kReadBlockTries; ++attempt) {|||    for (unsigned attempt = 0; ; ++attempt) {  // MUTANT'

# A backup assembled from rejected responses is not a backup. Dropping the
# status check makes every refusal look like a good block, and the file written
# afterwards would be 66560 bytes of whatever the reject path returned.
mutate kill "read-back accepts any status, not just ready" "$FP" \
'            if (st == egg::kStatusReady && back.size() == kLargeLen) { got = true; break; }|||            if (back.size() == kLargeLen) { got = true; break; }  // MUTANT'

# The payload is at +0x10 in the 1041-byte response, the same place the write
# command puts it. Reading from 0 would silently shift every block by 16 bytes
# and produce a plausible-looking, useless backup.
mutate kill "read-back copies from the frame start, not the payload offset" "$FP" \
'        rb.image.insert(rb.image.end(), back.begin() + kPayloadOffset,|||        rb.image.insert(rb.image.end(), back.begin(),  // MUTANT'

# §4.2c: the token must be a function of the IMAGE. Hashing only the first
# frame makes every image produce the same token, so --confirm would check
# nothing while still looking like it checked something.
mutate kill "approval token ignores the image bytes" "$FP" \
'    for (const auto& f : frames)
        all.insert(all.end(), f.begin(), f.end());|||    all = frames.front();  // MUTANT'

# §4.2b as revised 2026-09-05: the plan opens with EXACTLY ONE A1 3A, because
# that is what the vendor's capture opens with. A second one is not a harmless
# repeat -- it would be sent to a device already in the bootloader, an
# interaction nothing has ever observed, and it breaks the byte-for-byte match
# with the capture that is the whole basis for trusting this sequence.
mutate kill "the plan sends A1 3A twice" "$FP" \
'    out.push_back(bootloaderStart(|||    out.push_back(enterBootloader());  // MUTANT
    out.push_back(bootloaderStart('

# ---- What reaches the wire. ------------------------------------------------
#
# THE DEFECT THIS PAIR EXISTS FOR was real and shipped: preflight() sent a
# "benign" A1 08 round trip before A0 03, so the wire carried 134 frames where
# plannedFrames() had 133. Every test passed, because test_golden_vendor.py
# checks the PLAN against the capture and nothing checked the plan against the
# code. Found by driving the real path against the mock and counting.
#
# preflight takes no link now, so the defect cannot be written there again. It
# can still be written one function later, which is what this plants.
mutate kill "an extra frame is sent before the erase" "$WP" \
'Progress driveToVerifiedImage(BootloaderLink& link, const Image& img) {
    Progress p;|||Progress driveToVerifiedImage(BootloaderLink& link, const Image& img) {
    Progress p;
    roundTrip(link, wholeImageChecksumQuery(kBlockLast), 100,  // MUTANT
              kReportSmall, kSmallLen);'

# The other direction: a frame the vendor DOES send going missing. The per-block
# read-back is the verification, so dropping it also drops a frame -- and a
# count that matches the vendor is only meaningful if both directions are caught.
mutate kill "the per-block read-back frame is never sent" "$WP" \
'            std::vector<std::uint8_t> back;
            const int st = roundTrip(link, readBlock(idx), 50, kReportLarge,
                                     kLargeLen, &back);|||            std::vector<std::uint8_t> back(kLargeLen, 0);  // MUTANT
            back[1] = kReady;
            const int st = kReady;'

# The report id is part of the request. §5.6: the vendor's own repair loop
# passes 0xA0 to a fixed 64-byte read, so this is not a hypothetical slip --
# it is a mistake that shipped in the code we derived this from. The mock could
# not catch it until 2026-09-05 (it ignored the id and truncated), so this
# mutant is also the check that the mock fix actually measures something.
mutate kill "the write status is read as the large report" "$WP" \
'            if (roundTrip(link, writeBlock(idx, src, kBlockSize), 50,
                          kReportSmall, kSmallLen) != kReady) {|||            if (roundTrip(link, writeBlock(idx, src, kBlockSize), 50,
                          kReportLarge, kLargeLen) != kReady) {  // MUTANT'

# ---- The two defects the 2026-09-05 adversarial audit found. ----------------
# Both restore the EXACT code that shipped, so a survivor here means the fix was
# committed with nothing to hold it in place. Both are expected to be killed by
# TIMEOUT rather than by an assertion, because both defects hang: that is what
# made them dangerous, and inside driveToVerifiedImage a hang has no exit.
mutate kill "A1 09 is retried forever, hanging on a correctly-flashed mouse" "$WP" \
'    for (unsigned attempt = 1; attempt <= kCompleteAttempts; ++attempt) {|||    for (unsigned attempt = 1;; ++attempt) {  // MUTANT'

mutate kill "the repair pass skips a block that will not read back" "$WP" \
'                if (readable &&
                    std::memcmp(back.data() + 16, img.block(i), kBlockSize) == 0)
                    continue;|||                if (!readable ||   // MUTANT
                    std::memcmp(back.data() + 16, img.block(i), kBlockSize) == 0)
                    continue;'

# The mock faults those two tests need. A fault the mock cannot express is a
# defect the suite cannot see, so the faults are themselves load-bearing and
# get mutated like anything else.
mutate kill "the mock always acks A1 09, so the timing race is untestable" "$MB" \
'        if (f_.neverAckComplete) { pending_.clear(); break; }|||        if (false) { pending_.clear(); break; }  // MUTANT'

mutate kill "the mock never lets a written block go bad" "$MB" \
'        if (f_.unreadableAfterVerify >= 0 &&|||        if (false &&  // MUTANT'


# ---- The backup gate. -----------------------------------------------------
#
# These matter more than their size suggests. `flash` used to guarantee §4.2 by
# READING the application region out immediately before erasing it; on
# 2026-09-05 that was replaced by checking a file a previous run wrote. A check
# that stands in for a mechanism is only as good as the tests on it, and none of
# these failures is visible in the byte stream -- the flash proceeds and looks
# exactly right.

# The +1 read is the whole reason checkBackupFile allocates kFull+1. Asking for
# exactly kFull bytes makes a too-long file pass by silent truncation, which is
# how a config dump, a firmware image for another product, or a concatenation
# gets accepted as a backup.
mutate kill "backup accepts a file that is too long, by truncating it" "$FP" \
'    std::vector<std::uint8_t> buf(kBlockCount * kBlockSize + 1);|||    std::vector<std::uint8_t> buf(kBlockCount * kBlockSize);  // MUTANT'

mutate kill "backup accepts anything at least a full image long" "$FP" \
'    if (got != kBlockCount * kBlockSize) {|||    if (got < kBlockCount * kBlockSize) {  // MUTANT'

# A right-sized file of one repeated byte is what a read loop that returned
# nothing leaves on disk. Accepting it means erasing with an undo that is 66560
# copies of 0x00.
mutate kill "backup skips the failed-read check entirely" "$FP" \
'    if (!varied) {|||    if (false) {  // MUTANT'

# Scanning only the head passes a file whose first bytes happen to differ, and
# -- the direction that matters -- REJECTS a real backup whose first block is
# uniform. A false refusal before an erase is safe, but it is still wrong, and
# the test pins the boundary at the very last byte for exactly this.
mutate kill "backup's failed-read scan gives up after 16 bytes" "$FP" \
'    for (std::size_t i = 1; i < buf.size(); ++i)
        if (buf[i] != buf[0]) { varied = true; break; }|||    for (std::size_t i = 1; i < 16; ++i)  // MUTANT
        if (buf[i] != buf[0]) { varied = true; break; }'

# The digest is what a human compares between the read-firmware run and the
# flash run to confirm they are talking about the same file. One over the first
# block only still changes when the first block changes, so it looks fine.
mutate kill "backup digests only the first block" "$FP" \
'    bc.sha256 = sha256Hex(buf.data(), buf.size());|||    bc.sha256 = sha256Hex(buf.data(), kBlockSize);  // MUTANT'

mutate kill "backup reports ok before it has checked anything" "$FP" \
'    BackupCheck bc;
    if (path.empty()) {|||    BackupCheck bc;
    bc.ok = true;  // MUTANT
    if (path.empty()) {'

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

# classifyRecv handles all five Outcome enumerators and every arm returns, so
# the statement after the switch is unreachable for any value Transport can
# produce. Changing what it returns cannot change behaviour for any reachable
# input. It stays because a switch with no fallthrough return is a warning, and
# because "unreachable" is a claim (§1.2a) that this mutant is the check on: if
# the suite ever kills it, a sixth Outcome exists and this file does not know.
mutate equiv "recv: the unreachable fallthrough returns something else" "$HL" \
'    return Io::RecvFailed;
}

bool reconnectLoop|||    return Io::Disconnected;  // MUTANT (equivalent): unreachable
}

bool reconnectLoop'

echo
echo "Mutation testing EGGConfigCore"
echo

# --- Real defects. All of these must be killed. ----------------------------

# THE ONE THAT MATTERS MOST. §4.1: "Never let a reported success stand in for
# verifying the data itself." Trust the device's acknowledgement and skip the
# read-back entirely. A device that acks a write it never performed then looks
# identical to one that worked, which is exactly the failure MockConfigDevice's
# falseSuccessRate exists to produce.
mutate kill "trusts the write ack instead of reading back" "$CS" \
'    if (!read(o.after, rr)) { o.result = Result::VerifyReadFailed; return o; }|||    if (!read(o.after, rr)) { o.result = Result::Ok; return o; }  // MUTANT'

# Read back, then compare against the wrong thing. The read happens, the diff is
# computed, everything looks diligent, and the comparison is vacuous.
mutate kill "verifies the read-back against itself" "$CS" \
'    if (o.after[at] != want) { o.result = Result::VerifyMismatch; return o; }|||    if (o.after[at] != o.after[at]) { o.result = Result::VerifyMismatch; return o; }  // MUTANT'

# §4.1: "Never write after a failed read." Carry on with whatever `before`
# happens to contain.
mutate kill "writes even after the read failed" "$CS" \
'    if (!read(o.before, o.result)) return o;

    const std::size_t at = kPayloadOffset + f.recordOffset;|||    read(o.before, o.result);  // MUTANT
    if (o.before.size() != kLargeLen) o.before.assign(kLargeLen, 0);

    const std::size_t at = kPayloadOffset + f.recordOffset;'

# §4.1: "Read-modify-write always. Never construct a settings blob from
# scratch." Send a zeroed payload with one byte set, which is a factory reset
# performed by accident on every single write.
mutate kill "builds the payload from scratch instead of the read" "$CS" \
'    std::memcpy(frame.data() + kPayloadOffset,
                before.data() + kPayloadOffset, kPayloadLen);

    const std::size_t at = kPayloadOffset + f.recordOffset;|||    // MUTANT: no copy; the payload stays zeroed
    const std::size_t at = kPayloadOffset + f.recordOffset;'

# The 16-byte-early bug. A record offset is not a frame offset, and writing the
# field at f.recordOffset lands in the header instead of the payload. This is a
# real bug this project already made once; test_config_replay.py caught it.
mutate kill "writes the field at the record offset, not the payload offset" "$CS" \
'    const std::size_t at = kPayloadOffset + f.recordOffset;
    frame[at] = composeByte(before[at], f, encodedValue);|||    const std::size_t at = f.recordOffset;  // MUTANT
    frame[at] = composeByte(before[at], f, encodedValue);'

# A redundant `set` must send nothing. Under MatchVendor the frame legitimately
# differs from the read at record 0x01..0x04, so testing the FRAME rather than
# the FIELD writes on every re-apply of a setting the user already has.
mutate kill "a redundant set still writes, to normalise the policy bytes" "$CS" \
'    if (o.before[at] == want) { o.result = Result::AlreadySet; return o; }|||    // MUTANT: field-level no-op check removed'

# The unknown-byte policy must not fire under Preserve. Removing the early
# return makes Preserve behave as MatchVendor, i.e. silently ignores the flag.
mutate kill "the Preserve policy secretly zeroes the unknown bytes anyway" "$CS" \
'    if (policy != UnknownBytes::MatchVendor) return;|||    // MUTANT: policy ignored'

# Act on any record the device returns. §4.1: "Validate a read is structurally
# plausible before acting on it."
mutate kill "skips the plausibility check on a read" "$CS" \
'    if (!(log_ ? plausible(r.buf, *log_) : plausible(r.buf))) {
        result = Result::ReadImplausible; return false;
    }|||    // MUTANT: plausibility not checked'

# The policy zeroes one byte too many, clobbering record 0x00 -- which the
# vendor DOES write and which is inside the settings proper.
mutate kill "the unknown-byte policy zeroes one byte too many" "$CS" \
'    for (std::size_t r = kRecordUnknownFirst; r <= kRecordUnknownLast; ++r)
        frame[kPayloadOffset + r] = 0x00;|||    for (std::size_t r = kRecordUnknownFirst - 1; r <= kRecordUnknownLast; ++r)  // MUTANT
        frame[kPayloadOffset + r] = 0x00;'

# Sub-byte fields clobber their neighbours. Record 0x0b carries CPI downshift
# AND smoothing AND a high nibble the vendor never writes; §1.3 applies inside a
# byte exactly as it does between bytes.
mutate kill "sub-byte writes clobber the rest of the byte" "$CR" \
'    return static_cast<std::uint8_t>((old & ~f.mask) |
                                     ((value << f.shift) & f.mask));|||    return static_cast<std::uint8_t>((value << f.shift) & f.mask);  // MUTANT'

# The known-good blob stops being known-good: every read overwrites it, so the
# undo becomes a mirror of whatever state the device is in now.
mutate kill "the vault overwrites the first record it saved" "$CV" \
'    if (holds_) return false;                      // never overwrite|||    // MUTANT: overwrite freely'

# The vault accepts a record that failed validation, which is worse than having
# no vault: it looks like an undo and is not.
mutate kill "the vault accepts an implausible record" "$CV" \
'    if (!plausible(record)) {
        err_ = "the record offered was not structurally plausible";
        return false;
    }|||    // MUTANT: anything offered is saved'

# --- Equivalent. The suite must NOT fail on these. -------------------------

# THE PRE-SEND SELF-CHECK, and this label is a finding rather than a
# convenience. It compares the frame we built against the record we read and
# requires the difference to be exactly what we meant. It cannot fire, because
# buildFrame is the only thing that produces the frame and it is deterministic:
# frame[at] is composeByte(before[at], f, v) and `want` is the SAME call, so
# sawField is always true; every other byte is either copied verbatim or set by
# applyUnknownPolicy, which policyPermits accepts. No reachable input separates
# the two branches.
#
# That does NOT make the check pointless, and calling this "a hole" would be the
# wrong reading. Its job is to catch a FUTURE bug in buildFrame, and the four
# buildFrame mutants above are what demonstrate such bugs exist to be caught.
# What it means is that today the check is REDUNDANT against everything the
# suite can produce -- exactly the situation §6.2 says to report rather than
# paper over with a test that asserts something untrue.
mutate equiv "removes the pre-send self-check (redundant, not useless)" "$CS" \
'    if (!selfOk || !sawField) { o.result = Result::RefusedSelfCheck; return o; }|||    if (false) { o.result = Result::RefusedSelfCheck; return o; }  // MUTANT (equivalent)'

# applyUnknownPolicy is the only writer of those four bytes and it writes 0x00
# unconditionally, so by the time policyPermits sees a ByteChange for one of
# them, c.after is 0x00 already. The clause is defence against a change in
# applyUnknownPolicy, not a live condition -- so no reachable input can tell the
# difference, and a test that "caught" this would be asserting something untrue.
mutate equiv "drops a redundant clause from the policy self-check" "$CS" \
'           c.recordOffset <= kRecordUnknownLast &&
           c.after == 0x00;|||           c.recordOffset <= kRecordUnknownLast;  // MUTANT (equivalent)'

# Transport::frame(kReportLarge, ...) returns exactly wireLength(0xA0) = 1041 by
# construction, so this guard is unreachable. We keep it because Transport is
# the layer we would least like to be wrong about, not because it fires.
mutate equiv "removes an unreachable length guard in buildFrame" "$CS" \
'    std::vector<std::uint8_t> frame = Transport::frame(kReportLarge, kWriteSettings);
    if (frame.size() != kLargeLen) return {};

    // Read-modify-write|||    std::vector<std::uint8_t> frame = Transport::frame(kReportLarge, kWriteSettings);
    // MUTANT (equivalent): guard removed

    // Read-modify-write'

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
