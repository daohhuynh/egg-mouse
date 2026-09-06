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
#include "egg/RecordVault.h"
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
                           kReportLarge, "A1 12 read settings",
                           configDelayMs(cfg::kReadRequest));
    }

    Reply writeRecord(const std::vector<std::uint8_t>& frame) override {
        // The frame arrives complete. This function must not touch it: the
        // session self-checked those exact bytes, and anything altered here
        // would be bytes nothing checked.
        return t_.exchange(frame, kReportSmall, "A0 11 write settings",
                           configDelayMs(cfg::kWriteSettings));
    }

    Reply factoryReset() override {
        // config-protocol.md §7.4: FUN_00404720 builds a 64-byte frame carrying
        // nothing but the report id and 0x13, and sends it. The device composes
        // its own defaults; no host-side defaults blob exists anywhere.
        // AND THE 1100 ms. §7.4 gave us the frame; 0x4047af gives us the
        // wait, and until 2026-09-05 we implemented only the first half. The
        // vendor sleeps 1100 ms before its ONE status read. We were reading at
        // 100 ms and had abandoned the device by 1100 -- our entire poll window
        // closed exactly where the vendor's first read lands.
        return t_.exchange(Transport::frame(kReportSmall, cfg::kFactoryReset),
                           kReportSmall, "A1 13 factory reset",
                           configDelayMs(cfg::kFactoryReset));
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
      "  egg-config frames             offline: the exact bytes of every fixed\n"
      "                                command frame. Sends nothing.\n"
      "\n"
      "  egg-config factory-reset --yes    A1 13. restores the DEVICE's own\n"
      "                                    defaults. loses all settings.\n"
      "  egg-config restore F --yes        write a saved record back (A0 11),\n"
      "                                    then read it back and verify\n"
      "  egg-config set FIELD VALUE --yes  change ONE derived field\n"
      "  egg-config set                    list the settable fields\n"
      "  egg-config map BUTTON ACTION --yes   rebind one button\n"
      "  egg-config map                    list the buttons and actions\n"
      "  egg-config cpi N X [Y] --yes      set CPI stage N (1-4). Y defaults\n"
      "                                    to X. Legal: 10..10000 by 10, then\n"
      "                                    10050..30000 by 50 (§7.19).\n"
      "  egg-config cpi                    list the stages and the grid\n"
      "  egg-config handedness left|right --yes   swap the primary click.\n"
      "                                    NOT a flag: it MOVES your mapping\n"
      "                                    between entries 0 and 1 (§7.20), so\n"
      "                                    it reads the record before deciding.\n"
      "  egg-config multiclick BUTTON MODE [N] --yes\n"
      "                                    per-button click filter. MODE is\n"
      "                                    off (N = 0..25), gx-speed or\n"
      "                                    gx-safe -- the last two on the LEFT\n"
      "                                    and RIGHT buttons only (§7.22).\n"
      "  egg-config encode F V IN OUT      offline: apply one field to a saved\n"
      "                                    record and write the result. Sends\n"
      "                                    nothing and needs no device.\n"
      "  egg-config dryrun REC [F V]       offline: print the EXACT 1041-byte\n"
      "                                    frame that `set F V` -- or `restore`,\n"
      "                                    with no field -- would put on the\n"
      "                                    wire, and the diff against REC.\n"
      "\n"
      "  -v                        hex-dump every frame\n"
      "  --vault F                 where the known-good record is kept.\n"
      "                            Default ~/.egg-mouse-known-good.bin. The\n"
      "                            FIRST plausible record read on this machine\n"
      "                            is written there automatically and is never\n"
      "                            overwritten (§4.1).\n"
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
void explain(Result r, const char* refusal = nullptr) {
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
        case Result::RefusedCapability:
            // NOT an error in us and not a failure of the device. The device
            // told us it is a variant this field's meaning was not derived for,
            // and §7.25 says the write would be well formed and wrong. The
            // reason is printed rather than summarised: a refusal a person
            // cannot audit is one they will work around.
            std::puts("REFUSED, and nothing was written. The read succeeded; the\n"
                      "device is fine. This field is not safe to set on it:");
            if (refusal) std::printf("\n    %s\n", refusal);
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
        case Result::RefusedCapability: return 8;
        case Result::WriteRejected:    return 5;
        case Result::VerifyReadFailed: return 6;
        case Result::VerifyMismatch:   return 7;
    }
    return 1;
}

