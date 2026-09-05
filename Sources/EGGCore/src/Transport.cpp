#include "egg/Transport.h"

#include <hidapi.h>

#include <chrono>
#include <cstdio>
#include <thread>

namespace egg {
namespace {

void sleepMs(unsigned ms) {
    if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// config 1.07 0x4038ed / updater 1.10: a failed SetFeature is retried, with
// Sleep(0x32) = 50 ms before the second attempt, up to four attempts.
//
// The vendor retries only for GetLastError in {0x15, 0x17, 0x1D, 0x57, 0x65B}.
// Those are Windows error codes and hidapi gives us nothing comparable, so we
// cannot mirror the SET; we mirror the SHAPE -- four attempts, 50 ms apart --
// and we say so rather than pretending the behaviour is identical.
constexpr int      kSendAttempts = 4;
constexpr unsigned kSendGapMs    = 50;

}  // namespace

const char* describe(Outcome o) {
    switch (o) {
        case Outcome::Ok:            return "ok";
        case Outcome::TransportFail: return "transport failed";
        case Outcome::BusyTimeout:   return "device stayed busy past the budget";
        case Outcome::BadStatus:     return "device returned a status that is not ready";
        case Outcome::ShortRead:     return "short read";
    }
    return "?";
}

std::vector<std::uint8_t> Transport::frame(std::uint8_t reportId, std::uint8_t command) {
    const std::size_t n = wireLength(reportId);
    if (n == 0) return {};
    std::vector<std::uint8_t> b(n, 0);
    b[0] = reportId;
    b[kCmdOffset] = command;
    return b;
}

Outcome Transport::send(const std::vector<std::uint8_t>& buf, const char* what) {
    if (buf.empty()) {
        log_.refused("empty frame");
        return Outcome::TransportFail;
    }
    const std::size_t want = wireLength(buf[0]);
    if (want == 0) {
        char b[96];
        std::snprintf(b, sizeof b, "report id 0x%02x is not 0xA0 or 0xA1", buf[0]);
        log_.refused(b);
        return Outcome::TransportFail;
    }
    if (buf.size() != want) {
        char b[128];
        std::snprintf(b, sizeof b,
            "frame for report 0x%02x is %zu bytes, must be exactly %zu",
            buf[0], buf.size(), want);
        log_.refused(b);
        return Outcome::TransportFail;
    }

    log_.frame(Dir::Out, buf, what);

    for (int attempt = 1; attempt <= kSendAttempts; ++attempt) {
        int rc = hid_send_feature_report(reinterpret_cast<hid_device*>(dev_.raw()),
                                         buf.data(), buf.size());
        if (rc == static_cast<int>(buf.size())) return Outcome::Ok;
        if (rc >= 0) {
            char b[128];
            std::snprintf(b, sizeof b, "short write: %d of %zu bytes", rc, buf.size());
            log_.warn(b);
        }
        if (attempt < kSendAttempts) {
            char b[96];
            std::snprintf(b, sizeof b, "send attempt %d failed, retrying in %u ms",
                          attempt, kSendGapMs);
            log_.note(b);
            sleepMs(kSendGapMs);
        }
    }
    log_.warn("send failed after 4 attempts");
    return Outcome::TransportFail;
}

Reply Transport::receive(std::uint8_t reportId, const char* what) {
    Reply r;
    const std::size_t want = wireLength(reportId);
    if (want == 0) {
        char b[96];
        std::snprintf(b, sizeof b, "report id 0x%02x is not 0xA0 or 0xA1", reportId);
        log_.refused(b);
        return r;
    }

    sleepMs(busy_.initialSleepMs);

    unsigned waited = busy_.initialSleepMs;
    unsigned step   = busy_.stepMs;

    for (;;) {
        r.buf.assign(want, 0);
        r.buf[0] = reportId;   // hidapi wants the id in byte 0 on the way in
        int rc = hid_get_feature_report(reinterpret_cast<hid_device*>(dev_.raw()),
                                        r.buf.data(), r.buf.size());
        if (rc < 0) {
            log_.warn("hid_get_feature_report failed");
            r.outcome = Outcome::TransportFail;
            return r;
        }
        if (static_cast<std::size_t>(rc) < want) {
            char b[128];
            std::snprintf(b, sizeof b, "read %d bytes, report declares %zu", rc, want);
            log_.warn(b);
            r.buf.resize(static_cast<std::size_t>(rc));
            log_.frame(Dir::In, r.buf, what);
            r.outcome = Outcome::ShortRead;
            return r;
        }

        r.status = r.buf[kStatusOffset];
        log_.frame(Dir::In, r.buf, what);

        if (r.status == kStatusReady) { r.outcome = Outcome::Ok; return r; }

        if (r.status != busy_.busyStatus) {
            // Not ready and not busy. The vendor keeps polling here; we do not,
            // because an unrecognised status is a fact worth surfacing rather
            // than something to spin on.
            char b[128];
            std::snprintf(b, sizeof b,
                "status 0x%02x is neither ready (0x01) nor busy (0x%02x)",
                r.status, busy_.busyStatus);
            log_.warn(b);
            r.outcome = Outcome::BadStatus;
            return r;
        }

        if (waited >= busy_.budgetMs) {
            char b[96];
            std::snprintf(b, sizeof b, "still busy after %u ms (budget %u ms)",
                          waited, busy_.budgetMs);
            log_.warn(b);
            r.outcome = Outcome::BusyTimeout;
            return r;
        }
        sleepMs(step);
        waited += step;
        step   += busy_.stepMs;   // +100 ms per pass, both tools
    }
}

Reply Transport::exchange(const std::vector<std::uint8_t>& out,
                          std::uint8_t replyReportId, const char* what) {
    Outcome o = send(out, what);
    if (o != Outcome::Ok) {
        Reply r;
        r.outcome = o;
        return r;
    }
    return receive(replyReportId, what);
}

}  // namespace egg
