// MockConfigDevice.h -- a config device that lies, on demand.
//
// §4.3 calls the adversarial mock "the highest-value artefact in the project"
// and "needs no hardware", and until 2026-09-05 one existed for the bootloader
// and none for config. So the single most dangerous config failure -- a device
// that ACKNOWLEDGES a write it did not perform -- was the one thing no test
// could produce.
//
// COOPERATIVE ONLY WHEN TOLD TO BE. Every fault is off by default, so a test
// that forgets to enable one fails honestly rather than passing against a
// friendly device.
#pragma once

#include "egg/ConfigLink.h"
#include "egg/ConfigRecord.h"

#include <cstdint>
#include <string>
#include <vector>

namespace egg::cfg {

struct ConfigFaults {
    // Answer, but with a status that is not "ready".
    double rejectReadRate = 0.0;
    double rejectWriteRate = 0.0;

    // The transfer itself fails; the device may never have seen the request.
    double transportFailRate = 0.0;

    // Answer a read with the right length but a constant fill -- the shape a
    // half-initialised or wrong-collection read actually produces. This is what
    // plausible() exists to catch.
    double implausibleRate = 0.0;

    // Answer a read with the wrong length. Catches code that indexes before
    // checking size.
    double shortReadRate = 0.0;

    // THE DANGEROUS ONE. Acknowledge a write and store nothing. A session that
    // trusts the acknowledgement instead of reading back passes every other
    // test and fails this one.
    double falseSuccessRate = 0.0;

    // Store something other than what was sent.
    double corruptStoreRate = 0.0;

    // Store what was sent, and additionally change one byte of its own accord.
    // Real devices do this: a dependent field, a recomputed checksum. It must
    // be reported, not treated as our own bug.
    double extraByteRate = 0.0;

    // Model the hypothesis that record 0x01 belongs to the DEVICE: whatever the
    // host writes there, the device puts 0x80 back. If that were true, a
    // session that preserves 0x80 and one that zeroes it would both converge,
    // and neither policy could corrupt anything. Off by default because it is
    // [G]; the captures show the opposite (04-buttons read back 0x00).
    bool restoresUnknownByte = false;

    std::uint32_t seed = 1;
};

class MockConfigDevice : public ConfigLink {
public:
    // Starts holding a record shaped like the ones actually observed: record
    // 0x01 = 0x80 as a freshly-defaulted device reports, and a varied payload
    // so that plausible() has something real to accept.
    explicit MockConfigDevice(ConfigFaults f = {});

    Reply readRecord() override;
    Reply writeRecord(const std::vector<std::uint8_t>& frame) override;
    Reply factoryReset() override;
    void  note(const std::string& line) override;

    // Ground truth, for assertions. This is what the device ACTUALLY holds,
    // which is not necessarily what it reports.
    const std::vector<std::uint8_t>& stored() const { return record_; }
    std::uint8_t storedRecord(std::size_t off) const {
        return record_[kPayloadOffset + off];
    }

    unsigned reads() const { return reads_; }
    unsigned writes() const { return writes_; }
    unsigned acceptedWrites() const { return acceptedWrites_; }
    const std::vector<std::string>& notes() const { return notes_; }

    // Every frame the session actually put on the wire, in order. The golden
    // seam: a test can assert on exact bytes rather than on effects.
    const std::vector<std::vector<std::uint8_t>>& sentFrames() const { return sent_; }

private:
    bool roll(double p);

    ConfigFaults              f_;
    std::uint32_t             rng_;
    std::vector<std::uint8_t> record_;      // a full 1041-byte frame
    std::vector<std::vector<std::uint8_t>> sent_;
    std::vector<std::string>  notes_;
    unsigned reads_{0}, writes_{0}, acceptedWrites_{0};
};

}  // namespace egg::cfg
