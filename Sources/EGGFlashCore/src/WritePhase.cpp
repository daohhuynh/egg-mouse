// WritePhase.cpp -- the erase-through-completion sequence, in ONE file.
//
// engineering-rules.md §4.3: "Keep the write phase small and in one file. ~100 lines in
// one place can be audited by eye. Spread across eight files with clever
// abstractions, it cannot." So this file is deliberately flat. There is one
// helper for a send-and-read round trip and no other indirection.
//
// THE TWO REGIMES, and why the second looks so unlike normal code (§4.2):
//
//   preflight()             -- before A0 03. Nothing has changed on the device.
//                              Any single failure aborts. Abort is ALWAYS
//                              correct here.
//
//   driveToVerifiedImage()  -- after A0 03. The device has no valid
//                              application. Exiting cleanly GUARANTEES the bad
//                              outcome, so there is no failure return, no
//                              timeout that gives up, and no cancel. It retries
//                              the failed chunk, re-establishes a dropped
//                              connection, and keeps driving.
//
// This would be indefensible in ordinary code and is the only safe shape here.
// It is also no longer the desperate measure it once was: holding LEFT and
// RIGHT while plugging in forces the bootloader unconditionally
// (notes/bootloader-observed.md §1, [O]), so a device stuck in this loop is
// recoverable by the user. That is why the loop escalates to printing the
// recovery procedure rather than to returning an error.
//
// WHERE WE DELIBERATELY DIFFER FROM THE VENDOR (updater-protocol.md §5.6):
// its repair loop decides whether a rewrite worked by reading byte 1 of the
// SOURCE IMAGE and comparing it against the protocol's ready/busy values. So
// its "verified" depends on the firmware's contents rather than on the device,
// and a block whose second byte is 0x01 reports success having verified
// nothing. We reproduce the vendor's COMMANDS exactly and its VERIFICATION
// correctly. It also passes report id 0xA0 to a fixed 64-byte read on that same
// path; we do not copy that either.

#include "egg/WritePhase.h"

#include "egg/FlashCommands.h"
#include "egg/Protocol.h"

#include <cstring>
#include <string>

