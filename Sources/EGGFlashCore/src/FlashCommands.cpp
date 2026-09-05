#include "egg/FlashCommands.h"

#include "egg/Firmware.h"
#include "egg/Protocol.h"

#include <stdexcept>
#include <string>

namespace egg::fw {

std::string hex2(unsigned v) {
    static const char* d = "0123456789abcdef";
    std::string s = "0x";
    if (v > 0xFF) { s += d[(v >> 12) & 0xF]; s += d[(v >> 8) & 0xF]; }
    s += d[(v >> 4) & 0xF];
    s += d[v & 0xF];
    return s;
}

namespace {

Frame small(std::uint8_t cmd) {
    Frame f(kSmallLen, 0);          // 0x40 = 64
    f[0] = kReportSmall;            // 0xA1
    f[1] = cmd;
    return f;
}

Frame large(std::uint8_t cmd) {
    Frame f(kLargeLen, 0);          // 0x411 = 1041
    f[0] = kReportLarge;            // 0xA0
    f[1] = cmd;
    return f;
}

}  // namespace

Frame enterBootloader() {
    Frame f = small(0x3A);
    // [2..4] stay zero; the three-byte literal is at [5..7].
    f[5] = 0x5A;
    f[6] = 0xA5;
    f[7] = 0x32;
    return f;
}

Frame bootloaderStart(std::uint8_t blockCount, std::uint32_t wholeChecksum) {
    Frame f = large(0x03);
    f[16] = blockCount;
    f[17] = static_cast<std::uint8_t>(wholeChecksum & 0xFF);
    f[18] = static_cast<std::uint8_t>((wholeChecksum >> 8) & 0xFF);
    f[19] = static_cast<std::uint8_t>((wholeChecksum >> 16) & 0xFF);
    f[20] = static_cast<std::uint8_t>((wholeChecksum >> 24) & 0xFF);
    return f;
}

Frame writeBlock(std::uint16_t deviceIndex, const std::uint8_t* payload,
                 std::size_t payloadLen) {
    // §10.3 invariant 3, enforced in the builder. This is the guard that stands
    // between an arithmetic slip and a write below the application region --
    // the one remaining way to brick a device that is otherwise recoverable by
    // holding LEFT+RIGHT at plug-in (notes/bootloader-observed.md).
    if (deviceIndex < kBlockFirst || deviceIndex > kBlockLast)
        throw std::out_of_range(
            "block index " + std::to_string(deviceIndex) + " is outside [" +
            std::to_string(kBlockFirst) + "," + std::to_string(kBlockLast) +
            "]. Refusing to build the frame.");
    if (payloadLen != kBlockSize)
        throw std::invalid_argument(
            "write payload is " + std::to_string(payloadLen) +
            " bytes, must be exactly " + std::to_string(kBlockSize));

    Frame f = large(0x06);
    f[2] = static_cast<std::uint8_t>(deviceIndex & 0xFF);
    f[3] = static_cast<std::uint8_t>((deviceIndex >> 8) & 0xFF);
    const std::uint16_t sum = blockChecksum(payload, payloadLen);
    f[4] = static_cast<std::uint8_t>(sum & 0xFF);
    f[5] = static_cast<std::uint8_t>((sum >> 8) & 0xFF);
    // [6..15] stay zero; payload lands at [16..1039].
    for (std::size_t i = 0; i < payloadLen; ++i) f[16 + i] = payload[i];
    return f;
}

Frame readBlock(std::uint8_t deviceIndex) {
    if (deviceIndex < kBlockFirst || deviceIndex > kBlockLast)
        throw std::out_of_range("read block index outside [0x34,0x74]");
    Frame f = large(0x07);
    f[2] = deviceIndex;             // eight bits only -- see the header
    return f;
}

Frame wholeImageChecksumQuery(std::uint8_t lastBlock) {
    if (lastBlock < kBlockFirst || lastBlock > kBlockLast)
        throw std::out_of_range("checksum range end outside [0x34,0x74]");
    Frame f = small(0x08);
    f[2] = kBlockFirst;             // the vendor writes 0x34 as a literal too
    f[3] = lastBlock;
    return f;
}

Frame bootloaderComplete() { return small(0x09); }
Frame postSuccess()        { return small(0x13); }

std::uint32_t checksumResult(const std::uint8_t* resp, std::size_t n) {
    if (n < kChecksumResultOffset + 4)
        throw std::invalid_argument("checksum response too short");
    return static_cast<std::uint32_t>(resp[16]) |
           (static_cast<std::uint32_t>(resp[17]) << 8) |
           (static_cast<std::uint32_t>(resp[18]) << 16) |
           (static_cast<std::uint32_t>(resp[19]) << 24);
}

}  // namespace egg::fw
