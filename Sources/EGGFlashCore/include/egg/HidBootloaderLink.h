// HidBootloaderLink.h -- the device-backed BootloaderLink. The piece that was
// missing, and the piece BootloaderLink.h has been warning about:
//
//   "There is no device-backed BootloaderLink yet; when one is written, this is
//    the first thing to get right."
//
// -- referring to the N-1 contract, where the device sends len-1 bytes because
// buf[0] is the report-id slot it never transmits. Treating that as a short
// read makes driveToVerifiedImage retry a healthy device forever, inside the
// non-abortable post-erase loop, where there is no timeout and no cancel.
//
// SO IT IS NOT REIMPLEMENTED HERE. egg::Transport already implements N-1, was
// corrected against the physical mouse on 2026-09-05 (the 1041-byte 0xA0 report
// reading back 1040), and is the transport both executables already use. This
// class delegates to it. One implementation of that rule, and no second copy to
// drift out of agreement with the first -- the same argument CLAUDE.md §3 makes
// for one repo, applied one level down.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/Device.h"
#include "egg/Log.h"
#include "egg/Transport.h"

#include <memory>
#include <string>

namespace egg::fw {

class HidBootloaderLink : public BootloaderLink {
public:
    // Opens the bootloader's vendor collection. Returns nullptr and explains on
    // the log if it is absent or ambiguous -- Device::open refuses an ambiguous
    // match rather than taking the first, per build-design.md §1.
    static std::unique_ptr<HidBootloaderLink> open(egg::Log& log);

    Io   send(const std::vector<std::uint8_t>& frame) override;
    Io   recv(std::uint8_t reportId, std::size_t len,
              std::vector<std::uint8_t>& out) override;
    void sleepMs(unsigned ms) override;
    bool reconnect() override;
    void note(const std::string& line) override;

    // Counters, so a caller can report what the link actually endured rather
    // than what it assumed. §4.2 wants the post-erase phase to be loud.
    unsigned reconnects() const { return reconnects_; }
    unsigned sendFailures() const { return sendFailures_; }
    unsigned recvFailures() const { return recvFailures_; }

private:
    explicit HidBootloaderLink(egg::Log& log) : log_(log) {}
    bool attach();          // (re)open the device and rebuild the transport

    egg::Log&                     log_;
    std::unique_ptr<egg::Device>  dev_;
    std::unique_ptr<egg::Transport> tx_;
    unsigned reconnects_   = 0;
    unsigned sendFailures_ = 0;
    unsigned recvFailures_ = 0;
};

// How long reconnect() keeps trying before returning false.
//
// It returns false rather than looping forever ON PURPOSE, even though §4.2
// says the post-erase phase must not give up. The giving-up decision belongs to
// driveToVerifiedImage, which treats a false return as "sleep and try the whole
// operation again" and has no exit of its own. If the link looped internally
// instead, the phase could not log progress, could not re-print the recovery
// procedure, and could not tell a slow reconnect from a wedged one.
inline constexpr unsigned kReconnectBudgetMs = 20000;
inline constexpr unsigned kReconnectStepMs   = 100;

}  // namespace egg::fw
