// egg-config -- macOS configuration tool for the Endgame Gear OP1 8k v2.
//
// §4.4 stage 1 is `read`. The write path is `factory-reset` and `restore`, in
// that order, because §4.1 fixes it: "Implement factory reset first and confirm
// it works before any other write path. It is the undo for bad config state."
//
// `set` exists but is deliberately tiny. §1.3 forbids writing a byte whose
// meaning is [G], so the settable table holds ONLY fields whose meaning was
// derived from the vendor binary and cited to an address. Everything else in
// the 115-byte record is reachable through `restore`, which writes back bytes
// the DEVICE produced -- so no byte in it is a guess, even though we cannot yet
// say what most of them mean.
//
// THIS FILE IS ARGUMENT PARSING AND PRINTING. It holds no protocol knowledge
// and performs no read-modify-write. Rewired 2026-09-05: every safety property
// §4.1 asks for now lives in EGGConfigCore, where Tests/test_config.cpp drives
// it against a device that lies. Before that, all of it was inline here and
// only a real mouse could reach it.
#include "egg/ConfigRecord.h"
#include "egg/ConfigSession.h"
#include "egg/Device.h"
#include "egg/Log.h"
#include "egg/Protocol.h"
#include "egg/Transport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace egg;
using namespace egg::cfg;

namespace {

// ---------------------------------------------------------------------------
// The real ConfigLink: the session's three operations, over Transport.
// ---------------------------------------------------------------------------
// The only place in the config tool where bytes reach a device. Note that it
// does no validation and makes no decisions -- it hands back exactly what the
// transport reported, including a failure, so that the session's rules are the
// only rules. A link that "helpfully" retried or sanitised would be a second,
// untested policy sitting under the tested one.
class DeviceConfigLink : public ConfigLink {
public:
    DeviceConfigLink(Transport& t, Log& log) : t_(t), log_(log) {}

    Reply readRecord() override {
        // config-protocol.md §7.2: A1 12 out (64 bytes), answered on report
        // 0xA0 with 1041. The reply id is the OTHER report -- the id tracks the
        // transfer size, not the direction.
        return t_.exchange(Transport::frame(kReportSmall, cfg::kReadRequest),
                           kReportLarge, "A1 12 read settings");
    }

    Reply writeRecord(const std::vector<std::uint8_t>& frame) override {
        // The frame arrives complete. This function must not touch it: the
        // session self-checked those exact bytes, and anything altered here
        // would be bytes nothing checked.
        return t_.exchange(frame, kReportSmall, "A0 11 write settings");
    }

    Reply factoryReset() override {
        // config-protocol.md §7.4: FUN_00404720 builds a 64-byte frame carrying
        // nothing but the report id and 0x13, and sends it. The device composes
        // its own defaults; no host-side defaults blob exists anywhere.
        return t_.exchange(Transport::frame(kReportSmall, cfg::kFactoryReset),
                           kReportSmall, "A1 13 factory reset");
    }

    void note(const std::string& line) override { log_.note(line.c_str()); }

private:
    Transport& t_;
    Log&       log_;
};

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------
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
      "  egg-config encode F V IN OUT      offline: apply one field to a saved\n"
      "                                    record and write the result. Sends\n"
      "                                    nothing and needs no device.\n"
      "\n"
      "  -v                        hex-dump every frame\n"
      "  --unknown-bytes=P         P is `preserve` or `vendor`. Record bytes\n"
      "                            0x01..0x04 are the ONE place where read-\n"
      "                            modify-write and copying the vendor give\n"
      "                            different wire bytes: the device reports\n"
      "                            80 00 00 00 there and every vendor write\n"
      "                            carries 00 00 00 00. `preserve` sends back\n"
      "                            what was read (§1.3); `vendor` reproduces\n"
      "                            the vendor's frames exactly.\n"
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

// §4.1: "Read back and verify after every write; log the diff." The comparison
// itself is EGGConfigCore's; this only renders it.
int printDiff(const std::vector<ByteChange>& d,
              const char* whatBefore, const char* whatAfter) {
    std::printf("\n%-24s -> %s\n", whatBefore, whatAfter);
    std::size_t shown = 0;
    for (const ByteChange& c : d) {
        if (++shown > 64) continue;
        std::printf("  wire 0x%04zx  (payload +0x%04zx)   %02x -> %02x\n",
                    kPayloadOffset + c.recordOffset, c.recordOffset,
                    c.before, c.after);
    }
    if (d.size() > 64) std::printf("  ... and %zu more\n", d.size() - 64);
    std::printf("  %zu of %zu payload bytes differ\n", d.size(), kPayloadLen);
    return static_cast<int>(d.size());
}

int printDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
              const char* whatBefore, const char* whatAfter) {
    if (a.size() != b.size())
        std::printf("  *** lengths differ: %zu vs %zu\n", a.size(), b.size());
    return printDiff(diff(a, b), whatBefore, whatAfter);
}

