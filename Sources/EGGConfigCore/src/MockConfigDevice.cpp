#include "egg/MockConfigDevice.h"

#include <cstring>

namespace egg::cfg {

namespace {

// A record shaped like the ones in windows-run: report id, A1 01 status header
// on reads, record 0x01 = 0x80 as a freshly-defaulted device reports, and a
// payload varied enough that plausible() has something real to accept rather
// than a constant fill it would reject for the wrong reason.
std::vector<std::uint8_t> defaultRecord() {
    std::vector<std::uint8_t> r(kLargeLen, 0);
    r[0] = kReportSmall;      // §2.1: the device answers with 0xA1 either way
    r[kStatusOffset] = kStatusReady;
    std::uint8_t* p = r.data() + kPayloadOffset;
    p[0x01] = 0x80;          // [O] eight of nine captured reads
    p[0x05] = 0x01;          // polling divisor, 8000 Hz
    p[0x06] = 0x01;
    p[0x08] = 0x01;
    p[0x09] = 0x05;
    p[0x0b] = 0x02;
    p[0x0e] = 0x04;
    p[0x0d] = 0x00;
    p[0x70] = 0x00;
    p[0x71] = 0x01;
    // Eight seven-byte button entries at 0x37..0x6e, so the record has the
    // variety a real one does.
    for (std::size_t i = 0; i < 8; ++i) p[0x37 + i * 7] = static_cast<std::uint8_t>(i + 1);
    return r;
}

Reply fail(Outcome o) {
    Reply r;
    r.outcome = o;
    return r;
}

}  // namespace

MockConfigDevice::MockConfigDevice(ConfigFaults f)
    : f_(f), rng_(f.seed ? f.seed : 1), record_(defaultRecord()) {}

bool MockConfigDevice::roll(double p) {
    // xorshift32: deterministic, seeded, and reproducible across machines,
    // which std::mt19937's distributions are not guaranteed to be.
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (static_cast<double>(rng_) / 4294967296.0) < p;
}

Reply MockConfigDevice::readRecord() {
    ++reads_;
    if (f_.readFailsAfterWrite && writes_ > 0) return fail(Outcome::TransportFail);
    if (roll(f_.transportFailRate)) return fail(Outcome::TransportFail);
    if (roll(f_.rejectReadRate)) {
        Reply r;
        r.outcome = Outcome::BadStatus;
        r.status = 0x02;
        return r;
    }
    Reply r;
    r.outcome = Outcome::Ok;
    r.status = kStatusReady;
    r.buf = record_;
    // wire-observed.md §2.1: the device never sends the report-id slot, so an
    // N-byte report reads back N-1 bytes. record_ stays kLargeLen internally so
    // every payload offset is unchanged; only the REPLY is short. Until
    // 2026-09-05 this mock returned kLargeLen and so certified a Transport
    // check that rejected every real read.
    // MockConfigDevice implements ConfigLink, which sits ABOVE Transport, so it
    // returns what Transport hands up: full length, device bytes at [0..N-2].
    // Byte 0 is 0xA1 because that is what the device really answers with, for
    // EITHER report (§2.1) -- not 0xA0, which nothing ever sends.
    r.buf[0] = kReportSmall;
    r.buf[kStatusOffset] = kStatusReady;

    if (roll(f_.implausibleRate)) {
        // The shape a wrong-collection or half-initialised read produces.
        std::memset(r.buf.data() + kPayloadOffset, 0xFF, kPayloadLen);
    }
    if (roll(f_.shortReadRate)) {
        r.buf.resize(kLargeLen - 7);
    }
    return r;
}

Reply MockConfigDevice::writeRecord(const std::vector<std::uint8_t>& frame) {
    ++writes_;
    sent_.push_back(frame);

    if (roll(f_.transportFailRate)) return fail(Outcome::TransportFail);
    if (roll(f_.rejectWriteRate)) {
        Reply r;
        r.outcome = Outcome::BadStatus;
        r.status = 0x02;
        return r;
    }

    // The acknowledgement the session receives is identical in every branch
    // below. That is the whole point: it carries no information about whether
    // the store happened.
    Reply ack;
    ack.outcome = Outcome::Ok;
    ack.status = kStatusReady;
    ack.buf.assign(kSmallLen, 0);
    ack.buf[0] = kReportSmall;
    ack.buf[kStatusOffset] = kStatusReady;

    if (roll(f_.falseSuccessRate)) return ack;   // acknowledged, stored nothing

    if (frame.size() == kLargeLen) {
        std::memcpy(record_.data() + kPayloadOffset,
                    frame.data() + kPayloadOffset, kPayloadLen);
        ++acceptedWrites_;

        if (roll(f_.corruptStoreRate))
            record_[kPayloadOffset + 0x09] =
                static_cast<std::uint8_t>(record_[kPayloadOffset + 0x09] ^ 0xFF);
        if (roll(f_.extraByteRate))
            record_[kPayloadOffset + 0x72] =
                static_cast<std::uint8_t>(record_[kPayloadOffset + 0x72] + 1);
        if (f_.restoresUnknownByte)
            record_[kPayloadOffset + kRecordUnknownFirst] = 0x80;
    }
    return ack;
}

Reply MockConfigDevice::factoryReset() {
    if (roll(f_.transportFailRate)) return fail(Outcome::TransportFail);
    record_ = defaultRecord();
    Reply r;
    r.outcome = Outcome::Ok;
    r.status = kStatusReady;
    r.buf.assign(kSmallLen, 0);
    r.buf[0] = kReportSmall;
    r.buf[kStatusOffset] = kStatusReady;
    return r;
}

void MockConfigDevice::note(const std::string& line) { notes_.push_back(line); }

}  // namespace egg::cfg
