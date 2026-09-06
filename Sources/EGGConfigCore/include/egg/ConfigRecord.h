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
//     record 0x01. Nine large reads: eight report 0x80, one reports 0x00.
//
//     AN EARLIER VERSION OF THIS COMMENT EXPLAINED THAT ONE READ BY SAYING
//     04-buttons was "the only section captured without replugging the mouse
//     first". That was wrong, and machine-checking it is what found the real
//     pattern. The device's USB address is 4 in every capture from 00 through
//     07 and only changes at the flash captures (5, 6, 7, 8) -- so NO section
//     in that range was preceded by a replug, and replugging cannot be what
//     distinguishes 04-buttons from the rest.
//
//     What actually distinguishes it, over all nine reads, with no exception:
//
//       record 0x01 == 0x80  <=>  the record equals firmware defaults
//       record 0x01 == 0x00  <=>  the record holds what a host wrote
//
//     04-buttons is the ONLY read whose record still matched the previous
//     session's last write (0 of 115 bytes differ), and it is also the shortest
//     gap between sessions (124 s; the others are 231-5501 s). Every other read
//     had reverted to 01-baseline exactly. 10-postflash reads 0x80 and differs
//     from 01-baseline at record 0x71 alone, which is the byte whose DEFAULT
//     appears to differ between firmware 1.07 and 1.10 -- so it is at defaults
//     too, for its own firmware.
//
//     [G] why the settings reverted. Idle reload from non-volatile storage
//     fits; so does something in the gaps, which are outside every capture. The
//     correlation is n=9 with no counter-example and it is still a correlation.
//     It is NOT a basis for a write (§1.3) and nothing here depends on it.
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
// DECIDED 2026-09-05: MatchVendor. the owner's own reading ("shouldn't we do what
// they do? what if that's how the mouse recognizes the right thing"), and an
// independent adversarial review reached the same answer from the evidence:
//
//   [D] NO vendor config tool can write record 0x01-0x04, in any version. The
//       serializer was re-decoded in all four: cfg107 0x4042d0-0x4045dc,
//       cfg104 and cfg101 0x4042c0-0x4045cc, cfg100 0x4057b0-0x405abd (a
//       different code base, dest register %edx). 111 stores each, all
//       distinct, max record 0x72, holes {0x01,0x02,0x03,0x04} in every one.
//       The zeros are residue of the memset at 0x4041e7.
//   [D] Nothing in cfg107 ever READS the byte either -- a whole-file 32-bit
//       literal scan for the record base finds 2 hits for base+0 and ZERO for
//       base+1..base+4. So no GUI control can show or set it, which closes
//       §1.2a's "check the user-visible surface" test.
//   [O] 73 of 73 host writes carry 0x00. Zero host frames, from any tool, any
//       version, have ever carried 0x80.
//
// So both options write a byte whose meaning is [G] -- the frame is 1041 bytes
// and this byte is inside it. §1.3 assumes echoing a read back is the null
// action, and here that assumption is false. Given the choice between the value
// every observed host sends and a value no host has ever sent, on a device with
// no spare and an undo we have not confirmed works, we send what is observed.
//
// WHAT WE GIVE UP, said plainly: if record 0x01 is a persisted device-side bit,
// we clear it on every write. The vendor does too, on every APPLY, and has
// through a firmware flash and a factory reset -- but "the vendor does it" is
// not a proof that it is harmless, and if the vendor's zeroing is WHY its
// writes do not persist, we have copied that too.
//
// TO REVERSE IT: change this one line to UnknownBytes::Preserve. Both arms are
// implemented and both are scored against the vendor's 33 captured writes by
// Tests/test_config_replay.py, so nothing else needs to move. Under MatchVendor
// we reproduce all 33 byte-for-byte across all 1024 payload bytes; under
// Preserve we differ at record 0x01 alone.
//
// WHAT WOULD UPGRADE THIS FROM A DEFAULT TO A DERIVATION: FWFILE id 140 in
// updater 1.10 is the firmware that is actually on the mouse, and it contains
// the A0 11 handler. If that handler can be shown to load offset 0x01 of the
// received record, this needs real analysis rather than a default; if it
// provably never reads it, Preserve becomes free and §1.3 wins outright. That
// work is static and available now -- see working-memory.md.
inline constexpr UnknownBytes kDefaultUnknownBytes = UnknownBytes::MatchVendor;

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

