// BootloaderLink.h -- the one seam between the write phase and the world.
//
// Everything the write phase can do to a device goes through this interface,
// which is what makes the adversarial mock (§4.3, "the highest-value artefact
// in the project") possible at all. If the write phase could reach hidapi
// directly, none of its failure handling could be tested without a mouse, and
// the failure handling is the part that matters.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace egg::fw {

// Why not bool. notes/build-design.md §3.4a: three of the vendor's send
// functions return a uint8 that the caller compares against 0x01, and on their
// failure paths that byte does not come from the device -- it comes from the
// request buffer or a literal. So a transport failure and a device status share
// one channel there. Ours does not: a transport failure and a device status are
// different types, and no code path can confuse them.
enum class Io {
    Ok,
    SendFailed,      // the report never went out
    RecvFailed,      // no response, or the read errored
    ShortRead,       // fewer bytes than the report length
    Disconnected,    // the device went away; reconnect() may bring it back
};

class BootloaderLink {
public:
    virtual ~BootloaderLink() = default;

    // Send a feature report. `frame[0]` is the report id and frame.size() is
    // the exact wire length -- macOS does no padding, so both must be right.
    virtual Io send(const std::vector<std::uint8_t>& frame) = 0;

    // Fetch a feature report of exactly `len` bytes, id `reportId`.
    virtual Io recv(std::uint8_t reportId, std::size_t len,
                    std::vector<std::uint8_t>& out) = 0;

    virtual void sleepMs(unsigned ms) = 0;

    // Try to re-establish the connection after a drop. §4.2: after erase the
    // device has no valid application, so a disconnect is something to drive
    // through, not something to return from.
    virtual bool reconnect() = 0;

    // Somewhere for the phase to narrate. It must be a log by construction
    // (§3), because after erase there is no UI worth having.
    virtual void note(const std::string& line) = 0;
};

}  // namespace egg::fw
