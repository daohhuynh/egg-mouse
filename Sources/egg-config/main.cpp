// egg-config -- macOS configuration tool for the Endgame Gear OP1 8k v2.
//
// §4.4 stage 1 is `read`. The write path is `factory-reset` and `restore`, in
// that order, because §4.1 fixes it: "Implement factory reset first and confirm
// it works before any other write path. It is the undo for bad config state."
//
// `set` exists but is deliberately tiny. §1.3 forbids writing a byte whose
// meaning is [G], so the settable table below holds ONLY fields whose meaning
// was derived from the vendor binary and cited to an address. Everything else
// in the 115-byte record is reachable through `restore`, which writes back
// bytes the DEVICE produced -- so no byte in it is a guess, even though we
// cannot yet say what most of them mean.
#include "egg/Device.h"
#include "egg/Transport.h"
#include "egg/Protocol.h"
#include "egg/Log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
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
      "  egg-config set FIELD VALUE --yes  change ONE derived field\n"
      "  egg-config set                    list the settable fields\n"
      "\n"
      "  -v   hex-dump every frame\n"
      "\n"
      "Both writing commands require --yes. Neither can invent a byte: restore\n"
      "sends back only bytes the device itself produced, and factory-reset\n"
      "sends no payload at all -- the device composes its own defaults.\n"
      "\n"
      "`set` covers only fields whose MEANING is derived from the vendor\n"
      "binary and cited to an address -- a byte whose meaning is a guess must\n"
      "never reach the hardware. `egg-config set` with no arguments lists them.\n"
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

// ---------------------------------------------------------------------------
// The settable fields.
//
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
};

// config-protocol.md §7.5. Record 0x05 holds 8000/rate; the vendor's own reader
// at cfg107 0x413a79 drops anything that is not one of these seven powers of
// two to a no-op arm, so an unlisted value would be silently ignored rather
// than rejected -- which is exactly why this encoder refuses it here instead.
bool encodePolling(long hz, std::uint8_t& out) {
    if (hz <= 0 || 8000 % hz != 0) return false;
    const long div = 8000 / hz;
    if (div < 1 || div > 64 || (div & (div - 1)) != 0) return false;
    out = static_cast<std::uint8_t>(div);
    return true;
}

// config-protocol.md §7.6. Record 0x0e is the CPI stage count, combo index + 1.
bool encodeCpiLevels(long n, std::uint8_t& out) {
    if (n < 1 || n > 4) return false;
    out = static_cast<std::uint8_t>(n);
    return true;
}