// ---------------------------------------------------------------------------
// Capability gates -- a field whose MEANING depends on another record byte
// ---------------------------------------------------------------------------
// config-protocol.md §7.25. Record 0x6f decides what record 0x09 (`lod`) means.
// cfg107 exposes no CONTROL for it -- cfg100/101/104 do -- but it does write the
// byte: the compiled-in default writer zeroes object 0x2b at 0x413e3e, and the
// serializer copies it to record 0x6f at 0x4045c1. (This comment used to say
// "cfg107 never writes 0x6f", which is false in both halves.) It compares it to
// 1 at three sites and, when it is
// 1, the Lift-off Distance combo holds two entries instead of eleven, on a
// DIFFERENT scale -- `1` is 1.0mm there and 0.8mm otherwise -- and is greyed
// out (cfg107 0x40ec42, 0x40eeb6, 0x40f1e5).
//
// All 82 captured records report 0x6f == 0, so the eleven-step scale is the one
// this mouse uses and `set lod 0..10` is correct for it. But a value written
// under the other reading would be well formed, would verify against read-back,
// and would mean a different physical distance -- CLAUDE.md §2's exact failure
// shape: "nothing downstream of us catches a wrong-but-well-formed image".
//
// The guard is OFF-WIRE. Read-modify-write already has the byte in hand, so
// refusing costs no extra frame -- §4.2's rule that a safety measure changing
// the byte stream is not free, and that one which only refuses is always
// allowed.
//
// Returns nullptr when the field may be written, or the reason it may not.
const char* capabilityRefusal(const Settable& f, const std::uint8_t* record);

// Which record offsets currently GOVERN a gate -- not the offsets being
// written, the ones being consulted. Exposed for one reason: `setRun` cannot
// apply the gate table at all (it has an offset, not a field name), so the
// only thing keeping a block write from silently stepping over a governing
// byte is that no block contains one. That is a fact about today's table, and
// a fact is something a test can hold. See ConfigSession.cpp above setRun.
const std::size_t* gatedRecordOffsets(std::size_t& count);

extern const Settable kSettable[];
extern const std::size_t kSettableCount;
extern const Withheld  kWithheld[];
extern const std::size_t kWithheldCount;

// ---------------------------------------------------------------------------
// Button mapping  (config-protocol.md §7.14-§7.17)
// ---------------------------------------------------------------------------
// Eight seven-byte entries from record 0x37. Entry k is
//     +0 action type, +1 discriminated on +0, +2..+5 payload,
//     +6 the multiclick filter, which belongs to the Buttons page and MUST be
//        preserved -- the vendor's own handlers never touch it.
constexpr std::size_t kButtonBlockFirst = 0x37;
constexpr std::size_t kButtonEntryLen   = 7;
constexpr std::size_t kButtonEntryCount = 8;

enum class ButtonPayload : std::uint8_t {
    None,       // +2..+5 are zero
    FixedCpi,   // +2..+3 = X, +4..+5 = Y, u16 LE
    Key,        // +1 = HID modifier bitfield, +2 = HID keycode
};

// One item from the vendor's Button Mapping menu. b1 is ignored for Key, where
// the modifier is supplied at apply time.
struct ButtonAction {
    const char*   name;
    const char*   group;
    std::uint8_t  b0;
    std::uint8_t  b1;
    ButtonPayload payload;
    const char*   cite;
};

// One of the eight entries. `vendorExposes` records whether the vendor's own
// Button Mapping page offers a row for it -- it shows SIX (§7.15). We do not
// offer the other two either: LEFT because losing left-click is unrecoverable
// from software, and entry 5 because it is a control no capture ever moved.
struct ButtonSlot {
    const char*  name;
    std::uint8_t index;
    bool         vendorExposes;
    const char*  note;
};

