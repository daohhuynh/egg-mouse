// Protocol.h -- every derived protocol constant, AS DATA.
//
// CLAUDE.md §3: "Protocol constants live in EGGCore as data tables, not
// scattered through code." Nothing in this file is a guess. Every value cites
// the notes section and the address it was derived from, so a reader can check
// any of it against the binary without trusting this file.
//
// CLAUDE.md §1.2: only [O] and [D] reach the hardware. There are no [G] values
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
    unsigned     budgetMs;        // total, then give up
};

// [D] updater 1.10 FUN_00401330: cmpb $0x4 @0x40135d, cmpl $0x7d0 @0x40139d,
//     no initial sleep. Confirmed in fw104 FUN_00401ed0.
inline constexpr BusyPolicy kUpdaterBusy{0x04, 0, 100, 2000};

// [D] config 1.07 FUN_00403920: cmpl $0x3 @0x40396e, cmpl $0x3e8 @0x4039d7,
//     fixed Sleep(0x64) before the first re-read.
inline constexpr BusyPolicy kConfigBusy{0x03, 100, 100, 1000};

// ---------------------------------------------------------------------------
// Command spaces -- DISJOINT. Do not read across.
// ---------------------------------------------------------------------------
// config-protocol.md §1: "What is not shared is the command space." The framing
// is common; the command numbers are not. Mixing them is a write with a [G]
// meaning, which CLAUDE.md §1.3 forbids outright.

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
inline constexpr const char* kRecoveryProcedure =
    "Hold LEFT and RIGHT mouse buttons together, plug the cable in while still\n"
    "holding, and keep holding for a few more seconds. The mouse re-enumerates\n"
    "as PID 0x1977, Product \"Bootloader\", and can be re-flashed from there.";

}  // namespace egg
