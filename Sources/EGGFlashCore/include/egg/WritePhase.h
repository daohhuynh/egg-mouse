// WritePhase.h -- the non-abortable unit.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/Firmware.h"

#include <string>

namespace egg::fw {

struct Progress {
    // Writes the device ACKNOWLEDGED. Exceeds blockCount when blocks are
    // rewritten, and that gap is the useful part: it used to be incremented
    // beside blocksVerified, so the two were equal by construction and the
    // summary printed one number twice while looking like a cross-check.
    std::size_t blocksWritten = 0;
    std::size_t blocksVerified = 0;   // blocks that READ BACK correct
    std::size_t rewrites = 0;         // blocks that needed a repair pass
    std::size_t reconnects = 0;
    std::size_t sendFailures = 0;
    bool imageVerified = false;       // whole-image checksum matched
    bool completeAcked = false;       // A1 09 acknowledged
};

// Everything before the point of no return. Any single failure aborts and
// NOTHING has changed on the device (§4.2). Returns false with `error` set.
//
// IT TAKES NO LINK, AND THAT IS THE POINT. Until 2026-09-05 it took a
// BootloaderLink and sent one "benign" A1 08 round trip to prove the link
// worked. That put `a1 08 34 74` on the wire immediately BEFORE `a0 03` -- a
// frame the vendor never sends, at the moment of maximum cost, and one that
// `plannedFrames()` did not contain, so neither the approval token nor
// Tests/test_golden_vendor.py could see it. Found by driving the real code
// path against the mock and counting: 134 frames sent, 133 planned.
//
// It was the same defect the owner had caught in the A0 07 backup read an hour
// earlier, still present one function away. The fix is structural rather than
// a policy flag: with no link in scope, preflight CANNOT send, and no future
// edit can reintroduce this without changing the signature.
//
// Nothing is lost. `A0 03`'s own retry-and-reconnect loop is the round trip,
// and it is one the vendor actually performs; a link that cannot carry a frame
// fails there, before any erase, because a frame that never went out cannot
// have erased anything.
bool preflight(const Image& img, std::string& error);

// Everything after it. This function returns ONLY when the image is verified
// resident on the device. It has no failure return, no timeout that gives up
// and no cancel path -- see the comment at the top of WritePhase.cpp for why
// that is the safe shape rather than the reckless one.
Progress driveToVerifiedImage(BootloaderLink& link, const Image& img);

// The recovery procedure, [O], notes/bootloader-observed.md §1. Printed
// whenever the phase is struggling, because a recovery procedure nobody can
// find is not a recovery procedure (LIST 4 item 24).
//
// A std::string, not a string literal, because it is COMPOSED from
// egg::kButtonEntrySteps and egg::kButtonEntryReflashNote (Protocol.h) rather
// than restating them. There used to be a second constant with this name that
// said the same things in its own words, and a guard made both wrong at once.
extern const std::string kRecoveryProcedure;

// How many times A1 09 is sent before returning without an ack. The vendor uses
// 10 (updater-protocol.md §5.4 step 5); we allow more because we have no reason
// to be less patient.
//
// It MUST be bounded, but NOT for the reason first written here. I justified it
// as a timing race and then measured the captures, which refuted that: the
// device acks A1 09 in 0.19-0.32 ms and only leaves the bus 857-896 ms later,
// so a read at 50 ms has ~800 ms of margin and the ack should essentially always
// arrive (flash-wire-observed.md §2.1a). The real reason is narrower and still
// sufficient -- A1 09 is the command that makes the device GO AWAY, so "no ack"
// can mean the link is gone rather than that anything failed, and the evidence
// it worked is the device coming back, which only the caller can see. See the
// comment at the loop for why bounding this does not violate §4.2.
inline constexpr unsigned kCompleteAttempts = 30;

}  // namespace egg::fw