// ---------------------------------------------------------------------------
// CPI stages  (config-protocol.md §7.19)
// ---------------------------------------------------------------------------
// Four 5-byte records from record 0x23: `flag, X lo, X hi, Y lo, Y hi`, X and Y
// u16 little-endian, flag = (X != Y). §7.8 established the layout and the phase
// (flag FIRST); §7.19 adds the part that was missing and that §1.3 requires
// before a byte may be written -- WHAT VALUES ARE LEGAL.
//
// The vendor normalises every typed CPI through one function, cfg107
// `0x0040d880`-`0x0040d93a`, and the trackbar handler reaches the same set by a
// different route (`0x0040c2f0`-`0x0040c312`, and again for Y at
// `0x0040c354`-`0x0040c395`). Two independent derivations, one answer:
//
//     clamp to [10, 30000]
//     <= 10000 : round to the nearest multiple of 10   (ties up, rem >= 5)
//     >  10000 : round to the nearest multiple of 50   (ties up, rem >= 25)
//
// So the legal set is exactly {10,20,...,10000} u {10050,10100,...,30000}.
constexpr std::size_t kCpiBlockFirst = 0x23;
constexpr std::size_t kCpiEntryLen   = 5;
constexpr std::size_t kCpiStageCount = 4;
constexpr long kCpiMin  = 10;
constexpr long kCpiMax  = 30000;
constexpr long kCpiFineLimit = 10000;   // <= this rounds by 10, above by 50

// The vendor's normaliser, transcribed. Returns the value cfg107 would store
// for `v`, including its clamps and its round-half-up.
long normaliseCpi(long v);

// Compose one stage's five bytes. Refuses a value that is not already
// normalised rather than silently rounding it: a CPI the user did not ask for
// is exactly the kind of quiet substitution §4.1's diff-verify exists to catch,
// and the caller can print what normaliseCpi() would have done instead.
bool encodeCpiStageEntry(long x, long y, std::uint8_t out[kCpiEntryLen],
                         const char** err);

// Decode one stage out of a record payload.
struct CpiStage { long x; long y; bool flag; };
CpiStage decodeCpiStageEntry(const std::uint8_t* entry);

// ---------------------------------------------------------------------------
// Left-handed mode (config-protocol.md §7.20)
// ---------------------------------------------------------------------------
// NOT a flag byte. cfg107's checkbox handler `0x408b00` MOVES the user's
// assignment between button entries 0 and 1 and hard-sets whichever one is now
// the primary click to left-click, `00 01 00 00 00 00`.
//
// The vendor's transform is not idempotent -- it runs on a checkbox TRANSITION,
// so applying it twice would overwrite the user's assignment with the reset
// value. A CLI has no transition, only a requested end state, so it has to read
// the current state out of the record first. A record in neither state is one
// the vendor's UI cannot produce, and is refused rather than guessed at.
enum class Handedness { Right, Left, Unknown };

Handedness readHandedness(const std::uint8_t* record);

// Writes the 14 bytes of entries 0 and 1 as they should be after the change.
// `changed` comes back false when the record is already in the requested state,
// in which case nothing needs to be written at all.
bool applyHandedness(const std::uint8_t* record, Handedness want,
                     std::uint8_t out[2 * kButtonEntryLen], bool& changed,
                     const char** err);

// ---------------------------------------------------------------------------
// Multiclick filter and SPDT mode (config-protocol.md §7.22)
// ---------------------------------------------------------------------------
// One byte, two meanings, and NOT the same domain for every button. All five of
// left/right/middle/forward/back take a number 0..25 (CSliderCtrl::SetRange(0,
// 0x19) at cfg107 0x405f84, default 8 by TBM_SETPOS at 0x405f9f). Only left and
// right also take GX Speed (0xf1) or GX Safe (0xf0) -- the other three have no
// SPDT combo on the vendor's page, so those bytes there would be [G] (§1.3).
constexpr std::size_t  kMulticlickFirst  = 0x3d;   // = kButtonBlockFirst + 6
constexpr std::size_t  kMulticlickStride = kButtonEntryLen;
constexpr std::size_t  kMulticlickCount  = 5;
constexpr long         kMulticlickMax    = 25;
constexpr long         kMulticlickDefault = 8;
constexpr std::uint8_t kSpdtGxSpeed = 0xF1;
constexpr std::uint8_t kSpdtGxSafe  = 0xF0;

