// test_config.cpp -- CLAUDE.md §4.1's safety properties, asserted as
// invariants over randomised runs against a device that lies.
//
// §4.1 lists six requirements for the config tool. Until 2026-09-05 all six
// lived inside `int cmdSet(...)` in a CLI translation unit and NONE of them had
// a test, because reaching them required owning the mouse. They are checked
// here, on the seam ConfigLink, with no hardware:
//
//   - read-modify-write always; never construct a settings blob from scratch
//   - validate a read is structurally plausible before acting on it
//   - never write after a failed read
//   - read back and verify after every write; log the diff
//   - never let a reported success stand in for verifying the data itself
//
// §6.2: "A harness that cannot produce a bad result is not evidence." Several
// tests below arm a fault and REQUIRE the session to notice. If the adversarial
// section ever passes with every fault disabled, it is measuring nothing, so
// one test deliberately asserts that the faults actually bite.
#include "egg/ConfigRecord.h"
#include "egg/ConfigSession.h"
#include "egg/MockConfigDevice.h"
#include "egg/Protocol.h"
#include "egg/RecordVault.h"

#include <fstream>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace egg;
using namespace egg::cfg;

static int failures = 0;
static void ok(const char* what, bool cond, const std::string& detail = "") {
    std::printf("  %s  %s%s%s\n", cond ? "PASS" : "FAIL", what,
                detail.empty() ? "" : "  --  ", detail.c_str());
    if (!cond) ++failures;
}

// A plausible record in which EVERY payload byte is non-zero. The device's own
// records are full of zeros -- record 0x00 in particular is 0x00 in all nine
// captured reads and all 73 captured writes -- so a policy that zeroed one byte
// too many would be invisible against real data. It was: the mutation harness
// planted exactly that off-by-one and the suite did not notice.
static std::vector<std::uint8_t> variedRecord() {
    std::vector<std::uint8_t> r(kLargeLen, 0);
    r[0] = kReportLarge;
    r[kStatusOffset] = kStatusReady;
    for (std::size_t i = 0; i < kPayloadLen; ++i)
        r[kPayloadOffset + i] = static_cast<std::uint8_t>((i * 7 + 1) | 0x01);
    return r;
}

static const Settable& field(const char* n) {
    const Settable* f = findSettable(n);
    if (!f) { std::printf("  FAIL  no such field %s\n", n); ++failures; std::abort(); }
    return *f;
}

// ---------------------------------------------------------------------------
// 1. The record table and the encoders.
// ---------------------------------------------------------------------------
static void testTable() {
    std::printf("\nfield table\n");

    ok("every settable field cites a derivation", [] {
        for (std::size_t i = 0; i < kSettableCount; ++i) {
            const char* c = kSettable[i].cite;
            if (!c || !*c) return false;
        }
        return true;
    }());

    ok("every settable field lies inside the 115-byte record", [] {
        for (std::size_t i = 0; i < kSettableCount; ++i)
            if (kSettable[i].recordOffset >= kRecordLen) return false;
        return true;
    }());

    // §1.3 inside a byte, as much as between bytes.
    ok("no two whole-byte fields share a record offset", [] {
        for (std::size_t i = 0; i < kSettableCount; ++i)
            for (std::size_t j = i + 1; j < kSettableCount; ++j) {
                if (kSettable[i].recordOffset != kSettable[j].recordOffset) continue;
                if ((kSettable[i].mask & kSettable[j].mask) != 0) return false;
            }
        return true;
    }());

    ok("no settable field touches the four unknown bytes 0x01-0x04", [] {
        for (std::size_t i = 0; i < kSettableCount; ++i) {
            const std::size_t r = kSettable[i].recordOffset;
            if (r >= kRecordUnknownFirst && r <= kRecordUnknownLast) return false;
        }
        return true;
    }());

    // composeByte must never disturb bits outside the mask, for ANY input.
    ok("composeByte preserves every bit outside the mask, exhaustively", [] {
        for (std::size_t i = 0; i < kSettableCount; ++i) {
            const Settable& f = kSettable[i];
            for (unsigned old = 0; old < 256; ++old)
                for (unsigned v = 0; v < 256; ++v) {
                    const std::uint8_t got =
                        composeByte(static_cast<std::uint8_t>(old), f,
                                    static_cast<std::uint8_t>(v));
                    const std::uint8_t outside = static_cast<std::uint8_t>(~f.mask);
                    if ((got & outside) != (old & outside)) return false;
                }
        }
        return true;
    }());
}

