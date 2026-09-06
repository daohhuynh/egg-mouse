// WritePhase.cpp -- the erase-through-completion sequence, in ONE file.
//
// CLAUDE.md §4.3: "Keep the write phase small and in one file. ~100 lines in
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

const char* const kRecoveryProcedure =
    "RECOVERY (observed on the device, notes/bootloader-observed.md):\n"
    "  Hold LEFT and RIGHT mouse buttons together. Keep holding.\n"
    "  Plug the cable in. Keep holding a few more seconds, then release.\n"
    "  The mouse re-enumerates as PID 0x1977, Product \"Bootloader\".\n"
    "  Then run this command again. The bootloader is forced by hardware and\n"
    "  does not depend on any firmware being valid.";

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
                if (roundTrip(link, readBlock(idx), 50, kReportLarge, kLargeLen,
                              &back) != kReady || back.size() < kLargeLen)
                    continue;
                if (std::memcmp(back.data() + 16, img.block(i), kBlockSize) == 0)
                    continue;
                link.note("block " + hex2(idx) + " differs; rewriting.");
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
    for (unsigned attempt = 1;; ++attempt) {
        if (roundTrip(link, bootloaderComplete(), 50, kReportSmall,
                      kSmallLen) == kReady) { p.completeAcked = true; break; }
        link.sleepMs(attempt <= 10 ? attempt : 10);
        if (attempt % 20 == 0) {
            link.note("bootloader-complete not acknowledged after " +
                      std::to_string(attempt) + " attempts. The image IS "
                      "verified resident; still trying to close out.");
            link.note(kRecoveryProcedure);
        }
        if (attempt % 5 == 0 && link.reconnect()) ++p.reconnects;
    }
    return p;
}

}  // namespace egg::fw
