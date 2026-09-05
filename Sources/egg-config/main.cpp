// egg-config -- macOS configuration tool for the Endgame Gear OP1 8k v2.
//
// §4.4 stage 1 is `read`. The write path is `factory-reset` and `restore`, in
// that order, because §4.1 fixes it: "Implement factory reset first and confirm
// it works before any other write path. It is the undo for bad config state."
//
// THERE IS STILL NO `set`. Naming a field requires the field map, which the
// Windows captures produce; writing a byte whose meaning is [G] is forbidden
// outright by §1.3. `restore` is different in kind -- it writes back bytes the
// DEVICE produced, so no byte in it is a guess, even though we cannot yet say
// what most of them mean.
#include "egg/Device.h"
#include "egg/Transport.h"
#include "egg/Protocol.h"
#include "egg/Log.h"

#include <algorithm>
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
      "  egg-config diff A B           compare two saved records, offline\n"
      "\n"
      "  egg-config factory-reset --yes    A1 13. restores the DEVICE's own\n"
      "                                    defaults. loses all settings.\n"
      "  egg-config restore F --yes        write a saved record back (A0 11),\n"
      "                                    then read it back and verify\n"
      "\n"
      "  -v   hex-dump every frame\n"
      "\n"
      "Both writing commands require --yes. Neither can invent a byte: restore\n"
      "sends back only bytes the device itself produced, and factory-reset\n"
      "sends no payload at all -- the device composes its own defaults.\n"
      "\n"
      "There is no `set`. Changing one named field needs the field map, and a\n"
      "byte whose meaning is a guess must never reach the hardware.\n"
      "\n"
      "If anything ever goes wrong with the FIRMWARE (not settings):\n");
    std::puts(kRecoveryProcedure);
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

// The diff both write paths log afterwards. §4.1: "Read back and verify after
// every write; log the diff."
int reportDiff(const std::vector<std::uint8_t>& before,
               const std::vector<std::uint8_t>& after,
               const char* whatBefore, const char* whatAfter) {
    const std::size_t n = std::min(before.size(), after.size());
    std::size_t changed = 0;
    std::printf("\n%-24s -> %s\n", whatBefore, whatAfter);
    for (std::size_t i = kPayloadOffset; i < n; ++i) {
        if (before[i] == after[i]) continue;
        ++changed;
        if (changed <= 64)
            std::printf("  wire 0x%04zx  (payload +0x%04zx)   %02x -> %02x\n",
                        i, i - kPayloadOffset, before[i], after[i]);
    }
    if (changed > 64) std::printf("  ... and %zu more\n", changed - 64);
    if (before.size() != after.size())
        std::printf("  *** lengths differ: %zu vs %zu\n", before.size(), after.size());
    std::printf("  %zu of %zu payload bytes differ\n", changed, kPayloadLen);
    return static_cast<int>(changed);
}

bool loadRecord(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("cannot open %s\n", path.c_str()); return false; }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.size() != kLargeLen) {
        std::printf("%s is %zu bytes; a saved record is %zu. Refusing.\n",
                    path.c_str(), out.size(), kLargeLen);
        return false;
    }
    if (out[0] != kReportLarge) {
        std::printf("%s does not begin with report id 0xA0. Refusing.\n", path.c_str());
        return false;
    }
    return true;
}

int cmdDiff(const std::string& a, const std::string& b) {
    std::vector<std::uint8_t> A, B;
    if (!loadRecord(a, A) || !loadRecord(b, B)) return 1;
    reportDiff(A, B, a.c_str(), b.c_str());
    return 0;
}

// Read the current record and insist it is trustworthy. Every write path starts
// here. §4.1: "Never write after a failed read."
bool readCurrent(Transport& t, Log& log, std::vector<std::uint8_t>& out) {
    auto req = Transport::frame(kReportSmall, cfg::kReadRequest);
    Reply r = t.exchange(req, kReportLarge, "A1 12 read settings");
    if (!r.ok()) {
        std::printf("read failed: %s (status 0x%02x). NOT WRITING.\n",
                    describe(r.outcome), r.status);
        return false;
    }
    if (!plausible(r.buf, log)) {
        std::puts("read returned a structurally implausible record. NOT WRITING.");
        return false;
    }
    out = std::move(r.buf);
    return true;
}

