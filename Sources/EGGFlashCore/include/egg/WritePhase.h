// WritePhase.h -- the non-abortable unit.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/Firmware.h"

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
extern const char* const kRecoveryProcedure;

}  // namespace egg::fw