// A successful read-back proves the device holds what we sent RIGHT NOW. It
// does not prove the setting survives a power cycle, and the capture run says
// plainly that it may not -- so the tool says so too rather than letting
// "confirmed" carry a weight it has not earned.
//
// [O] windows-run, chronological order (which is NOT the file numbering -- 07
// runs before 06): of the six session gaps with a settings read on both sides,
// FIVE showed the vendor's own writes gone, the record back at firmware
// defaults byte for byte. The one survivor, 04-buttons, is the shortest gap at
// 124 s; the others are 231-5501 s. The device's USB address stayed 4 across
// every one of those captures, so no re-enumeration is visible in the data.
// [G] why. Idle reload from non-volatile storage fits; so does something in the
// gaps, which are outside every capture. No persist or commit command has been
// found in the four config binaries (§7.1: cmdscan over ALL of .text finds
// exactly A1 12, A0 11, A1 02, A1 13 and no others).
void noteOnPersistence() {
    std::puts(
      "\nThis was verified by reading the device back, so it is what the mouse\n"
      "holds now. A read-back cannot say what survives later.\n"
      "TWO writes have survived a power cycle intact, both 2026-09-05, both\n"
      "0 of 1024 payload bytes changed (config-wire-observed.md §5):\n"
      "  unplugged    8.6 s  -> identical\n"
      "  unplugged 1706.8 s  -> identical   (28.4 minutes)\n"
      "The long one is what matters: 1707 s exceeds FOUR of the five capture\n"
      "gaps across which the vendor's own settings vanished, so \"the record\n"
      "decays off power\" cannot explain those four. Power loss does not do\n"
      "this.\n"
      "What emptied the vendor's records is ANSWERED as of 2026-09-05: the\n"
      "tester reset settings to defaults between capture sections, for a\n"
      "clean per-section baseline. That accounts for the five that emptied,\n"
      "and \"I may have missed one or two\" accounts for the single gap that\n"
      "held. Stated as recollection, not a log -- but it needs no device\n"
      "mechanism, and no device mechanism was ever found.\n"
      "So there is no known way this record is lost while the mouse sits\n"
      "unplugged. If it matters to you, the check is still cheap: unplug,\n"
      "replug and `egg-config read`.");
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
    // BYTE 0 IS NOT VALIDATED, AND MUST NOT BE -- the same rule ConfigRecord's
    // plausible() states, for the same reason, and this is where it was missed.
    //
    // This used to require out[0] == kReportLarge (0xA0). No platform puts 0xA0
    // there: the device never sends the report-id slot at all, so on Windows the
    // vendor's own 0xA1 survives in it and on macOS hidapi leaves 0x00. Every
    // file this tool writes therefore begins 00 01 on macOS -- including
    // ~/.egg-mouse-known-good.bin, the §4.1 vault, and egg-before-reset.bin.
    //
    // So the check rejected the tool's OWN undo files. `factory-reset` would
    // read, save the undo, wipe the settings, and then `restore` would refuse
    // to load either file back. The undo existed, was written correctly, and
    // could not be used. Found 2026-09-05 by an adversarial audit run before
    // the first write reached the device; confirmed by running `egg-config diff`
    // against the real vault, which refused it.
    //
    // plausible() is the right gate and is strictly stronger: it checks the
    // length and that the payload is not uniform, which is what a truncated or
    // zeroed save from a failed session actually looks like.
    if (!plausible(out)) {
        std::printf("%s is not a plausible settings record (1041 bytes, and a\n"
                    "payload that is not all one byte). Refusing.\n", path.c_str());
        return false;
    }
    return true;
}

// Returns true only when the bytes are ON DISK at the size we asked for.
//
// It used to return `bool(f)` with no close() and no re-stat. 1041 bytes fit
// inside libc++'s filebuf, so at the moment that boolean was computed NOTHING
// had reached the filesystem -- measured: stat() reports size 0 there, and the
// bytes appear only when the stream is destroyed after the return. Only an
// open() failure was caught; ENOSPC, EDQUOT and EIO at flush time all returned
// true.
//
// That mattered because cmdFactoryReset uses this value as the gate on the
// destructive command -- "could not save the pre-reset record ... NOT
// RESETTING" -- so the reset could proceed on a zero-byte undo. RecordVault
// already did the right thing three files away, with the comment "Confirm from
// the filesystem rather than from the stream's own opinion"; §4.1's rule about
// never letting a reported success stand in for verified data applies to our
// own disk writes too. Found by adversarial audit, 2026-09-05.
bool saveRecord(const std::string& path, const std::vector<std::uint8_t>& r) {
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { std::printf("could not write %s\n", path.c_str()); return false; }
        f.write(reinterpret_cast<const char*>(r.data()),
                static_cast<std::streamsize>(r.size()));
        f.close();
        if (!f) { std::printf("could not finish writing %s\n", path.c_str()); return false; }
    }
    std::ifstream back(path, std::ios::binary | std::ios::ate);
    if (!back) { std::printf("wrote %s but cannot reopen it\n", path.c_str()); return false; }
    const std::streamoff n = back.tellg();
    if (n != static_cast<std::streamoff>(r.size())) {
        std::printf("wrote %s but it came back %lld bytes, not %zu\n",
                    path.c_str(), static_cast<long long>(n), r.size());
        return false;
    }
    return true;
}

// §4.1: "Save a known-good blob to disk on first connect."
//
// The home directory, not the working directory, and deliberately: a blob whose
// location depends on where you happened to be standing is not an undo you can
// find in six months. One well-known path, written once, never overwritten.
std::string defaultVaultPath() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.egg-mouse-known-good.bin"
                : std::string(".egg-mouse-known-good.bin");
}

// Say what the vault did, once, after a session that read something. Silence
// would make the difference between "we have an undo" and "we do not" invisible
// -- and that difference is the whole reason the file exists.
void reportVault(const ConfigSession& s, const FileRecordVault& v) {
    if (s.justStored()) {
        std::printf("\nsaved the first known-good record to %s (§4.1).\n"
                    "This file is never overwritten. `egg-config restore` it if\n"
                    "settings are ever lost -- including by a firmware flash,\n"
                    "since A1 13 is both Factory Reset and the updater's last\n"
                    "command and we cannot yet tell whether it means the same in\n"
                    "both places.\n", v.where().c_str());
    } else if (!v.holds()) {
        std::printf("\n*** NO known-good record is saved");
        if (!v.lastError().empty())
            std::printf(": %s", v.lastError().c_str());
        std::printf(".\n    Settings lost from here would not be recoverable from disk.\n");
    }
}

// ---------------------------------------------------------------------------
// Read-only commands
// ---------------------------------------------------------------------------
// §7.25. Which derived fields is THIS device's record not safe to set? The
// answer is in the record, so anything that already holds one can answer it --
// which is the whole point of an off-wire gate (§4.2). Printed in a stable
// shape so `EGGApp/Commands.swift` can parse it without knowing a single record
// offset; a GUI that hard-coded 0x6f would be a second copy of the table, and
// the file's own header says why that is the bug to avoid.
void reportGates(const std::uint8_t* record) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < kSettableCount; ++i) {
        const char* why = capabilityRefusal(kSettable[i], record);
        if (!why) continue;
        if (n++ == 0)
            std::puts("\nfields this device will NOT accept:");
        // TWO spaces after the padded name, not one. `%-16s %s` looks fine
        // until a field name is longer than the pad -- `motion-jitter-filter`
        // is twenty characters -- and then the separator collapses to a single
        // space and EGGApp's parser, which splits on the first double space,
        // reads the whole line as a name with no reason. Found by checking the
        // longest name in the table rather than by seeing it happen.
        std::printf("  %-20s  %s\n", kSettable[i].name, why);
    }
    if (n == 0)
        std::puts("\nfields this device will NOT accept: none -- "
                  "every derived field is settable on it.");
}