void listSettable() {
    std::puts("settable fields (only those whose MEANING is derived, §1.3):");
    for (std::size_t i = 0; i < kSettableCount; ++i) {
        const Settable& f = kSettable[i];
        if (f.mask == 0xFF)
            std::printf("  %-16s record 0x%02zx        accepts %s\n"
                        "                   %s\n",
                        f.name, f.recordOffset, f.accepts, f.cite);
        else
            std::printf("  %-16s record 0x%02zx & 0x%02x  accepts %s\n"
                        "                   %s\n",
                        f.name, f.recordOffset, f.mask, f.accepts, f.cite);
    }
    std::puts("\nderived, but deliberately NOT settable yet:");
    for (std::size_t i = 0; i < kWithheldCount; ++i)
        std::printf("  %-24s %s\n", kWithheld[i].name, kWithheld[i].why);
    std::puts("\nEverything else in the record is writable only via `restore`,\n"
              "which sends back bytes the device itself produced.");
}

// Every failure the session can report, rendered once. Each Result has its own
// text because "it failed" is not an answer a user can act on -- and because
// the difference between "we did not write" and "we wrote and cannot confirm"
// is the difference between a retry and a recovery.
void explain(Result r) {
    switch (r) {
        case Result::Ok:
            break;
        case Result::ReadFailed:
            std::puts("read failed. NOT WRITING.");
            break;
        case Result::ReadImplausible:
            std::puts("read returned a structurally implausible record. NOT WRITING.");
            break;
        case Result::AlreadySet:
            std::puts("already set. Nothing to write.");
            break;
        case Result::RefusedSelfCheck:
            std::puts("*** internal error: the frame we built differs from what we\n"
                      "    intended. NOT WRITING. This is a bug in egg-config, not\n"
                      "    in the device -- please report it with the command used.");
            break;
        case Result::WriteRejected:
            std::puts("the device did not acknowledge the write. Nothing was\n"
                      "confirmed; the device most likely holds what it held before.");
            break;
        case Result::VerifyReadFailed:
            std::puts("*** the write was acknowledged but the read-back FAILED.\n"
                      "    The device's state is UNKNOWN. Do not write again until\n"
                      "    a read succeeds.");
            break;
        case Result::VerifyMismatch:
            std::puts("*** the device acknowledged the write and does NOT hold what\n"
                      "    we sent. Read it back before doing anything else.");
            break;
    }
}

// Exit codes, kept distinct so a script can tell the cases apart. Held here so
// the mapping is one table rather than a scattering of magic numbers.
int rcFor(Result r) {
    switch (r) {
        case Result::Ok:               return 0;
        case Result::ReadFailed:       return 3;
        case Result::ReadImplausible:  return 3;
        case Result::AlreadySet:       return 0;
        case Result::RefusedSelfCheck: return 4;
        case Result::WriteRejected:    return 5;
        case Result::VerifyReadFailed: return 6;
        case Result::VerifyMismatch:   return 7;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
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

bool saveRecord(const std::string& path, const std::vector<std::uint8_t>& r) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::printf("could not write %s\n", path.c_str()); return false; }
    f.write(reinterpret_cast<const char*>(r.data()),
            static_cast<std::streamsize>(r.size()));
    return static_cast<bool>(f);
}