// ---------------------------------------------------------------------------
// 2. plausible() -- the gate on acting at all.
// ---------------------------------------------------------------------------
static void testPlausible() {
    std::printf("\nstructural validation\n");
    MockConfigDevice dev;
    Reply r = dev.readRecord();
    ok("a cooperative device's record is plausible", plausible(r.buf));

    std::vector<std::uint8_t> shortBuf = r.buf; shortBuf.resize(kLargeLen - 1);
    ok("a short record is refused", !plausible(shortBuf));

    // BYTE 0 MUST NOT AFFECT PLAUSIBILITY. It is the report-id slot the device
    // never sends (wire-observed.md §2.1). This test used to assert the exact
    // opposite -- it set byte 0 to 0xA1 and required plausible() to REFUSE --
    // and 0xA1 is what the real device actually answers with, for either
    // report. So the suite was pinning a check that rejects every genuine read.
    // Found 2026-09-05 by the first read from the physical mouse; on macOS
    // hidapi leaves the slot 0x00, which the old check also refused.
    for (std::uint8_t slot : {std::uint8_t(0x00),   // macOS hidapi
                              std::uint8_t(0xA0),   // the id we requested
                              std::uint8_t(0xA1),   // what the device sends
                              std::uint8_t(0x5A)}) {
        std::vector<std::uint8_t> v = r.buf; v[0] = slot;
        char msg[96];
        std::snprintf(msg, sizeof msg,
                      "record stays plausible with report-id slot 0x%02x", slot);
        ok(msg, plausible(v));
    }

    for (int fill : {0x00, 0xFF, 0x5A}) {
        std::vector<std::uint8_t> flat = r.buf;
        std::memset(flat.data() + kPayloadOffset, fill, kPayloadLen);
        char b[64]; std::snprintf(b, sizeof b, "a constant 0x%02x payload is refused", fill);
        ok(b, !plausible(flat));
    }
}

// ---------------------------------------------------------------------------
// 3. The §4.1 invariants, over randomised runs.
// ---------------------------------------------------------------------------
static void testInvariants() {
    std::printf("\n§4.1 invariants, over seeds\n");

    // INVARIANT: no frame is ever emitted after a read that failed or that
    // returned an implausible record. This is the config analogue of the
    // flasher's "no write is emitted unless preflight passed".
    bool everWroteAfterBadRead = true;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        ConfigFaults f;
        f.seed = seed;
        f.rejectReadRate = 0.5;
        f.implausibleRate = 0.5;
        f.shortReadRate = 0.2;
        f.transportFailRate = 0.2;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 7);
        const bool badRead = o.result == Result::ReadFailed ||
                             o.result == Result::ReadImplausible;
        if (badRead && dev.writes() != 0) { everWroteAfterBadRead = false; break; }
    }
    ok("inv: never writes after a failed or implausible read, 200 seeds",
       everWroteAfterBadRead);

    // INVARIANT: whenever a frame IS emitted, it differs from the record we
    // read in exactly the bytes the policy sanctions and no others.
    bool onlySanctioned = true;
    std::string why;
    for (std::uint32_t seed = 1; seed <= 200 && onlySanctioned; ++seed)
        for (UnknownBytes pol : {UnknownBytes::Preserve, UnknownBytes::MatchVendor}) {
            ConfigFaults f; f.seed = seed;
            MockConfigDevice dev(f);
            ConfigSession s(dev, pol);
            SetOutcome o = s.set(field("lod"), static_cast<std::uint8_t>(seed % 11));
            if (!o.wrote) continue;
            for (const ByteChange& c : o.weChanged) {
                const bool isField = c.recordOffset == o.intendedOffset;
                const bool isUnknown = c.recordOffset >= kRecordUnknownFirst &&
                                       c.recordOffset <= kRecordUnknownLast;
                if (isField) continue;
                if (pol == UnknownBytes::MatchVendor && isUnknown && c.after == 0) continue;
                onlySanctioned = false;
                char b[128];
                std::snprintf(b, sizeof b, "seed %u moved record 0x%02zx", seed,
                              c.recordOffset);
                why = b;
                break;
            }
        }
    ok("inv: an emitted frame differs only where the policy allows, 400 runs",
       onlySanctioned, why);

    // INVARIANT: every byte of the record that is neither the field nor an
    // unknown byte survives the round trip untouched. This is read-modify-write
    // stated as a property rather than trusted as an implementation detail.
    bool preserved = true;
    for (std::uint32_t seed = 1; seed <= 100 && preserved; ++seed) {
        ConfigFaults f; f.seed = seed;
        MockConfigDevice dev(f);
        std::vector<std::uint8_t> before = dev.stored();
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("polling"), 2);
        if (o.result != Result::Ok) continue;
        for (std::size_t r = 0; r < kPayloadLen; ++r) {
            if (r == 0x05) continue;
            if (r >= kRecordUnknownFirst && r <= kRecordUnknownLast) continue;
            if (dev.storedRecord(r) != before[kPayloadOffset + r]) { preserved = false; break; }
        }
    }
    ok("inv: every other byte of the record is preserved, 100 seeds", preserved);

    // INVARIANT: the frame for a given (record, field, value, policy) is
    // identical every time. §4.3's determinism requirement, for config.
    ok("inv: the frame for a given input is byte-identical every run", [] {
        MockConfigDevice dev;
        std::vector<std::uint8_t> rec = dev.stored();
        std::vector<std::uint8_t> first =
            ConfigSession::buildFrame(rec, field("lod"), 4, UnknownBytes::MatchVendor);
        for (int i = 0; i < 50; ++i) {
            auto again = ConfigSession::buildFrame(rec, field("lod"), 4,
                                                   UnknownBytes::MatchVendor);
            if (again != first) return false;
        }
        return !first.empty();
    }());

    // A frame is never built from a record that did not validate.
    ok("inv: buildFrame refuses an implausible record", [] {
        std::vector<std::uint8_t> flat(kLargeLen, 0);
        flat[0] = kReportLarge;
        return ConfigSession::buildFrame(flat, field("lod"), 3,
                                         UnknownBytes::MatchVendor).empty();
    }());
}

