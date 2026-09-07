// Firmware.h -- the image, its blocks, and its two checksums.
//
// CLAUDE.md §1.4: "Whichever firmware resource the official updater loads, it
// must be a constant in our code -- never a variable, never selected at
// runtime, never chosen from a list. Other resources in the binary may belong
// to other products."
//
// That rule is honoured literally here. The resource TYPE, the resource NAME
// and the expected SHA-256 of the bytes are all compile-time constants. The
// caller supplies a path to the vendor .exe and nothing else; there is no API
// that takes a resource id, and no way to ask for a different one.
//
// Why extract from the .exe at all rather than ship the image: the bytes are
// Endgame's, so they are not in this repo (.gitignore excludes *.bin). Reading
// them out of the user's own copy of the updater keeps it that way, and it
// makes the identity check and the flash happen in the same program -- §4.2
// wants preflight and write to share one notion of which image this is.
#pragma once

#include "egg/FirmwareManifest.h"
#include "egg/Protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace egg::fw {

// [D] updater-protocol.md §4. FindResourceW(NULL, (LPCWSTR)0x8c, L"FWFILE") at
// 0x00403200 in updater 1.10, and the identical call at 0x00404330 in updater
// 1.04 -- a second, independently compiled code base agreeing on both operands.
inline constexpr const char* kResourceType = "FWFILE";
inline constexpr std::uint16_t kResourceName = 140;      // 0x8c

// [D] the resource is 66560 bytes, so block_count = 66560 >> 10 = 65 exactly
// and the remainder is 0. The vendor's partial-block path is never taken with
// the image it ships, so we refuse rather than emulate it (§10.3 invariant 2).
inline constexpr std::size_t kBlockSize = 1024;          // [D] 0x400 throughout
inline constexpr std::size_t kExpectedImageSize = 66560;
inline constexpr std::size_t kExpectedBlockCount = kBlockCount;  // 65

// kBlockFirst / kBlockLast / kBlockCount live in EGGCore's Protocol.h and are
// NOT redeclared here. CLAUDE.md §3 keeps protocol constants in one place
// precisely so a second copy cannot drift; the first build of this header did
// redeclare them, with a different width, and the compiler caught it.

// SHA-256 of FWFILE/140 as extracted from
// "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"
// (the .exe itself: c8f9... see notes/binaries.md). Pinned 2026-09-05.
//
// §2: "Assume the device does not validate the image it is given ... nothing
// downstream of us catches a wrong-but-well-formed image." This constant is
// that guard, and it runs before the first byte goes out.
inline constexpr const char* kExpectedSha256 =
    "8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0";

// The whole-image checksum the A0 03 start command must declare.
//
// [D] FUN_00403580 @ 0x00403580, read from raw disassembly 2026-09-05. The
// notes named this function but never gave its algorithm, and its result is a
// value we WRITE -- so it could not stay unstated (§1.3).
//
// The loop at 0x4035b0..0x4035d6 reads four consecutive bytes per iteration
// into four accumulators, 0x400 bytes per block; 0x4035d8..0x4035df folds three
// of them into the fourth. esi, edi and -0x8(%ebp) are re-zeroed at the top of
// every block (0x4035a0..0x4035a7) but the running total at -0x4(%ebp) is not.
// So the four accumulators are a 4-way unroll of one sum, and the result is the
// plain 32-bit sum of every byte of the image.
//
// For FWFILE/140 of updater 1.10 that is 0x0081d57d.
std::uint32_t wholeImageChecksum(const std::vector<std::uint8_t>& image);

// [D] §3.3: a 16-bit sum of the 1024 payload bytes at [0x10..0x40F] of the
// write frame, accumulated at 0x401a00..0x401a34, emitted low byte at [4] and
// high byte at [5].
std::uint16_t blockChecksum(const std::uint8_t* block, std::size_t n);

// What a validated image looks like. There is no constructor that takes bytes
// without checking them -- an Image only exists if it passed.
class Image {
public:
    // Extracts FWFILE/140 from the PE at exePath and validates it against the
    // pinned constants above. On any failure returns false and fills `error`;
    // `*this` is left empty.
    //
    // Validation is all-or-nothing and happens here rather than at the call
    // site, because §4.2 says "any single preflight failure aborts. No path
    // proceeds on a partial pass."
    bool loadFromExecutable(const std::string& exePath, std::string& error);

    // The same, for a release from the manifest (FirmwareManifest.h). This is
    // the multi-version path, and its FIRST act is to hash the whole .exe and
    // require it to equal `rel.updaterSha256`. Only then is `rel.resourceId`
    // used -- so the id still comes from a compile-time table row that an exact
    // file hash selected, never from anything in the file itself (§1.4).
    //
    // The single-argument overload above stays, unchanged, as the path for the
    // one release this build was derived against. Two entry points rather than
    // one generalised one, for the same reason ConfigSession keeps `set` and
    // `setRun` apart: the narrow one is what every existing test drives, and
    // widening it would weaken the checks that catch our own bugs.
    bool loadFromRelease(const std::string& exePath, const Release& rel,
                         std::string& error);

    // The THIRD and last loader: an image this tool read off this device with
    // A0 07 and saved with `read-firmware`. the owner's call, 2026-09-06 -- restoring
    // a backup is allowed, but ONLY a backup, and only one whose bytes have not
    // changed since it was taken.
    //
    // It refuses unless a sidecar written by `read-firmware` sits beside the
    // file AND records the sha256 the file hashes to right now. See
    // Provenance.h. The size and block-count checks are the same as the other
    // two loaders'; what it deliberately does NOT check is kExpectedSha256,
    // because the whole point is to write back something OTHER than the image
    // this build was derived against.
    //
    // WHY THIS DOES NOT WEAKEN §1.4. That rule exists so a resource belonging
    // to another product can never be flashed, and it is about resource
    // SELECTION: "never a variable, never selected at runtime, never chosen
    // from a list." Nothing here opens a PE, reads a resource directory or
    // consults an id -- there is no selection to influence. The bytes can only
    // have arrived from this device's own flash, and the sidecar is what makes
    // that checkable rather than assumed.
    //
    // What it is NOT: a defence against a forged sidecar. The threat model (§2)
    // is bugs in our own code and mistakes at the keyboard -- naming the wrong
    // file -- not an adversary with write access to the backup directory.
    bool loadFromBackup(const std::string& imagePath, std::string& error);

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::size_t blockCount() const { return bytes_.size() / kBlockSize; }
    std::uint32_t checksum() const { return checksum_; }
    const std::string& sha256() const { return sha_; }
    bool valid() const { return !bytes_.empty(); }

    // Block i of the image, 0-based. The DEVICE index for it is i + kBlockFirst
    // and that addition happens in exactly one place (§10.3 invariant 4).
    const std::uint8_t* block(std::size_t i) const {
        return bytes_.data() + i * kBlockSize;
    }
    std::uint8_t deviceIndex(std::size_t i) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t checksum_ = 0;
    std::string sha_;
};

// Exposed for testing: pull one resource out of a PE32 file. Deliberately not
// general -- it takes the type and name it is given and does no searching.
bool extractResource(const std::string& exePath, const char* type,
                     std::uint16_t name, std::vector<std::uint8_t>& out,
                     std::string& error);

std::string sha256Hex(const std::uint8_t* data, std::size_t n);

}  // namespace egg::fw
