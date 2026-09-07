#include "egg/FlashPlan.h"
#include "egg/FlashCommands.h"

#include <cstdio>
#include <cstring>

namespace egg::fw {

namespace {

// Same shape as WritePhase's private roundTrip, and deliberately a separate
// copy rather than a shared one: that copy lives inside the non-abortable phase
// and this one must be able to give up. Sharing it would mean one function
// whose "do we ever stop?" answer depends on the caller, which is the exact
// conditional engineering-rules.md §3 splits the two executables to avoid.
int roundTripAbortable(BootloaderLink& link, const Frame& frame,
                       unsigned sleepMs, std::uint8_t replyId,
                       std::size_t replyLen, std::vector<std::uint8_t>* reply) {
    if (link.send(frame) != Io::Ok) return -1;
    link.sleepMs(sleepMs);
    std::vector<std::uint8_t> r;
    if (link.recv(replyId, replyLen, r) != Io::Ok) return -1;
    if (r.size() < 2) return -1;
    if (reply) *reply = r;
    return r[1];
}

}  // namespace

ReadBack readApplicationRegion(BootloaderLink& link) {
    ReadBack rb;
    rb.image.reserve(kBlockCount * kBlockSize);

    for (std::uint16_t idx = kBlockFirst; idx <= kBlockLast; ++idx) {
        std::vector<std::uint8_t> back;
        int st = -1;
        bool got = false;

        for (unsigned attempt = 0; attempt < kReadBlockTries; ++attempt) {
            if (attempt) link.sleepMs(100);
            st = roundTripAbortable(link, readBlock(static_cast<std::uint8_t>(idx)),
                                    50, kReportLarge, kLargeLen, &back);
            if (st == egg::kStatusReady && back.size() == kLargeLen) { got = true; break; }
            // A disconnect before erase is still a reason to stop, but trying a
            // reconnect first distinguishes "the cable moved" from "the
            // bootloader will not answer this command".
            if (st < 0) link.reconnect();
        }

        rb.lastStatus = st;
        if (!got) {
            rb.ok = false;
            rb.error = "block " + hex2(idx) + " did not read back after " +
                       std::to_string(kReadBlockTries) + " attempts (last status " +
                       (st < 0 ? std::string("no reply") : hex2(static_cast<unsigned>(st))) +
                       "). NOTHING has been erased or written.";
            return rb;
        }

        // The payload sits at kPayloadOffset, exactly where the write command
        // puts it. §3.4: the read response is 1041 bytes and the data is the
        // 1024 at [16..1039].
        rb.image.insert(rb.image.end(), back.begin() + kPayloadOffset,
                        back.begin() + kPayloadOffset + kBlockSize);
        ++rb.blocksRead;
    }

    if (rb.image.size() != kBlockCount * kBlockSize) {
        rb.ok = false;
        rb.error = "read " + std::to_string(rb.image.size()) +
                   " bytes, expected " + std::to_string(kBlockCount * kBlockSize);
        return rb;
    }
    rb.ok = true;
    return rb;
}

BackupCheck checkBackupFile(const std::string& path) {
    BackupCheck bc;
    if (path.empty()) {
        bc.reason = "no backup file was named";
        return bc;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        bc.reason = "cannot open " + path;
        return bc;
    }
    // ONE BYTE MORE than a backup. Reading exactly kBlockCount*kBlockSize and
    // checking the count would accept a file that is too LONG by silently
    // taking its first 66560 bytes -- and a file of the wrong length is exactly
    // the sign that it is not what the reader thinks it is.
    std::vector<std::uint8_t> buf(kBlockCount * kBlockSize + 1);
    const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    bc.size = got;
    if (got != kBlockCount * kBlockSize) {
        bc.reason = path + " is " + std::to_string(got) + " bytes; a backup is exactly " +
                    std::to_string(kBlockCount * kBlockSize);
        return bc;
    }
    buf.resize(got);

    // §4.1's "structurally plausible" applied to a file. A read loop that
    // returned nothing but produced a right-sized buffer anyway leaves this.
    bool varied = false;
    for (std::size_t i = 1; i < buf.size(); ++i)
        if (buf[i] != buf[0]) { varied = true; break; }
    if (!varied) {
        bc.reason = path + " is " + std::to_string(got) +
                    " identical bytes (" + hex2(buf[0]) + "); that is a failed read";
        return bc;
    }

    bc.sha256 = sha256Hex(buf.data(), buf.size());
    bc.ok = true;
    return bc;
}

std::vector<Frame> plannedFrames(const Image& img) {
    std::vector<Frame> out;
    // enter + start + (write+verify) per block + wholesum + complete + reset.
    out.reserve(5 + img.blockCount() * 2);

    // A1 3A leads, exactly as both captures do. §4.2b was revised on
    // 2026-09-05: a FLASH takes the vendor's entry, because their proven
    // sequence begins with it and nothing anywhere has ever flashed a
    // button-entered bootloader. It is in the PLAN, not just in the code, so
    // the approval token covers the entry too.
    out.push_back(enterBootloader());
    out.push_back(bootloaderStart(static_cast<std::uint8_t>(img.blockCount()),
                                  img.checksum()));
    for (std::size_t i = 0; i < img.blockCount(); ++i) {
        const std::uint8_t idx = img.deviceIndex(i);
        out.push_back(writeBlock(idx, img.block(i), kBlockSize));
        out.push_back(readBlock(idx));
    }
    out.push_back(wholeImageChecksumQuery(img.deviceIndex(img.blockCount() - 1)));
    out.push_back(bootloaderComplete());

    // A1 13, the vendor's post-flash factory reset. Restored to the plan
    // 2026-09-05 by the owner, reversing a decision of mine that was aesthetic
    // ("wiping settings is not part of writing firmware") rather than derived.
    //
    // THREE REASONS IT BELONGS HERE, none of which is "the vendor does it":
    //  - It is the best-understood command in the set. [D] at fw110 0x403dcf
    //    and cfg107 0x40479f, byte-identical in two binaries, and [O] -- it
    //    was run on this mouse and scored 21/21 against a predicted byte table
    //    (notes/prediction-factory-reset.md).
    //  - The undo is verified: the vault plus egg-config restore, also 21/21.
    //  - THE HAZARD IN SKIPPING IT. The settings record differs between
    //    firmware versions -- payload +0x71 separates 1.07's baseline from
    //    1.10's. Leaving stale settings under newly written firmware means the
    //    firmware reads a record it did not write, which is a state the
    //    vendor's tool never produces. Their reset is plausibly there for
    //    exactly this.
    //
    // NOTE ON DESTINATION: this frame does NOT go to the bootloader. §5.4 step
    // 7 sends it after the device has re-enumerated as the application, so it
    // is in the PLAN (and therefore in the approval token) but it is sent by
    // the CLI's post-phase, not by driveToVerifiedImage.
    out.push_back(postSuccess());
    return out;
}

std::string confirmTokenForFrames(const std::vector<Frame>& frames) {
    std::vector<std::uint8_t> all;
    for (const auto& f : frames)
        all.insert(all.end(), f.begin(), f.end());
    const std::string full = sha256Hex(all.data(), all.size());
    return full.substr(0, kConfirmTokenChars);
}

std::string confirmToken(const Image& img) {
    return confirmTokenForFrames(plannedFrames(img));
}

}  // namespace egg::fw