// ---------------------------------------------------------------------------
// 4. The adversarial device. Each of these MUST be caught.
// ---------------------------------------------------------------------------
static void testAdversarial() {
    std::printf("\nadversarial device (§4.3)\n");

    // THE ONE THAT MATTERS. The device acknowledges and stores nothing. A
    // session that trusted the acknowledgement would report success.
    {
        ConfigFaults f; f.falseSuccessRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("REPORTS FALSE SUCCESS on a write, and is caught by the read-back",
           o.result == Result::VerifyMismatch, describe(o.result));
        ok("  ...and the device really did store nothing",
           dev.storedRecord(0x09) != 6);
    }

    {
        ConfigFaults f; f.corruptStoreRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("corrupts the byte it stores, and is caught",
           o.result == Result::VerifyMismatch, describe(o.result));
    }

    {
        // Changes a byte we did not send. Not our bug, but it must surface in
        // the diff rather than be silently accepted.
        ConfigFaults f; f.extraByteRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("changes an extra byte of its own accord, and the diff shows it",
           o.result == Result::Ok && o.changed.size() > 1,
           describe(o.result));
    }

    {
        ConfigFaults f; f.rejectWriteRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("rejects the write, and we say so rather than claiming success",
           o.result == Result::WriteRejected, describe(o.result));
    }

    {
        ConfigFaults f; f.implausibleRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("returns a garbage record, and nothing is written",
           o.result == Result::ReadImplausible && dev.writes() == 0,
           describe(o.result));
    }

    {
        ConfigFaults f; f.shortReadRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("returns a short record, and nothing is written",
           o.result == Result::ReadImplausible && dev.writes() == 0,
           describe(o.result));
    }

    {
        ConfigFaults f; f.transportFailRate = 1.0;
        MockConfigDevice dev(f);
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        ok("goes silent, and nothing is written",
           o.result == Result::ReadFailed && dev.writes() == 0, describe(o.result));
    }

    // §6.2: prove the harness can produce a bad result. If a fully cooperative
    // device did not succeed, every test above would be measuring the wrong
    // thing.
    {
        MockConfigDevice dev;
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 6);
        // A redundant `set` must put NOTHING on the wire, under either policy.
    // Under MatchVendor our frame differs from the read at record 0x01..0x04
    // even when the field is already correct, so a naive "did the frame change?"
    // test would write every time someone re-applied a setting they already had.
    for (UnknownBytes pol : {UnknownBytes::Preserve, UnknownBytes::MatchVendor}) {
        MockConfigDevice dev;
        ConfigSession s(dev, pol);
        // record 0x05 is 0x01 in the mock, i.e. 8000 Hz.
        SetOutcome o = s.set(field("polling"), 1);
        ok((std::string("a redundant set sends nothing (") + describe(pol) + ")").c_str(),
           o.result == Result::AlreadySet && !o.wrote &&
           dev.sentFrames().empty() && dev.writes() == 0,
           describe(o.result));
    }

    // ACKNOWLEDGED, THEN UNREADABLE. The only outcome in the enum that means
    // "we wrote and cannot say what the device holds", and until the mutation
    // harness planted a mutant that turned it into Ok, nothing reached it.
    {
        MockConfigDevice dev(ConfigFaults{.readFailsAfterWrite = true});
        ConfigSession s(dev, kDefaultUnknownBytes);
        SetOutcome o = s.set(field("polling"), 2);
        ok("acks the write then will not answer the read, and we say STATE UNKNOWN",
           o.result == Result::VerifyReadFailed && o.wrote && o.after.empty(),
           describe(o.result));
        ok("...and it is NOT reported as success",
           o.result != Result::Ok && o.result != Result::AlreadySet);
        ok("...and the frame really did go out, so this is the unknown-state case,\n            not a refusal before the write",
           dev.sentFrames().size() == 1 && dev.acceptedWrites() == 1);
    }

    ok("a cooperative device succeeds -- so the failures above are real",
           o.result == Result::Ok && dev.storedRecord(0x09) == 6, describe(o.result));
    }
}