namespace egg::fw {

// AMENDED 2026-09-06, the same hour the entry receipt was added, because the
// receipt made this text WRONG. It says "then run this command again" after a
// BUTTON entry -- and flash/restore-firmware now refuse a bootloader they did
// not enter themselves with A1 3A. Following this to the letter would have
// produced a refusal at the one moment a recovery instruction is read closely.
//
// The same defect was found once before, at cmdLeaveBootloader's failure path,
// and the comment there is the rule: a recovery instruction that is wrong is
// worse than none. A guard added anywhere has to be checked against every text
// that tells someone what to do next.
// COMPOSED, not restated. Until 2026-09-06 this was a second string literal
// saying the same things as egg::kRecoveryProcedure in its own words, and when
// the entry receipt made the instruction wrong it made BOTH wrong -- only one
// was noticed. Building it from the shared pieces means the next amendment
// cannot reach one printout and miss the other, and Tests/test_flash.cpp
// asserts the containment rather than trusting this comment.
//
// The indent is applied here rather than stored in the shared text, because
// egg-config prints the same facts unindented in a help paragraph.
namespace {
std::string indentLines(const char* text, const char* prefix) {
    std::string out(prefix);
    for (const char* p = text; *p; ++p) {
        out += *p;
        if (*p == '\n') out += prefix;
    }
    return out;
}
}  // namespace

const std::string kRecoveryProcedure =
    "RECOVERY (observed on the device, notes/bootloader-observed.md):\n"
    + indentLines(egg::kButtonEntrySteps, "  ") + "\n"
    + indentLines(egg::kButtonEntryReflashNote, "  ") + "\n"
    // Flasher-only, and deliberately NOT in the shared text: egg-config's help
    // prints the same facts in a paragraph about firmware going wrong, where
    // "run this command again" would be an instruction to re-run egg-config.
    "  Then run this command again, with that flag.";

namespace {

std::string hex8(std::uint32_t v) {
    static const char* d = "0123456789abcdef";
    std::string s = "0x";
    for (int i = 28; i >= 0; i -= 4) s += d[(v >> i) & 0xF];
    return s;
}

constexpr std::uint8_t kReady = 0x01;   // [D] §2, the only success status

// One command plus its status read. Returns the device's status byte, or
// nullopt-shaped -1 for a transport failure -- which is NOT a status and must
// never be compared against 0x01 (§3.4a is exactly the confusion being avoided).
int roundTrip(BootloaderLink& link, const std::vector<std::uint8_t>& frame,
              unsigned sleepMs, std::uint8_t replyId, std::size_t replyLen,
              std::vector<std::uint8_t>* reply = nullptr) {
    if (link.send(frame) != Io::Ok) return -1;
    link.sleepMs(sleepMs);
    std::vector<std::uint8_t> r;
    if (link.recv(replyId, replyLen, r) != Io::Ok) return -1;
    if (r.size() < 2) return -1;
    if (reply) *reply = r;
    return r[1];
}

}  // namespace

bool preflight(const Image& img, std::string& error) {
    if (!img.valid()) { error = "no validated image"; return false; }
    if (img.blockCount() != kExpectedBlockCount) {
        error = "image is " + std::to_string(img.blockCount()) +
                " blocks; refusing anything but " +
                std::to_string(kExpectedBlockCount) +
                " rather than emulate an untested vendor path";
        return false;
    }
    // §10.3 invariant 3, checked over the WHOLE range before anything is sent,
    // not per block as we go.
    for (std::size_t i = 0; i < img.blockCount(); ++i) {
        const std::uint8_t d = img.deviceIndex(i);
        if (d < kBlockFirst || d > kBlockLast) {
            error = "block " + std::to_string(i) + " maps to device index " +
                    std::to_string(d) + ", outside [0x34,0x74]";
            return false;
        }
    }
    // NO ROUND TRIP. §4.2's "round-trip a benign query" was written before the
    // vendor's sequence was known, and today's amendment to the same section --
    // "a safety measure that changes the byte stream is not free" -- settles the
    // conflict against it. Their flash opens at A0 03; anything we send first is
    // our invention, sent to a device that is already latched, one frame before
    // the point of no return, and with no evidence of how it answers outside a
    // session. §4.2a gate 2 also applies: "does the link work" is answerable
    // without touching the device, by the enumeration and the successful open
    // that got us here.
    return true;
}

Progress driveToVerifiedImage(BootloaderLink& link, const Image& img) {
    Progress p;
    const std::uint8_t blocks = static_cast<std::uint8_t>(img.blockCount());

    // ---- A0 03. THE POINT OF NO RETURN. -------------------------------------
    // The vendor retries with Sleep(100,200,300,400,500) and then aborts. We
    // keep the schedule and drop the abort.
    for (unsigned attempt = 0, d = 100;; ++attempt, d = (d < 500 ? d + 100 : 500)) {
        if (roundTrip(link, bootloaderStart(blocks, img.checksum()), 3000,
                      kReportSmall, kSmallLen) == kReady) break;
        if (attempt && attempt % 5 == 0) {
            link.note("bootloader start not acknowledged after " +
                      std::to_string(attempt) + " attempts; still trying.");
            link.note(kRecoveryProcedure);
        }
        if (!link.reconnect()) link.sleepMs(d); else ++p.reconnects;
    }
    link.note("bootloader start acknowledged; writing " +
              std::to_string(blocks) + " blocks.");
    link.sleepMs(500);

    // ---- The blocks. --------------------------------------------------------
    for (std::size_t i = 0; i < img.blockCount(); ++i) {
        const std::uint8_t idx = img.deviceIndex(i);   // §10.3 inv. 4: ONE counter
        const std::uint8_t* src = img.block(i);

        for (unsigned attempt = 0;; ++attempt) {
            if (attempt) ++p.rewrites;
            // Write. The vendor gives up after 5; §4.2 forbids giving up here.
            if (roundTrip(link, writeBlock(idx, src, kBlockSize), 50,
                          kReportSmall, kSmallLen) != kReady) {
                ++p.sendFailures;
                if (!link.reconnect()) link.sleepMs(200); else ++p.reconnects;
                if (attempt && attempt % 10 == 0) {
                    link.note("block " + hex2(idx) + " has failed " +
                              std::to_string(attempt) + " times; still trying.");
                    link.note(kRecoveryProcedure);
                }
                continue;
            }
            ++p.blocksWritten;   // the device ACKED a write. Not the same event
                                 // as verifying one, and on a lying device the
                                 // two diverge -- which is the point.
            // Verify, correctly. Read the block back and check BOTH the 1024
            // bytes and the device's own 16-bit checksum at resp[6..7] (§5.5
            // step 5). Never the source pointer (§5.6).
            std::vector<std::uint8_t> back;
            const int st = roundTrip(link, readBlock(idx), 50, kReportLarge,
                                     kLargeLen, &back);
            if (st != kReady || back.size() < kLargeLen) {
                if (!link.reconnect()) link.sleepMs(200); else ++p.reconnects;
                continue;
            }
            const std::uint16_t devSum =
                static_cast<std::uint16_t>(back[6] | (back[7] << 8));
            const bool bytesMatch =
                std::memcmp(back.data() + 16, src, kBlockSize) == 0;
            const bool sumMatch = devSum == blockChecksum(src, kBlockSize);
            if (bytesMatch && sumMatch) break;
            link.note("block " + hex2(idx) + " read back wrong (" +
                      (bytesMatch ? "bytes ok" : "bytes differ") + ", " +
                      (sumMatch ? "sum ok" : "sum differs") + "); rewriting.");
            link.sleepMs(100);
        }
        // TWO COUNTERS, TWO EVENTS. They used to be incremented side by side
        // here, so they were equal by construction and the summary printed two
        // numbers that looked like a cross-check and were one number twice.
        // A statistic that cannot disagree with itself is not evidence.
        ++p.blocksVerified;      // reached only once the read-back matched

        // SAY SO, once per block. Added 2026-09-06.
        //
        // This phase takes roughly 10-15 seconds and, until now, said NOTHING
        // for the whole of it -- in the exact window where the tool has just
        // announced that Ctrl-C is ignored. "It has hung" and "it is working"
        // looked identical at the one moment the difference matters most, and
        // the GUI inherited the silence because it only streams what the CLI
        // prints.
        //
        // Through link.note() and not printf, for two reasons. It goes to
        // STDERR, and stdout carries the frame stream that
        // Tests/test_golden_vendor.py diffs against Endgame's capture -- a
        // progress line there would make that diff depend on how the run went.
        // And it goes through the link, so the mock sees it too and the
        // adversarial scenarios exercise the same path.
        //
        // It reports blocksVerified rather than the loop index because the
        // counter is the thing that means "came back correct". BE PRECISE
        // ABOUT WHAT THAT BUYS, THOUGH: today the two are EQUAL on every
        // reachable path -- the counter is bumped exactly once, immediately
        // above, and the outer loop has no `continue` and no `break` -- so
        // this is a choice of source of truth, not a behavioural difference,
        // and no test can tell them apart. Tests/mutants.sh carries it as an
        // EQUIVALENT mutant with that proof, because the first draft of this
        // comment claimed a distinction the tests were then credited with
        // catching, which is exactly the shape §6.2 warns about.
        //
        // The rewrite count rides along because a flash that is quietly
        // repairing itself is the thing an operator most wants to see early.
        link.note("block " + std::to_string(p.blocksVerified) + "/" +
                  std::to_string(img.blockCount()) + " verified" +
                  (p.rewrites ? "  (" + std::to_string(p.rewrites) +
                                " rewrite(s) so far)"
                              : std::string()));
    }

    // ---- Whole-image checksum. ---------------------------------------------
    link.sleepMs(200);
    for (unsigned attempt = 0;; ++attempt) {
        std::vector<std::uint8_t> r;
        const int st = roundTrip(link, wholeImageChecksumQuery(
                                     img.deviceIndex(img.blockCount() - 1)),
                                 100, kReportSmall, kSmallLen, &r);
        if (st == kReady && r.size() >= kChecksumResultOffset + 4) {
            const std::uint32_t got = checksumResult(r.data(), r.size());
            if (got == img.checksum()) { p.imageVerified = true; break; }
            // A mismatch here means a block is wrong despite per-block verify.
            // Not a reason to stop: it is a reason to find it and rewrite it.
            link.note("whole-image checksum mismatch: device " + hex8(got) +
                      " vs host " + hex8(img.checksum()) + "; re-verifying blocks.");
            for (std::size_t i = 0; i < img.blockCount(); ++i) {
                const std::uint8_t idx = img.deviceIndex(i);
                std::vector<std::uint8_t> back;
                const int rst = roundTrip(link, readBlock(idx), 50, kReportLarge,
                                          kLargeLen, &back);
                const bool readable = (rst == kReady && back.size() >= kLargeLen);

                // A BLOCK THAT WILL NOT READ IS REWRITTEN, NOT SKIPPED. Found
                // by adversarial audit 2026-09-05: this used to `continue` on a
                // failed read, which declined to repair the one block most
                // likely to BE the corruption. The outer loop then re-queried
                // A1 08, got the same mismatch, and repeated forever -- an
                // unbounded stream of A0 07 with the single A0 06 that could
                // have recovered the device never sent, inside the phase that
                // by design has no exit.
                //
                // The main write loop above already treats a failed read as a
                // reason to rewrite (line ~160). The two paths disagreed about
                // what a failed read-back means; they no longer do.
                if (readable &&
                    std::memcmp(back.data() + 16, img.block(i), kBlockSize) == 0)
                    continue;
                link.note("block " + hex2(idx) +
                          (readable ? " differs; rewriting."
                                    : " did not read back; rewriting anyway."));
                roundTrip(link, writeBlock(idx, img.block(i), kBlockSize), 50,
                          kReportSmall, kSmallLen);
                ++p.rewrites;
            }
            continue;
        }
        if (!link.reconnect()) link.sleepMs(200); else ++p.reconnects;
        if (attempt && attempt % 10 == 0) link.note(kRecoveryProcedure);
    }

    // ---- A1 09 complete. Vendor: 10 tries with Sleep(1..10) ms. -------------
    //
    // BOUNDED, AND THAT IS NOT A VIOLATION OF §4.2. Found by adversarial audit
    // 2026-09-05; this loop used to be `for (unsigned attempt = 1;; ++attempt)`
    // with no exit but a 0x01 status, and that was wrong in the most expensive
    // possible way.
    //
    // §4.2's "quitting is the bug" is about returning WITHOUT A VERIFIED IMAGE.
    // By the time control reaches here, p.imageVerified is already true: the
    // blocks are written, read back, compared, and the whole-image checksum
    // agrees. The image is resident. A1 09 does not put it there -- it closes
    // the session and asks the device to leave the bootloader.
    //
    // And that is exactly why spinning here is harmful rather than safe. A1 09
    // is the one command that makes the device GO AWAY: [O] both captures show
    // it re-enumerating as 0x1978 in 857-896 ms (flash-wire-observed.md §2.1).
    // So "the send landed, the status read did not" means the link may simply
    // no longer exist -- and on that path the old loop spun forever on a mouse
    // that had just been flashed correctly, never returned, never let the
    // caller send A1 13, and every 20 attempts printed kRecoveryProcedure,
    // which tells the user to re-enter the bootloader and "run this command
    // again" -- i.e. to erase a perfectly good mouse.
    //
    // CORRECTION, 2026-09-05, same day: I first wrote that a missing ack was
    // "a routine timing race" because roundTrip reads at 50 ms. Then I measured
    // it. THE DEVICE ACKS A1 09 IN 0.19-0.32 ms (§2.1a) -- the 64 ms visible in
    // the capture is Windows sleeping before it reads, not the device thinking.
    // There is no race at 50 ms; there is ~800 ms of margin. The bound is still
    // correct, for the narrower reason above, and p.completeAcked should
    // normally come back TRUE. If a real run ever reports it false, that is a
    // finding worth chasing rather than the expected case.
    //
    // THE EVIDENCE THAT A1 09 WORKED IS THE DEVICE COMING BACK, not the status
    // byte. Only the caller can see that, so this returns and lets it look.
    // The vendor bounds the same loop at 10 and then reports failure.
    for (unsigned attempt = 1; attempt <= kCompleteAttempts; ++attempt) {
        if (roundTrip(link, bootloaderComplete(), 50, kReportSmall,
                      kSmallLen) == kReady) { p.completeAcked = true; break; }
        link.sleepMs(attempt <= 10 ? attempt : 10);
    }
    if (!p.completeAcked)
        link.note("bootloader-complete was not acknowledged in " +
                  std::to_string(kCompleteAttempts) + " attempts. THE IMAGE IS "
                  "VERIFIED RESIDENT -- this is not a failed flash. The most "
                  "likely reason is that the device acted on A1 09 and left the "
                  "bootloader before the status could be read. Whether it came "
                  "back is the caller's to check.");
    return p;
}

}  // namespace egg::fw