// ---------------------------------------------------------------------------
// Read-only commands
// ---------------------------------------------------------------------------
int cmdRead(bool verbose, const std::string& savePath, UnknownBytes policy) {
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;

    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);

    std::vector<std::uint8_t> rec;
    Result r = Result::Ok;
    if (!s.read(rec, r)) {
        std::printf("\n");
        explain(r);
        return rcFor(r);
    }

    std::printf("\nsettings record, 1024 payload bytes at +0x%zx:\n\n", kPayloadOffset);
    std::fputs(hexDump(rec.data() + kPayloadOffset, kPayloadLen).c_str(), stdout);

    // §4.1: "Save a known-good blob to disk on first connect."
    if (!savePath.empty()) {
        if (!saveRecord(savePath, rec)) return 4;
        std::printf("\nsaved all %zu bytes (frame included, not just the payload) to %s\n",
                    rec.size(), savePath.c_str());
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

int cmdDiff(const std::string& a, const std::string& b) {
    std::vector<std::uint8_t> A, B;
    if (!loadRecord(a, A) || !loadRecord(b, B)) return 1;
    printDiff(A, B, a.c_str(), b.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// Writing commands
// ---------------------------------------------------------------------------
int cmdFactoryReset(bool verbose, bool yes, UnknownBytes policy) {
    if (!yes) {
        std::puts("factory-reset restores the DEVICE's own defaults and loses every\n"
                  "setting on the mouse. It does not touch firmware. Re-run with --yes.");
        return 2;
    }
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);

    // §4.1: "Never write after a failed read." A reset is still a write, and it
    // is the one write whose undo is the file we are about to save.
    std::vector<std::uint8_t> before;
    Result r = Result::Ok;
    if (!s.read(before, r)) { explain(r); return rcFor(r); }

    std::puts("read ok. saving the pre-reset record to egg-before-reset.bin");
    if (!saveRecord("egg-before-reset.bin", before)) {
        std::puts("could not save the pre-reset record, so the reset would have no\n"
                  "undo. NOT RESETTING.");
        return 4;
    }

    r = s.factoryReset();
    if (r != Result::Ok) {
        std::puts("factory reset was not acknowledged.");
        return 4;
    }
    std::puts("acknowledged. re-reading to see what actually changed.");

    std::vector<std::uint8_t> after;
    if (!s.read(after, r)) {
        std::puts("the reset was acknowledged but the read-back failed, so what the\n"
                  "device now holds is UNKNOWN. egg-before-reset.bin still has the\n"
                  "old record; `egg-config restore egg-before-reset.bin --yes` puts\n"
                  "it back once the device answers again.");
        return 5;
    }
    const int changed = printDiff(before, after, "before reset", "after reset");
    if (changed == 0)
        std::puts("\n*** the device acknowledged the reset and NOTHING changed.\n"
                  "    Either it was already at defaults, or 0x13 did not do what\n"
                  "    we think. Do not treat this as a confirmed factory reset.");
    return 0;
}

// Observed 2026-09-05 in windows-run/02-basic.pcapng: when the vendor changed
// the CPI stage COUNT at record 0x0e it sometimes also moved record 0x0d, the
// ACTIVE stage. Going from 1 stage to 2 it reset the active stage from 1 to 0.
// It did not always do so -- setting the count to 1 while stage index 1 was
// active left an active stage outside the range, so the vendor is not applying
// a clamp we could simply copy.
//
// We move exactly one byte, by design, so we cannot mirror it even if we wanted
// to. What we can do is not let it happen silently. This warns; it does not
// refuse, because refusing would be inventing a rule the vendor itself breaks.
void warnIfActiveStageWouldBeOutOfRange(const std::vector<std::uint8_t>& rec,
                                        const Settable& f, long v) {
    if (std::string(f.name) != "cpi-levels") return;
    const unsigned active = rec[kPayloadOffset + 0x0d];
    if (static_cast<long>(active) < v) return;
    std::printf(
        "  NOTE: record 0x0d (the ACTIVE CPI stage) is %u, which is not below\n"
        "        the new count of %ld. The vendor's tool sometimes moves that\n"
        "        byte too; we move exactly one byte and will leave it at %u.\n"
        "        Set the active stage separately if the mouse behaves oddly.\n",
        active, v, active);
}

// Field lookup and value parsing, shared by `set` and `encode` so that the two
// cannot drift into accepting different things.
const Settable* resolve(const std::string& field, const std::string& value,
                        long& v, std::uint8_t& encoded, bool listOnFailure) {
    const Settable* f = findSettable(field.c_str());
    if (!f) {
        std::printf("`%s` is not a settable field.%s", field.c_str(),
                    listOnFailure ? "\n\n" : "\n");
        if (listOnFailure) listSettable();
        return nullptr;
    }
    char* end = nullptr;
    v = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end && *end)) {
        std::printf("`%s` is not a number.\n", value.c_str());
        return nullptr;
    }
    if (!f->encode(v, encoded)) {
        std::printf("%ld is not a legal %s. Accepts: %s\n", v, f->name, f->accepts);
        return nullptr;
    }
    return f;
}

