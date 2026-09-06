#include "egg/MockBootloader.h"

#include "egg/FlashCommands.h"
#include "egg/Protocol.h"

namespace egg::fw {

std::uint32_t MockBootloader::next() {
    // Numerical Recipes LCG. Deterministic and reproducible from the seed,
    // which is the whole requirement -- nothing here needs statistical quality.
    rng_ = rng_ * 1664525u + 1013904223u;
    return rng_;
}

bool MockBootloader::roll(double p) {
    if (p <= 0.0) return false;
    return (next() >> 8) < static_cast<std::uint32_t>(p * 16777216.0);
}

bool MockBootloader::reconnect() {
    ++reconnects_;
    down_ = false;
    silence_ = 0;
    return true;
}

Io MockBootloader::send(const std::vector<std::uint8_t>& frame) {
    sent_.push_back(frame);
    if (down_) return Io::Disconnected;
    if (silence_) { --silence_; return Io::SendFailed; }
    if (roll(f_.disconnectRate)) { down_ = true; return Io::Disconnected; }
    if (roll(f_.silenceRate)) { silence_ = f_.silenceLength; return Io::SendFailed; }
    if (roll(f_.stallRate)) sleepMs(60000);

    pending_.clear();
    if (frame.size() < 2) return Io::SendFailed;
    const std::uint8_t cmd = frame[1];

    auto reply = [&](std::size_t len, std::uint8_t status) {
        pending_.assign(len, 0);
        pending_[0] = (len == kLargeLen) ? kReportLarge : kReportSmall;
        pending_[1] = status;
    };
    const std::uint8_t rejected = 0x02;
    const bool reject = roll(f_.rejectRate);

    switch (cmd) {
    case 0x03:                                    // bootloader start
        if (frame.size() < 21) { reply(kSmallLen, rejected); break; }
        if (!reject) {
            started_ = true;
            declBlocks_ = frame[16];
            declSum_ = static_cast<std::uint32_t>(frame[17]) |
                       (static_cast<std::uint32_t>(frame[18]) << 8) |
                       (static_cast<std::uint32_t>(frame[19]) << 16) |
                       (static_cast<std::uint32_t>(frame[20]) << 24);
            // Real erase semantics: start wipes whatever was resident.
            flash_.clear();
        }
        reply(kSmallLen, reject ? rejected : 0x01);
        break;

    case 0x06: {                                  // write one block
        if (frame.size() < kLargeLen) { reply(kSmallLen, rejected); break; }
        const std::uint16_t bi =
            static_cast<std::uint16_t>(frame[2] | (frame[3] << 8));
        idx_.push_back(bi);
        if (reject) { reply(kSmallLen, rejected); break; }
        // FALSE SUCCESS: acknowledge, store nothing. Only a read-back catches
        // this, which is exactly the point of testing it.
        if (roll(f_.falseSuccessRate)) { reply(kSmallLen, 0x01); break; }
        std::vector<std::uint8_t> data(frame.begin() + 16,
                                       frame.begin() + 16 + kBlockSize);
        if (roll(f_.corruptStoreRate) && !data.empty())
            data[next() % data.size()] ^= 0xFF;
        if (roll(f_.corruptPreservingSumRate) && data.size() > 1) {
            // +1 here, -1 there: the bytes change, the 16-bit sum does not.
            std::size_t a = next() % data.size(), b = next() % data.size();
            while (b == a) b = (b + 1) % data.size();
            if (data[a] != 0xFF && data[b] != 0x00) { ++data[a]; --data[b]; }
        }
        flash_[static_cast<std::uint8_t>(bi & 0xFF)] = std::move(data);
        reply(kSmallLen, 0x01);
        break;
    }
    case 0x07: {                                  // read one block back
        const std::uint8_t bi = frame.size() > 2 ? frame[2] : 0;
        auto it = flash_.find(bi);
        if (reject || it == flash_.end()) { reply(kLargeLen, rejected); break; }
        reply(kLargeLen, 0x01);
        std::uint16_t sum = blockChecksum(it->second.data(), it->second.size());
        if (roll(f_.lieAboutBlockSumRate)) sum = static_cast<std::uint16_t>(sum ^ 0x0100);
        pending_[6] = static_cast<std::uint8_t>(sum & 0xFF);
        pending_[7] = static_cast<std::uint8_t>((sum >> 8) & 0xFF);
        for (std::size_t i = 0; i < it->second.size(); ++i)
            pending_[16 + i] = it->second[i];
        break;
    }
    case 0x08: {                                  // whole-image checksum
        if (reject) { reply(kSmallLen, rejected); break; }
        std::uint32_t s = 0;
        for (const auto& kv : flash_) for (std::uint8_t b : kv.second) s += b;
        if (lies_ < f_.lieAboutWholeChecksumTimes) { ++lies_; s ^= 0xA5A5u; }
        reply(kSmallLen, 0x01);
        pending_[16] = static_cast<std::uint8_t>(s & 0xFF);
        pending_[17] = static_cast<std::uint8_t>((s >> 8) & 0xFF);
        pending_[18] = static_cast<std::uint8_t>((s >> 16) & 0xFF);
        pending_[19] = static_cast<std::uint8_t>((s >> 24) & 0xFF);
        break;
    }
    case 0x09:                                    // complete
        if (!reject) completed_ = true;
        reply(kSmallLen, reject ? rejected : 0x01);
        break;
    default:
        reply(kSmallLen, reject ? rejected : 0x01);
        break;
    }
    return Io::Ok;
}

Io MockBootloader::recv(std::uint8_t reportId, std::size_t len,
                        std::vector<std::uint8_t>& out) {
    if (down_) return Io::Disconnected;
    if (silence_) { --silence_; return Io::RecvFailed; }
    if (pending_.empty()) return Io::RecvFailed;

    // THE REPORT ID IS PART OF THE REQUEST, and until 2026-09-05 this ignored
    // it. A caller asking for 64 bytes of 0xA1 while the device held a
    // 1041-byte 0xA0 answer got the first 64 bytes and Io::Ok, so "reads the
    // wrong report after a command" was a whole defect class the mock could not
    // catch -- on the very path where the vendor's own code gets it wrong
    // (§5.6: their repair loop passes 0xA0 to a fixed 64-byte read). A real
    // device cannot serve an 0xA1 report out of an 0xA0 answer.
    //
    // Found by audit, 2026-09-05, not by a failing test: nothing was asking for
    // the wrong id, so this was a gap in what the mock could measure rather
    // than a bug it was hiding.
    if (len != egg::wireLength(reportId) || pending_.size() != len ||
        pending_[0] != reportId) {
        pending_.clear();
        return Io::ShortRead;
    }

    out = pending_;
    pending_.clear();
    // MALFORMED: truncate, so anything indexing resp[6] or resp[16..19]
    // without a length check reads past the end.
    if (roll(f_.malformedRate)) {
        out.resize(out.size() > 4 ? (next() % 5) : 0);
        return Io::Ok;
    }
    if (out.size() < len) return Io::ShortRead;
    out.resize(len);
    out[0] = reportId;
    return Io::Ok;
}

}  // namespace egg::fw
