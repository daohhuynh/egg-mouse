// egg-config -- macOS configuration tool for the Endgame Gear OP1 8k v2.
//
// STAGE 1 ONLY (CLAUDE.md §4.4): "One read-only command round trip. Exercises
// enumeration, channel open, report framing, response parsing, timing. Nothing
// is written; nothing to undo."
//
// There is deliberately no write path in this file yet. §4.1 requires factory
// reset to be implemented and confirmed before any other write, and the
// settings record's byte layout is not derived -- it is what the Windows
// capture is for. Writing a field we cannot name would be a [G] byte reaching
// the hardware, which §1.3 forbids outright.
#include "egg/Device.h"
#include "egg/Transport.h"
#include "egg/Protocol.h"
#include "egg/Log.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace egg;

namespace {

void usage() {
    std::puts(
      "egg-config -- Endgame Gear OP1 8k v2, macOS\n"
      "\n"
      "  egg-config devices            list every VID 0x3367 interface\n"
      "  egg-config read [--save F]    read the settings record and dump it\n"
      "  egg-config info               small query (A1 02)\n"
      "\n"
      "  -v   hex-dump every frame\n"
      "\n"
      "This build is READ-ONLY. It has no write path and cannot change the mouse.");
}

int cmdDevices() {
    std::vector<Match> all = enumerateAll();
    if (all.empty()) { std::puts("no VID 0x3367 device attached."); return 1; }
    std::printf("%-8s %-10s %-7s %-9s %-14s %s\n",
                "PID", "usagepage", "usage", "version", "manufacturer", "product");
    for (const Match& m : all) {
        const char* mode = m.productId == kProductIdApplication ? " (application)"
                         : m.productId == kProductIdBootloader  ? " (BOOTLOADER)"
                         : "";
        std::printf("0x%04x   0x%04x     0x%02x    0x%04x    %-14s %s%s\n",
                    m.productId, m.usagePage, m.usage, m.releaseNumber,
                    m.manufacturer.c_str(), m.product.c_str(), mode);
    }
    return 0;
}

// CLAUDE.md §4.1: "Validate a read is structurally plausible before acting on
// it. Never let a reported success stand in for verifying the data itself."
//
// We cannot check field values -- the layout is not derived. We CAN check the
// things the protocol itself fixes, and an all-zero or all-0xFF buffer is the
// classic shape of a read that succeeded and returned nothing.
bool plausible(const std::vector<std::uint8_t>& r, Log& log) {
    if (r.size() != kLargeLen) {
        log.warn("record is not 1041 bytes"); return false;
    }
    if (r[0] != kReportLarge) {
        log.warn("record does not begin with report id 0xA0"); return false;
    }
    const std::uint8_t* p = r.data() + kPayloadOffset;
    bool allSame = true;
    for (std::size_t i = 1; i < kPayloadLen; ++i)
        if (p[i] != p[0]) { allSame = false; break; }
    if (allSame) {
        char b[96];
        std::snprintf(b, sizeof b,
            "all 1024 payload bytes are 0x%02x -- that is not a settings record",
            p[0]);
        log.warn(b);
        return false;
    }
    unsigned distinct = 0; bool seen[256] = {};
    for (std::size_t i = 0; i < kPayloadLen; ++i)
        if (!seen[p[i]]) { seen[p[i]] = true; ++distinct; }
    char b[96];
    std::snprintf(b, sizeof b, "%u distinct byte values across the 1024-byte payload",
                  distinct);
    log.note(b);
    return true;
}

int cmdRead(bool verbose, const std::string& savePath) {
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;

    Transport t(*dev, kConfigBusy, log);

    // config-protocol.md §7.2: A1 12 out (64 bytes), answered on report 0xA0
    // with 1041. Note the reply id is the OTHER report -- the id tracks the
    // transfer size, not the direction.
    auto req = Transport::frame(kReportSmall, cfg::kReadRequest);
    Reply r = t.exchange(req, kReportLarge, "A1 12 read settings");

    if (!r.ok()) {
        std::printf("\nread failed: %s (status byte 0x%02x)\n",
                    describe(r.outcome), r.status);
        return 2;
    }
    if (!plausible(r.buf, log)) {
        std::puts("\nread returned a structurally implausible record; not saving.");
        return 3;
    }

    std::printf("\nsettings record, 1024 payload bytes at +0x%zx:\n\n", kPayloadOffset);
    std::fputs(hexDump(r.buf.data() + kPayloadOffset, kPayloadLen).c_str(), stdout);

    // §4.1: "Save a known-good blob to disk on first connect."
    if (!savePath.empty()) {
        std::ofstream f(savePath, std::ios::binary);
        if (!f) { std::printf("could not write %s\n", savePath.c_str()); return 4; }
        f.write(reinterpret_cast<const char*>(r.buf.data()),
                static_cast<std::streamsize>(r.buf.size()));
        std::printf("\nsaved all %zu bytes (frame included, not just the payload) to %s\n",
                    r.buf.size(), savePath.c_str());
    }
    return 0;
}

int cmdInfo(bool verbose) {
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;

    Transport t(*dev, kConfigBusy, log);
    // config-protocol.md §7.3: A1 02 out, answered on 0xA1 with 64 bytes, from
    // which the vendor unpacks several dwords. What those fields MEAN is [G]
    // and is not guessed here -- we print the bytes and stop.
    auto req = Transport::frame(kReportSmall, cfg::kSmallQuery);
    Reply r = t.exchange(req, kReportSmall, "A1 02 small query");
    if (!r.ok()) {
        std::printf("\nquery failed: %s (status byte 0x%02x)\n",
                    describe(r.outcome), r.status);
        return 2;
    }
    std::puts("\n64-byte reply. The vendor unpacks dwords from +0x10 onward;");
    std::puts("what they mean is NOT derived, so they are printed and not named.\n");
    std::fputs(hexDump(r.buf.data(), r.buf.size()).c_str(), stdout);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::string save;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "--save" && i + 1 < argc) save = argv[++i];
        else args.push_back(a);
    }
    if (args.empty()) { usage(); return 0; }
    const std::string& cmd = args[0];
    if (cmd == "devices") return cmdDevices();
    if (cmd == "read")    return cmdRead(verbose, save);
    if (cmd == "info")    return cmdInfo(verbose);
    usage();
    return 1;
}
