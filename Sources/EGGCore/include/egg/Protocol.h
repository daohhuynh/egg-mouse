// Protocol.h -- every derived protocol constant, AS DATA.
//
// engineering-rules.md §3: "Protocol constants live in EGGCore as data tables, not
// scattered through code." Nothing in this file is a guess. Every value cites
// the notes section and the address it was derived from, so a reader can check
// any of it against the binary without trusting this file.
//
// engineering-rules.md §1.2: only [O] and [D] reach the hardware. There are no [G] values
// here. If you are tempted to add one, it belongs in a comment, not a constant.
#pragma once

#include <cstdint>
#include <cstddef>

namespace egg {

// ---------------------------------------------------------------------------
// Device identity
// ---------------------------------------------------------------------------
// [D] updater 1.10 0x4010fa (movl $0x3367,%eax) / 0x4010ff (cmpw).
// [D] config 1.07  0x403738 / 0x40373d -- the same test in a different binary.
// [O] observed on the device 2026-09-04, notes/device-predictions.md.
inline constexpr std::uint16_t kVendorId = 0x3367;

// [D] updater-protocol.md §1.1: an exhaustive E8-rel32 scan finds exactly nine
// callers of the enumerator and every pushed PID is one of these two literals.
// There is no path that computes a PID, reads one from a resource, or takes one
// from a list -- so ours are compile-time constants too.
// [O] both observed: 0x1978 in normal use, 0x1977 after LEFT+RIGHT at plug-in.
inline constexpr std::uint16_t kProductIdApplication = 0x1978;
inline constexpr std::uint16_t kProductIdBootloader  = 0x1977;

// The bootloader's full identity. [O] TWICE, by two independent routes that
// agree on all three fields:
//   - on this machine, entered by holding LEFT+RIGHT at plug-in
//     (notes/bootloader-observed.md §2);
//   - in Endgame's own captures 08 and 09, entered by sending A1 3A
//     (notes/flash-wire-observed.md §2.1) -- VID 0x3367, PID 0x1977,
//     bcdDevice 0x0006, iProduct string exactly "Bootloader".
//
// THAT AGREEMENT IS WHY SOFTWARE ENTRY IS AS SAFE AS THE BUTTON. The hardware
// path is [O] to leave the application intact and hand back to it on a power
// cycle. If A1 3A lands on a device presenting the identical identity, it is
// landing in the same place -- which is the strongest evidence available
// without opening the mouse, and it is still [G] that the two are the same
// code, because USB identity is what the firmware chooses to report.
//
// §4.2: "if the bootloader reports a version or an identity of any kind, refuse
// anything unrecognised." All three are checked, not just the PID.
inline constexpr std::uint16_t kBootloaderRelease = 0x0006;
inline constexpr const char*   kBootloaderProduct = "Bootloader";

// The vendor collection, and the reason this is not optional.
// [D] config 1.07 0x403777 (movl $0xff01,%edx) / 0x40377c / 0x403785.
// [D] updater 1.10 0x40113c / 0x401145 -- different function, identical test.
// [O] confirmed in the bootloader's own report descriptor, 2026-09-04.
//
// FOUR conditions, ALL required. notes/build-design.md §1: the device presents
// more than one vendor collection on the same VID/PID -- 0xFF02/0x01 and
// 0xFF02/0x02 also exist -- so an enumerator that stops at the first VID/PID
// match has a coin flip, not a bug it will notice.
inline constexpr std::uint16_t kUsagePageVendor = 0xFF01;
inline constexpr std::uint16_t kUsageVendor     = 0x02;

// ---------------------------------------------------------------------------
// Report geometry
// ---------------------------------------------------------------------------
// [D] updater 1.10: movl $0x411,%esi; config 1.07 0x404238 -- the same.
// [O] the device declares exactly these: report 0xA0 as 128 bits x 65 = 1040
//     payload + 1 report id = 1041; report 0xA1 as 8 bits x 63 = 63 + 1 = 64.
//     Both in ONE collection (0xFF01/0x02), MaxFeatureReportSize 1041.
//     notes/bootloader-observed.md §3. This closes updater-protocol.md §6.1.
//
// macOS does no padding -- IOHIDDeviceSetReport takes an explicit length -- so
// we must send exactly these and treat any mismatch as a preflight failure
// rather than something to paper over.
inline constexpr std::uint8_t kReportLarge = 0xA0;   // 1041 bytes
inline constexpr std::uint8_t kReportSmall = 0xA1;   //   64 bytes

inline constexpr std::size_t kLargeLen = 0x411;      // 1041
inline constexpr std::size_t kSmallLen = 0x40;       //   64

// [D] Frame layout, shared by both tools (config-protocol.md §1):
//   [0] report id
//   [1] command on the way out, status on the way back
//   [16 .. 1039]  1024-byte bulk payload, large reports only
inline constexpr std::size_t kCmdOffset     = 1;
inline constexpr std::size_t kStatusOffset  = 1;
inline constexpr std::size_t kPayloadOffset = 0x10;
inline constexpr std::size_t kPayloadLen    = 1024;

// [D] The settings record occupies only the first 115 bytes of the 1024-byte
// payload -- config-protocol.md §7.3, derived from the serializer FUN_004042d0
// at cfg107 0x004042d0..0x004045dc, which writes record offsets 0x00..0x72 and
// nothing beyond. Record offset r is payload offset r, so it is wire 0x10 + r.
// Corroborated: the last non-zero byte of 01-baseline.pcapng is wire 0x81 [O].
inline constexpr std::size_t kRecordLen = 0x73;   // 115

// [D] The four record bytes the vendor's serializer never writes, so its frames
// carry zeros there while the device reports 0x80 00 00 00. §7.4 -- the single
// place where §4.1 read-modify-write and vendor-mimicry disagree on a wire byte.
// DECIDED 2026-09-05: zero them, matching the vendor. The policy lives in
// EGGConfigCore/ConfigRecord.h as kDefaultUnknownBytes, which is where the
// reasoning and the reversal instructions are; this header only names the
// range. (Said "UNRESOLVED, pending a decision" until 2026-09-06, months of commits
// after the decision -- one decision described in three places is two places
// too many, and both of the extras went stale.)
inline constexpr std::size_t kRecordUnknownFirst = 0x01;
inline constexpr std::size_t kRecordUnknownLast  = 0x04;

static_assert(kRecordLen <= kPayloadLen,
              "the record cannot be larger than the payload that carries it");
static_assert(kRecordUnknownLast < kRecordLen,
              "the unknown bytes must lie inside the record");

static_assert(kPayloadOffset + kPayloadLen + 1 == kLargeLen,
              "1041 = 1 report id + 15 header + 1024 payload + 1 trailing");

// Returns the exact wire length for a report id, or 0 if the id is not one we
// are allowed to send. build-design.md §1.2: "Never send a frame whose report
// id is not 0xA0 or 0xA1. Never send a length other than the one the table
// gives for that report id."
constexpr std::size_t wireLength(std::uint8_t reportId) {
    return reportId == kReportLarge ? kLargeLen
         : reportId == kReportSmall ? kSmallLen
         : 0;
}

// ---------------------------------------------------------------------------
// Status convention -- A PARAMETER, NOT A CONSTANT
// ---------------------------------------------------------------------------
// config-protocol.md §1, and this is a trap: the two receive wrappers are
// structurally near-identical, so reading one and assuming the other is easy.
// The busy byte and the poll budget DIFFER between the tools. Verified in raw
// disassembly on both sides, and the updater value is confirmed across two
// independent code bases (fw110 0x40135d/0x40139d, fw104 0x401f08/0x401f4d).
inline constexpr std::uint8_t kStatusReady = 0x01;   // both tools

struct BusyPolicy {
    std::uint8_t busyStatus;      // resp[1] meaning "ask again"
    unsigned     initialSleepMs;  // before the first re-read
    unsigned     stepMs;          // added per pass
    unsigned     maxPasses;       // how many re-reads, then give up
};

// CORRECTED 2026-09-05 -- THE CONSTANT WAS RIGHT AND ITS MEANING WAS WRONG.
// This field used to be `budgetMs`, holding 1000 for config and 2000 for the
// updater, taken from the vendor's `cmpl $0x3e8` and `cmpl $0x7d0` and read as
// a millisecond budget. It is not one. The vendor's counter is incremented by a
// CONSTANT 0x64 once per pass and compared against those values, so they are
// pass counts of 10 and 20, not elapsed time:
//
//   config 1.07  0x4039ce  8b 45 f8        movl  -0x8(%ebp),%eax
//                0x4039d1  83 c0 64        addl  $0x64,%eax      <- constant
//                0x4039d7  3d e8 03 00 00  cmpl  $0x3e8,%eax     <- 1000/100 = 10
//   updater 1.10 0x40139a  83 c6 64        addl  $0x64,%esi
//                0x40139d  81 fe d0 07 ..  cmpl  $0x7d0,%esi     <- 2000/100 = 20
//
// Read as milliseconds against our growing step, the config loop gave up after
// 5 reads spanning 1100 ms where the vendor allows 10 spanning ~4.6 s, and the
// updater after 7 spanning 2100 ms where the vendor allows 20. That direction
// of error is the dangerous one: §4.2 says that after the erase, giving up IS
// the bug, and this policy is what the flasher's post-erase loop will poll on.
//
// Recovered from raw bytes per §1.2b, not from a decompiled view.
inline constexpr BusyPolicy kUpdaterBusy{0x04, 0, 100, 20};
inline constexpr BusyPolicy kConfigBusy{0x03, 100, 100, 10};

// ---------------------------------------------------------------------------
// Per-command delay before the first status read
// ---------------------------------------------------------------------------
// THE VENDOR TIMES EACH COMMAND SEPARATELY AND WE DID NOT. egg-config applied
// one 100 ms delay to everything; the flasher already got this right and passes
// a delay per command (WritePhase.cpp roundTrip). The two halves of the project
// disagreed and the config half is the one about to touch the device.
//
// [D] config 1.07, each a `push imm` immediately before `call *0x52d22c`, which
// rabin2 resolves to KERNEL32!Sleep. Verified from raw bytes 2026-09-05:
//   A1 02  0x40467b  6a 32              Sleep(50)
//   A1 12  0x403b96  6a 50              Sleep(80)
//   A1 13  0x4047af  68 4c 04 00 00     Sleep(1100)
//   A0 11  0x404248  0f b7 55 08        movzwl 0x8(%ebp) -- caller-supplied
//
// [O] and the wire agrees, across all ten windows-run captures, every gap being
// the Sleep plus ~10-15 ms of transfer:
//   a1 02  n=9   56.5 - 64.3 ms      a1 12  n=9   91.3 - 93.3 ms
//   a1 13  n=3  911.3 - 1110.1 ms    a0 11  n=73 302.0 - 317.7 ms
//
// A1 13 is the one that mattered: we polled at 100 ms, eleven times sooner than
// any vendor code path, and our whole poll window closed at 1100 ms -- exactly
// where the config tool's FIRST read lands.
inline constexpr unsigned kDelaySmallQuery   = 50;
inline constexpr unsigned kDelayReadRequest  = 80;
inline constexpr unsigned kDelayFactoryReset = 1100;
// A0 11's SLEEP IS 300 ms AND IT IS [D]. An earlier version of this comment
// said it was not derivable, and was wrong in a way engineering-rules.md §1.2a names in so
// many words. It read:
//
//   "the caller does not push it as a literal -- `push $0x12c` appears nowhere
//    in cfg107's code band, checked exhaustively over the whole file for
//    300/305/310/320 in both the imm32 and imm16 push forms."
//
// The scan was accurate. The conclusion was not: THE CALLER DOES NOT PUSH IT,
// IT STORES IT.
//
//   0x413ed1  c7 44 24 10 2c 01 00 00   movl $0x12c, 0x10(%esp)   APPLY, 0x413ea0
//   0x41403f  be 2c 01 00 00            movl $0x12c, %esi         live CPI, 0x414010
//
// 0x404180 has exactly two in-edges (config-protocol.md §7.10's call/jump
// accounting) and both are above. §1.2a clause 1 is that a scan for `push
// imm32` says nothing about `movl $imm32` -- stating a search space is what
// makes the gap visible, and nobody looked through it. See §7.27.
//
// THE NUMBER STAYS AT 320 ANYWAY, and the reasons are now positive rather than
// defensive:
//   - 300 ms is the vendor's FIRST attempt. It retries at 350 and then 400
//     (`addl $0x32` at 0x413f3b/0x414363, `cmpl $0x3` at 0x413f41/0x414366), so
//     320 is a wait Endgame's own tool spends on this command.
//   - The [O] window 302.0-317.7 ms over 73 writes is now EXPLAINED -- 300 ms
//     of Sleep plus transfer -- rather than merely observed. Two independent
//     routes agreeing beats either alone.
//   - It is device-proven at 320 (stage 4, 21/21). Moving a working constant to
//     match a freshly-derived one buys nothing and risks what works.
//
// THE VENDOR'S THREE-ATTEMPT RETRY IS NOT ADOPTED. Retrying a WRITE is policy,
// not free verification, and §4.2 cuts against it: a silent re-send after an
// unacknowledged write puts a second A0 11 on the wire in a state we cannot
// characterise. egg-config refuses and reports instead.
inline constexpr unsigned kDelayWriteRecord  = 320;   // [D] 300 + [O] 302.0-317.7

// The delay for a config command, or the policy default when we have no cited
// figure. A [G] delay is not a [G] byte -- it cannot corrupt a record -- but an
// uncited one still must not masquerade as derived, so anything absent here
// falls back rather than being invented.
constexpr unsigned configDelayMs(std::uint8_t command) {
    return command == 0x02 ? kDelaySmallQuery
         : command == 0x12 ? kDelayReadRequest
         : command == 0x13 ? kDelayFactoryReset
         : command == 0x11 ? kDelayWriteRecord
         : kConfigBusy.initialSleepMs;
}

// ---------------------------------------------------------------------------
// Command spaces -- DISJOINT. Do not read across.
// ---------------------------------------------------------------------------
// config-protocol.md §1: "What is not shared is the command space." The framing
// is common; the command numbers are not. Mixing them is a write with a [G]
// meaning, which engineering-rules.md §1.3 forbids outright.

// config 1.07. [D] config-protocol.md §7.1 -- cmdscan.py over ALL of .text,
// not just the device band, finds these four and no others.
namespace cfg {
inline constexpr std::uint8_t kReadRequest   = 0x12;  // A1 12 -> 0xA0 1041 in
inline constexpr std::uint8_t kWriteSettings = 0x11;  // A0 11, 1024 at +0x10
inline constexpr std::uint8_t kSmallQuery    = 0x02;  // A1 02 -> 0xA1 64 in
inline constexpr std::uint8_t kFactoryReset  = 0x13;  // A1 13, no payload
}  // namespace cfg

// updater 1.10. [D] updater-protocol.md §3, §3.7a -- seven, and that seven is
// all of them is established over the whole .text by two independent methods.
namespace fw {
inline constexpr std::uint8_t kBootloaderStart    = 0x03;  // A0 03
inline constexpr std::uint8_t kWriteBlock         = 0x06;  // A0 06
inline constexpr std::uint8_t kReadBlock          = 0x07;  // A0 07
inline constexpr std::uint8_t kWholeImageChecksum = 0x08;  // A1 08
inline constexpr std::uint8_t kBootloaderComplete = 0x09;  // A1 09
inline constexpr std::uint8_t kFactoryReset       = 0x13;  // A1 13
inline constexpr std::uint8_t kEnterBootloader    = 0x3A;  // A1 3A + 5A A5 32

// [D] updater-protocol.md §10.1. The index is computed, never chosen:
// leal 0x34(%ebx),%ecx for the write and again for the verify, from one
// counter, so they cannot drift apart.
//
// THIS IS THE ONE INVARIANT THAT CARRIES REAL WEIGHT. notes/bootloader-observed.md:
// with a hardware-forced bootloader entry confirmed, every other failure mode is
// recoverable. Writing below kBlockFirst is the only way to reach a state that
// is not.
inline constexpr std::uint16_t kBlockFirst = 0x34;
inline constexpr std::uint16_t kBlockLast  = 0x74;
inline constexpr std::size_t   kBlockCount = kBlockLast - kBlockFirst + 1;  // 65
static_assert(kBlockCount == 65, "block_count is 65 for every image we have seen");
static_assert(kBlockCount * kPayloadLen == 66560, "and 65 * 1024 = 66560 bytes");
}  // namespace fw

// ---------------------------------------------------------------------------
// The recovery procedure. [O] notes/bootloader-observed.md.
// ---------------------------------------------------------------------------
// Printed by egg-flash before it sends anything, per that file: a recovery
// procedure nobody can find is not a recovery procedure.
// THERE USED TO BE TWO OF THESE, and the duplication is what this shape
// exists to remove. `egg::kRecoveryProcedure` here was the short text
// egg-config printed and `egg::fw::kRecoveryProcedure` was the long one
// egg-flash printed, and they said the same thing twice. On 2026-09-06 the
// entry receipt made BOTH wrong and only one was noticed; the second was found
// by the COMPILER, reporting the name as ambiguous inside a test that used it
// unqualified. That is luck, not a check, and the fix for luck is not a better
// comment saying "amend both or neither".
//
// So there is now ONE definition of each fact, here, and
// egg::fw::kRecoveryProcedure is BUILT FROM THEM (WritePhase.cpp). Amending
// the procedure amends both printouts by construction, and Tests/test_flash.cpp
// asserts the composition rather than trusting it.
//
// Split in two because the two executables need different amounts of it:
// egg-config prints both parts as its "if the firmware goes wrong" note, and
// the flasher wraps them in its own heading.
inline constexpr const char* kButtonEntrySteps =
    "Hold LEFT and RIGHT mouse buttons together. Keep holding.\n"
    "Plug the cable in. Keep holding a few more seconds, then release.\n"
    "The mouse re-enumerates as PID 0x1977, Product \"Bootloader\".";

inline constexpr const char* kButtonEntryReflashNote =
    "It can be re-flashed from there -- with --i-know-this-is-button-entered,\n"
    "because the buttons are not an A1 3A entry and egg-flash refuses a\n"
    "bootloader it did not enter itself (engineering-rules.md 4.2b). The bootloader is\n"
    "forced by hardware and does not depend on any firmware being valid.";

}  // namespace egg