int cmdRead(bool verbose, const std::string& savePath, UnknownBytes policy,
            const std::string& vaultPath) {
    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;

    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

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
    reportGates(rec.data());
    reportVault(s, vault);
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
    Reply r = t.exchange(req, kReportSmall, "A1 02 small query",
                         configDelayMs(cfg::kSmallQuery));
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

// Every fixed-length command frame this tool can put on the wire, hex, one per
// line. The exact counterpart of `egg-flash stream`, and it exists for the same
// reason: so the bytes can be diffed against a capture of Endgame's own tool
// WITHOUT a device, without a write, and without trusting our own decoder.
//
// This closes a real gap. `test_golden_vendor.py` pins the flasher's whole
// 135-frame stream against 08-flash.pcapng and `test_config_replay.py` pins the
// A0 11 write frames against 73 captured writes -- but until now NOTHING pinned
// the three short commands, and `07-factory-reset.pcapng` was referenced by no
// test at all. A1 13 is the one command in this tool that destroys user data,
// and it was the least checked frame in the repo.
//
// Nothing here touches a device or reads a file, so it is safe to run and safe
// to pipe. Output goes to stdout alone (§4.3's golden-diff argument, same as
// the flasher's `stream`).
int cmdFrames() {
    struct Cmd { std::uint8_t id; std::uint8_t op; const char* what; };
    static const Cmd kAll[] = {
        { kReportSmall, cfg::kSmallQuery,   "A1 02 small query"    },
        { kReportSmall, cfg::kReadRequest,  "A1 12 read settings"  },
        { kReportSmall, cfg::kFactoryReset, "A1 13 factory reset"  },
    };
    for (const Cmd& c : kAll) {
        const std::vector<std::uint8_t> f = Transport::frame(c.id, c.op);
        std::printf("%-22s ", c.what);
        for (std::uint8_t b : f) std::printf("%02x", b);
        // The delay before the first status read is as much a part of what
        // goes on the wire as the bytes are, and it is the half we got wrong.
        std::printf(" wait=%ums\n", configDelayMs(c.op));
    }
    std::printf("%-22s %s wait=%ums\n", "A0 11 write settings",
                "(payload depends on the record; see `dryrun`)",
                configDelayMs(cfg::kWriteSettings));
    // A0 11 is deliberately absent: it carries a 1024-byte payload that depends
    // on what the device just returned, so it has no fixed form. `dryrun` emits
    // it, against a record, and that is what test_config_replay.py checks.
    return 0;
}

// ---------------------------------------------------------------------------
// Writing commands
// ---------------------------------------------------------------------------
int cmdFactoryReset(bool verbose, bool yes, UnknownBytes policy,
                    const std::string& vaultPath) {
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
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

    // §4.1: "Never write after a failed read." A reset is still a write, and it
    // is the one write whose undo is the file we are about to save.
    std::vector<std::uint8_t> before;
    Result r = Result::Ok;
    if (!s.read(before, r)) { explain(r); return rcFor(r); }
    reportVault(s, vault);

    // NEVER OVERWRITE AN EARLIER PRE-RESET RECORD. This used to be a plain
    // truncating write to a fixed name, so a second factory-reset -- the
    // natural response to a failure message, or just wanting to be sure --
    // replaced the real pre-reset record with the already-defaulted one, under
    // a filename saying otherwise. The failure text then pointed the user
    // straight at it. RecordVault has had the right guard all along; this path
    // did not. Found by adversarial audit, 2026-09-05.
    std::string undoPath = "egg-before-reset.bin";
    for (int n = 2; n < 1000; ++n) {
        std::ifstream probe(undoPath, std::ios::binary);
        if (!probe) break;
        char buf[64];
        std::snprintf(buf, sizeof buf, "egg-before-reset-%d.bin", n);
        undoPath = buf;
    }
    std::printf("read ok. saving the pre-reset record to %s\n", undoPath.c_str());
    if (!saveRecord(undoPath, before)) {
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
        std::printf("the reset was acknowledged but the read-back failed, so what the\n"
                    "device now holds is UNKNOWN. %s still has the old record;\n"
                    "  egg-config restore %s --yes\n"
                    "puts it back once the device answers again. The vault at %s\n"
                    "is the older, never-overwritten copy if you need it instead.\n",
                    undoPath.c_str(), undoPath.c_str(), vaultPath.c_str());
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

    // §7.25 again. `encode` produces a file that `restore` will later put on the
    // wire, so a gated field written here reaches the device by a longer road,
    // not a safer one.
    if (const char* why = capabilityRefusal(*f, before.data())) {
        std::printf("REFUSED, and %s was not written. On a record like %s,\n"
                    "%s is not safe to set:\n\n    %s\n",
                    outPath.c_str(), inPath.c_str(), f->name, why);
        return 8;
    }

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

// §4.3: "Dry-run mode emitting the exact byte stream that would be sent."
// egg-flash has had this from the start; egg-config had only `encode`, which
// writes a RECORD file -- the payload, not the frame. The header is where the
// report id and the command live, and a tool that can show you 1024 of the 1041
// bytes it would send is showing you the wrong 1024 if the header is wrong.
//
// Sends nothing, opens no device, and takes the record from a file so it is
// reproducible and diffable.
int cmdDryRun(const std::string& recordPath, const std::string& field,
              const std::string& value, UnknownBytes policy) {
    std::vector<std::uint8_t> before;
    if (!loadRecord(recordPath, before)) return 1;
    if (!plausible(before)) {
        std::printf("%s is not a structurally plausible record. Refusing, for the\n"
                    "same reason the device path would: a frame built from a bad\n"
                    "read is exactly what §4.1 forbids.\n", recordPath.c_str());
        return 1;
    }

    std::vector<std::uint8_t> frame;
    const Settable* f = nullptr;
    if (field.empty()) {
        frame = ConfigSession::buildRestoreFrame(before, policy);
        std::printf("restore %s\n", recordPath.c_str());
    } else {
        long v = 0;
        std::uint8_t encoded = 0;
        f = resolve(field, value, v, encoded, false);
        if (!f) return 2;
        // §7.25's gate, here too. A dry run prints the approval token §4.2c
        // binds a write to, so producing one for a field `set` will refuse
        // would mean handing someone an approval for something that cannot
        // happen. Refuse in the same place the device path refuses.
        if (const char* why = capabilityRefusal(*f, before.data())) {
            std::printf("REFUSED. %s could not be set on a device holding this\n"
                        "record, so no frame and no token are produced:\n\n"
                        "    %s\n", f->name, why);
            return 8;
        }
        warnIfActiveStageWouldBeOutOfRange(before, *f, v);
        frame = ConfigSession::buildFrame(before, *f, encoded, policy);
        std::printf("set %s = %s   (record 0x%02zx", f->name, value.c_str(),
                    f->recordOffset);
        if (f->mask != 0xFF) std::printf(" & 0x%02x", f->mask);
        std::printf(")\n  derived: %s\n", f->cite);
    }
    if (frame.size() != kLargeLen) {
        std::puts("no frame was produced. Nothing would be sent.");
        return 1;
    }

    std::printf("policy   %s\n", describe(policy));
    std::printf("frame    report 0x%02x, command 0x%02x, %zu bytes\n",
                frame[0], frame[kCmdOffset], frame.size());

    // Every byte our frame differs from the record we started from. This is the
    // whole point: not "here is a frame" but "here is exactly what we changed
    // and nothing else moved".
    const std::vector<ByteChange> d = diff(before, frame);
    printDiff(d, recordPath.c_str(), "the frame we would send");
    if (f) {
        bool sane = false;
        for (const ByteChange& c : d)
            if (c.recordOffset == f->recordOffset) sane = true;
        if (!sane && !d.empty())
            std::puts("  NOTE: the field's own byte is not in that list, which means\n"
                      "        it already holds the value asked for.");
    }

    std::puts("\nthe exact bytes, as they would go on the wire:");
    for (std::size_t i = 0; i < frame.size(); i += 32) {
        std::printf("  %04zx  ", i);
        for (std::size_t k = i; k < i + 32 && k < frame.size(); ++k)
            std::printf("%02x", frame[k]);
        std::printf("\n");
    }
    std::puts("\nNothing was sent. No device was opened.");
    return 0;
}

void listButtons() {
    std::puts("Buttons (egg-config map <button> <action>):");
    for (std::size_t i = 0; i < kButtonSlotCount; ++i) {
        const ButtonSlot& b = kButtonSlots[i];
        std::printf("  %-12s entry %u, record 0x%02zx%s\n", b.name, b.index,
                    kButtonBlockFirst + kButtonEntryLen * b.index,
                    b.vendorExposes ? "" : "   [not offered -- see below]");
    }
    std::puts("\nActions:");
    const char* group = "";
    for (std::size_t i = 0; i < kButtonActionCount; ++i) {
        const ButtonAction& a = kButtonActions[i];
        if (std::strcmp(group, a.group) != 0) {
            group = a.group;
            std::printf("  %s:\n", group);
        }
        if (a.payload == ButtonPayload::FixedCpi)
            std::printf("    %-14s takes a CPI value, e.g. fixed-cpi:1600\n", a.name);
        else if (a.payload == ButtonPayload::Key)
            std::printf("    %-14s takes a key, e.g. key:a  key:ctrl+shift+a  key:f5\n", a.name);
        else
            std::printf("    %-14s %02x %02x\n", a.name, a.b0, a.b1);
    }
    std::puts("\nKeys: a-z, 0-9, f1-f12, kp0-kp9, enter escape backspace tab space");
    std::puts("      minus equal leftbracket rightbracket backslash semicolon quote");
    std::puts("      grave comma period slash capslock insert home pageup delete end");
    std::puts("      pagedown left right up down.  Modifiers: ctrl shift alt win.");
    for (std::size_t i = 0; i < kButtonSlotCount; ++i)
        if (!kButtonSlots[i].vendorExposes)
            std::printf("\n%s is NOT offered: %s.\n",
                        kButtonSlots[i].name, kButtonSlots[i].note);
}

// Parse "<action>" or "<action>:<arg>" into the pieces encodeButtonEntry wants.
bool parseAction(const std::string& spec, const ButtonAction*& act,
                 long& arg, std::uint8_t& mods) {
    act = nullptr; arg = 0; mods = 0;
    const std::size_t colon = spec.find(':');
    const std::string name = spec.substr(0, colon);
    const std::string rest = colon == std::string::npos ? "" : spec.substr(colon + 1);

    act = findButtonAction(name.c_str());
    if (!act) {
        std::printf("`%s` is not an action.\n\n", name.c_str());
        listButtons();
        return false;
    }
    if (act->payload == ButtonPayload::None) {
        if (!rest.empty()) {
            std::printf("%s takes no argument.\n", act->name);
            return false;
        }
        return true;
    }
    if (rest.empty()) {
        std::printf("%s needs an argument, e.g. %s\n", act->name,
                    act->payload == ButtonPayload::FixedCpi ? "fixed-cpi:1600"
                                                            : "key:ctrl+a");
        return false;
    }
    if (act->payload == ButtonPayload::FixedCpi) {
        char* end = nullptr;
        arg = std::strtol(rest.c_str(), &end, 10);
        if (end == rest.c_str() || (end && *end)) {
            std::printf("`%s` is not a number.\n", rest.c_str());
            return false;
        }
        return true;
    }
    // key:[mods+]name -- the last '+'-separated token is the key itself.
    const std::size_t plus = rest.rfind('+');
    const std::string keyName = plus == std::string::npos ? rest : rest.substr(plus + 1);
    const std::string modSpec = plus == std::string::npos ? "" : rest.substr(0, plus);
    if (!hidModifiers(modSpec.c_str(), mods)) {
        std::printf("`%s` is not a modifier. Use ctrl, shift, alt, win.\n",
                    modSpec.c_str());
        return false;
    }
    std::uint8_t usage = 0;
    if (!hidKeycode(keyName.c_str(), usage)) {
        std::printf("`%s` is not a key this tool knows.\n\n", keyName.c_str());
        listButtons();
        return false;
    }
    arg = usage;
    return true;
}

int cmdMap(const std::string& button, const std::string& spec,
           bool verbose, bool yes, UnknownBytes policy,
           const std::string& vaultPath) {
    const ButtonSlot* slot = findButtonSlot(button.c_str());
    if (!slot) {
        std::printf("`%s` is not a button.\n\n", button.c_str());
        listButtons();
        return 2;
    }
    if (!slot->vendorExposes) {
        // Not a [G] byte -- §7.17 gives every action type. This is the OTHER
        // half of §1.3: knowing what a byte means is not the same as knowing
        // how the firmware reacts to it, and neither the vendor's own tool nor
        // any capture has ever moved these two entries.
        std::printf("egg-config will not remap `%s`.\n  %s.\n",
                    slot->name, slot->note);
        std::puts("The bytes are understood; what the firmware does with a change"
                  " here is not.");
        return 2;
    }

    const ButtonAction* act = nullptr;
    long arg = 0;
    std::uint8_t mods = 0;
    if (!parseAction(spec, act, arg, mods)) return 2;

    const std::size_t at = kButtonBlockFirst + kButtonEntryLen * slot->index;

    if (!yes) {
        std::uint8_t preview[kButtonEntryLen];
        const char* err = nullptr;
        // +6 shown as ?? because it is read from the device and copied through.
        if (!encodeButtonEntry(*act, arg, mods, 0x00, preview, &err)) {
            std::printf("%s\n", err ? err : "cannot encode that");
            return 2;
        }
        std::printf("map %s = %s would write record 0x%02zx..0x%02zx:\n",
                    slot->name, spec.c_str(), at, at + kButtonEntryLen - 1);
        std::printf("  %02x %02x %02x %02x %02x %02x ??\n", preview[0], preview[1],
                    preview[2], preview[3], preview[4], preview[5]);
        std::printf("  +6 is the multiclick filter and is preserved exactly as read.\n");
        std::printf("  derived: %s\n", act->cite);
        std::puts("Re-run with --yes.");
        return 2;
    }

    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

    // Read first, because +6 belongs to the device and we are only borrowing
    // the other six bytes.
    std::vector<std::uint8_t> before;
    Result rr = Result::Ok;
    if (!s.read(before, rr)) { reportVault(s, vault); explain(rr); return rcFor(rr); }
    reportVault(s, vault);

    std::uint8_t entry[kButtonEntryLen];
    const char* err = nullptr;
    if (!encodeButtonEntry(*act, arg, mods,
                           before[kPayloadOffset + at + 6], entry, &err)) {
        std::printf("%s\n", err ? err : "cannot encode that");
        return 2;
    }

    SetOutcome o = s.setRun(at, entry, kButtonEntryLen);
    if (o.result == Result::AlreadySet) {
        std::printf("%s is already mapped to %s. Nothing to write.\n",
                    slot->name, spec.c_str());
        return 0;
    }
    if (o.result != Result::Ok) {
        explain(o.result, o.refusal);
        if (!o.changed.empty()) printDiff(o.changed, "before", "after");
        return rcFor(o.result);
    }
    std::printf("%s -> %s. Verified on the device.\n", slot->name, spec.c_str());
    printDiff(o.changed, "before", "after");
    return 0;
}

void listCpi() {
    std::puts("egg-config cpi STAGE VALUE [Y]   -- set one CPI stage\n");
    std::puts("  STAGE  1, 2, 3 or 4. Stage numbering matches the vendor's");
    std::puts("         `CPI 1`..`CPI 4` boxes, which is NOT the same thing as");
    std::puts("         `cpi-stage`: that field says which one is ACTIVE, and");
    std::puts("         `cpi-levels` says how many of them the loop cycles.");
    std::printf("  VALUE  %ld..%ld. Step 10 up to %ld, step 50 above it.\n",
                kCpiMin, kCpiMax, kCpiFineLimit);
    std::puts("  Y      optional. Omit it and Y is set equal to X, which is");
    std::puts("         what the vendor's box does until `X/Y Settings` is");
    std::puts("         ticked. The X!=Y flag byte is computed, never typed.\n");
    std::puts("  derived: config-protocol.md §7.19, cfg107 0x40d880-0x40d93a");
    std::puts("           (the vendor's own clamp-and-round), corroborated by");
    std::puts("           the trackbar map at 0x40c2f0-0x40c312.");
}

int cmdCpi(const std::string& stageArg, const std::string& xArg,
           const std::string& yArg, bool verbose, bool yes,
           UnknownBytes policy, const std::string& vaultPath) {
    char* end = nullptr;
    const long stage = std::strtol(stageArg.c_str(), &end, 10);
    if (!end || *end || stage < 1 ||
        stage > static_cast<long>(kCpiStageCount)) {
        std::printf("`%s` is not a CPI stage.\n\n", stageArg.c_str());
        listCpi();
        return 2;
    }
    end = nullptr;
    const long x = std::strtol(xArg.c_str(), &end, 10);
    if (!end || *end) {
        std::printf("`%s` is not a number.\n\n", xArg.c_str());
        listCpi();
        return 2;
    }
    long y = x;
    if (!yArg.empty()) {
        end = nullptr;
        y = std::strtol(yArg.c_str(), &end, 10);
        if (!end || *end) {
            std::printf("`%s` is not a number.\n\n", yArg.c_str());
            listCpi();
            return 2;
        }
    }

    std::uint8_t entry[kCpiEntryLen];
    const char* err = nullptr;
    if (!encodeCpiStageEntry(x, y, entry, &err)) {
        // Say what the vendor WOULD have stored. Refusing without that turns a
        // one-keystroke typo into a guessing game about which nearby number is
        // legal, and the whole point of refusing is that we will not choose for
        // the user.
        std::printf("%s.\n", err ? err : "cannot encode that");
        long said = -1;
        for (long v : {x, y}) {
            const long n = normaliseCpi(v);
            if (v != n && v != said) {
                std::printf("  %ld is not on the grid; the vendor's own tool"
                            " would store %ld.\n", v, n);
                said = v;
            }
        }
        std::printf("  Legal values: %ld..%ld in steps of 10, then %ld..%ld in"
                    " steps of 50.\n", kCpiMin, kCpiFineLimit,
                    kCpiFineLimit + 50, kCpiMax);
        return 2;
    }

    const std::size_t at =
        kCpiBlockFirst + kCpiEntryLen * static_cast<std::size_t>(stage - 1);

    if (!yes) {
        std::printf("cpi %ld = %ld x %ld would write record 0x%02zx..0x%02zx:\n",
                    stage, x, y, at, at + kCpiEntryLen - 1);
        std::printf("  %02x %02x %02x %02x %02x\n", entry[0], entry[1], entry[2],
                    entry[3], entry[4]);
        std::printf("  [0] is the X!=Y flag, computed: %s\n",
                    entry[0] ? "X and Y differ" : "X and Y are equal");
        if (stage == 4 && x != y)
            std::puts("  NOTE: for stage 4 ONLY, cfg107 computes this flag from"
                      " stage 3's boxes\n"
                      "  (0x40edde/0x40ede4, §7.8 -- a vendor bug). We compute it"
                      " from stage 4,\n"
                      "  so this byte can legitimately differ from what the"
                      " vendor would send.");
        std::puts("  derived: config-protocol.md §7.19, cfg107 0x40d880-0x40d93a");
        std::puts("Re-run with --yes.");
        return 2;
    }

    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

    std::vector<std::uint8_t> before;
    Result rr = Result::Ok;
    if (!s.read(before, rr)) { reportVault(s, vault); explain(rr); return rcFor(rr); }
    reportVault(s, vault);

    SetOutcome o = s.setRun(at, entry, kCpiEntryLen);
    if (o.result == Result::AlreadySet) {
        std::printf("CPI %ld is already %ld x %ld. Nothing to write.\n",
                    stage, x, y);
        return 0;
    }
    if (o.result != Result::Ok) {
        explain(o.result, o.refusal);
        if (!o.changed.empty()) printDiff(o.changed, "before", "after");
        return rcFor(o.result);
    }
    std::printf("CPI %ld -> %ld x %ld. Verified on the device.\n", stage, x, y);
    printDiff(o.changed, "before", "after");
    return 0;
}

// ---------------------------------------------------------------------------
// handedness -- config-protocol.md §7.20
// ---------------------------------------------------------------------------
// The ONE verb here whose dry run has to talk to the device, and the reason is
// worth stating rather than hiding: left-handed mode is a MOVE, not a flag, so
// what it would write depends on which entry currently holds the user's
// assignment. There is no plan to print without reading first.
//
// §4.2a's gate, answered: (1) it answers "which state is the record in, and
// what would change" -- without it the dry run can only describe a rule, not a
// plan; (2) it cannot be answered off-device for the LIVE record, and
// `--from FILE` is provided for when a saved one will do; (3) no hardware
// alternative exists; (4) nothing is written, and an `A1 12` read is the exact
// round trip `egg-config read` already performs and that the write path would
// perform first regardless. **The dry run adds no frame the apply would not
// already have sent.**
// Seven bytes as hex, for showing a button entry either side of a change.
static std::string hex7(const std::uint8_t* e) {
    char buf[3 * kButtonEntryLen + 1];
    for (std::size_t i = 0; i < kButtonEntryLen; ++i)
        std::snprintf(buf + 3 * i, 4, "%02x ", e[i]);
    buf[3 * kButtonEntryLen - 1] = '\0';
    return std::string(buf);
}

void listHandedness() {
    std::puts("egg-config handedness left|right   -- swap the primary click\n");
    std::puts("  Left-handed mode is NOT a flag byte. cfg107 moves your button\n"
              "  assignment between record entries 0 and 1 and hard-sets\n"
              "  whichever one is now the primary click to left-click.\n");
    std::puts("  Because it is a move, this tool reads the record first and\n"
              "  refuses if it is in neither state -- that would mean guessing\n"
              "  which entry holds your mapping, and guessing wrong moves it.\n");
    std::puts("  --from FILE   decide against a saved record instead of the\n"
              "                device. Sends nothing at all.\n");
    std::puts("  derived: config-protocol.md §7.20, cfg107 0x408b00");
}

static const char* handednessName(Handedness h) {
    switch (h) {
        case Handedness::Right: return "right";
        case Handedness::Left:  return "left";
        default:                return "neither";
    }
}

int cmdHandedness(const std::string& want, const std::string& fromFile,
                  bool verbose, bool yes, UnknownBytes policy,
                  const std::string& vaultPath) {
    Handedness target = Handedness::Unknown;
    if (want == "left")  target = Handedness::Left;
    if (want == "right") target = Handedness::Right;
    if (target == Handedness::Unknown) {
        std::printf("`%s` is not a handedness.\n\n", want.c_str());
        listHandedness();
        return 2;
    }

    std::vector<std::uint8_t> before;
    Log log(verbose);
    std::unique_ptr<Device> dev;
    std::unique_ptr<Transport> t;
    std::unique_ptr<DeviceConfigLink> link;
    std::unique_ptr<ConfigSession> s;
    FileRecordVault vault(vaultPath);

    if (!fromFile.empty()) {
        if (!loadRecord(fromFile, before)) return 2;
    } else {
        dev = Device::open(kProductIdApplication, log);
        if (!dev) return 1;
        t = std::make_unique<Transport>(*dev, kConfigBusy, log);
        link = std::make_unique<DeviceConfigLink>(*t, log);
        s = std::make_unique<ConfigSession>(*link, policy, &log);
        s->setVault(&vault);
        Result rr = Result::Ok;
        if (!s->read(before, rr)) {
            reportVault(*s, vault); explain(rr); return rcFor(rr);
        }
        reportVault(*s, vault);
    }

    const std::uint8_t* rec = before.data() + kPayloadOffset;
    const Handedness now = readHandedness(rec);
    std::uint8_t out[2 * kButtonEntryLen];
    bool changed = false;
    const char* err = nullptr;
    if (!applyHandedness(rec, target, out, changed, &err)) {
        std::printf("%s.\n", err ? err : "cannot do that");
        std::printf("  entry 0 (left)  is %s\n", hex7(rec + kButtonBlockFirst).c_str());
        std::printf("  entry 1 (right) is %s\n",
                    hex7(rec + kButtonBlockFirst + kButtonEntryLen).c_str());
        std::puts("  One of them has to be 00 01 00 00 00 00 (left-click).\n"
                  "  `egg-config map` can put a button back, or `factory-reset`.");
        return 2;
    }
    if (!changed) {
        std::printf("Already %s-handed. Nothing to write.\n", want.c_str());
        return 0;
    }

    if (!yes) {
        std::printf("handedness %s -> %s would write record 0x%02zx..0x%02zx:\n",
                    handednessName(now), want.c_str(), kButtonBlockFirst,
                    kButtonBlockFirst + 2 * kButtonEntryLen - 1);
        std::printf("  entry 0 (left)   %s  ->  %s\n",
                    hex7(rec + kButtonBlockFirst).c_str(), hex7(out).c_str());
        std::printf("  entry 1 (right)  %s  ->  %s\n",
                    hex7(rec + kButtonBlockFirst + kButtonEntryLen).c_str(),
                    hex7(out + kButtonEntryLen).c_str());
        std::puts("  Byte +6 is the multiclick filter (§7.22) and travels with\n"
                  "  its own button. cfg107's OFF path copies it in one\n"
                  "  direction instead; we swap both ways so a filter you set\n"
                  "  deliberately is not silently overwritten.");
        std::puts("  derived: config-protocol.md §7.20, cfg107 0x408b00");
        std::puts(fromFile.empty() ? "Re-run with --yes."
                                   : "This was decided offline; re-run without"
                                     " --from and with --yes to apply it.");
        return 2;
    }
    if (!fromFile.empty()) {
        std::puts("--from is offline only. Re-run without it to write.");
        return 2;
    }

    SetOutcome o = s->setRun(kButtonBlockFirst, out, 2 * kButtonEntryLen);
    if (o.result == Result::AlreadySet) {
        std::printf("Already %s-handed. Nothing to write.\n", want.c_str());
        return 0;
    }
    if (o.result != Result::Ok) {
        explain(o.result, o.refusal);
        if (!o.changed.empty()) printDiff(o.changed, "before", "after");
        return rcFor(o.result);
    }
    std::printf("Handedness -> %s. Verified on the device.\n", want.c_str());
    printDiff(o.changed, "before", "after");
    return 0;
}

// ---------------------------------------------------------------------------
// multiclick -- config-protocol.md §7.22
// ---------------------------------------------------------------------------
static const char* kMulticlickButtons[kMulticlickCount] = {
    "left", "right", "middle", "forward", "back"};

void listMulticlick() {
    std::puts("egg-config multiclick BUTTON MODE [VALUE]\n");
    std::puts("  BUTTON   left, right, middle, forward or back");
    std::puts("  MODE     off       VALUE is the filter, 0..25 (default 8)");
    std::puts("           gx-speed  LEFT and RIGHT only -- stores 0xf1");
    std::puts("           gx-safe   LEFT and RIGHT only -- stores 0xf0\n");
    std::puts("  One byte, two meanings, and the legal set is NOT the same for\n"
              "  every button: only left and right have an SPDT combo on the\n"
              "  vendor's page, so a GX mode on the other three would be a byte\n"
              "  its own software cannot produce (§1.3).\n");
    std::puts("  Record bytes:");
    for (std::size_t i = 0; i < kMulticlickCount; ++i)
        std::printf("    %-8s 0x%02zx%s\n", kMulticlickButtons[i],
                    kMulticlickFirst + kMulticlickStride * i,
                    i < 2 ? "   (has an SPDT combo)" : "");
    std::puts("\n  derived: config-protocol.md §7.22, cfg107 0x405f84 (range),\n"
              "           0x406645/0x406653 and 0x406705/0x406713 (the GX bytes)");
}

int cmdMulticlick(const std::string& buttonArg, const std::string& mode,
                  const std::string& valueArg, bool verbose, bool yes,
                  UnknownBytes policy, const std::string& vaultPath) {
    std::size_t button = kMulticlickCount;
    for (std::size_t i = 0; i < kMulticlickCount; ++i)
        if (buttonArg == kMulticlickButtons[i]) button = i;
    if (button == kMulticlickCount) {
        std::printf("`%s` has no multiclick filter.\n\n", buttonArg.c_str());
        listMulticlick();
        return 2;
    }
    long value = kMulticlickDefault;
    if (!valueArg.empty()) {
        char* end = nullptr;
        value = std::strtol(valueArg.c_str(), &end, 10);
        if (!end || *end) {
            std::printf("`%s` is not a number.\n\n", valueArg.c_str());
            listMulticlick();
            return 2;
        }
    }
    std::uint8_t byte = 0;
    const char* err = nullptr;
    if (!encodeMulticlick(button, mode.c_str(), value, byte, &err)) {
        std::printf("%s.\n\n", err ? err : "cannot encode that");
        listMulticlick();
        return 2;
    }
    const std::size_t at = kMulticlickFirst + kMulticlickStride * button;

    if (!yes) {
        // The suffix is built into a NAMED string first. Writing
        // `(" " + std::to_string(v)).c_str()` inline dangles: the temporary is
        // destroyed at the end of the full expression, before printf reads it.
        // It printed the right thing anyway on this build, which is exactly why
        // CMakeLists turns format mistakes into errors and why this one has to
        // be fixed on sight rather than when it misbehaves.
        const std::string suffix =
            (mode == "off") ? (" " + std::to_string(value)) : std::string();
        std::printf("multiclick %s %s%s would write 0x%02x to record 0x%02zx"
                    " (wire 0x%03zx).\n", kMulticlickButtons[button],
                    mode.c_str(), suffix.c_str(), byte, at, kPayloadOffset + at);
        std::puts("  This byte is SHARED: 0..25 is a filter value, 0xf1 is GX\n"
                  "  Speed and 0xf0 is GX Safe. Writing one erases the other.");
        std::puts("  derived: config-protocol.md §7.22, cfg107 0x405f84 /"
                  " 0x406645 / 0x406653");
        std::puts("Re-run with --yes.");
        return 2;
    }

    Log log(verbose);
    auto dev = Device::open(kProductIdApplication, log);
    if (!dev) return 1;
    Transport t(*dev, kConfigBusy, log);
    DeviceConfigLink link(t, log);
    ConfigSession s(link, policy, &log);
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

    std::vector<std::uint8_t> before;
    Result rr = Result::Ok;
    if (!s.read(before, rr)) { reportVault(s, vault); explain(rr); return rcFor(rr); }
    reportVault(s, vault);

    SetOutcome o = s.setRun(at, &byte, 1);
    if (o.result == Result::AlreadySet) {
        std::printf("%s multiclick is already that. Nothing to write.\n",
                    kMulticlickButtons[button]);
        return 0;
    }
    if (o.result != Result::Ok) {
        explain(o.result, o.refusal);
        if (!o.changed.empty()) printDiff(o.changed, "before", "after");
        return rcFor(o.result);
    }
    std::printf("%s multiclick -> %s. Verified on the device.\n",
                kMulticlickButtons[button], mode.c_str());
    printDiff(o.changed, "before", "after");
    return 0;
}

int cmdSet(const std::string& field, const std::string& value,
           bool verbose, bool yes, UnknownBytes policy,
           const std::string& vaultPath) {
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
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

    // Everything §4.1 requires happens inside this one call: read, validate,
    // read-modify-write, self-check the frame before it goes out, write, read
    // back, verify the DATA rather than the acknowledgement.
    SetOutcome o = s.set(*f, encoded);
    reportVault(s, vault);

    if (!o.before.empty())
        warnIfActiveStageWouldBeOutOfRange(o.before, *f, v);

    if (o.result == Result::AlreadySet) {
        std::printf("%s is already 0x%02x. Nothing to write.\n",
                    f->name, o.intendedValue);
        return 0;
    }
    if (o.result != Result::Ok) {
        explain(o.result, o.refusal);
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
    noteOnPersistence();
    return 0;
}

int cmdRestore(const std::string& path, bool verbose, bool yes, UnknownBytes policy,
               const std::string& vaultPath) {
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
    FileRecordVault vault(vaultPath);
    s.setVault(&vault);

    RestoreOutcome o = s.restore(want);
    reportVault(s, vault);

    // "before the restore", NOT "now": o.incoming is diff(o.before, want) and
    // this prints AFTER the write, so "now" named the state we had just left.
    // It reads as a failed restore to anyone scanning the output.
    if (!o.before.empty())
        printDiff(o.incoming, "before the restore", path.c_str());

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
    noteOnPersistence();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false, yes = false;
    std::string save;
    UnknownBytes policy = kDefaultUnknownBytes;
    std::string vaultPath = defaultVaultPath();
    std::string fromFile;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "--yes") yes = true;
        else if (a == "--save" && i + 1 < argc) save = argv[++i];
        else if (a == "--vault" && i + 1 < argc) vaultPath = argv[++i];
        // `handedness` only. It decides from a SAVED record instead of the
        // device, so the plan can be inspected with nothing on the wire.
        else if (a == "--from" && i + 1 < argc) fromFile = argv[++i];
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
    if (cmd == "read")    return cmdRead(verbose, save, policy, vaultPath);
    if (cmd == "info")    return cmdInfo(verbose);
    if (cmd == "factory-reset") return cmdFactoryReset(verbose, yes, policy, vaultPath);
    if (cmd == "restore" && args.size() > 1)
        return cmdRestore(args[1], verbose, yes, policy, vaultPath);
    if (cmd == "diff" && args.size() > 2)    return cmdDiff(args[1], args[2]);
    if (cmd == "frames")  return cmdFrames();
    if (cmd == "set" && args.size() > 2)
        return cmdSet(args[1], args[2], verbose, yes, policy, vaultPath);
    if (cmd == "dryrun" && args.size() == 4)
        return cmdDryRun(args[1], args[2], args[3], policy);
    if (cmd == "dryrun" && args.size() == 2)
        return cmdDryRun(args[1], "", "", policy);
    if (cmd == "encode" && args.size() == 5)
        return cmdEncode(args[1], args[2], args[3], args[4], policy);
    if (cmd == "map" && args.size() > 2)
        return cmdMap(args[1], args[2], verbose, yes, policy, vaultPath);
    if (cmd == "map") { listButtons(); return 2; }
    if (cmd == "cpi" && args.size() > 2)
        return cmdCpi(args[1], args[2], args.size() > 3 ? args[3] : std::string(),
                      verbose, yes, policy, vaultPath);
    if (cmd == "cpi") { listCpi(); return 2; }
    if (cmd == "handedness" && args.size() > 1)
        return cmdHandedness(args[1], fromFile, verbose, yes, policy, vaultPath);
    if (cmd == "handedness") { listHandedness(); return 2; }
    if (cmd == "multiclick" && args.size() > 2)
        return cmdMulticlick(args[1], args[2],
                             args.size() > 3 ? args[3] : std::string(),
                             verbose, yes, policy, vaultPath);
    if (cmd == "multiclick") { listMulticlick(); return 2; }
    if (cmd == "set") { listSettable(); return 2; }
    usage();
    return 1;
}