// config-protocol.md §7.8/§7.9. Three checkboxes that reach a whole byte, each
// stored by a `setne` -- so the byte is 0 or 1 and nothing else. They are listed
// here rather than folded into one encoder because the citation differs per
// field and a wrong citation is worse than a duplicated four-line function.
bool encodeBool(long v, std::uint8_t& out) {
    if (v != 0 && v != 1) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

// config-protocol.md §7.9. Record 0x70 is the Sensor Angle Tuning trackbar
// position, stored raw by `movb %al` at cfg107 0x411b25 -- so a negative angle
// is two's complement. The RANGE is not derived: the vendor reads TBM_GETPOS
// and never clamps, so the bound lives in a TBM_SETRANGE we have not read. This
// encoder therefore accepts the full signed byte and says so, rather than
// inventing a limit that would silently reject a legal angle.
bool encodeSensorAngle(long deg, std::uint8_t& out) {
    if (deg < -128 || deg > 127) return false;
    out = static_cast<std::uint8_t>(static_cast<signed char>(deg));
    return true;
}

const Settable kSettable[] = {
    {"polling",    0x05, encodePolling,
     "125, 250, 500, 1000, 2000, 4000 or 8000 (Hz)",
     "config-protocol.md §7.5, cfg107 0x413a79 / 0x413a94-0x413b8f"},
    {"cpi-levels", 0x0e, encodeCpiLevels,
     "1, 2, 3 or 4",
     "config-protocol.md §7.6, cfg107 0x410c47 / 0x410c59 CB_GETCURSEL"},
    {"angle-snapping", 0x0a, encodeBool, "0 or 1",
     "config-protocol.md §7.8, cfg107 0x40ecb8 setne -> obj 0x28, serialised 0x4042f8"},
    {"motion-sync", 0x0c, encodeBool, "0 or 1",
     "config-protocol.md §7.9, cfg107 0x411ad1 setne -> obj 0x2a, serialised 0x404306"},
    {"force-max-fps", 0x71, encodeBool, "0 or 1",
     "config-protocol.md §7.9, cfg107 0x411b0b setne -> obj 0x2d, serialised 0x4045cf"},
    {"sensor-angle", 0x70, encodeSensorAngle, "-128 to 127 (degrees, two's complement)",
     "config-protocol.md §7.9, cfg107 0x411b19 TBM_GETPOS / 0x411b25 -> obj 0x2c"},
    // NAMED AFTER THE BYTE, NOT AFTER THE CHECKBOX, and that is deliberate.
    // The vendor's control is captioned "Disable LED on Lift-Off" and stores
    // the INVERSE of its tick (sete at cfg107 0x40ecd2), so a field of that
    // name taking 0/1 would mean the opposite of what half of all users would
    // assume. Here 1 = the underside DPI indicator stays lit when the mouse is
    // lifted, which is the vendor default; 0 = it goes out, which is the
    // vendor's box TICKED. Confirmed on the device by the owner, 2026-09-05.
    {"led-on-liftoff", 0x08, encodeBool,
     "0 or 1 -- 1 = DPI indicator stays lit when lifted (default); "
     "0 = it goes out, i.e. the vendor's \"Disable LED on Lift-Off\" TICKED",
     "config-protocol.md §7.8, cfg107 0x40ecd2 SETE -> obj 0x26, serialised 0x4042ea"},
};

// Derived, deliberately NOT settable, and each for a stated reason. Listed so
// that "why can I not set this?" has an answer in the tool rather than only in
// the notes -- §1.2a's rule that absence is a claim applies to our own UI too.
struct Withheld { const char* name; const char* why; };
const Withheld kWithheld[] = {
    {"disable-led-on-liftoff",
     "the vendor's caption, and deliberately not our field name, because the "
     "vendor stores its INVERSE (sete at cfg107 0x40ecd2). Use `led-on-liftoff`, "
     "which is named after the byte: set it to 0 to get this checkbox's effect"},
    {"slamclick-filter",
     "record 0x06 BIT 0, not a whole byte. Setting it needs a read-modify-write "
     "of a single bit inside a byte whose other bits cfg107 never writes (§1.3), "
     "which is a different code path from every field above"},
    {"cpi-downshift", "record 0x0b bits 3:2, and the combo index is remapped "
     "(0->2, 1->3, 2->1, 3->0). Derived but unscored; shares a byte with smoothing"},
    {"smoothing", "record 0x0b bits 1:0, remapped (0->2, 1->0, 2->1). Derived "
     "but unscored; shares a byte with cpi-downshift"},
};

void listSettable() {
    std::puts("settable fields (only those whose MEANING is derived, §1.3):");
    for (const Settable& f : kSettable)
        std::printf("  %-12s record 0x%02zx   accepts %s\n"
                    "               %s\n",
                    f.name, f.recordOffset, f.accepts, f.cite);
    std::puts("\nderived, but deliberately NOT settable yet:");
    for (const Withheld& w : kWithheld)
        std::printf("  %-24s %s\n", w.name, w.why);
    std::puts("\nEverything else in the record is writable only via `restore`,\n"
              "which sends back bytes the device itself produced.");
}

int cmdSet(const std::string& field, const std::string& value,
           bool verbose, bool yes) {
    const Settable* f = nullptr;
    for (const Settable& c : kSettable)
        if (field == c.name) { f = &c; break; }
    if (!f) {
        std::printf("`%s` is not a settable field.\n\n", field.c_str());
        listSettable();
        return 2;
    }
    char* end = nullptr;
    const long v = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end && *end)) {
        std::printf("`%s` is not a number.\n", value.c_str());
        return 2;
    }
    std::uint8_t want = 0;
    if (!f->encode(v, want)) {
        std::printf("%ld is not a legal %s. Accepts: %s\n",
                    v, f->name, f->accepts);
        return 2;
    }
    if (!yes) {
        std::printf("set %s = %ld would write 0x%02x to record 0x%02zx"
                    " (wire 0x%03zx).\n"
                    "  derived: %s\n"
                    "Exactly one byte changes. Re-run with --yes.\n",
                    f->name, v, want, f->recordOffset,
                    kPayloadOffset + f->recordOffset, f->cite);
        return 2;
    }

    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);

    // §4.1: read-modify-write, and never write after a failed read.
    std::vector<std::uint8_t> before;
    if (!readCurrent(t, log, before)) return 3;

    const std::size_t at = kPayloadOffset + f->recordOffset;
    if (before[at] == want) {
        std::printf("%s is already 0x%02x. Nothing to write.\n", f->name, want);
        return 0;
    }

    std::vector<std::uint8_t> want_rec = before;
    want_rec[at] = want;

    // The invariant that makes this command safe to have at all: our outgoing
    // payload differs from what we read in EXACTLY ONE byte, at the offset the
    // cited derivation names. Checked, not asserted in a comment.
    std::size_t moved = 0, movedAt = 0;
    for (std::size_t i = kPayloadOffset; i < kLargeLen; ++i)
        if (want_rec[i] != before[i]) { ++moved; movedAt = i; }
    if (moved != 1 || movedAt != at) {
        std::printf("internal error: %zu payload bytes would change"
                    " (expected exactly 1, at 0x%03zx). NOT WRITING.\n",
                    moved, at);
        return 4;
    }

    auto out = Transport::frame(kReportLarge, cfg::kWriteSettings);
    std::memcpy(out.data() + kPayloadOffset,
                want_rec.data() + kPayloadOffset, kPayloadLen);
    Reply w = t.exchange(out, kReportSmall, "A0 11 write settings");
    if (!w.ok()) {
        std::printf("write was not acknowledged: %s (status 0x%02x)\n",
                    describe(w.outcome), w.status);
        return 5;
    }

    std::vector<std::uint8_t> after;
    if (!readCurrent(t, log, after)) {
        std::puts("*** the write was acknowledged but the read-back FAILED.\n"
                  "    The device's state is unknown. Do not write again until\n"
                  "    a read succeeds.");
        return 6;
    }
    const int changed = reportDiff(before, after, "before", "after");
    if (after[at] != want) {
        std::printf("*** record 0x%02zx is 0x%02x, not the 0x%02x we sent."
                    " The write did NOT take.\n",
                    f->recordOffset, after[at], want);
        return 7;
    }
    if (changed != 1)
        std::printf("\nNOTE: %d bytes changed on the device, not 1. The extra\n"
                    "      ones are the device's own doing -- we sent exactly one\n"
                    "      difference. Worth reading before trusting this field.\n",
                    changed);
    std::printf("\n%s = %ld confirmed on the device.\n", f->name, v);
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
    if (cmd == "set" && args.size() > 2)
        return cmdSet(args[1], args[2], verbose, yes);
    if (cmd == "set") { listSettable(); return 2; }
    usage();
    return 1;
}