// `button` is 0..4 in the block's own order. `mode` is "off", "gx-speed" or
// "gx-safe"; `value` is used only for "off".
bool encodeMulticlick(std::size_t button, const char* mode, long value,
                      std::uint8_t& out, const char** err);

// The inverse, for printing a record back to a person.
const char* describeMulticlick(std::uint8_t byte, long& value);

extern const ButtonAction kButtonActions[];
extern const std::size_t  kButtonActionCount;
extern const ButtonSlot   kButtonSlots[];
extern const std::size_t  kButtonSlotCount;

const ButtonAction* findButtonAction(const char* name);
const ButtonSlot*   findButtonSlot(const char* name);

// "a", "f1", "enter", "kp0", ... -> HID Keyboard/Keypad usage. Returns false
// for anything not in the vendor's own translator (cfg107 0x40a120).
bool hidKeycode(const char* name, std::uint8_t& out);
// "ctrl+shift" -> 0x03. Empty string is 0. Returns false on an unknown name.
bool hidModifiers(const char* spec, std::uint8_t& out);

// ---------------------------------------------------------------------------
// FIXED CPI, the button payload  (config-protocol.md §7.31)
// ---------------------------------------------------------------------------
// A DIFFERENT domain from the CPI stages above, and conflating them was a real
// defect: this path used to test `arg < 50 || arg > 26000`, a bound that cited
// nothing, rejected legal values at both ends and accepted illegal ones in the
// middle. cfg107's dialog 150 (`FIXED CPI`) normalises through `0x00401e70`:
//
//     clamp to [10, 30000]                    0x401e70 cmpl $0xa
//                                             0x401e7b cmpl $0x7530
//     round to the nearest multiple of 10     0x401e89 (0xcccccccd, shr 3)
//     ties up (rem >= 5)                      0x401e9d cmpl $0x5
//
// and `0x0040174d` stores that normalised result back into member `0x300`,
// which is the value that reaches the record. Note the STEP: this dialog rounds
// to 10 across the whole range, where the CPI-stage normaliser at `0x0040d880`
// switches to 50 above 10000. Using the stage grid here would wrongly reject
// 10010, which this dialog produces.
inline constexpr long kFixedCpiMin  = 10;
inline constexpr long kFixedCpiMax  = 30000;
inline constexpr long kFixedCpiStep = 10;

long normaliseFixedCpi(long v);

// Compose one entry's seven bytes. `keepPlus6` is the multiclick byte read back
// from the device and is copied through untouched (§1.3, read-modify-write).
// `mods` is the modifier bitfield for Key and must be 0 otherwise.
//
// `argX` is the keycode for Key. For FixedCpi it is the X CPI and `argY` the Y
// CPI, which are INDEPENDENT: cfg107 `0x00401eb0` reads members `0x300` (X) and
// `0x304` (Y) into `0x0057f328`/`0x0057f32c`, and `0x00407c7e`/`0x00407ca0`
// store those to entry `+2` and `+4`. This used to force Y = X on the strength
// of a comment claiming the dialog had one CPI box; §7.19.3 records two
// trackbars on dialog 150, ids 1077 and 1080. `argY` is ignored for every other
// payload -- pass argX, and the caller that means "one value" says so by
// passing it twice rather than by a sentinel this function has to interpret.
bool encodeButtonEntry(const ButtonAction& a, long argX, long argY,
                       std::uint8_t mods, std::uint8_t keepPlus6,
                       std::uint8_t out[kButtonEntryLen], const char** err);

// Look a field up by name, or nullptr.
const Settable* findSettable(const char* name);

// (old & ~mask) | ((value << shift) & mask), and identity for whole-byte
// fields. Split out so the one place that decides which bits move is testable.
std::uint8_t composeByte(std::uint8_t old, const Settable& f, std::uint8_t value);

}  // namespace egg::cfg
