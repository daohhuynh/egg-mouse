// Device.h -- find the vendor collection, and refuse anything ambiguous.
#pragma once

#include "egg/Protocol.h"
#include "egg/Log.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct hid_device_;

namespace egg {

struct Match {
    std::string   path;
    std::uint16_t productId{};
    std::uint16_t usagePage{};
    std::uint16_t usage{};
    std::uint16_t releaseNumber{};
    std::string   manufacturer;
    std::string   product;
};

// ---------------------------------------------------------------------------
// bcdDevice -> the version Endgame's own updater displays
// ---------------------------------------------------------------------------
// [D] notes/updater-protocol.md, "Firmware version display", from updater 1.10
// FUN_004011f0 instruction by instruction at 0x401245-0x401263: it formats the
// HID VersionNumber with L"%x", parses THAT STRING back with __wtol, truncates
// to 16 bits, and the caller divides by 100.0 for
// L"Mouse firmware current version %.2f".
//
// So it is a BCD round trip, not arithmetic, and getting it wrong is easy and
// consequential: read as a plain integer, 0x0143 divides to 3.23 rather than
// 1.43. Each nibble is one decimal digit.
//
// WHERE WE DEPART FROM THE VENDOR, deliberately. A nibble above 9 formats as a
// letter, and __wtol stops at the first non-digit -- so the vendor would show
// 0x01a0 as 0.01, a number that looks like a version and is not one. We return
// false instead and the caller prints the raw field. Reproducing a misleading
// display is not "mirroring the vendor"; §1.2a's rule that a silent absence is
// a claim applies to a wrong-looking answer even harder than to a missing one.
//
// The updater only ever DISPLAYS this and never gates on it [D]. Neither do we.
bool decodeBcdVersion(std::uint16_t bcdDevice, unsigned& major, unsigned& minor);

// "1.10", or the empty string when the field is not valid BCD.
std::string describeVersion(std::uint16_t bcdDevice);

// Every VID-0x3367 interface hidapi can see, unfiltered. For diagnostics and
// for saying something useful when the four-part match fails.
std::vector<Match> enumerateAll();

// The four-part predicate of config 1.07 FUN_004035f0 / updater 1.10
// FUN_00401000: VID, PID, UsagePage 0xFF01, Usage 0x02 -- all four required.
//
// Returns every interface satisfying it. Callers must treat a size other than 1
// as a failure: zero means not present, and MORE THAN ONE means we cannot tell
// which collection we would be talking to. build-design.md §1 is explicit that
// taking the first match is a coin flip rather than a bug we would notice.
std::vector<Match> findVendorCollection(std::uint16_t productId);

class Device {
public:
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // Opens the single interface matching all four fields, or returns nullopt
    // and explains why on the log. Never opens on an ambiguous match.
    static std::unique_ptr<Device> open(std::uint16_t productId, Log& log);

    hid_device_* raw() const { return dev_; }
    const Match& match() const { return match_; }

private:
    Device(hid_device_* d, Match m) : dev_(d), match_(std::move(m)) {}
    hid_device_* dev_;
    Match        match_;
};

// True once hid_init has run. Idempotent.
bool initHid();

}  // namespace egg
