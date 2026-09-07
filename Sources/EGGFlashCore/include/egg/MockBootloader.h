// MockBootloader.h -- an ADVERSARIAL bootloader. It is not trying to help.
//
// engineering-rules.md §4.3 names this "the highest-value artefact in the project" and
// says why: the dangerous failures -- wrong chunk boundary, wrong opcode,
// trusting a reported success -- compile clean in any language. Only something
// that behaves badly on purpose can find them, and this needs no hardware.
//
// It is COOPERATIVE ONLY when told to be. Every fault below is off by default
// and switched on explicitly by a test, so a test that forgets to enable a
// fault fails honestly rather than passing against a friendly device.
//
// Determinism is required, not incidental: §4.3 also wants "the byte stream for
// a given input is identical every run". The generator is a plain LCG seeded
// per test so a failure is reproducible from its seed alone.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/Firmware.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace egg::fw {

struct Faults {
    // Reject a fraction of commands with a non-ready status.
    double rejectRate = 0.0;
    // Return a response that is too short, or has a plausible-looking but
    // wrong shape. This is the one that catches code reading resp[6] without
    // checking the length.
    double malformedRate = 0.0;
    // THE DANGEROUS ONE. Report status 0x01 for a write that was not stored.
    // A flasher that trusts a reported success instead of reading back will
    // pass every other test and fail this one.
    double falseSuccessRate = 0.0;
    // Stop answering entirely for a while, mid-write.
    double silenceRate = 0.0;
    unsigned silenceLength = 3;
    // Drop the link; recover only when reconnect() is called.
    double disconnectRate = 0.0;
    // Answer, but only after a delay longer than any timeout. Modelled as a
    // very large sleepMs, which the test clock records rather than performs.
    double stallRate = 0.0;
    // Corrupt what is stored, so the read-back differs from what was sent.
    double corruptStoreRate = 0.0;
    // Corrupt what is stored WITHOUT changing the 16-bit sum (+1 on one byte,
    // -1 on another). Only the byte-by-byte comparison can catch this; the
    // per-block checksum still matches. Added 2026-09-05 after mutation
    // testing showed the byte comparison could be deleted with no test
    // failing -- the two guards were redundant against every fault the mock
    // could then produce, so neither was actually being measured.
    double corruptPreservingSumRate = 0.0;
    // Store correctly but REPORT a wrong per-block checksum. The mirror image:
    // only the checksum comparison catches this one.
    double lieAboutBlockSumRate = 0.0;
    // Report a whole-image checksum that disagrees with what was actually
    // stored, for the first N queries, to exercise the re-verify path and then
    // let it converge. A device that lies FOREVER is a different test -- see
    // the note in test_flash.cpp about why that one cannot terminate.
    unsigned lieAboutWholeChecksumTimes = 0;
    // Accept A1 09's send but never acknowledge it. Models the [O] timing race:
    // the device acts on A1 09 and re-enumerates in 857-896 ms while roundTrip
    // reads the status after 50 ms, so the ack is simply not there to be read.
    // Added 2026-09-05 -- the mock could not express this, and the unbounded
    // A1 09 loop it would have caught went undetected until an audit found it.
    bool neverAckComplete = false;
    // A block that answers A0 07 with a non-ready status forever AFTER it has
    // been written and verified once. Models flash that goes bad mid-session,
    // which is the case the whole-image repair pass exists for.
    int unreadableAfterVerify = -1;      // device block index, or -1
    std::uint32_t seed = 1;
};

class MockBootloader : public BootloaderLink {
public:
    explicit MockBootloader(Faults f = {}) : f_(f), rng_(f.seed ? f.seed : 1) {}

    Io send(const std::vector<std::uint8_t>& frame) override;
    Io recv(std::uint8_t reportId, std::size_t len,
            std::vector<std::uint8_t>& out) override;
    void sleepMs(unsigned ms) override { slept_ += ms; }
    bool reconnect() override;
    void note(const std::string& line) override { log_.push_back(line); }

    // What the device actually holds, keyed by device block index.
    const std::map<std::uint8_t, std::vector<std::uint8_t>>& flash() const {
        return flash_;
    }

    // Give the device a RESIDENT image, as though it had been flashed before
    // this process started.
    //
    // This exists because the mock's default -- rejecting A0 07 for any block
    // it has not seen written this session -- is itself a HYPOTHESIS about the
    // device, not a fact. Whether the bootloader serves reads outside a flash
    // session it started with A0 03 is [G] (notes/bootloader-observed.md), and
    // a mock that can only express one of the two answers would quietly make
    // the read-back path untestable against the other. So both are modelled:
    // preload() for "the device serves its resident image", the default for
    // "reads only work inside a session".
    //
    // §4.3's whole point is that a mock built from our assumptions cannot catch
    // an error in those assumptions. This is the smallest correction available:
    // where the assumption is not known, make the mock able to be either.
    void preload(const std::vector<std::uint8_t>& image) {
        flash_.clear();
        for (std::size_t i = 0; i * kBlockSize < image.size(); ++i) {
            const std::size_t off = i * kBlockSize;
            const std::size_t n = std::min(kBlockSize, image.size() - off);
            std::vector<std::uint8_t> blk(kBlockSize, 0);
            std::copy(image.begin() + static_cast<long>(off),
                      image.begin() + static_cast<long>(off + n), blk.begin());
            flash_[static_cast<std::uint8_t>(kBlockFirst + i)] = std::move(blk);
        }
    }
    // Every block index this link was ever ASKED to write, in order, including
    // rejected and repeated ones. The out-of-range guard is tested against
    // this rather than against what was stored.
    const std::vector<std::uint16_t>& writtenIndices() const { return idx_; }
    const std::vector<std::string>& log() const { return log_; }
    unsigned sleptMs() const { return slept_; }
    unsigned reconnects() const { return reconnects_; }
    bool started() const { return started_; }
    bool completed() const { return completed_; }
    std::uint8_t declaredBlockCount() const { return declBlocks_; }
    std::uint32_t declaredChecksum() const { return declSum_; }

    // Every frame that reached the device, verbatim -- the dry-run and
    // golden-file comparison (§4.3) reads this.
    const std::vector<std::vector<std::uint8_t>>& sent() const { return sent_; }

private:
    bool roll(double p);
    std::uint32_t next();

    Faults f_;
    std::uint32_t rng_;
    std::map<std::uint8_t, std::vector<std::uint8_t>> flash_;
    std::vector<std::uint16_t> idx_;
    std::vector<std::vector<std::uint8_t>> sent_;
    std::vector<std::string> log_;
    std::vector<std::uint8_t> pending_;   // the response recv() will hand back
    std::set<std::uint8_t> readOnce_;
    unsigned slept_ = 0, silence_ = 0, reconnects_ = 0, lies_ = 0;
    bool down_ = false, started_ = false, completed_ = false;
    std::uint8_t declBlocks_ = 0;
    std::uint32_t declSum_ = 0;
};

}  // namespace egg::fw
