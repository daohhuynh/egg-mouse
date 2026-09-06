// BootloaderEntry.h -- CLAUDE.md §4.4 stage 2: enter the bootloader, confirm
// what we landed on, and send it NOTHING.
//
// This is deliberately not part of the write phase and shares no code with it.
// Stage 2's whole point is that it is the mode switch and the re-enumeration in
// isolation: one 64-byte report to the APPLICATION device, then pure
// observation. No BootloaderLink is constructed, so there is no object here
// capable of writing to a bootloader even by accident.
//
// THE DECISION IS THE RE-ENUMERATION, NOT A STATUS BYTE. The vendor's own entry
// path (updater 1.10 0x004037e1) tests only that a read happened; it never
// looks at resp[1], and then it polls for PID 0x1977. We do the same, for the
// same reason: the device answering says it received something, while the PID
// changing says it acted. resp[1] is recorded and logged, never gated on.
#pragma once

#include "egg/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace egg::fw {

// One enumerated interface, reduced to the three fields §4.2 requires us to
// check. Mirrors egg::Match so the CLI can fill it from real enumeration and a
// test can fill it from a table.
struct SeenDevice {
    std::uint16_t productId{};
    std::uint16_t releaseNumber{};
    // The device presents SEVEN interfaces on one PID (0x0001/0x06, 0xff01/0x02,
    // 0x000c/0x01, 0xff02/0x01, 0xff02/0x02, 0x0001/0x02, 0x0001/0x01 -- read off
    // the real mouse 2026-09-05). Counting interfaces where you mean devices is
    // therefore off by six, and that is not hypothetical: the first run of
    // stage 2 against hardware refused with "no application device" while
    // staring at a successfully opened one.
    std::uint16_t usagePage{};
    std::uint16_t usage{};
    std::string   product;
    // [O] "EGG" on the bootloader, "Endgame Gear" on the application
    // (notes/bootloader-observed.md §2). REPORTED, never gated on: a fourth
    // gate can only turn a successful entry into a printed failure, and this
    // string has been read on macOS but never on the wire, so a mismatch here
    // would more likely be a platform difference than a wrong device.
    std::string   manufacturer;
};

enum class EntryResult {
    EnteredAndConfirmed,   // 0x1977 + bcdDevice 0x0006 + "Bootloader"
    NoApplicationDevice,   // nothing to send to; nothing was sent
    SendFailed,            // the report did not go out; nothing changed
    NoReenumeration,       // acknowledged, but 0x1977 never appeared
    UnrecognisedIdentity,  // 0x1977 appeared and something did not match
};

const char* describe(EntryResult r);

struct EntryOutcome {
    EntryResult result{EntryResult::NoApplicationDevice};

    // Exactly the bytes that went out, so a test and a log can both check them
    // against the capture rather than against our intent.
    std::vector<std::uint8_t> sent;

    bool         replyRead{false};
    std::uint8_t status{0};          // resp[1]. Recorded, never gated on.
    unsigned     ackMs{0};           // send -> reply
    unsigned     reenumerateMs{0};   // reply -> 0x1977 present

    // What we actually found, whether or not we accepted it. On
    // UnrecognisedIdentity this is the thing that was refused, and printing it
    // is the entire value of refusing rather than proceeding.
    bool       sawBootloaderPid{false};
    SeenDevice seen{};
};

// The seams. Both are injected so the whole path runs with no hardware: §4.3
// requires the failure handling to be testable, and a timeout that only fires
// against a real mouse is a timeout nobody has tested.
struct EntryEnv {
    // Sends the frame and fetches a 64-byte 0xA1 reply. Returns false if the
    // report never went out. `reply` is filled only when a read succeeded;
    // `readOk` distinguishes "sent but no answer" from "sent and answered",
    // because the vendor treats those differently and so must we.
    std::function<bool(const std::vector<std::uint8_t>& frame,
                       std::vector<std::uint8_t>& reply,
                       bool& readOk)> exchange;

    // Every VID-0x3367 interface visible right now.
    std::function<std::vector<SeenDevice>()> enumerate;

    // Injected so tests do not actually sleep.
    std::function<void(unsigned ms)> sleepMs;

    // Monotonic milliseconds, for the two measurements this rung exists to make.
    std::function<unsigned()> nowMs;
};

// Poll cadence while waiting for the bootloader to appear.
//
// [O] the device re-enumerated 448-489 ms after the ack in the two captures
// (flash-wire-observed.md §2.1), so 25 ms steps put ~18 samples inside the
// observed window and make the measurement worth reporting.
//
// The ceiling is OURS, not the vendor's. Endgame polls for 22.5 s
// (updater-protocol.md §5.3a); we stop at 10 s because stage 2 has nothing to
// lose by giving up -- nothing has been erased and the device is either in the
// bootloader, where a replug fixes it, or still in the application, where
// nothing happened at all. §4.2: before erase, abort is always correct.
inline constexpr unsigned kEntryPollStepMs = 25;
inline constexpr unsigned kEntryPollCeilMs = 10000;

// Sends A1 3A once and watches. Does NOT retry the send: the vendor's
// nine-attempt loop (updater-protocol.md §5.3a) has never been exercised on the
// wire -- both captures succeeded first try -- so retrying here would be
// running untested code against the one mouse for no gain. If the first attempt
// fails, stage 2 has failed safely and can simply be run again.
EntryOutcome enterBootloaderAndConfirm(EntryEnv& env);

// How many openable vendor collections (UsagePage 0xFF01, Usage 0x02) this PID
// publishes. Exposed so the CONFIRMATION PROMPT can show what it is about to
// act on, using the same predicate the send path uses rather than a second
// copy of it that can drift. The first hardware run of stage 2 aborted on this
// count being wrong and the prompt could not have shown it.
std::size_t countVendorCollections(const std::vector<SeenDevice>& seen,
                                   std::uint16_t productId);

}  // namespace egg::fw
