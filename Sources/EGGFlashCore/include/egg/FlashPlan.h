// FlashPlan.h -- everything that happens AROUND the write phase: reading the
// device's current image off before touching it, and binding an approval to the
// exact bytes it approves.
//
// Deliberately not in WritePhase.cpp. CLAUDE.md §4.3: "Keep the write phase
// small and in one file. ~100 lines in one place can be audited by eye." This
// is the code that runs BEFORE the point of no return, where abort is always
// correct (§4.2), so it must not share a file with the code that may never
// return.
#pragma once

#include "egg/BootloaderLink.h"
#include "egg/FlashCommands.h"
#include "egg/Firmware.h"

#include <cstdint>
#include <string>
#include <vector>

namespace egg::fw {

// ---------------------------------------------------------------------------
// Read the device's application region back, block by block, with A0 07.
//
// CLAUDE.md §4.2, added 2026-09-05: "Never erase without a saved copy of what
// is being erased. No A0 03 until the current application region has been read
// back block by block, written to disk, and the file re-read and checked."
//
// This is what makes a failed flash recoverable rather than permanent, and it
// is why it is worth doing even when the image about to be written is believed
// good. It is also §4.4 stage 3 -- it validates the block arithmetic of §3.8
// against the device with ZERO writes.
//
// EVERY FAILURE HERE ABORTS. Nothing has been erased, so §4.2's first regime
// applies without qualification: a partial read is not a backup, and pretending
// otherwise would hand the write phase a false guarantee.
// ---------------------------------------------------------------------------
struct ReadBack {
    bool ok = false;
    // kBlockCount * kBlockSize bytes, blocks in device order kBlockFirst..kBlockLast.
    std::vector<std::uint8_t> image;
    std::size_t blocksRead = 0;
    // resp[1] of the first block that failed, when one did. Recorded because
    // "the bootloader refuses A0 07 outside a flash session" and "the link
    // broke" are different findings and only the status byte separates them.
    int lastStatus = -1;
    std::string error;
};

// Attempts up to kReadBlockTries per block before giving up on the whole read.
inline constexpr unsigned kReadBlockTries = 5;

ReadBack readApplicationRegion(BootloaderLink& link);

// ---------------------------------------------------------------------------
// Is a file on disk a usable backup of the application region?
//
// The other half of the same §4.2 rule, and the half that decides whether an
// erase is allowed to happen. the owner's call, 2026-09-05: `flash` no longer TAKES
// the backup -- doing so put 65 A0 07 frames in front of A0 03, which the
// vendor never sends, inserting our own rule into the one sequence there is a
// capture of. `read-firmware` takes it in a separate run and `flash` checks it.
//
// The trade is only sound if the check is real, which is why the DECISION lives
// here rather than in the CLI: this file is mutation-tested (Tests/mutants.sh)
// and main.cpp is not. The CLI does the printing and nothing else.
//
// What it rejects, and why each shape is one somebody actually produces:
//   - a path that is empty or unopenable       -- no backup was taken
//   - anything but exactly kBlockCount*kBlockSize bytes, INCLUDING one byte
//     too long                                 -- a truncated or wrong file
//   - a file that is all one repeated byte     -- an A0 07 loop that failed and
//                                                 got written out anyway
// It deliberately does NOT try to judge whether the contents are "real"
// firmware. Nothing here can: the device serves whatever is resident, and a
// backup of a half-flashed device is still the best undo available. §1.2a --
// this check sees length and uniformity, and nothing else.
// ---------------------------------------------------------------------------
struct BackupCheck {
    bool ok = false;
    std::string reason;    // why not, when !ok. Empty when ok.
    std::string sha256;    // of the bytes read, when ok.
    std::size_t size = 0;  // bytes actually read, whether ok or not.
};

BackupCheck checkBackupFile(const std::string& path);

// ---------------------------------------------------------------------------
// The frames OUR flasher sends, in order, on the happy path.
//
// It BEGINS with A1 3A, as the vendor's does. §4.2b, revised 2026-09-05: a
// flash takes the vendor's entry because their proven sequence begins with it
// and nothing has ever flashed a button-entered bootloader. It still differs
// from `egg-flash stream` -- which emits the vendor's sequence verbatim for the
// golden diff -- by omitting the trailing A1 13 factory reset.
//
// Retries are not represented. The plan is the happy path, which is what makes
// it deterministic and therefore usable as an approval token.
std::vector<Frame> plannedFrames(const Image& img);

// ---------------------------------------------------------------------------
// CLAUDE.md §4.2c: "an approval must be bound to the exact bytes it approves."
//
// SHA-256 over the concatenation of plannedFrames(), truncated to something a
// person will actually retype. If the image changes, or the block range
// changes, or a command is added, the token changes and a previously-issued
// approval stops working.
//
// Truncation is safe here because this is not a security boundary. There is no
// adversary choosing images; the failure being designed out is a HUMAN one --
// typing --yes on autopilot, or approving a dry run and then flashing something
// else. Eight hex characters is far past the point where that happens by
// accident, and short enough that nobody copy-pastes it without looking.
inline constexpr std::size_t kConfirmTokenChars = 8;

std::string confirmToken(const Image& img);

// The same computation over an explicit frame list.
//
// EXPOSED BECAUSE OF A MUTATION-TESTING HOLE, 2026-09-05. The first test of the
// token asserted that plannedFrames() carries the image payloads -- true, and
// irrelevant: a mutant that made confirmToken() hash only the FIRST frame
// SURVIVED, because nothing tested confirmToken itself. The property that
// matters ("a different plan yields a different token") cannot be tested
// through the Image overload at all, since Image only constructs from a vendor
// .exe whose SHA-256 matches the pinned constant -- there is exactly one Image
// this program can build, so there is no second one to compare against.
//
// So the seam is the fix, not a cleverer assertion. §6.2: when the harness
// cannot produce a bad result, change the code until it can.
std::string confirmTokenForFrames(const std::vector<Frame>& frames);

}  // namespace egg::fw
