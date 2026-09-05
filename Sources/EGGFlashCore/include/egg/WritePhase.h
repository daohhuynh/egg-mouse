// WritePhase.h -- the non-abortable unit.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/Firmware.h"

namespace egg::fw {

struct Progress {
    std::size_t blocksWritten = 0;
    std::size_t blocksVerified = 0;
    std::size_t rewrites = 0;         // blocks that needed a repair pass
    std::size_t reconnects = 0;
    std::size_t sendFailures = 0;
    bool imageVerified = false;       // whole-image checksum matched
    bool completeAcked = false;       // A1 09 acknowledged
};

// Everything before the point of no return. Any single failure aborts and
// NOTHING has changed on the device (§4.2). Returns false with `error` set.
bool preflight(BootloaderLink& link, const Image& img, std::string& error);

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