int cmdFactoryReset(bool verbose, bool yes) {
    if (!yes) {
        std::puts("factory-reset restores the DEVICE's own defaults and loses every\n"
                  "setting on the mouse. It does not touch firmware. Re-run with --yes.");
        return 2;
    }
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);

    std::vector<std::uint8_t> before;
    if (!readCurrent(t, log, before)) return 3;
    std::puts("read ok. saving the pre-reset record to egg-before-reset.bin");
    { std::ofstream f("egg-before-reset.bin", std::ios::binary);
      f.write(reinterpret_cast<const char*>(before.data()),
              static_cast<std::streamsize>(before.size())); }

    // config-protocol.md §7.4: FUN_00404720 builds a 64-byte frame carrying
    // nothing but the report id and 0x13, and sends it. The device composes its
    // own defaults; no host-side defaults blob exists anywhere.
    auto req = Transport::frame(kReportSmall, cfg::kFactoryReset);
    Reply r = t.exchange(req, kReportSmall, "A1 13 factory reset");
    if (!r.ok()) {
        std::printf("factory reset was not acknowledged: %s (status 0x%02x)\n",
                    describe(r.outcome), r.status);
        return 4;
    }
    std::puts("acknowledged. re-reading to see what actually changed.");

    std::vector<std::uint8_t> after;
    if (!readCurrent(t, log, after)) {
        std::puts("the reset was acknowledged but the read-back failed, so what the\n"
                  "device now holds is UNKNOWN. egg-before-reset.bin still has the\n"
                  "old record; `egg-config restore egg-before-reset.bin --yes` puts\n"
                  "it back once the device answers again.");
        return 5;
    }
    const int changed = reportDiff(before, after, "before reset", "after reset");
    if (changed == 0)
        std::puts("\n*** the device acknowledged the reset and NOTHING changed.\n"
                  "    Either it was already at defaults, or 0x13 did not do what\n"
                  "    we think. Do not treat this as a confirmed factory reset.");
    return 0;
}

int cmdRestore(const std::string& path, bool verbose, bool yes) {
    std::vector<std::uint8_t> want;
    if (!loadRecord(path, want)) return 1;
    if (!yes) {
        std::printf("restore would write all %zu payload bytes of %s to the mouse.\n"
                    "Re-run with --yes.\n", kPayloadLen, path.c_str());
        return 2;
    }
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);

    std::vector<std::uint8_t> before;
    if (!readCurrent(t, log, before)) return 3;
    reportDiff(before, want, "on the device now", path.c_str());

    // config-protocol.md §7.2a: the A0 11 frame is [0]=0xA0, [1]=0x11,
    // [2..15]=0 from the memset, payload at [16..1039]. Derived from
    // FUN_00404180, not guessed, and not from a capture.
    auto out = Transport::frame(kReportLarge, cfg::kWriteSettings);
    std::memcpy(out.data() + kPayloadOffset, want.data() + kPayloadOffset, kPayloadLen);

    // §7.4. The ONE place we knowingly put different bytes on the wire than the
    // vendor does. Its serializer never writes record 0x01..0x04, so it sends
    // zeros; the device reports 0x80 there. §1.3 says preserve what we do not
    // understand, so we send it back -- and say so, because a divergence from
    // the only writer observed to work must never be silent. the owner has not ruled
    // on this yet; when he does, this block is the single place to change.
    {
        bool differs = false;
        for (std::size_t k = kRecordUnknownFirst; k <= kRecordUnknownLast; ++k)
            if (out[kPayloadOffset + k] != 0) differs = true;
        if (differs) {
            std::printf(
                "\nNOTE: record bytes 0x%02zx..0x%02zx are being PRESERVED as",
                kRecordUnknownFirst, kRecordUnknownLast);
            for (std::size_t k = kRecordUnknownFirst; k <= kRecordUnknownLast; ++k)
                std::printf(" %02x", out[kPayloadOffset + k]);
            std::puts("\n      The vendor tool sends zeros there (config-protocol.md"
                      " §7.4).\n      This is a deliberate, unresolved difference,"
                      " not a defect.");
        }
    }
    Reply w = t.exchange(out, kReportSmall, "A0 11 write settings");
    if (!w.ok()) {
        std::printf("write was not acknowledged: %s (status 0x%02x)\n",
                    describe(w.outcome), w.status);
        return 4;
    }

    std::vector<std::uint8_t> after;
    if (!readCurrent(t, log, after)) {
        std::puts("write acknowledged but the read-back failed. UNVERIFIED.");
        return 5;
    }
    // §4.1: never let a reported success stand in for verifying the data.
    const int stillDiffer = reportDiff(want, after, path.c_str(), "read back");
    if (stillDiffer) {
        std::printf("\n*** %d payload bytes did not take. The write was acknowledged\n"
                    "    and the device does NOT hold what we sent.\n", stillDiffer);
        return 6;
    }
    std::puts("\nverified: the device holds exactly what was sent.");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false, yes = false;
    std::string save;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "--yes") yes = true;
        else if (a == "--save" && i + 1 < argc) save = argv[++i];
        else args.push_back(a);
    }
    if (args.empty()) { usage(); return 0; }
    const std::string& cmd = args[0];
    if (cmd == "devices") return cmdDevices();
    if (cmd == "read")    return cmdRead(verbose, save);
    if (cmd == "info")    return cmdInfo(verbose);
    if (cmd == "factory-reset") return cmdFactoryReset(verbose, yes);
    if (cmd == "restore" && args.size() > 1) return cmdRestore(args[1], verbose, yes);
    if (cmd == "diff" && args.size() > 2)    return cmdDiff(args[1], args[2]);
    usage();
    return 1;
}
