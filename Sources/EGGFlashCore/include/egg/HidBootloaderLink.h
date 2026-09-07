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
// drift out of agreement with the first -- the same argument engineering-rules.md §3 makes
// for one repo, applied one level down.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/Device.h"
#include "egg/Log.h"
#include "egg/Transport.h"

#include <functional>
#include <memory>
#include <string>

namespace egg::fw {

// ---------------------------------------------------------------------------
// What the link DECIDES, separated from what it DOES
// ---------------------------------------------------------------------------
// Extracted from the class 2026-09-06. The class was untestable and, worse,
// ungradeable: it was the one file in the post-erase path that mutants.sh could
// not touch, because nothing but a physical mouse in bootloader mode could
// reach a single line of it. So a mutation here -- collapsing BadStatus into a
// failure, or an off-by-one in the reconnect budget -- would have been caught
// by nobody, in the one code path §4.2 says must never give up.
//
// The split is by testability, not by taste: everything below is a pure
// function of an outcome and a couple of facts, so it can be graded on a
// machine with no device attached. Everything that opens a handle, sends a
// report or sleeps stays in the class, where only hardware can exercise it.
//
// This changes no byte on the wire (§4.2: a safety measure that changes the
// byte stream is not free -- this one is off-wire and therefore free).

// TransportFail after the device has left the bus is a DISCONNECT, which
// reconnect() can fix; while it is still present it is a send failure, which
// reconnect() cannot. driveToVerifiedImage picks between reconnecting and
// simply retrying on exactly this distinction, so it is not cosmetic.
Io classifySend(egg::Outcome o, bool devicePresent);

// `got` is how many bytes came back, `want` how many the report declares.
//
// Ok, BadStatus and BusyTimeout all mean "the device answered and the bytes are
// valid". BadStatus only says resp[1] was not 0x01, and judging resp[1] is the
// CALLER's job: WritePhase::roundTrip returns it so the phase can decide, and
// BootloaderLink's contract is to fetch a report, not to grade it. Collapsing
// BadStatus into a failure here would turn every busy or not-yet-ready answer
// into an indistinguishable failure and throw away the status byte the retry
// logic is built on.
Io classifyRecv(egg::Outcome o, std::size_t got, std::size_t want,
                bool devicePresent);

// The reconnect loop, with the device, the clock and the log passed in.
//
// Returns true as soon as `tryAttach` succeeds, having called `note` with how
// long it took; false once the budget is spent. `tryAttach` is called before
// the first sleep, so a device that is already back costs nothing.
bool reconnectLoop(unsigned budgetMs, unsigned stepMs,
                   const std::function<bool()>& tryAttach,
                   const std::function<void(unsigned)>& sleep,
                   const std::function<void(const std::string&)>& note);

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
