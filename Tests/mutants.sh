#!/bin/bash
# mutants.sh -- do test-flash and test-config actually catch anything?
#
# engineering-rules.md §6.2: "A harness that cannot produce a bad result is not evidence.
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

# The two off-wire guards chosen on 2026-09-06 -- the restore provenance
# sidecar and the bootloader entry receipt. Both are pure file logic with no
# device in them, so every line here is gradeable without hardware, which is
# exactly the property that made them worth putting in the library instead of
# in main.cpp's printf blocks.
PV=Sources/EGGFlashCore/src/Provenance.cpp

# The guard that makes the erase-through-verify unit unstoppable. It used
# to be a class inside egg-flash/main.cpp, where nothing could test it and
# nothing could mutate it -- which is how it came to leave SIGPIPE at its
# default disposition (terminate) while the phase writes to stderr.
NQ=Sources/EGGFlashCore/src/NoQuitDuringWrite.cpp

# ONE list. Backup and restore are both derived from it, because they used to be
# two hardcoded lists and the second one drifted the moment a fourth file was
# added: a mutant editing Firmware.h applied, restore() did not know that file
# existed, and the mutation survived into every LATER mutant -- which then made
# two correctly-labelled equivalent mutants look wrongly labelled. The harness
# accused itself of a bug it did not have. Add a file here and nowhere else.
# EGGCore. The bcdDevice -> version decode lives here (Device.h cites updater
# 1.10 FUN_004011f0), and it is graded by test-config, which is where its
# tests are. Note the objdir below: EGGCore is a third library and a case
# that fell through to EGGFlashCore would delete the wrong objects and grade
# a binary that never contained the mutation.
DV=Sources/EGGCore/src/Device.cpp

# The bootloader-entry state machine. Every [O] fact about A1 3A came off this
# path, and engineering-rules.md §4.2b puts the LATCHED window's opening here rather than
# at the erase -- so a bug in it costs the mouse before a single firmware byte
# has been sent. It had no mutants until 2026-09-07 despite test_flash.cpp
# driving `enterBootloaderAndConfirm` thirteen times.
BE=Sources/EGGFlashCore/src/BootloaderEntry.cpp

# The release table and its two lookups. Nothing could mutate this before
# 2026-09-07 in any meaningful sense: mutants.sh grades everything outside
# EGGConfigCore with `test-flash`, and `test-flash` never named the manifest,
# so a mutation of `releaseForUpdaterSha` -- the function that decides WHICH
# image is written to the one mouse -- would have SURVIVED by construction.
# test_flash.cpp's testManifest() is what makes these gradeable.
FM=Sources/EGGFlashCore/src/FirmwareManifest.cpp

MUTABLE=("$WP" "$FC" "$FW" "$FWH" "$CS" "$CR" "$CV" "$FP" "$MB" "$HL" "$PV" "$DV" "$NQ" "$BE" "$FM")

BACKUP=$(mktemp -d)
cp "${MUTABLE[@]}" "$BACKUP/"

restore() {
  local f
  for f in "${MUTABLE[@]}"; do cp "$BACKUP/$(basename "$f")" "$f"; done
  touch "${MUTABLE[@]}"
}
# EXIT alone is not enough, and this cost a corrupted tree on 2026-09-06:
# ctest's TIMEOUT killed this script mid-run and ConfigRecord.cpp was left
# holding `// MUTANT: no high clamp` -- a CPI ceiling silently removed. bash
# runs the EXIT trap for SIGINT/SIGTERM/SIGHUP only if they are trapped, so
# name them. SIGKILL still cannot be caught by anything; `Tools/no-mutants.sh`
# is the backstop for that, and it is what found the leftover.
trap 'restore; cmake --build build >/dev/null 2>&1; rm -rf "$BACKUP"' EXIT INT TERM HUP

