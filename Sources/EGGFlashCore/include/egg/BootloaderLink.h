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
    //
    // CONTRACT, AND IT IS A TRAP IF YOU MISS IT. On success `out` MUST be
    // exactly `len` bytes. The DEVICE only sends len-1: buf[0] is the report-id
    // slot and it never transmits it, so hid_get_feature_report returns len-1
    // and that is a COMPLETE read, not a short one (wire-observed.md §2.1;
    // observed on the physical mouse 2026-09-05 -- the 1041-byte 0xA0 report
    // read back 1040, and the 64-byte 0xA1 reads back 63, matching both Windows
    // capture files in frames/). An implementation must therefore size `out` to
    // `len`, let the device fill [0..len-2], and leave the final byte zero --
    // which is the vendor's own model, since buf[i] == wire[i].
    //
    // Return ShortRead ONLY below len-1. Treating len-1 as short is the bug
    // this comment exists to prevent, and it is not a harmless one: the
    // read-back check in driveToVerifiedImage sits inside the post-A0 03
    // non-abortable loop, so a recv that reports ShortRead on every good read
    // makes the flasher retry a healthy device forever (§4.2 -- there is no
    // timeout and no cancel, by design).
    //
    // Exactly this defect shipped in the CONFIG transport and passed the whole
    // suite, because the mock returned what the wrong check expected. There is
    // no device-backed BootloaderLink yet; when one is written, this is the
    // first thing to get right. See engineering-rules.md decision #1: a mock built from
    // our own understanding cannot catch an error in that understanding.
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
