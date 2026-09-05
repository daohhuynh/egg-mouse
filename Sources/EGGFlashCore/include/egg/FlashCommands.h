// FlashCommands.h -- the seven flash commands, as pure byte builders.
//
// Every one of these was read from RAW DISASSEMBLY, not the decompiler
// (updater-protocol.md §3). That distinction is load-bearing: Ghidra's output
// for these functions is wrong by omission in at least three places, and each
// omission is a byte that goes to the device. It drops the 32-bit checksum
// FUN_00401890 writes to buf[17..20], and it drops the block index
// FUN_00401980 writes to buf[2..3] entirely. A builder written from the
// decompiled C would send zeros in both places and look perfectly correct.
//
// These functions do no I/O. That is deliberate: it makes the entire byte
// stream reproducible and testable with no device, which is what the dry-run
// and golden-file requirements in §4.3 rest on.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace egg::fw {

using Frame = std::vector<std::uint8_t>;

// "0x" + std::to_string(0x34) is "0x52", which is wrong in the one place it
// matters most: the log the user reads after the application region is blank.
std::string hex2(unsigned v);

// [D] §3.1, FUN_00403750. [0]=0xA1 [1]=0x3A [2..4]=0 [5]=0x5A [6]=0xA5
// [7]=0x32, rest zero, length 0x40.
// The vendor sends it up to 9 times with Sleep(i*2) between, each followed by a
// 64-byte read; any successful read ends the loop.
Frame enterBootloader();

// [D] §3.2, FUN_00401890 (memset at 0x4018bc, length 0x411).
// [0]=0xA0 [1]=0x03 [2..7]=0
// [16]    = blockCount & 0xFF          store at 0x4018c4
// [17..20]= wholeChecksum, 32-bit LE   stores at 0x4018ce..0x40190f
//
// The start command DECLARES up front how many blocks are coming and what the
// total must sum to. Zeros there -- which is what a decompiler-only reading
// produces -- would be wrong.
Frame bootloaderStart(std::uint8_t blockCount, std::uint32_t wholeChecksum);

// [D] §3.3, FUN_00401980 (memset at 0x4019b9, length 0x411).
// [0]=0xA0 [1]=0x06                    movw $0x6a0 at 0x4019d5
// [2]  = index & 0xFF                  0x4019be
// [3]  = (index >> 8) & 0xFF           0x4019de
// [4]  = sum & 0xFF                    0x401a54
// [5]  = (sum >> 8) & 0xFF             0x401a5a
// [6..15] = 0
// [16..1039] = the 1024 payload bytes  rep movsl, 0x100 dwords, 0x4019ee
//
// Throws if payloadLen != 1024 or if the device index is outside
// [kBlockFirst, kBlockLast]. Both are §10.3 invariants and both are enforced
// HERE, in the builder, rather than only at the call site -- a guard that lives
// only in the caller is a guard the next caller forgets.
Frame writeBlock(std::uint16_t deviceIndex, const std::uint8_t* payload,
                 std::size_t payloadLen);

// [D] §3.4, FUN_00401ad0 (memset at 0x401af7, length 0x411).
// [0]=0xA0 [1]=0x07  movw $0x7a0 at 0x401b0b
// [2] = index & 0xFF                   0x401b14 -- ONE byte, unlike the write
//
// The asymmetry is real and is carried deliberately: the write puts a 16-bit
// index at [2..3], the read puts an 8-bit index at [2]. With the indices this
// tool uses (0x34..0x74) it never shows, but reproducing the vendor exactly is
// free and guessing is not.
Frame readBlock(std::uint8_t deviceIndex);

// [D] §3.5, FUN_00401bb0 (base -0x44(%ebp), length 0x40).
// [0]=0xA1 [1]=0x08  movw $0x8a1 at 0x401be5
// [2] = 0x34         0x401beb -- first block, a literal in the vendor too
// [3] = lastBlock    0x401bef, and the call site 0x403b6c computes it as
//                    (byte)blockCount + 0x33, so 65 blocks -> 0x74
//
// The response's result is a 32-bit LE value at resp[16..19], assembled at
// 0x401c55..0x401c7a.
Frame wholeImageChecksumQuery(std::uint8_t lastBlock);

// [D] §3.6, built inline in FUN_00403960. [0]=0xA1 [1]=0x09 [2..5]=0, len 0x40.
Frame bootloaderComplete();

// [D] §3.7, built inline in FUN_00403960. [0]=0xA1 [1]=0x13 [2..5]=0, len 0x40.
//
// Sent only AFTER the update has already succeeded, then Sleep(900) and one
// 64-byte read. What it actually does is not derivable from the tool -- [G].
// We send it because the vendor does and because mirroring the vendor exactly
// is the cheapest safety available (§4.2), not because we know what it means.
Frame postSuccess();

// Where the 32-bit result of the checksum query lives in the response.
// [D] §3.5, 0x401c55..0x401c7a: resp[19]<<24 | resp[18]<<16 | resp[17]<<8 |
// resp[16].
inline constexpr std::size_t kChecksumResultOffset = 16;
std::uint32_t checksumResult(const std::uint8_t* resp, std::size_t n);

}  // namespace egg::fw
