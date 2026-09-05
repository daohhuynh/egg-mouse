// ConfigRecord.h -- the settings record, its validation, and the field table.
//
// Moved out of Sources/egg-config/main.cpp 2026-09-05, unchanged in behaviour,
// so that it can be tested against a device that misbehaves. §3 lists
// EGGConfigCore as "read-modify-write, diff-verify"; until now that lived in a
// CLI translation unit and nothing but a real mouse could reach it.
#pragma once

#include "egg/Protocol.h"
#include "egg/Log.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace egg::cfg {

// ---------------------------------------------------------------------------
// The four bytes where read-modify-write and vendor-mimicry disagree
// ---------------------------------------------------------------------------
// Protocol.h names kRecordUnknownFirst = 0x01 and kRecordUnknownLast = 0x04:
// the vendor's serializer (cfg107 FUN_004042d0) never writes them, so its
// frames carry 00 00 00 00 there, while the device reports 80 00 00 00.
//
// [O] windows-run, all eleven captures: 73 A0 11 writes, EVERY ONE has 0x00 at
//     record 0x01. Nine large reads: eight report 0x80, and the one that
//     reports 0x00 is 04-buttons, the only section captured without replugging
//     the mouse first -- i.e. the one still holding what the vendor had just
//     written. So the device does not restore 0x80 after a write; it comes
//     back when the record reloads from firmware defaults.
//
// THE TENSION, stated rather than resolved silently. §1.3 says "Never write a
// byte whose meaning is [G]... Do not clean up, zero, or normalise unknown
// bytes", which reads as "preserve". But §1.3 exists to stop us INVENTING
// values, and here 0x00 is the observed vendor constant while 0x80-in-a-write
// is a byte combination no host has ever been seen to send. Under §1.2 --
// only [O] and [D] reach the hardware -- zeroing is the observed option and
// preserving is the novel one.
//
// So this is a policy, not a constant, and both arms are tested. One line
// changes it, and no caller may leave it implicit.
enum class UnknownBytes {
    // Copy record 0x01..0x04 through from what we read. §1.3 read literally.
    Preserve,
    // Zero record 0x01..0x04, reproducing the vendor's frames byte-for-byte.
    MatchVendor,
};

const char* describe(UnknownBytes p);

// THE default, in one place, so that changing it is one line and no caller can
// leave the choice implicit by accident. Every executable and every test reads
// its default from here.
//
// Currently Preserve. Both arms are implemented, both are tested, and
// Tests/test_config_replay.py scores BOTH against the vendor's 33 captured
// writes -- so this constant is a decision, not a dependency.
inline constexpr UnknownBytes kDefaultUnknownBytes = UnknownBytes::Preserve;

// "preserve" / "vendor". Returns false for anything else rather than falling
// back to a default: a mistyped policy silently choosing one of two different
// byte streams is precisely the failure this whole file exists to prevent.
bool parseUnknownBytes(const char* s, UnknownBytes& out);

// ---------------------------------------------------------------------------
// Structural validation -- §4.1 "Validate a read is structurally plausible
// before acting on it. Never let a reported success stand in for verifying the
// data itself."
// ---------------------------------------------------------------------------
// Deliberately weak, and that is the point: it rejects the shapes a broken or
// lying device actually produces (wrong length, wrong report id, a constant
// fill) without pretending to know what a valid settings record looks like.
// A stronger check would encode guesses about record contents, which §1.3
// forbids reaching the hardware.
bool plausible(const std::vector<std::uint8_t>& r, Log& log);

// Same test with nothing to log to, for callers that only want the verdict.
bool plausible(const std::vector<std::uint8_t>& r);

// ---------------------------------------------------------------------------
// Diff
// ---------------------------------------------------------------------------
struct ByteChange {
    std::size_t  recordOffset;
    std::uint8_t before;
    std::uint8_t after;
};

// Every payload byte that differs, as record offsets. Both buffers must be
// full 1041-byte frames; a size mismatch yields an empty result rather than a
// partial one, because a partial diff of a malformed read is a lie.
std::vector<ByteChange> diff(const std::vector<std::uint8_t>& before,
                             const std::vector<std::uint8_t>& after);

// ---------------------------------------------------------------------------
// The settable field table
// ---------------------------------------------------------------------------
// §1.3: "Never write a byte whose meaning is [G]." So a field belongs here only
// when its MEANING is [D] -- derived from the vendor binary and traceable to an
// address. Knowing where a byte lives is not enough; §7.3 maps all 115 record
// bytes and only a handful of them are legal to name.
//
// Adding a row is a protocol claim. Cite it, or leave the field out.
struct Settable {
    const char* name;
    std::size_t recordOffset;             // offset within the settings record
    bool (*encode)(long v, std::uint8_t& out);
    const char* accepts;
    const char* cite;
    // Sub-byte fields. mask/shift are on the RECORD BYTE; the encoder returns
    // the field's value already in field units, and the caller composes
    //     (old & ~mask) | (value << shift)
    // so bits outside the mask are preserved untouched. §1.3 applies inside a
    // byte exactly as it does between bytes: record 0x0b carries two unrelated
    // settings and a high nibble the vendor never writes, and clobbering the
    // neighbours would be as wrong as clobbering a neighbouring byte.
    std::uint8_t mask = 0xFF;
    std::uint8_t shift = 0;
};

// Derived, deliberately NOT settable, and each for a stated reason. Listed so
// that "why can I not set this?" has an answer in the tool rather than only in
// the notes -- §1.2a's rule that absence is a claim applies to our own UI too.
struct Withheld { const char* name; const char* why; };

extern const Settable kSettable[];
extern const std::size_t kSettableCount;
extern const Withheld  kWithheld[];
extern const std::size_t kWithheldCount;

// Look a field up by name, or nullptr.
const Settable* findSettable(const char* name);

// (old & ~mask) | ((value << shift) & mask), and identity for whole-byte
// fields. Split out so the one place that decides which bits move is testable.
std::uint8_t composeByte(std::uint8_t old, const Settable& f, std::uint8_t value);

}  // namespace egg::cfg
