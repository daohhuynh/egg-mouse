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

// A failed SetFeature is retried, up to four attempts, with a fixed gap.
//
// THE GAP IS NOT THE SAME IN BOTH TOOLS, and this comment used to say it was:
// it cited "config 1.07 0x4038ed / updater 1.10" for one Sleep(0x32) = 50 ms.
// Verified from raw bytes 2026-09-05 -- cfg107 0x4038ed is `6a 32` (50 ms) but
// fw110 0x401312 is `6a 0a` (10 ms). Transport is shared by both executables,
// so a reader checking the flasher's retry timing against that citation was
// checking it against a number updater 1.10 does not contain. A false [D] in
// the file where the derivations live.
//
// We use the config figure, 50 ms, which is the slower of the two and so
// cannot retry sooner than either vendor.
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

Reply Transport::receive(std::uint8_t reportId, const char* what,
                         unsigned firstReadDelayMs) {
    Reply r;
    const std::size_t want = wireLength(reportId);
    if (want == 0) {
        char b[96];
        std::snprintf(b, sizeof b, "report id 0x%02x is not 0xA0 or 0xA1", reportId);
        log_.refused(b);
        return r;
    }

    const unsigned firstDelay = (firstReadDelayMs == kUsePolicyDelay)
                              ? busy_.initialSleepMs : firstReadDelayMs;
    sleepMs(firstDelay);

    unsigned waited  = firstDelay;
    unsigned step    = busy_.stepMs;
    unsigned passes  = 0;

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
        // N-1 IS THE COMPLETE READ, NOT A SHORT ONE. wire-observed.md §2.1:
        // buf[0] is the report-id slot and THE DEVICE NEVER SENDS IT, so an
        // N-byte report reads back N-1 bytes of content. Both captured Windows
        // replies show it -- frames/01-baseline-003-in-01.bin is 1040 for the
        // 1041-byte 0xA0 report, frames/01-baseline-001-in-01.bin is 63 for the
        // 64-byte 0xA1 -- and 0x10 + 1024 = 1040 lands the payload flush.
        //
        // This check demanded `want` and so failed every real read. It passed
        // the whole test suite because MockConfigDevice returns `want`: the
        // mock was built from the same wrong reading, which is exactly the
        // limit CLAUDE.md decision #1 records ("if the derivation is wrong the
        // mock is confidently wrong in the same direction"). Caught 2026-09-05
        // by the first read from the real device.
        //
        // Resizing to rc leaves the buffer byte-comparable with the capture
        // files, index for index.
        const std::size_t least = want - 1;
        if (static_cast<std::size_t>(rc) < least) {
            char b[160];
            std::snprintf(b, sizeof b,
                "read %d bytes, expected at least %zu (report %zu, less the "
                "report-id slot the device does not send)", rc, least, want);
            log_.warn(b);
            r.buf.resize(static_cast<std::size_t>(rc));
            log_.frame(Dir::In, r.buf, what);
            r.outcome = Outcome::ShortRead;
            return r;
        }
        // Deliberately NOT resized to rc. The buffer stays `want` with the
        // device's N-1 bytes at [0..N-2] and the last byte left zero, which is
        // exactly the vendor's own model: it allocates N, writes the report id
        // into buf[0] itself, and the device fills the rest. wire-observed.md
        // §2.1 proves buf[i] == wire[i]. Everything above Transport therefore
        // keeps seeing a full-length record and needs no change.

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

        // PASSES, NOT MILLISECONDS. The vendor's counter advances by a
        // constant 0x64 per pass and is compared against 0x3e8 / 0x7d0, so
        // those are 10 and 20 re-reads -- see Protocol.h. Counting wall time
        // instead made us give up after 5 reads spanning 1.1 s where the config
        // tool allows 10 spanning ~4.6 s, and §4.2 makes giving up early the
        // dangerous direction for the flasher's post-erase loop.
        if (passes >= busy_.maxPasses) {
            char b[128];
            std::snprintf(b, sizeof b,
                "still busy after %u re-reads spanning %u ms (limit %u re-reads)",
                passes, waited, busy_.maxPasses);
            log_.warn(b);
            r.outcome = Outcome::BusyTimeout;
            return r;
        }
        sleepMs(step);
        waited += step;
        step   += busy_.stepMs;   // +100 ms per pass, both tools
        ++passes;
    }
}

Reply Transport::exchange(const std::vector<std::uint8_t>& out,
                          std::uint8_t replyReportId, const char* what,
                          unsigned firstReadDelayMs) {
    Outcome o = send(out, what);
    if (o != Outcome::Ok) {
        Reply r;
        r.outcome = o;
        return r;
    }
    return receive(replyReportId, what, firstReadDelayMs);
}

}  // namespace egg
