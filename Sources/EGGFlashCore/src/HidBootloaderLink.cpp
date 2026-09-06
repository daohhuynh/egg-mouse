#include "egg/HidBootloaderLink.h"
#include "egg/FlashCommands.h"

#include <chrono>
#include <cstdio>
#include <string>
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

// ---------------------------------------------------------------------------
// The decisions. Pure, and therefore gradeable by mutants.sh with no mouse.
// ---------------------------------------------------------------------------
Io classifySend(egg::Outcome o, bool devicePresent) {
    switch (o) {
        case egg::Outcome::Ok:
            return Io::Ok;
        case egg::Outcome::TransportFail:
            return devicePresent ? Io::SendFailed : Io::Disconnected;
        default:
            return Io::SendFailed;
    }
}

Io classifyRecv(egg::Outcome o, std::size_t got, std::size_t want,
                bool devicePresent) {
    switch (o) {
        case egg::Outcome::Ok:
        case egg::Outcome::BadStatus:
        case egg::Outcome::BusyTimeout:
            return got == want ? Io::Ok : Io::ShortRead;

        case egg::Outcome::ShortRead:
            // Transport has already applied the N-1 rule: it returns ShortRead
            // only BELOW want-1. So this really is short.
            return Io::ShortRead;

        case egg::Outcome::TransportFail:
            return devicePresent ? Io::RecvFailed : Io::Disconnected;
    }
    return Io::RecvFailed;
}

bool reconnectLoop(unsigned budgetMs, unsigned stepMs,
                   const std::function<bool()>& tryAttach,
                   const std::function<void(unsigned)>& sleep,
                   const std::function<void(const std::string&)>& note) {
    for (unsigned waited = 0; waited <= budgetMs; waited += stepMs) {
        if (tryAttach()) {
            note("reconnected to the bootloader after " +
                 std::to_string(waited) + " ms");
            return true;
        }
        sleep(stepMs);
    }
    // False is not "give up" -- see the header. driveToVerifiedImage owns that
    // decision and does not have one.
    note("could not reconnect within " + std::to_string(budgetMs) +
         " ms; the phase will keep trying");
    return false;
}

// ---------------------------------------------------------------------------
// The doing. Only a mouse in bootloader mode reaches past the first line of
// each of these, which is exactly why the deciding is not in here.
// ---------------------------------------------------------------------------
Io HidBootloaderLink::send(const std::vector<std::uint8_t>& frame) {
    if (!tx_) { ++sendFailures_; return Io::Disconnected; }
    // "what" is only a log label. The frame itself decides the wire length;
    // Transport::send refuses a buffer whose length disagrees with its report
    // id, because macOS does no padding and a wrong length is a wrong frame.
    const char* what = frame.size() >= 2 && frame[0] == egg::kReportLarge
                     ? "A0 command" : "A1 command";
    const Io io = classifySend(
        tx_->send(frame, what),
        !egg::findVendorCollection(egg::kProductIdBootloader).empty());
    if (io != Io::Ok) ++sendFailures_;
    return io;
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

    // CHANGED 2026-09-06, deliberately and visibly. The old inline switch
    // returned ShortRead from the Ok/BadStatus/BusyTimeout arm WITHOUT
    // incrementing recvFailures_, while every other failure arm did. That was
    // an accident of how the arms were written, not a decision. The counter is
    // diagnostic only -- nothing in either executable reads it (grepped), and
    // WritePhase keeps its own -- so making it uniform costs nothing and stops
    // the log under-reporting a real short read.
    const Io io = classifyRecv(
        r.outcome, out.size(), len,
        !egg::findVendorCollection(egg::kProductIdBootloader).empty());
    if (io != Io::Ok) ++recvFailures_;
    return io;
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

    const bool back = reconnectLoop(
        kReconnectBudgetMs, kReconnectStepMs,
        [this] {
            // The presence check first: attach() opens a handle, and asking for
            // one on an empty bus logs a warning per attempt.
            return !egg::findVendorCollection(egg::kProductIdBootloader).empty()
                && attach();
        },
        [this](unsigned ms) { sleepMs(ms); },
        [this](const std::string& line) { note(line); });
    if (back) ++reconnects_;
    return back;
}

void HidBootloaderLink::note(const std::string& line) {
    // stderr, for the same reason reportSettingsUndo uses it: stdout carries
    // the frame stream that gets diffed against the vendor capture, and a
    // progress line on stdout would make that diff depend on how the run went.
    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);
}

}  // namespace egg::fw