// Offline: apply one field to a record file and write the result out. No
// device, no transport, nothing sent. This exists so the write path can be
// replayed against the vendor's own writes -- the captures give us 73 pairs of
// (record before, record after) with a known field change between them, which
// is a far better test than any record we could invent.
//
// The payload it writes is produced by ConfigSession::buildFrame, i.e. by the
// SAME code that composes what would go on the wire. Until 2026-09-05 this
// command reimplemented the arithmetic, which meant the replay test scored a
// second implementation rather than the one that reaches the device.
int cmdEncode(const std::string& field, const std::string& value,
              const std::string& inPath, const std::string& outPath,
              UnknownBytes policy) {
    long v = 0;
    std::uint8_t encoded = 0;
    const Settable* f = resolve(field, value, v, encoded, false);
    if (!f) return 2;

    std::vector<std::uint8_t> before;
    if (!loadRecord(inPath, before)) return 1;

    warnIfActiveStageWouldBeOutOfRange(before, *f, v);

    std::vector<std::uint8_t> frame = ConfigSession::buildFrame(before, *f, encoded, policy);
    if (frame.size() != kLargeLen) {
        std::printf("%s is not a plausible settings record. NOT WRITING.\n",
                    inPath.c_str());
        return 1;
    }

    // Keep the file's own header -- a saved record is not a frame, and the
    // replay corpus stores records with a zeroed header. Only the payload,
    // which is the part that reaches the device, comes from buildFrame.
    std::vector<std::uint8_t> out = before;
    std::memcpy(out.data() + kPayloadOffset, frame.data() + kPayloadOffset, kPayloadLen);

    if (!saveRecord(outPath, out)) return 1;
    return 0;
}

int cmdSet(const std::string& field, const std::string& value,
           bool verbose, bool yes, UnknownBytes policy) {
    long v = 0;
    std::uint8_t encoded = 0;
    const Settable* f = resolve(field, value, v, encoded, true);
    if (!f) return 2;

    if (!yes) {
        if (f->mask == 0xFF) {
            std::printf("set %s = %ld would write 0x%02x to record 0x%02zx"
                        " (wire 0x%03zx).\n", f->name, v, encoded, f->recordOffset,
                        kPayloadOffset + f->recordOffset);
        } else {
            std::printf("set %s = %ld would write field value 0x%02x into the"
                        " 0x%02x bits of record 0x%02zx (wire 0x%03zx),\n"
                        "leaving the other bits of that byte untouched.\n",
                        f->name, v, encoded, f->mask, f->recordOffset,
                        kPayloadOffset + f->recordOffset);
        }
        std::printf("  derived: %s\n", f->cite);
        if (policy == UnknownBytes::MatchVendor)
            std::printf("  policy:  record 0x%02zx..0x%02zx will additionally be sent as 00,\n"
                        "           matching the vendor. --unknown-bytes=preserve keeps\n"
                        "           whatever the device reports there.\n",
                        kRecordUnknownFirst, kRecordUnknownLast);
        std::puts("Exactly one byte changes for this field. Re-run with --yes.");
        return 2;
    }

    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);

    // Everything §4.1 requires happens inside this one call: read, validate,
    // read-modify-write, self-check the frame before it goes out, write, read
    // back, verify the DATA rather than the acknowledgement.
    SetOutcome o = s.set(*f, encoded);

    if (!o.before.empty())
        warnIfActiveStageWouldBeOutOfRange(o.before, *f, v);

    if (o.result == Result::AlreadySet) {
        std::printf("%s is already 0x%02x. Nothing to write.\n",
                    f->name, o.intendedValue);
        return 0;
    }
    if (o.result != Result::Ok) {
        explain(o.result);
        if (o.result == Result::VerifyMismatch && !o.after.empty()) {
            const std::size_t at = kPayloadOffset + f->recordOffset;
            std::printf("    record 0x%02zx is 0x%02x, not the 0x%02x we sent.\n",
                        f->recordOffset, o.after[at], o.intendedValue);
            printDiff(o.changed, "before", "after");
        }
        return rcFor(o.result);
    }

    const int changed = printDiff(o.changed, "before", "after");
    if (changed != static_cast<int>(o.weChanged.size()))
        std::printf("\nNOTE: %d bytes changed on the device; we sent %zu difference(s).\n"
                    "      The extra ones are the device's own doing. Worth reading\n"
                    "      before trusting this field.\n",
                    changed, o.weChanged.size());
    std::printf("\n%s = %ld confirmed on the device.\n", f->name, v);
    return 0;
}

