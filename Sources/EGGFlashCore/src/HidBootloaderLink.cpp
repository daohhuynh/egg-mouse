#include "egg/HidBootloaderLink.h"
#include "egg/FlashCommands.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace egg::fw {

std::unique_ptr<HidBootloaderLink> HidBootloaderLink::open(egg::Log& log) {
    std::unique_ptr<HidBootloaderLink> l(new HidBootloaderLink(log));
    if (!l->attach()) return nullptr;
    return l;
}

bool HidBootloaderLink::attach() {
    tx_.reset();
    dev_ = egg::Device::open(egg::kProductIdBootloader, log_);
    if (!dev_) return false;
    tx_ = std::make_unique<egg::Transport>(*dev_, egg::kUpdaterBusy, log_);
    return true;
}

Io HidBootloaderLink::send(const std::vector<std::uint8_t>& frame) {
    if (!tx_) { ++sendFailures_; return Io::Disconnected; }
    // "what" is only a log label. The frame itself decides the wire length;
    // Transport::send refuses a buffer whose length disagrees with its report
    // id, because macOS does no padding and a wrong length is a wrong frame.
    const char* what = frame.size() >= 2 && frame[0] == egg::kReportLarge
                     ? "A0 command" : "A1 command";
    switch (tx_->send(frame, what)) {
        case egg::Outcome::Ok: return Io::Ok;
        case egg::Outcome::TransportFail:
            ++sendFailures_;
            // A transport failure after the device has gone is a disconnect,
            // which reconnect() can fix; before that it is a send failure,
            // which it cannot. Distinguishing them lets driveToVerifiedImage
            // pick between reconnecting and simply retrying.
            return egg::findVendorCollection(egg::kProductIdBootloader).empty()
                 ? Io::Disconnected : Io::SendFailed;
        default:
            ++sendFailures_;
            return Io::SendFailed;
    }
}

Io HidBootloaderLink::recv(std::uint8_t reportId, std::size_t len,
                           std::vector<std::uint8_t>& out) {
    if (!tx_) { ++recvFailures_; return Io::Disconnected; }
    if (len != egg::wireLength(reportId)) {
        // Not a device problem: a caller asked for a length the report does not
        // have. Fail loudly rather than truncate.
        note("recv asked for " + std::to_string(len) + " bytes of report 0x" +
             hex2(reportId) + ", which is " +
             std::to_string(egg::wireLength(reportId)) + " bytes");
        ++recvFailures_;
        return Io::ShortRead;
    }

    const egg::Reply r = tx_->receive(reportId, "read");
    out = r.buf;

    switch (r.outcome) {
        // Ok and BadStatus BOTH mean "the device answered and the bytes are
        // valid". BadStatus only says resp[1] was not 0x01, and judging resp[1]
        // is the CALLER's job: WritePhase::roundTrip returns it so the phase can
        // decide, and BootloaderLink's contract is to fetch a report, not to
        // grade it. Collapsing BadStatus into a failure here would turn every
        // busy or not-yet-ready answer into an indistinguishable -1 and throw
        // away the status byte the retry logic is built on.
        case egg::Outcome::Ok:
        case egg::Outcome::BadStatus:
        case egg::Outcome::BusyTimeout:
            return out.size() == len ? Io::Ok : Io::ShortRead;

        case egg::Outcome::ShortRead:
            // Transport has already applied the N-1 rule: it returns ShortRead
            // only BELOW len-1. So this really is short.
            ++recvFailures_;
            return Io::ShortRead;

        case egg::Outcome::TransportFail:
            ++recvFailures_;
            return egg::findVendorCollection(egg::kProductIdBootloader).empty()
                 ? Io::Disconnected : Io::RecvFailed;
    }
    ++recvFailures_;
    return Io::RecvFailed;
}

void HidBootloaderLink::sleepMs(unsigned ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool HidBootloaderLink::reconnect() {
    // Drop what we have first. Holding a stale hid_device across a
    // re-enumeration is how a link comes back "connected" to a handle the
    // kernel has already torn down.
    tx_.reset();
    dev_.reset();

    for (unsigned waited = 0; waited <= kReconnectBudgetMs;
         waited += kReconnectStepMs) {
        if (!egg::findVendorCollection(egg::kProductIdBootloader).empty() && attach()) {
            ++reconnects_;
            note("reconnected to the bootloader after " +
                 std::to_string(waited) + " ms");
            return true;
        }
        sleepMs(kReconnectStepMs);
    }
    // False is not "give up" -- see the header. driveToVerifiedImage owns that
    // decision and does not have one.
    note("could not reconnect within " + std::to_string(kReconnectBudgetMs) +
         " ms; the phase will keep trying");
    return false;
}

void HidBootloaderLink::note(const std::string& line) {
    // stderr, for the same reason reportSettingsUndo uses it: stdout carries
    // the frame stream that gets diffed against the vendor capture, and a
    // progress line on stdout would make that diff depend on how the run went.
    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);
}

}  // namespace egg::fw
