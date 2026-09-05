// Transport.h -- the one place bytes reach the device.
//
// Both executables share this. config-protocol.md §1 establishes that the
// framing, the report ids, the lengths, the payload offset and the resp[1]
// status convention are common to the updater and the config tool -- the same
// house code, not a coincidence of conventions. What is NOT shared is the
// command space and the busy convention, so those are parameters.
#pragma once

#include "egg/Protocol.h"
#include "egg/Device.h"
#include "egg/Log.h"

#include <cstdint>
#include <vector>

namespace egg {

// What a device exchange actually produced. Deliberately NOT a bool:
// updater-protocol.md §3.4a records that the vendor's own send wrappers return
// a byte the caller compares against 0x01 which, on their failure paths, does
// not come from the device at all -- it comes from the request buffer or from a
// literal. CLAUDE.md §4.1: "Never let a reported success stand in for verifying
// the data itself."
enum class Outcome {
    Ok,             // the device answered, and resp[1] == kStatusReady
    TransportFail,  // the OS refused the transfer; the device may never have seen it
    BusyTimeout,    // the device kept saying busy until the budget ran out
    BadStatus,      // the device answered with something that is not ready
    ShortRead,      // fewer bytes came back than the report declares
};

const char* describe(Outcome o);

struct Reply {
    Outcome                   outcome{Outcome::TransportFail};
    std::vector<std::uint8_t> buf;
    std::uint8_t              status{0};   // resp[1], whatever it was
    bool ok() const { return outcome == Outcome::Ok; }
};

class Transport {
public:
    Transport(Device& dev, BusyPolicy busy, Log& log)
        : dev_(dev), busy_(busy), log_(log) {}

    // Build a zeroed frame of exactly the right length for this report id, with
    // the id at [0] and the command at [1]. Returns an empty vector for any id
    // that is not 0xA0 or 0xA1 -- there is no third report and no caller may
    // invent one.
    static std::vector<std::uint8_t> frame(std::uint8_t reportId, std::uint8_t command);

    // Send exactly wireLength(buf[0]) bytes. Refuses a buffer whose length does
    // not match what the table says for its report id, because macOS does no
    // padding and a wrong length is a wrong frame, not a tolerated one.
    Outcome send(const std::vector<std::uint8_t>& buf, const char* what);

    // Read a feature report of the given id, polling while the device reports
    // busy under this transport's policy.
    //
    // firstReadDelayMs overrides how long to wait before the FIRST read. The
    // vendor times this per command and we did not (Protocol.h,
    // configDelayMs): A1 13 gets 1100 ms and we were reading at 100. Pass
    // kUsePolicyDelay to keep the policy's own initialSleepMs.
    static constexpr unsigned kUsePolicyDelay = ~0u;
    Reply receive(std::uint8_t reportId, const char* what,
                  unsigned firstReadDelayMs = kUsePolicyDelay);

    // send() then receive(), which is every exchange either tool performs.
    Reply exchange(const std::vector<std::uint8_t>& out,
                   std::uint8_t replyReportId, const char* what,
                   unsigned firstReadDelayMs = kUsePolicyDelay);

private:
    Device&    dev_;
    BusyPolicy busy_;
    Log&       log_;
};

}  // namespace egg
