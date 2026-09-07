// FirmwareManifest.h -- the set of firmware releases this build knows about.
//
// WHY THIS EXISTS, and why it does not break engineering-rules.md §1.4.
//
// §1.4: "Whichever firmware resource the official updater loads, it must be a
// constant in our code -- never a variable, never selected at runtime, never
// chosen from a list. Other resources in the binary may belong to other
// products."
//
// That was written when there was one updater and one hand-read `push $0x8c`,
// and it is doing real work: updater 1.10 carries SIX FWFILE resources of
// identical length, and five of them would be accepted by the device -- right
// size, valid per-block checksums, and they read back exactly as written. §2
// assumes the device validates nothing, so a wrong-but-well-formed image is a
// brick with nothing downstream to catch it.
//
// Supporting more than one release therefore cannot mean letting a user point
// at a resource. Here is what it means instead, and the argument that §1.4 is
// intact:
//
//   - The table below is SOURCE. Adding a row is a commit, not an input.
//   - A row is selected by the SHA-256 OF THE WHOLE .exe. An updater whose
//     bytes are not in this table cannot be flashed at all -- there is no
//     "unknown file" path, and no way to supply an id.
//   - Once a row is selected, `resourceId` is a compile-time constant reached
//     from that row. It is never read out of the file's resource directory,
//     never the largest resource, never "the one that looks right".
//   - Each row's id was recovered by Tools/pe/resource_id.py from the vendor's
//     OWN FindResourceW call site -- the same derivation that was done by hand
//     for 1.10, now done mechanically and cross-checked against that hand
//     result for all four updaters we hold, including 1.04, which is a second
//     code base with a different call address and a different IAT slot.
//
// So the rule §1.4 protects -- the id is never chosen at run time -- still
// holds byte for byte. What changed is that the table has more than one row.
//
// HOW TO ADD A RELEASE
//   python3 Tools/pe/ingest.py <new-updater.exe> --label 1.11
// prints a row, or refuses and says why. Paste it into FirmwareManifest.cpp,
// set the date, rebuild, run ctest. It will refuse rather than guess if the id
// cannot be recovered, if the code loads an id that is not present, if FWFILE
// is loaded with more than one id, or if the image is not 65 whole blocks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace egg::fw {

struct Release {
    // What a person types: `egg-flash flash --version 1.10 <exe>`. Cosmetic;
    // the hash is what protects the mouse. Deliberately NOT derived from the
    // version resource -- the four updaters report 1.0.4.0, 1.0.6.0, 1.0.7.0
    // and 1.1.0.0 for what Endgame calls 1.04, 1.06, 1.07 and 1.10, and a rule
    // fitting four points is not a rule.
    const char* label;

    // VS_FIXEDFILEINFO's own quadruple, recorded as found. Never parsed into a
    // label, only shown, so a mismatch with `label` is visible to a reader.
    std::uint16_t version[4];

    // THE KEY. SHA-256 of the entire .exe.
    const char* updaterSha256;

    // [D] from the vendor's FindResourceW call site. See the header comment.
    std::uint16_t resourceId;

    std::size_t   imageSize;
    const char*   imageSha256;

    // The value A0 03 declares: the plain 32-bit sum of every image byte.
    // Recorded so the manifest and the runtime computation can disagree
    // loudly rather than the runtime value being taken on trust.
    std::uint32_t wholeImageChecksum;

    // Has THIS release been flashed onto the one mouse and verified? Exactly
    // one row may honestly say true. engineering-rules.md §5: "A different version is a
    // different device until shown otherwise", and this is that distinction
    // made mechanical rather than left to a reader's memory.
    bool provenOnDevice;

    const char* provenance;
};

extern const Release kReleases[];
extern const std::size_t kReleaseCount;

// By label, or nullptr. There is no fuzzy match and no default.
const Release* findRelease(const char* label);

// By the SHA-256 of the .exe's bytes, or nullptr. This is the one that gates a
// flash: an .exe that hashes to nothing in the table has no row, so no
// resource id, so no image.
const Release* releaseForUpdaterSha(const std::string& sha256);

// The release this build was derived against and has driven end to end on the
// device. `egg-flash` defaults to it and requires an explicit acknowledgement
// for any other (see main.cpp), because every [O] in this project came from it.
const Release& primaryRelease();

}  // namespace egg::fw