// ---------------------------------------------------------------------------
// 5. The unknown-bytes policy, both arms.
// ---------------------------------------------------------------------------
static void testUnknownBytePolicy() {
    std::printf("\nrecord 0x01-0x04 policy (ConfigRecord.h)\n");

    {
        MockConfigDevice dev;
        ok("the mock reports 0x80 at record 0x01, as eight of nine captured reads do",
           dev.storedRecord(kRecordUnknownFirst) == 0x80);
    }

    {
        MockConfigDevice dev;
        ConfigSession s(dev, UnknownBytes::MatchVendor);
        SetOutcome o = s.set(field("lod"), 3);
        bool zeroed = true;
        for (std::size_t r = kRecordUnknownFirst; r <= kRecordUnknownLast; ++r)
            if (o.sent[kPayloadOffset + r] != 0x00) zeroed = false;
        ok("MatchVendor: our frame carries 00 00 00 00 at 0x01-0x04, as the "
           "vendor's 73 captured writes do", zeroed && o.result == Result::Ok,
           describe(o.result));
    }

    {
        MockConfigDevice dev;
        ConfigSession s(dev, UnknownBytes::Preserve);
        SetOutcome o = s.set(field("lod"), 3);
        ok("Preserve: our frame carries the 0x80 we read back out again",
           o.result == Result::Ok &&
           o.sent[kPayloadOffset + kRecordUnknownFirst] == 0x80,
           describe(o.result));
    }

    {
        // The hypothesis that would make the choice moot: if the byte belonged
        // to the device, it would restore 0x80 whatever we sent, and both
        // policies would converge. Off by default because it is [G] and the
        // captures show the opposite -- but if it were ever true, this is what
        // it would look like, and neither policy would corrupt anything.
        ConfigFaults f; f.restoresUnknownByte = true;
        MockConfigDevice a(f), b(f);
        ConfigSession sa(a, UnknownBytes::MatchVendor);
        ConfigSession sb(b, UnknownBytes::Preserve);
        SetOutcome oa = sa.set(field("lod"), 3);
        SetOutcome ob = sb.set(field("lod"), 3);
        ok("if the DEVICE owned that byte, both policies would converge",
           oa.result == Result::Ok && ob.result == Result::Ok &&
           a.stored() == b.stored());
    }

    // THE BLAST RADIUS, asserted exactly rather than by exclusion. Every payload
    // byte here is non-zero, so a policy that reaches one byte too far in either
    // direction changes a byte that was not zero already and the difference is
    // visible. Against a real record it would not be.
    ok("the policy changes EXACTLY record 0x01-0x04 and nothing else", [] {
        std::vector<std::uint8_t> rec = variedRecord();
        if (!plausible(rec)) return false;
        auto v = ConfigSession::buildFrame(rec, field("polling"), 2,
                                           UnknownBytes::MatchVendor);
        auto p = ConfigSession::buildFrame(rec, field("polling"), 2,
                                           UnknownBytes::Preserve);
        if (v.size() != kLargeLen || p.size() != kLargeLen) return false;
        std::vector<std::size_t> moved;
        for (std::size_t r = 0; r < kPayloadLen; ++r)
            if (v[kPayloadOffset + r] != p[kPayloadOffset + r]) moved.push_back(r);
        const std::vector<std::size_t> want{kRecordUnknownFirst,
                                            kRecordUnknownFirst + 1,
                                            kRecordUnknownFirst + 2,
                                            kRecordUnknownLast};
        if (moved != want) return false;
        // ...and the Preserve arm really did carry the originals through, or
        // the comparison above would be between two identical wrong answers.
        for (std::size_t r = kRecordUnknownFirst; r <= kRecordUnknownLast; ++r)
            if (p[kPayloadOffset + r] != rec[kPayloadOffset + r]) return false;
        return true;
    }());

    // Whatever the policy, it may never touch a byte outside 0x01..0x04.
    ok("neither policy changes any byte outside the field and 0x01-0x04", [] {
        MockConfigDevice dev;
        std::vector<std::uint8_t> rec = dev.stored();
        auto v = ConfigSession::buildFrame(rec, field("polling"), 2,
                                           UnknownBytes::MatchVendor);
        auto p = ConfigSession::buildFrame(rec, field("polling"), 2,
                                           UnknownBytes::Preserve);
        if (v.empty() || p.empty()) return false;
        for (std::size_t r = 0; r < kPayloadLen; ++r) {
            if (r >= kRecordUnknownFirst && r <= kRecordUnknownLast) continue;
            if (v[kPayloadOffset + r] != p[kPayloadOffset + r]) return false;
        }
        return true;
    }());
}