# Which suite grades a mutation, and which object files must die for the
# rebuild to be real. Derived from the path so that adding a mutant never means
# remembering to say which target it belongs to -- the drift that already cost
# this script one false result when MUTABLE was two hardcoded lists.
target_for() {
  case "$1" in
    Sources/EGGConfigCore/*) echo "test-config" ;;
    Sources/EGGCore/*)       echo "test-config" ;;
    *)                       echo "test-flash" ;;
  esac
}
objdir_for() {
  case "$1" in
    Sources/EGGConfigCore/*) echo "EGGConfigCore" ;;
    Sources/EGGCore/*)       echo "EGGCore" ;;
    *)                       echo "EGGFlashCore" ;;
  esac
}

# THE DECLARED SIZE OF THIS SUITE. Checked at the end against what actually
# ran. Bump it deliberately when adding a mutant; never to make a run go green.
#
# Why it exists: on 2026-09-06 an apostrophe inside a single-quoted mutant
# pattern closed the quote, bash aborted at that line, and the 9 mutants below
# it -- including every equivalent -- never ran. The script printed nothing
# about it because the summary lives below the error too. A count fixed at the
# TOP is the only version of this check that a truncated script cannot skip.
DECLARED_KILL=134
DECLARED_EQUIV=9
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
  # HARNESS BUG #6, 2026-09-06, and the same family as the apostrophe that
  # truncated a run: `$name` is echoed inside DOUBLE quotes, so a backtick in a
  # mutant name is command substitution. `"sensor-angle decodes 0x80, which
  # `set` refuses"` ran bash's `set` builtin, which dumps every variable and
  # function, and the run printed a page of shell internals in place of the
  # mutant's name. The mutation ALSO did not apply -- a whitespace mismatch --
  # so the two failures were indistinguishable in the log.
  #
  # Refused rather than escaped: a name is a sentence for a human, and there is
  # no reason for one to contain a shell metacharacter.
  case "$name" in
    *'`'*|*'$('*)
      echo "FATAL: mutant name contains a backtick or \$( -- it is echoed"
      echo "       inside double quotes and would be executed. Reword it."
      exit 2 ;;
  esac

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

# engineering-rules.md §4.3 lists four invariants to assert, and the fourth is "the
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

# ---- The restore provenance gate and the entry receipt. --------------------
#
# Both were decided on 2026-09-06 and both fail SILENTLY when broken: the
# restore proceeds, the frames are correct, the device does what it is told. The
# only evidence a guard has stopped working is that it stopped refusing, and
# nothing on the wire shows it. That is the same shape as the backup gate above
# and it gets the same treatment.

# THE ONE THAT MATTERS. Without the sidecar check, `restore-firmware` writes any
# 66560-byte file with two different bytes in it -- which is what the verb was
# specifically not allowed to be. The answer was: yes, but only its own
# backups. This mutant is that word "only" being deleted.
mutate kill "restore accepts an image with no provenance at all" "$FW" \
'    const Provenance p = readProvenance(imagePath);
    if (!p.ok) {|||    const Provenance p = readProvenance(imagePath);
    if (false) {  // MUTANT'

# The sidecar exists but is never compared against the bytes. A backup edited,
# truncated-and-repadded, or replaced at the same path passes -- and the file
# that vouches for it is still sitting there looking correct.
mutate kill "restore never checks the image against its recorded hash" "$FW" \
'    if (sha != p.sha256) {|||    if (false) {  // MUTANT'

# Hashing only the first block. Every byte after 0x400 becomes unchecked, so a
# backup whose first 1024 bytes are intact restores whatever follows them.
mutate kill "restore hashes only the first block of the image" "$FW" \
'    const std::string sha = sha256Hex(raw.data(), raw.size());
    if (sha != p.sha256) {|||    const std::string sha = sha256Hex(raw.data(), kBlockSize);  // MUTANT
    if (sha != p.sha256) {'

# The provenance gate must be an ADDITION to the shape checks, never a way past
# them. 66560 identical bytes with a valid sidecar is a failed A0 07 loop that
# someone backed up; writing it over a working application is the worst thing
# this verb can do.
mutate kill "restore skips the failed-read check when a sidecar is present" "$FW" \
'    if (!varied) {
        char b[64];|||    if (false) {  // MUTANT
        char b[64];'

mutate kill "restore accepts any file at least a full image long" "$FW" \
'    if (raw.size() != kExpectedImageSize) {
        error = imagePath + " is " + std::to_string(raw.size()) +|||    if (raw.size() < kExpectedImageSize) {  // MUTANT
        error = imagePath + " is " + std::to_string(raw.size()) +'

# The magic line is what stops any file called <image>.origin from counting.
mutate kill "provenance accepts a sidecar written by something else" "$PV" \
'    if (magic != kProvenanceMagic) {|||    if (false) {  // MUTANT'

# A sidecar with no sha256 line at all would otherwise compare against "" --
# and the empty string is not the hash of anything, so this one fails closed
# rather than open. It is still a mutant worth planting: the failure it causes
# is a real backup being REFUSED, which sends a user looking for an override.
mutate kill "provenance accepts a sidecar that records no hash" "$PV" \
'    if (vals[0].empty()) { p.reason = path + " records no sha256"; return p; }|||    if (false) { }  // MUTANT'

# restore-firmware requires --backup (a fresh read of what is on the device NOW)
# to be a DIFFERENT file from the image being restored. A lexical compare misses
# ./x.bin vs x.bin, which is the same file spelled two ways -- and then the rule
# "never erase without a saved copy of what is being erased" is satisfied by a
# file that does not describe what is being erased.
mutate kill "the same-file guard compares strings, so ./x and x differ" "$PV" \
'    if (a == b) return true;
    char ra[4096], rb[4096];|||    if (a == b) return true;
    return false;  // MUTANT
    char ra[4096], rb[4096];'

mutate kill "the same-file guard calls two paths the same when neither resolves" "$PV" \
'    if (!pa || !pb) return false;|||    if (!pa || !pb) return true;  // MUTANT'

# The receipt lives in HOME, not the working directory: a Finder-launched .app
# runs with cwd "/" and could never write one there, so a cwd-relative path made
# the GUI's flash refuse every time. Reverting it to cwd must fail the suite.
mutate kill "the receipt path goes back to the working directory" "$PV" \
'    if (const char* home = std::getenv("HOME"))
        if (*home) return std::string(home) + "/" + kEntryReceiptName;|||    // MUTANT'

# ---- The entry receipt gate. ------------------------------------------------
#
# §4.2b: "EVERYTHING THAT TOUCHES FIRMWARE ENTERS BY A1 3A." Until 2026-09-06
# that rule had no code behind it -- PID, bcdDevice and product string are
# identical for a button entry, so nothing could tell them apart. These three
# are the rule becoming enforceable and then not being enforced.

# A receipt copied from another machine, or hand-edited to get past the gate,
# is present and well-formed and describes something else. Without this
# comparison the gate checks that a FILE exists rather than that THIS mouse was
# entered by A1 3A, which is a different and much weaker claim.
mutate kill "the entry gate ignores which bootloader the receipt describes" "$PV" \
'    if (g.receipt.present &&
        (g.receipt.bcdDevice != liveBcd || g.receipt.product != liveProduct)) {|||    if (false) {  // MUTANT'

mutate kill "the entry gate checks the bcdDevice but not the product string" "$PV" \
'        (g.receipt.bcdDevice != liveBcd || g.receipt.product != liveProduct)) {|||        (g.receipt.bcdDevice != liveBcd)) {  // MUTANT'

# The reason must distinguish "no receipt" from "a receipt for something else".
# They send a person to different places -- one to another directory, one to
# work out where the file came from -- and collapsing them wastes the second.
mutate kill "a wrong-device receipt is reported as simply absent" "$PV" \
'        g.reason = entryReceiptPath() + " describes a different "
                   "bootloader (" + b + " \"" + g.receipt.product +
                   "\") than the one attached";|||        g.reason = g.receipt.reason;  // MUTANT'

mutate kill "the entry gate allows anything, receipt or not" "$PV" \
'    if (g.receipt.present) {
        g.allowed = true;
        return g;
    }|||    g.allowed = true;  // MUTANT
    return g;'

# The escape hatch swallowing the normal path. Every allowed run then prints the
# override warning, which trains the user to ignore it -- and the one run that
# IS an override stops standing out, which is the only thing it is for.
mutate kill "the entry gate reports every pass as an override" "$PV" \
'    if (g.receipt.present) {
        g.allowed = true;
        return g;
    }|||    if (g.receipt.present) {
        g.allowed = true;
        g.overridden = true;  // MUTANT
        return g;
    }'

# The inverse: the override never marked, so a button-entered flash prints the
# same line as an A1 3A one and the deviation from §4.2b goes unrecorded.
mutate kill "the entry gate hides that the override was used" "$PV" \
'        g.allowed = true;
        g.overridden = true;
        return g;|||        g.allowed = true;  // MUTANT
        return g;'

# A receipt written by anything else -- including one a user creates by hand to
# get past the gate without reading what the gate is for -- must not count.
mutate kill "any file at the receipt path counts as a receipt" "$PV" \
'    if (magic != kReceiptMagic) {|||    if (false) {  // MUTANT'

# clearEntryReceipt is called the moment the image is verified resident, because
# a completed flash is what clears the A1 3A latch. A receipt that outlives the
# latch it describes then vouches for a LATER button entry.
mutate kill "the receipt is never cleared after a flash" "$PV" \
'void clearEntryReceipt() { std::remove(entryReceiptPath().c_str()); }|||void clearEntryReceipt() { }  // MUTANT'

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

    // 1a. CAPABILITY GATE|||    read(o.before, o.result);  // MUTANT
    if (o.before.size() != kLargeLen) o.before.assign(kLargeLen, 0);

    // 1a. CAPABILITY GATE'

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

# --------------------------------------------------------------------------
# The CPI encoder. config-protocol.md §7.19 derives the grid from cfg107
# `0x40d880`; these ask whether the C++ that implements it is actually graded.
# Every one of them produces a CPI the vendor's own tool would never store, and
# engineering-rules.md §1.3 makes an ungrounded value a byte we may not write.
# --------------------------------------------------------------------------

# The clamp stops protecting the low end: `cpi 1 0` would encode 0, and a mouse
# reporting zero counts per inch does not move.
mutate kill "cpi: the low clamp is dropped" "$CR" \
'    if (v < kCpiMin) v = kCpiMin;|||    // MUTANT: no low clamp'

# 30000 is the vendor's ceiling. Past it the u16 store at 0x57f220 still fits
# (65535), so nothing downstream would catch it -- which is the whole point.
mutate kill "cpi: the high clamp is dropped" "$CR" \
'    if (v > kCpiMax) v = kCpiMax;|||    // MUTANT: no high clamp'

# Half-up becomes half-down. `cmpl $5` / `jb` at 0x40d8c7 says up.
mutate kill "cpi: rounding goes half DOWN" "$CR" \
'    return (rem * 2 >= step ? q + 1 : q) * step;|||    return (rem * 2 > step ? q + 1 : q) * step;  // MUTANT'

# The coarse step above 10000 becomes fine. 10010 would be accepted; the vendor
# stores only multiples of 50 up there (`imull $0x32` at 0x40d92c).
mutate kill "cpi: one step size everywhere" "$CR" \
'    const long step = (v <= kCpiFineLimit) ? 10 : 50;|||    const long step = 10;  // MUTANT'

# EQUIVALENT, and it took a run to establish it. `<` and `<=` choose a
# different step ONLY at v == 10000 exactly -- everything else is clamped into
# [10,30000] and then falls on the same side of both tests. At 10000 the fine
# arm gives 1000*10 and the coarse arm gives 200*50, and those are the same
# number, so no input can tell the two apart.
#
# The code keeps `<=` anyway, because cfg107 encodes `jbe 0x40d8b6` at
# 0x40d91e and a faithful transcription of a derived rule is worth more than a
# provably-indistinguishable one (§1.3's spirit: we do not "tidy" the vendor).
# Listed here so the next person to notice the surviving mutant finds the proof
# instead of re-deriving it, or worse, "fixing" the test.
mutate equiv "cpi: the changeover comparison is strict (10000 is on both grids)" "$CR" \
'    const long step = (v <= kCpiFineLimit) ? 10 : 50;|||    const long step = (v < kCpiFineLimit) ? 10 : 50;  // MUTANT'

# Y stops being checked, so `cpi 1 1600 1605` writes an off-grid Y.
mutate kill "cpi: only X is validated" "$CR" \
'    for (long v : {x, y}) {|||    for (long v : {x}) {  // MUTANT'

# The flag inverts. §7.8 derives it as X != Y; the device is then told the two
# axes match when they do not.
mutate kill "cpi: the X!=Y flag is inverted" "$CR" \
'    out[0] = static_cast<std::uint8_t>(x != y ? 1 : 0);|||    out[0] = static_cast<std::uint8_t>(x == y ? 1 : 0);  // MUTANT'

# Byte order flips. 1600 becomes 0x0640 = 16000 on the device.
mutate kill "cpi: the u16 is stored big-endian" "$CR" \
'    out[1] = static_cast<std::uint8_t>(x & 0xFF);
    out[2] = static_cast<std::uint8_t>((x >> 8) & 0xFF);|||    out[1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);  // MUTANT
    out[2] = static_cast<std::uint8_t>(x & 0xFF);'

# X and Y swap. Invisible on every capture but `02-basic`'s two asymmetric
# records, which is exactly why those two are worth having.
mutate kill "cpi: X and Y are swapped in the payload" "$CR" \
'    out[3] = static_cast<std::uint8_t>(y & 0xFF);
    out[4] = static_cast<std::uint8_t>((y >> 8) & 0xFF);|||    out[3] = static_cast<std::uint8_t>(x & 0xFF);  // MUTANT
    out[4] = static_cast<std::uint8_t>((x >> 8) & 0xFF);'

# Decode stops inverting encode, so the diff a person reads before approving a
# write would describe bytes other than the ones being sent.
mutate kill "cpi: decode reads the flag from the wrong byte" "$CR" \
'    s.flag = e[0] != 0;|||    s.flag = e[1] != 0;  // MUTANT'

# --------------------------------------------------------------------------
# Handedness (§7.20) and the multiclick/SPDT byte (§7.22). Both write button
# entries, and both have a refusal that is the whole point of the feature.
# --------------------------------------------------------------------------

# The state test stops ignoring byte +6, so a record whose multiclick filter is
# not 8 reads as Unknown and the verb refuses work it should do.
mutate kill "handedness: the state test includes the multiclick byte" "$CR" \
'    return std::memcmp(entry, kLeftClickEntry, sizeof kLeftClickEntry) == 0;|||    return std::memcmp(entry, kLeftClickEntry, kButtonEntryLen) == 0;  // MUTANT'

# Unknown stops being refused and is treated as right-handed. This is the
# guess that moves a mapping the user did not ask to move.
mutate kill "handedness: an unrecognisable record is assumed right-handed" "$CR" \
'    if (a == b) return Handedness::Unknown;   // neither, or ambiguously both|||    if (a == b) return Handedness::Right;  // MUTANT'

# The already-in-that-state short circuit goes away, so applying twice runs the
# vendor's non-idempotent transform twice and destroys the assignment.
mutate kill "handedness: applying twice is no longer a no-op" "$CR" \
'    if (now == want) return true;             // nothing to write|||    // MUTANT: always transform'

# +6 is copied instead of swapped -- cfg107 arm A's behaviour, which silently
# overwrites one button multiclick filter with the other one.
mutate kill "handedness: byte +6 is copied, not swapped" "$CR" \
'    vac[6] = vacPlus6;|||    vac[6] = keepPlus6;  // MUTANT'

# The vacated slot keeps the old action instead of becoming left-click, so both
# entries end up bound to the same thing and there is no primary click.
mutate kill "handedness: the vacated entry is not reset to left-click" "$CR" \
'    std::memcpy(vac, kLeftClickEntry, sizeof kLeftClickEntry);|||    // MUTANT: leave the vacated entry alone'

# GX modes become legal on middle/forward/back -- bytes the vendor page cannot
# produce there, i.e. a [G] write (1.3).
mutate kill "multiclick: every button is allowed a GX mode" "$CR" \
'    const bool hasSpdt = (button < 2);        // left and right only (§7.22.3)|||    const bool hasSpdt = true;  // MUTANT'

# The two GX bytes swap, so asking for Safe stores Speed.
mutate kill "multiclick: GX Safe and GX Speed are swapped" "$CR" \
'        out = kSpdtGxSpeed;
        return true;|||        out = kSpdtGxSafe;  // MUTANT
        return true;'

# The range opens past 25, into the bytes the vendor uses for GX.
mutate kill "multiclick: the filter range is unbounded above" "$CR" \
'    if (value < 0 || value > kMulticlickMax) {|||    if (value < 0) {  // MUTANT'

# An unknown mode silently becomes `off`, so a typo writes a filter value.
mutate kill "multiclick: an unknown mode falls through to off" "$CR" \
'    if (!mode || std::strcmp(mode, "off") != 0) {|||    if (false) {  // MUTANT'

# describe() stops rejecting bytes the page cannot produce, so a record in an
# unexplained state would be reported as a valid setting.
mutate kill "multiclick: describe accepts any byte" "$CR" \
'    if (byte <= kMulticlickMax) return "off";
    return nullptr;             // a value the vendor'\''s page cannot produce|||    return "off";  // MUTANT'

# §7.25's capability gate, removed. `set lod` would then be accepted on a device
# whose record 0x6f says the value means a different lift-off distance -- a
# write that is well formed, verifies, and is wrong.
mutate kill "the lod capability gate never fires" "$CR" \
'        if (record[kPayloadOffset + g.governedBy] != g.onlyWhen) return g.why;|||        (void)g;  // MUTANT'

# The gate fires on the wrong byte, so it neither protects lod nor stays quiet.
mutate kill "the capability gate reads the byte next door" "$CR" \
'        if (record[kPayloadOffset + g.governedBy] != g.onlyWhen) return g.why;|||        if (record[kPayloadOffset + g.governedBy + 1] != g.onlyWhen) return g.why;  // MUTANT'

# The gate compares the wrong way round: it would refuse exactly the devices it
# was derived on and permit the ones it was not.
mutate kill "the capability gate is inverted" "$CR" \
'        if (record[kPayloadOffset + g.governedBy] != g.onlyWhen) return g.why;|||        if (record[kPayloadOffset + g.governedBy] == g.onlyWhen) return g.why;  // MUTANT'

# The gate stops matching by name, so it applies to every field or to none.
mutate kill "the capability gate ignores which field is being set" "$CR" \
'        if (std::strcmp(f.name, g.field) != 0) continue;|||        (void)f;  // MUTANT'

# The refusal happens, but AFTER the frame is on the wire -- which is not a
# refusal at all. §4.2: an off-wire guard is only free while it stays off-wire.
mutate kill "the capability gate runs after the write" "$CS" \
'    if ((o.refusal = capabilityRefusal(f, o.before.data())) != nullptr) {
        o.result = Result::RefusedCapability;
        return o;
    }|||    // MUTANT: gate moved after the send'

# gatedRecordOffsets exists so a test can hold the one thing the gate table
# cannot enforce for itself: that no setRun block steps over a governing byte.
# An accessor with no mutant is an accessor nobody has checked, and the test
# that reads it would pass on an empty answer -- which is the vacuous-pass
# shape this suite exists to catch.
mutate kill "the gated-offset list reports itself as empty" "$CR" \
'    count = sizeof(kGates) / sizeof(kGates[0]);|||    count = 0;  // MUTANT'

# Subtler, and the reason the disjointness test is not alone. This reports the
# gate's TRIGGER VALUE (0x00) where its governed OFFSET (0x6f) belongs. 0x00 is
# outside every setRun run, so the disjointness check still passes; only the
# adjacency check notices.
mutate kill "the gated-offset list reports the value, not the offset" "$CR" \
'    for (std::size_t i = 0; i < count; ++i) offs[i] = kGates[i].governedBy;|||    for (std::size_t i = 0; i < count; ++i) offs[i] = kGates[i].onlyWhen;  // MUTANT'

# ---------------------------------------------------------------------------
# FIXED CPI (§7.31). The defect these replace SHIPPED and scored 14/14 on the
# capture replay, because every captured FIXED CPI entry has X == Y and a legal
# value. So each of these breaks the encoder in a way no recorded example can
# see, which is the whole point of grading this file rather than trusting it.
# ---------------------------------------------------------------------------

# The original bug, restored verbatim: a bound that cites nothing, rejects 10-40
# and 26010-30000, and accepts 1337.
mutate kill "the fixed-cpi bound goes back to the uncited 50..26000" "$CR" \
'    if (v > kFixedCpiMax) v = kFixedCpiMax;|||    if (v > 26000) v = 26000;  // MUTANT'

# The other half of the original bug: Y is a copy of X, so the dialog'"'"'s second
# trackbar (1080) can never be expressed.
mutate kill "fixed-cpi Y is copied from X instead of encoded" "$CR" \
'        out[4] = static_cast<std::uint8_t>(y & 0xFF);|||        out[4] = out[2];  // MUTANT'

# Silently coercing instead of refusing. Every value becomes legal and the user
# gets a CPI they did not ask for, past both the verify and the diff.
mutate kill "fixed-cpi rounds the value instead of refusing it" "$CR" \
'            if (v != normaliseFixedCpi(v)) {|||            if (false) {  // MUTANT'

# The step confusion this section exists to prevent: using the CPI-STAGE grid
# for the button payload. Only differs above 10000, so it needs the test that
# checks the two normalisers disagree there.
mutate kill "fixed-cpi uses the CPI-stage 50 step above 10000" "$CR" \
'    const long q = v / kFixedCpiStep, rem = v - q * kFixedCpiStep;
    return (rem * 2 >= kFixedCpiStep ? q + 1 : q) * kFixedCpiStep;|||    const long st = (v <= 10000) ? 10 : 50;  // MUTANT
    const long q = v / st, rem = v - q * st;
    return (rem * 2 >= st ? q + 1 : q) * st;'

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

# ---------------------------------------------------------------------------
# `egg-config show` -- the decoders. Added 2026-09-06 with the feature.
# ---------------------------------------------------------------------------
# These read bytes off the mouse and turn them into sentences a person acts on.
# A wrong reading here does not corrupt anything by itself; it tells someone
# their lift-off distance is 1.0mm when it is 1.1mm, and they believe it.

mutate kill "lod is off by one millimetre step" "$CR" \
'    const int tenths = 7 + v;|||    const int tenths = 8 + v;  // MUTANT'

mutate kill "polling shows the stored divisor instead of the frequency" "$CR" \
'    std::snprintf(buf, n, "%ld Hz", 8000L / div);|||    std::snprintf(buf, n, "%u Hz", div);  // MUTANT'

mutate kill "decodeBool accepts any byte as a boolean" "$CR" \
'bool decodeBool(std::uint8_t v, char* buf, std::size_t n) {
    if (v > 1) return false;|||bool decodeBool(std::uint8_t v, char* buf, std::size_t n) {
    // MUTANT: no range check'

mutate kill "sensor-angle decodes 0x80, a value the set path refuses" "$CR" \
'    if (deg < -127) return false;          // 0x80 is outside what encode takes|||    if (deg < -128) return false;  // MUTANT'

mutate kill "cpi-stage decodes a fifth stage that does not exist" "$CR" \
'bool decodeCpiStage(std::uint8_t v, char* buf, std::size_t n) {
    if (v > 3) return false;|||bool decodeCpiStage(std::uint8_t v, char* buf, std::size_t n) {
    if (v > 7) return false;  // MUTANT'

mutate kill "the downshift remap is read forwards instead of inverted" "$CR" \
'    for (int i = 0; i < 4; ++i)
        if (kMap[i] == stored) {|||    for (int i = 0; i < 4; ++i)
        if (static_cast<std::uint8_t>(i) == stored) {  // MUTANT'

mutate kill "the smoothing remap is read forwards instead of inverted" "$CR" \
'    for (int i = 0; i < 3; ++i)
        if (kMap[i] == stored) {|||    for (int i = 0; i < 3; ++i)
        if (static_cast<std::uint8_t>(i) == stored) {  // MUTANT'

mutate kill "a key binding is matched on +1 too, so every modifier hides it" "$CR" \
'        const bool hit = a.payload == ButtonPayload::Key
                             ? entry[0] == a.b0
                             : entry[0] == a.b0 && entry[1] == a.b1;|||        const bool hit = entry[0] == a.b0 && entry[1] == a.b1;  // MUTANT'

mutate kill "fixed-cpi Y is decoded from the X bytes" "$CR" \
'        b.cpiY = entry[4] | (static_cast<long>(entry[5]) << 8);|||        b.cpiY = entry[2] | (static_cast<long>(entry[3]) << 8);  // MUTANT'

mutate kill "hidKeyName loses the keypad zero, whose usage is out of run" "$CR" \
'        v[0x62] = "kp0";|||        // MUTANT: kp0 dropped'

# ---------------------------------------------------------------------------
# bcdDevice -> the version Endgame's updater displays (EGGCore).
# ---------------------------------------------------------------------------
# engineering-rules.md §5: everything observed before 2026-09-05 07:53 is firmware 1.07 and
# everything after is 1.10, and DEFAULTS differ between them. A version display
# that is confidently wrong mislabels every observation made under it.

mutate kill "the version field is read as a plain integer, not as BCD" "$DV" \
'    const unsigned n = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3];
    major = n / 100;
    minor = n % 100;|||    major = bcd / 100;  // MUTANT
    minor = bcd % 100;'

mutate kill "a nibble above 9 is accepted, so 0x01a0 becomes a version" "$DV" \
'        if (d[i] > 9) return false;      // formats as a letter; __wtol stops|||        if (d[i] > 15) return false;  // MUTANT'

mutate kill "the minor part loses its leading zero, so 1.07 prints as 1.7" "$DV" \
'    std::snprintf(b, sizeof b, "%u.%02u", major, minor);|||    std::snprintf(b, sizeof b, "%u.%u", major, minor);  // MUTANT'

mutate kill "the digit weights are wrong, so the top nibble is lost" "$DV" \
'    const unsigned n = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3];|||    const unsigned n = d[1] * 100 + d[2] * 10 + d[3];  // MUTANT'

# ---------------------------------------------------------------------------
# The write phase must not be silent, and must not be stoppable.
# ---------------------------------------------------------------------------

mutate kill "the write phase goes silent again" "$WP" \
'        link.note("block " + std::to_string(p.blocksVerified) + "/" +|||        if (false) link.note("block " + std::to_string(p.blocksVerified) + "/" +  // MUTANT'

# EQUIVALENT, and finding that out is why this mutant is here. I wrote it as a
# `kill` on the assumption that the test named "under repair the count still
# tracks VERIFIED blocks, not attempts" could tell the two apart. It cannot, and
# neither can any other test, because THE TWO EXPRESSIONS ARE EQUAL ON EVERY
# REACHABLE PATH: `p.blocksVerified` starts at 0 (WritePhase.h has the default
# initialiser), the outer `for` over blocks has no `continue` and no `break`,
# and the counter is incremented exactly once, immediately above the note. So
# blocksVerified == i + 1 at that line, always. The retries live in an INNER
# loop that only exits by `break`, so a repaired block still reaches the note
# once, with both expressions equal.
#
# Left as equivalent rather than deleted, because it records the claim the
# source comment used to make too strongly and now does not.
mutate equiv "progress reports the loop index instead of verified blocks" "$WP" \
'        link.note("block " + std::to_string(p.blocksVerified) + "/" +|||        link.note("block " + std::to_string(i + 1) + "/" +  // MUTANT'

# The falsifiable half of the progress property: a line per ATTEMPT rather than
# per block. A device repairing block 9 four times would report reaching block
# 12, which is worse than silence -- see the test of the same name.
mutate kill "progress announces a block on every write attempt" "$WP" \
'            link.note("block " + hex2(idx) + " read back wrong (" +|||            link.note("block " + std::to_string(p.blocksVerified + 1) + "/" + std::to_string(img.blockCount()) + " verified");  // MUTANT
            link.note("block " + hex2(idx) + " read back wrong (" +'

mutate kill "progress stops mentioning rewrites" "$WP" \
'                  (p.rewrites ? "  (" + std::to_string(p.rewrites) +
                                " rewrite(s) so far)"
                              : std::string()));|||                  std::string());  // MUTANT'

# The recovery text is COMPOSED from Protocol.h's shared constants now, instead
# of being a second copy of them. These grade that composition -- the previous
# arrangement was two literals that a single amendment could make disagree, and
# the disagreement was found by the compiler rather than by a test.

mutate kill "the recovery text loses the button-entry steps" "$WP" \
'    + indentLines(egg::kButtonEntrySteps, "  ") + "\n"|||    + indentLines(egg::kButtonEntryReflashNote, "  ") + "\n"  // MUTANT'

mutate kill "the recovery text stops telling the reader to re-run it" "$WP" \
'    "  Then run this command again, with that flag.";|||    "";  // MUTANT'

mutate kill "SIGPIPE goes back to its default, which kills the process" "$NQ" \
'const int kSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGPIPE};|||const int kSignals[] = {SIGINT, SIGTERM, SIGHUP};  // MUTANT'

mutate kill "the guard restores SIG_DFL instead of what it saved" "$NQ" \
'        if (prev_[i] != SIG_ERR) std::signal(kSignals[i], prev_[i]);|||        if (prev_[i] != SIG_ERR) std::signal(kSignals[i], SIG_DFL);  // MUTANT'

mutate kill "the guard leaves the signals at their default" "$NQ" \
'        prev_[i] = std::signal(kSignals[i], SIG_IGN);|||        prev_[i] = std::signal(kSignals[i], SIG_DFL);  // MUTANT'

# ---------------------------------------------------------------------------
# BootloaderEntry.cpp -- the LATCHED window opens here (engineering-rules.md 4.2b)
# ---------------------------------------------------------------------------
# Two of these are bugs this file ACTUALLY HAD against hardware, which is why
# they are the first two.

mutate kill "identity accepts on PID and bcdDevice, dropping the product string" "$BE" \
'        && d.product       == kBootloaderProduct;|||        ;  // MUTANT'

mutate kill "identity stops checking bcdDevice" "$BE" \
'    return d.productId     == kProductIdBootloader
        && d.releaseNumber == kBootloaderRelease|||    return d.productId     == kProductIdBootloader
        && true  // MUTANT'

mutate kill "vendor collections are counted as interfaces -- the bug that refused a good mouse" "$BE" \
'    return d.productId == pid
        && d.usagePage == kUsagePageVendor
        && d.usage     == kUsageVendor;|||    return d.productId == pid;  // MUTANT'

mutate kill "two mice are accepted, so A1 3A goes to whichever enumerated first" "$BE" \
'        if (apps != 1) {|||        if (apps < 1) {  // MUTANT'

mutate kill "zero mice are accepted too" "$BE" \
'        if (apps != 1) {|||        if (false) {  // MUTANT'

mutate kill "the re-enumeration poll gives up before the first sleep" "$BE" \
'    for (unsigned waited = 0; waited <= kEntryPollCeilMs; waited += kEntryPollStepMs) {|||    for (unsigned waited = 0; waited < 0; waited += kEntryPollStepMs) {  // MUTANT'

mutate kill "an unrecognised identity is reported as a confirmed entry" "$BE" \
'            o.result = isKnownBootloader(d) ? EntryResult::EnteredAndConfirmed
                                            : EntryResult::UnrecognisedIdentity;
            return o;
        }
        env.sleepMs(kEntryPollStepMs);|||            o.result = EntryResult::EnteredAndConfirmed;  // MUTANT
            return o;
        }
        env.sleepMs(kEntryPollStepMs);'

mutate kill "a failed send is reported as a successful one" "$BE" \
'    if (!env.exchange(o.sent, reply, readOk)) {
        o.result = EntryResult::SendFailed;|||    if (false) {  // MUTANT
        o.result = EntryResult::SendFailed;'

# EQUIVALENT. `o.status` is recorded for the log and is deliberately NOT
# consulted -- capture 09 answered 0x00 where 08 answered 0xa1 for the identical
# command, and prediction #3 came back 0x03 on the device (a value in neither
# capture). Nothing branches on it, so widening the guard that fills it in
# cannot change any outcome. If this one is KILLED, something started gating on
# the status byte and engineering-rules.md 4.2b's argument needs rewriting, not this line.
mutate equiv "the status byte is recorded from a longer reply" "$BE" \
'    if (readOk && reply.size() >= 2) o.status = reply[1];|||    if (readOk && reply.size() >= 3) o.status = reply[1];  // MUTANT'

# ---------------------------------------------------------------------------
# FirmwareManifest.cpp -- which image reaches the mouse
# ---------------------------------------------------------------------------

# NOT a prefix mutation, and the difference matters. Loosening the guard to
# `> 64` looks like it would let a 63-character prefix through, and it does not:
# the compare loop reads sha256[63], which for a 63-char std::string is the
# defined '\0' and mismatches, so the lookup still returns nullptr and the
# mutant would SURVIVE while looking like a real defect. `< 64` is the one that
# bites -- it lets a hash with trailing junk match on its first 64 characters.
mutate kill "an updater hash with trailing junk matches on its first 64 chars" "$FM" \
'    if (sha256.size() != 64) return nullptr;|||    if (sha256.size() < 64) return nullptr;  // MUTANT'

mutate kill "any hash selects the first release" "$FM" \
'        if (j == 64) return &kReleases[i];|||        if (j >= 0) return &kReleases[i];  // MUTANT'

mutate kill "findRelease falls back to the primary instead of refusing" "$FM" \
'        if (std::strcmp(kReleases[i].label, label) == 0) return &kReleases[i];
    return nullptr;|||        if (std::strcmp(kReleases[i].label, label) == 0) return &kReleases[i];
    return &kReleases[0];  // MUTANT'

mutate kill "findRelease prefix-matches a label" "$FM" \
'        if (std::strcmp(kReleases[i].label, label) == 0) return &kReleases[i];|||        if (std::strncmp(kReleases[i].label, label, 3) == 0) return &kReleases[i];  // MUTANT'

mutate kill "primaryRelease returns the LAST row instead of the proven first" "$FM" \
'    return kReleases[0];
}|||    return kReleases[kReleaseCount - 1];  // MUTANT
}'

mutate kill "1.07 claims to have been proven on the device" "$FM" \
'     0x0081d408u,
     false,|||     0x0081d408u,
     true,  // MUTANT'

echo
echo "killed $KILLED of $((KILLED+HOLES)) real defects; $EQUIV_OK of $((EQUIV_OK+EQUIV_BAD)) equivalent mutants behaved as predicted"
[ "$BROKEN" -gt 0 ] && echo "$BROKEN mutants failed to apply or compile -- they measured nothing."
FAIL=0
[ "$HOLES" -gt 0 ] && { echo "$HOLES HOLE(S). Each is a bug the suite would not notice."; FAIL=1; }
[ "$EQUIV_BAD" -gt 0 ] && { echo "$EQUIV_BAD equivalent mutant(s) were killed -- the label is wrong."; FAIL=1; }
[ "$BROKEN" -gt 0 ] && FAIL=1
[ "$KILLED" -eq 0 ] && { echo "ZERO real defects killed. §6.2: a red flag, not a pass."; FAIL=1; }

# Did the whole script actually run? See DECLARED_KILL above.
RAN_KILL=$((KILLED+HOLES)); RAN_EQUIV=$((EQUIV_OK+EQUIV_BAD))
if [ $((RAN_KILL+RAN_EQUIV+BROKEN)) -ne $((DECLARED_KILL+DECLARED_EQUIV)) ]; then
  echo "MUTANT COUNT MISMATCH: declared $((DECLARED_KILL+DECLARED_EQUIV)), ran $((RAN_KILL+RAN_EQUIV+BROKEN))."
  echo "Something above did not execute. A short run is not a clean run."
  FAIL=1
fi
[ "$FAIL" -eq 0 ] && echo "The suite can produce a bad result, which is what makes its passes mean anything."
exit $FAIL