int cmdRestore(const std::string& path, bool verbose, bool yes, UnknownBytes policy) {
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
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);

    RestoreOutcome o = s.restore(want);

    if (!o.before.empty())
        printDiff(o.incoming, "on the device now", path.c_str());

    // The one place we knowingly put different bytes on the wire than the file
    // holds. Announced, because a divergence from the only writer observed to
    // work must never be silent.
    if (o.wrote) {
        bool differs = false;
        for (std::size_t k = kRecordUnknownFirst; k <= kRecordUnknownLast; ++k)
            if (o.sent[kPayloadOffset + k] != want[kPayloadOffset + k]) differs = true;
        if (differs) {
            std::printf("\nNOTE: record 0x%02zx..0x%02zx are being sent as",
                        kRecordUnknownFirst, kRecordUnknownLast);
            for (std::size_t k = kRecordUnknownFirst; k <= kRecordUnknownLast; ++k)
                std::printf(" %02x", o.sent[kPayloadOffset + k]);
            std::printf(", not the");
            for (std::size_t k = kRecordUnknownFirst; k <= kRecordUnknownLast; ++k)
                std::printf(" %02x", want[kPayloadOffset + k]);
            std::printf(" in the file.\n      Policy: %s (config-protocol.md §7.4).\n",
                        describe(policy));
        }
    }

    if (o.result != Result::Ok) {
        explain(o.result);
        if (o.result == Result::VerifyMismatch)
            printDiff(o.remaining, "what we sent", "what came back");
        return rcFor(o.result);
    }
    std::puts("\nverified: the device holds exactly what was sent.");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false, yes = false;
    std::string save;
    UnknownBytes policy = kDefaultUnknownBytes;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "--yes") yes = true;
        else if (a == "--save" && i + 1 < argc) save = argv[++i];
        else if (a.rfind("--unknown-bytes=", 0) == 0) {
            // No silent fallback. A mistyped policy would otherwise choose one
            // of two different byte streams without saying so.
            if (!parseUnknownBytes(a.substr(16).c_str(), policy)) {
                std::printf("`%s` is not a policy. Use --unknown-bytes=preserve"
                            " or --unknown-bytes=vendor.\n", a.substr(16).c_str());
                return 2;
            }
        }
        else args.push_back(a);
    }
    if (args.empty()) { usage(); return 0; }
    const std::string& cmd = args[0];
    if (cmd == "devices") return cmdDevices();
    if (cmd == "read")    return cmdRead(verbose, save, policy);
    if (cmd == "info")    return cmdInfo(verbose);
    if (cmd == "factory-reset") return cmdFactoryReset(verbose, yes, policy);
    if (cmd == "restore" && args.size() > 1) return cmdRestore(args[1], verbose, yes, policy);
    if (cmd == "diff" && args.size() > 2)    return cmdDiff(args[1], args[2]);
    if (cmd == "set" && args.size() > 2)
        return cmdSet(args[1], args[2], verbose, yes, policy);
    if (cmd == "encode" && args.size() == 5)
        return cmdEncode(args[1], args[2], args[3], args[4], policy);
    if (cmd == "set") { listSettable(); return 2; }
    usage();
    return 1;
}