// ---------------------------------------------------------------------------
// §4.1: "Save a known-good blob to disk on first connect."
// ---------------------------------------------------------------------------
// The dangerous failure is not "no file". It is a file that LOOKS like an undo
// and is not -- one written from a bad read, or one overwritten later by
// whatever a half-broken session left on the device. Both are checked here.
static std::string tmpPath(const char* leaf) {
    const char* d = std::getenv("TMPDIR");
    std::string dir = d ? d : "/tmp";
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return dir + "egg-vault-test-" + leaf;
}

static void testVault() {
    std::printf("\nknown-good blob (§4.1)\n");

    const std::string path = tmpPath("first.bin");
    std::remove(path.c_str());

    // The device's own first record, kept, and the exact bytes recoverable.
    std::vector<std::uint8_t> first;
    {
        MockConfigDevice dev;
        first = dev.stored();
        FileRecordVault v(path);
        ok("a fresh vault holds nothing", !v.holds());
        ConfigSession s(dev, kDefaultUnknownBytes);
        s.setVault(&v);
        std::vector<std::uint8_t> rec; Result r = Result::Ok;
        const bool got = s.read(rec, r);
        ok("the first plausible read fills the vault", got && v.holds() && s.justStored());
    }

    // NEVER OVERWRITTEN. A second session, a device now holding something else,
    // and the file must still be the original.
    {
        MockConfigDevice dev;
        // Move the device away from what the vault holds.
        dev.writeRecord(ConfigSession::buildFrame(dev.stored(), field("polling"), 64,
                                                  UnknownBytes::Preserve));
        FileRecordVault v(path);
        ok("a later session sees the vault as already held", v.holds());
        ConfigSession s(dev, kDefaultUnknownBytes);
        s.setVault(&v);
        std::vector<std::uint8_t> rec; Result r = Result::Ok;
        s.read(rec, r);
        ok("...and does not overwrite it", !s.justStored());

        std::vector<std::uint8_t> onDisk;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            onDisk.resize(kLargeLen);
            const std::size_t n = std::fread(onDisk.data(), 1, kLargeLen, f);
            onDisk.resize(n);
            std::fclose(f);
        }
        ok("the file still holds the FIRST record, byte for byte",
           onDisk == first,
           onDisk.size() != kLargeLen ? "wrong length on disk" : "");
        ok("...and the device has genuinely moved on, so that meant something",
           rec != first);
    }

    // A record that failed validation must never reach the vault, even if the
    // caller offers it. The session already screens; the vault screens again,
    // because this file exists so a bad session cannot destroy a good record.
    {
        const std::string p2 = tmpPath("implausible.bin");
        std::remove(p2.c_str());
        FileRecordVault v(p2);
        std::vector<std::uint8_t> junk(kLargeLen, 0xFF);
        junk[0] = kReportLarge;
        ok("the vault refuses an implausible record offered directly",
           !v.offer(junk) && !v.holds(), v.lastError());

        // AND IT MUST NOT HAVE WRITTEN IT. The comment above says a bad record
        // "must never reach the vault"; until 2026-09-05 nothing checked that,
        // and once alreadyGood() started screening for plausibility as well as
        // length, the post-write check masked a missing pre-write one -- offer()
        // could write junk, notice afterwards, and still return false. The
        // return value looked identical and mutants.sh reported the hole.
        // A rejected offer leaves NO file, not a file we then disown.
        {
            std::ifstream probe(p2, std::ios::binary);
            ok("...and leaves no file behind at all", !probe.good());
        }

        MockConfigDevice dev(ConfigFaults{.implausibleRate = 1.0});
        ConfigSession s(dev, kDefaultUnknownBytes);
        s.setVault(&v);
        std::vector<std::uint8_t> rec; Result r = Result::Ok;
        const bool got = s.read(rec, r);
        ok("a session whose every read is garbage saves nothing",
           !got && r == Result::ReadImplausible && !v.holds());
        std::remove(p2.c_str());
    }

    // A truncated file from an interrupted run is not a known-good blob, and
    // must not suppress the save that would replace it.
    {
        const std::string p3 = tmpPath("truncated.bin");
        std::FILE* f = std::fopen(p3.c_str(), "wb");
        ok("(setup) a short file exists", f != nullptr);
        if (f) { std::fputs("not a record", f); std::fclose(f); }
        FileRecordVault v(p3);
        ok("a truncated file does not count as a saved record", !v.holds());
        MockConfigDevice dev;
        ConfigSession s(dev, kDefaultUnknownBytes);
        s.setVault(&v);
        std::vector<std::uint8_t> rec; Result r = Result::Ok;
        s.read(rec, r);
        ok("...and is replaced by a real one", v.holds() && s.justStored());
        std::remove(p3.c_str());
    }

    // An unwritable path must be REPORTED, not swallowed -- and must not stop
    // the session, because losing access to the device to protect a backup
    // would be the wrong trade.
    {
        FileRecordVault v("/this/directory/does/not/exist/known-good.bin");
        MockConfigDevice dev;
        ConfigSession s(dev, kDefaultUnknownBytes);
        s.setVault(&v);
        std::vector<std::uint8_t> rec; Result r = Result::Ok;
        const bool got = s.read(rec, r);
        ok("an unwritable vault path fails loudly and does not break the read",
           got && r == Result::Ok && !v.holds() && !v.lastError().empty(),
           v.lastError());
    }

    // Every path that reads must fill it, not just `read`. This is the reason
    // the vault hangs off the session rather than off a call site.
    {
        for (const char* which : {"set", "restore", "factory-reset"}) {
            const std::string p4 = tmpPath(which);
            std::remove(p4.c_str());
            MockConfigDevice dev;
            FileRecordVault v(p4);
            ConfigSession s(dev, kDefaultUnknownBytes);
            s.setVault(&v);
            if (std::string(which) == "set") {
                s.set(field("polling"), 2);
            } else if (std::string(which) == "restore") {
                s.restore(dev.stored());
            } else {
                std::vector<std::uint8_t> rec; Result r = Result::Ok;
                s.read(rec, r);          // what cmdFactoryReset does before A1 13
                s.factoryReset();
            }
            ok((std::string("`") + which + "` fills the vault too").c_str(), v.holds());
            std::remove(p4.c_str());
        }
    }
    std::remove(path.c_str());
}

int main() {
    std::printf("EGGConfigCore\n");
    testTable();
    testPlausible();
    testInvariants();
    testAdversarial();
    testUnknownBytePolicy();
    testVault();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILURES" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
