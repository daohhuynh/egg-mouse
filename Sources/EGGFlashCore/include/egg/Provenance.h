// Provenance.h -- two small off-wire guards, both decided by the owner 2026-09-06.
//
// Neither puts a byte on the wire. §4.2b: "Off-wire guards -- refusing,
// checking a file, pinning a hash, requiring a token -- cost nothing and are
// always allowed." That is the whole reason these are files and not frames.
//
// 1. BACKUP PROVENANCE. `read-firmware` writes a sidecar next to the image it
//    saved. `restore-firmware` refuses any image without one, or whose bytes
//    have changed since. So the only image that can ever be written back is one
//    THIS TOOL read off THIS device.
//
//    This does not weaken §1.4, which exists so a resource belonging to another
//    product can never be flashed. The vendor-.exe path is untouched: resource
//    type, name and sha256 stay compile-time constants. loadFromBackup never
//    opens a PE at all, and its bytes can only have arrived via A0 07.
//
// 2. ENTRY RECEIPT. §4.2b requires firmware work to enter the bootloader by
//    A1 3A, but `flash` could not tell an A1 3A-entered bootloader from a
//    button-entered one -- PID, bcdDevice and product string are identical.
//    So the rule was not enforced. `enter-bootloader` now leaves a receipt and
//    the fromBoot branch requires it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace egg::fw {

// ---------------------------------------------------------------- provenance

// The sidecar path for an image path: "<image>.origin". One function so the
// two ends cannot disagree about it.
std::string provenancePathFor(const std::string& imagePath);

// Written by read-firmware after saveAndVerify has confirmed the file reads
// back byte-identical. Returns false and fills `error` on any write failure --
// a backup whose provenance could not be recorded is not a restorable backup,
// and read-firmware says so rather than leaving a file that will be refused
// later for reasons the user cannot see.
// `entry` records HOW the bootloader being read was entered -- "a1-3a" when an
// entry receipt was present at the time of the read, "unknown" otherwise. It is
// INFORMATIONAL and nothing gates on it. §4.2b's argument for A1 3A entry is
// about what a read PROVES, and a backup's value is its bytes; refusing to take
// a backup is the one refusal that can leave the user worse off than before.
// Recording it means a restore can say which kind of backup it is holding
// rather than having to assume.
bool writeProvenance(const std::string& imagePath, const std::string& sha256,
                     std::size_t size, std::uint16_t bootloaderPid,
                     std::uint16_t bcdDevice, const std::string& product,
                     const std::string& entry, std::string& error);

struct Provenance {
    bool ok = false;
    std::string reason;        // why not, when !ok
    std::string sha256;
    std::size_t size = 0;
    std::uint16_t bcdDevice = 0;
    std::string product;
    std::string taken;         // ISO-8601, informational only
    std::string entry;         // "a1-3a" or "unknown"; informational only
};

// Reads and parses the sidecar. Does NOT hash the image -- the caller does
// that, because the caller has already read the bytes and hashing twice is how
// a check ends up validating something other than what it loaded.
Provenance readProvenance(const std::string& imagePath);

// ------------------------------------------------------------------- receipt

// Fixed name, in the USER'S HOME DIRECTORY -- beside the settings vault, which
// is where this project already keeps per-user state.
//
// IT WAS THE CURRENT DIRECTORY UNTIL 2026-09-06, and that was wrong twice over.
// A person running `enter-bootloader` in one directory and `flash` in another
// would be refused while the mouse was latched -- bad, but recoverable. Worse,
// and found by audit: a Finder-launched .app inherits a working directory of
// "/", which is not writable. The GUI's own step 0 could therefore never write
// a receipt, and every subsequent flash from the app would refuse. The feature
// would have broken the GUI outright.
//
// Home is right for the same reason the vault lives there: the receipt is about
// this USER and this MOUSE, not about a directory. Still a plain dotfile with a
// name you can grep for and delete -- a guard nobody can find is one they route
// around.
inline constexpr const char* kEntryReceiptName = ".egg-mouse-entry-receipt";

// The absolute path. Every message that mentions the receipt prints THIS, not
// the bare name: a reader who is being refused needs to know which file is
// missing, and a bare dotfile name does not say where to look.
std::string entryReceiptPath();

// Written by enter-bootloader ONLY on EnteredAndConfirmed -- i.e. after A1 3A
// went out AND the mouse came back with the identity we expect.
bool writeEntryReceipt(std::uint16_t bcdDevice, const std::string& product,
                       std::string& error);

struct EntryReceipt {
    bool present = false;
    std::string reason;
    std::uint16_t bcdDevice = 0;
    std::string product;
    std::string sent;          // ISO-8601 of the A1 3A
};

EntryReceipt readEntryReceipt();

// Removed after a completed flash, because a completed flash CLEARS the A1 3A
// latch [O] -- so a receipt outliving the latch it describes is precisely the
// stale state worth avoiding.
void clearEntryReceipt();

// THE DECISION, as a pure function, and in the library rather than the CLI for
// the same reason checkBackupFile is (FlashPlan.h): this file is reachable by
// Tests/mutants.sh and main.cpp is not. The CLI does the printing and nothing
// else -- a gate whose logic lives only in a printf block is a gate no mutant
// can be planted in, and §6.2 says a harness that cannot produce a bad result
// is not evidence.
//
// Consulted ONLY when the mouse is found already in the bootloader. On the
// fromApp path there is nothing to decide: this tool is about to send A1 3A
// itself and enterBootloaderAndConfirm checks the identity that comes back.
struct EntryGate {
    bool allowed = false;
    bool overridden = false;   // allowed only because of the escape hatch
    std::string reason;        // why not, when !allowed
    EntryReceipt receipt;      // what was found, if anything
};

// `liveBcd` / `liveProduct` are the identity of the bootloader ACTUALLY
// present, which the caller has just read off the bus.
//
// BE HONEST ABOUT WHAT THIS CATCHES, because an audit on 2026-09-06 was right
// to call the first version of this comment overstated. Every OP1 8k v2
// bootloader reports the same bcdDevice and the same product string, so this
// comparison CANNOT distinguish one mouse from another and cannot detect a
// receipt carried over from a different device of the same model. What it does
// catch is a receipt that has been hand-edited or copied from something that is
// not this bootloader at all -- and, being off-wire, it costs nothing (\u00a74.2b).
// It is a cheap consistency check, not device binding, and it must not be
// described as the latter anywhere a reader would rely on it.
EntryGate entryGate(bool allowButtonEntry, std::uint16_t liveBcd,
                    const std::string& liveProduct);

// Do two paths name the same file? `./backup.bin` and `backup.bin` do, and
// comparing them as strings says they do not -- which is how restore-firmware's
// "these must be two different files" guard was defeated (audit, 2026-09-06).
//
// In the library rather than the CLI because it is a DECISION: a true answer
// refuses a run. Resolves symlinks, `.` and `..`; a path that cannot be resolved
// (it does not exist yet) compares as itself, so this never invents an equality
// that is not there. Two empty paths are NOT the same file -- an unnamed backup
// is a different refusal, handled before this is reached.
bool sameFile(const std::string& a, const std::string& b);

}  // namespace egg::fw
