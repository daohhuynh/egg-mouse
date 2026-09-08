// test_flash.cpp -- the seven invariants of updater-protocol.md §10.3, the
// derived byte maps, and the flasher driven against an adversarial device.
//
// engineering-rules.md §4.3: "Assert invariants, not examples, over randomised runs." The
// examples here exist only to pin the byte maps, which ARE examples by nature.
// Everything about behaviour is an invariant checked over many seeds.
//
// §6.2: "A harness that cannot produce a bad result is not evidence." Several
// of these deliberately arm a fault and require the flasher to notice. If the
// adversarial section ever passes with the faults disabled, it is measuring
// nothing.
#include "egg/BootloaderEntry.h"
#include "egg/FlashCommands.h"
#include "egg/FirmwareManifest.h"
#include "egg/FlashPlan.h"
#include "egg/Firmware.h"
#include "egg/HidBootloaderLink.h"
#include "egg/MockBootloader.h"
#include "egg/Protocol.h"
#include "egg/Provenance.h"
#include "egg/NoQuitDuringWrite.h"
#include "egg/WritePhase.h"

#include <unistd.h>

#include <concepts>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace egg;
using namespace egg::fw;

static int failures = 0;

// THE DELETED OVERLOAD, and it belongs here as much as in test_config.cpp.
//
// `ok("...", []{ return x == y; })` compiles against the `bool` overload -- a
// lambda converts to `true` -- so the assertion is never evaluated and the test
// prints PASS having checked nothing. That happened for real in test_config.cpp,
// where ctest was green while all ten CPI mutants SURVIVED, and only
// Tests/mutants.sh caught it.
//
// The guard was then added to test_config.cpp ALONE. This file is the one that
// drives the erase, and it had no protection against the identical mistake for
// two days: `ok("no write before preflight", []{...})` would have printed PASS
// over the single most consequential invariant in the project. Added 2026-09-07.
//
// A template constrained on callability is a better match than `bool` for
// anything callable, and being `= delete` makes the mistake a compile error
// rather than something a later mutation run has to notice.
template <class F>
    requires requires(F f) { { f() } -> std::convertible_to<bool>; }
void ok(const char* what, F fn, const std::string& detail = "") = delete;

static void ok(const char* what, bool cond, const std::string& detail = "") {
    std::printf("  %s  %s%s%s\n", cond ? "PASS" : "FAIL", what,
                detail.empty() ? "" : "  --  ", detail.c_str());
    if (!cond) ++failures;
}

static const char* kExe =
    "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe";

// A synthetic image with the right shape, for tests that must not depend on
// the vendor file being present.
static std::vector<std::uint8_t> synthetic() {
    std::vector<std::uint8_t> v(kExpectedImageSize);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::uint8_t>((i * 31 + (i >> 7)) & 0xFF);
    return v;
}

// ---------------------------------------------------------------------------
// 1. The derived byte maps. These are examples on purpose: they pin what
//    updater-protocol.md §3 says each frame looks like, byte for byte.
// ---------------------------------------------------------------------------
static void testByteMaps() {
    std::printf("\nDerived byte maps (updater-protocol.md §3)\n");

    Frame f = enterBootloader();
    ok("A1 3A enter-bootloader is 64 bytes", f.size() == kSmallLen);
    ok("A1 3A layout [0]=A1 [1]=3A [5]=5A [6]=A5 [7]=32",
       f[0] == 0xA1 && f[1] == 0x3A && f[2] == 0 && f[3] == 0 && f[4] == 0 &&
       f[5] == 0x5A && f[6] == 0xA5 && f[7] == 0x32);

    f = bootloaderStart(65, 0x0081d57d);
    ok("A0 03 start is 1041 bytes", f.size() == kLargeLen);
    ok("A0 03 declares block count at [16]", f[16] == 65);
    ok("A0 03 declares checksum LE32 at [17..20]",
       f[17] == 0x7d && f[18] == 0xd5 && f[19] == 0x81 && f[20] == 0x00);

    std::vector<std::uint8_t> payload(kBlockSize, 0);
    payload[0] = 0x01; payload[1] = 0x02;         // sum = 3
    f = writeBlock(0x34, payload.data(), payload.size());
    ok("A0 06 write is 1041 bytes", f.size() == kLargeLen);
    ok("A0 06 index is SIXTEEN bits at [2..3]", f[2] == 0x34 && f[3] == 0x00);
    ok("A0 06 payload sum LE16 at [4..5]", f[4] == 0x03 && f[5] == 0x00);
    ok("A0 06 payload starts at [16]", f[16] == 0x01 && f[17] == 0x02);
    ok("A0 06 bytes [6..15] are zero",
       std::memcmp(f.data() + 6, std::vector<std::uint8_t>(10, 0).data(), 10) == 0);

    f = readBlock(0x74);
    ok("A0 07 read index is EIGHT bits at [2], [3] stays zero",
       f[1] == 0x07 && f[2] == 0x74 && f[3] == 0x00);

    f = wholeImageChecksumQuery(0x74);
    ok("A1 08 asks the range 0x34..0x74",
       f.size() == kSmallLen && f[1] == 0x08 && f[2] == 0x34 && f[3] == 0x74);

    ok("A1 09 complete", bootloaderComplete()[1] == 0x09);
    ok("A1 13 post-success", postSuccess()[1] == 0x13);

    std::vector<std::uint8_t> r(kSmallLen, 0);
    r[16] = 0x7d; r[17] = 0xd5; r[18] = 0x81; r[19] = 0x00;
    ok("checksum result is LE32 at resp[16..19]",
       checksumResult(r.data(), r.size()) == 0x0081d57d);
}

// ---------------------------------------------------------------------------
// 2. Checksums, against the vendor's own algorithms.
// ---------------------------------------------------------------------------
static void testChecksums() {
    std::printf("\nChecksums (§3.3 per-block, §3.5 + FUN_00403580 whole-image)\n");

    std::vector<std::uint8_t> b(kBlockSize, 0xFF);
    // 1024 * 255 = 261120 = 0x3FC00; truncated to 16 bits -> 0xFC00.
    ok("per-block sum is SIXTEEN bits, and truncation is the point",
       blockChecksum(b.data(), b.size()) == 0xFC00);

    std::vector<std::uint8_t> img(kExpectedImageSize, 0xFF);
    // 66560 * 255 = 16972800 = 0x102FF00. Does NOT fit in 16 bits, so a
    // whole-image checksum that truncated would be wrong.
    ok("whole-image sum is THIRTY-TWO bits",
       wholeImageChecksum(img) == 16972800u);
}

// ---------------------------------------------------------------------------
// 3. The seven invariants of §10.3.
// ---------------------------------------------------------------------------
static void testInvariants() {
    std::printf("\n§10.3 invariants\n");

    Image img;
    std::string err;
    const bool loaded = img.loadFromExecutable(kExe, err);
    ok("the vendor image loads and validates", loaded, err);
    if (!loaded) {
        std::printf("       (needs \"%s\" in the repo root -- it is gitignored "
                    "but present locally)\n", kExe);
        return;
    }

    // 1 and 2.
    ok("inv 1: size is a whole number of 1024-byte blocks",
       img.bytes().size() % kBlockSize == 0 &&
       img.blockCount() == img.bytes().size() / kBlockSize);
    ok("inv 2: block count is exactly 65", img.blockCount() == 65);
    ok("image SHA-256 matches the pinned constant",
       img.sha256() == kExpectedSha256, img.sha256());
    ok("whole-image checksum is 0x0081d57d (FUN_00403580)",
       img.checksum() == 0x0081d57du);

    // 3, FUZZED rather than asserted. This is the one carrying real weight:
    // hardware recovery is [O], so the only remaining way to brick the device
    // is our own arithmetic emitting an index below 0x34.
    bool everOut = false, everThrew = true;
    for (int v = -2000; v <= 2000; ++v) {
        const int idx = v;
        std::vector<std::uint8_t> p(kBlockSize, 0);
        bool threw = false;
        try {
            Frame fr = writeBlock(static_cast<std::uint16_t>(idx & 0xFFFF),
                                  p.data(), p.size());
            const std::uint16_t emitted =
                static_cast<std::uint16_t>(fr[2] | (fr[3] << 8));
            if (emitted < kBlockFirst || emitted > kBlockLast) everOut = true;
        } catch (const std::exception&) { threw = true; }
        const std::uint16_t masked = static_cast<std::uint16_t>(idx & 0xFFFF);
        const bool shouldThrow = masked < kBlockFirst || masked > kBlockLast;
        if (shouldThrow && !threw) everThrew = false;
    }
    ok("inv 3: no in-range frame ever carries an out-of-range index", !everOut);
    ok("inv 3: EVERY out-of-range index is refused, over 4001 values", everThrew);

    // 4 and 5, checked through the mock: indices come from one counter and
    // appear in strict order, each only after the previous verified.
    MockBootloader dev;
    std::string e2;
    ok("preflight passes on a good image", preflight(img, e2), e2);
    ok("preflight sent NOTHING to do it", dev.sent().empty());
    Progress p = driveToVerifiedImage(dev, img);
    ok("inv 7: returns only with a verified image", p.imageVerified);
    ok("completion was acknowledged", p.completeAcked);
    ok("all 65 blocks verified", p.blocksVerified == 65);

    bool ordered = true, inRange = true;
    for (std::size_t i = 0; i < dev.writtenIndices().size(); ++i) {
        const std::uint16_t bi = dev.writtenIndices()[i];
        if (bi < kBlockFirst || bi > kBlockLast) inRange = false;
        if (bi != kBlockFirst + i) ordered = false;
    }
    ok("inv 3 end-to-end: every index the device saw is in [0x34,0x74]", inRange);
    ok("inv 4/5: indices are 0x34..0x74 in order, one counter, no gaps",
       ordered && dev.writtenIndices().size() == 65);
    ok("the device declares 65 blocks and the right checksum",
       dev.declaredBlockCount() == 65 && dev.declaredChecksum() == img.checksum());

    // The image the device ends up holding must equal the source, block for
    // block. Verifying our own progress counter proves nothing.
    bool resident = dev.flash().size() == 65;
    for (std::size_t i = 0; i < img.blockCount() && resident; ++i) {
        auto it = dev.flash().find(img.deviceIndex(i));
        if (it == dev.flash().end() ||
            std::memcmp(it->second.data(), img.block(i), kBlockSize) != 0)
            resident = false;
    }
    ok("the bytes actually resident on the device equal the image", resident);

    // 6: no write is emitted unless preflight passed. It used to be checked by
    // failing a device and looking for write frames; preflight has no link now,
    // so the property is structural and the test says so directly -- a failing
    // preflight is a decision taken with an untouched device.
    Image empty;                        // never loaded: not a valid image
    std::string e3;
    MockBootloader bad;
    ok("inv 6: preflight FAILS on an image that was never loaded",
       !preflight(empty, e3));
    ok("inv 6: and it reached that verdict having sent nothing", bad.sent().empty());
}

// ---------------------------------------------------------------------------
// 4. Adversarial runs. Each arms one fault and requires survival.
// ---------------------------------------------------------------------------
static void testAdversarial() {
    std::printf("\nAdversarial device (§4.3)\n");
    Image img;
    std::string err;
    if (!img.loadFromExecutable(kExe, err)) { ok("image for adversarial runs", false, err); return; }

    struct Case { const char* name; Faults f; };
    std::vector<Case> cases;
    auto mk = [](void (*set)(Faults&)) { Faults f; set(f); return f; };
    cases.push_back({"rejects 30% of commands",
                     mk([](Faults& f){ f.rejectRate = 0.30; })});
    cases.push_back({"returns malformed responses 20% of the time",
                     mk([](Faults& f){ f.malformedRate = 0.20; })});
    cases.push_back({"REPORTS FALSE SUCCESS on 25% of writes",
                     mk([](Faults& f){ f.falseSuccessRate = 0.25; })});
    cases.push_back({"goes silent mid-write",
                     mk([](Faults& f){ f.silenceRate = 0.10; f.silenceLength = 4; })});
    cases.push_back({"disconnects and reappears",
                     mk([](Faults& f){ f.disconnectRate = 0.08; })});
    cases.push_back({"stalls past every timeout",
                     mk([](Faults& f){ f.stallRate = 0.05; })});
    cases.push_back({"corrupts what it stores",
                     mk([](Faults& f){ f.corruptStoreRate = 0.15; })});
    // These two exist to make the write phase's two verification guards
    // INDEPENDENTLY testable. Before them, either guard could be deleted with
    // no test failing, because every corruption the mock produced tripped both.
    cases.push_back({"corrupts bytes but PRESERVES the 16-bit sum",
                     mk([](Faults& f){ f.corruptPreservingSumRate = 0.15; })});
    cases.push_back({"stores correctly but MISREPORTS the block checksum",
                     mk([](Faults& f){ f.lieAboutBlockSumRate = 0.15; })});
    // Lies twice, then tells the truth. It cannot lie forever: §4.2 forbids a
    // give-up path, so against a permanently-lying device the correct
    // behaviour IS to never return, and a test cannot assert that in finite
    // time. Two lies exercises the re-verify-and-rewrite path and converges.
    cases.push_back({"lies about the whole-image checksum, twice",
                     mk([](Faults& f){ f.lieAboutWholeChecksumTimes = 2; })});
    cases.push_back({"everything at once",
                     mk([](Faults& f){ f.rejectRate = 0.15; f.malformedRate = 0.10;
                                       f.falseSuccessRate = 0.10; f.silenceRate = 0.05;
                                       f.disconnectRate = 0.05; f.corruptStoreRate = 0.10;
                                       f.corruptPreservingSumRate = 0.08;
                                       f.lieAboutBlockSumRate = 0.08; })});

    for (auto& c : cases) {
        bool allGood = true;
        std::string why;
        for (std::uint32_t seed = 1; seed <= 8 && allGood; ++seed) {
            Faults f = c.f;
            f.seed = seed;
            MockBootloader dev(f);
            Progress p = driveToVerifiedImage(dev, img);
            if (!p.imageVerified) { allGood = false; why = "returned unverified"; break; }
            if (dev.flash().size() != 65) { allGood = false; why = "wrong block count resident"; break; }
            for (std::size_t i = 0; i < img.blockCount(); ++i) {
                auto it = dev.flash().find(img.deviceIndex(i));
                if (it == dev.flash().end() ||
                    std::memcmp(it->second.data(), img.block(i), kBlockSize) != 0) {
                    allGood = false;
                    why = "block 0x" + std::to_string(img.deviceIndex(i)) +
                          " wrong on device, seed " + std::to_string(seed);
                    break;
                }
            }
            for (std::uint16_t bi : dev.writtenIndices())
                if (bi < kBlockFirst || bi > kBlockLast) {
                    allGood = false; why = "OUT OF RANGE INDEX EMITTED"; break;
                }
        }
        ok(c.name, allGood, why);
    }
}

// ---------------------------------------------------------------------------
// 5. Determinism, and the identity guard.
// ---------------------------------------------------------------------------
static void testDeterminismAndIdentity() {
    std::printf("\nDeterminism and image identity\n");
    Image img;
    std::string err;
    if (!img.loadFromExecutable(kExe, err)) { ok("image", false, err); return; }

    MockBootloader a, b;
    driveToVerifiedImage(a, img);
    driveToVerifiedImage(b, img);
    ok("§4.3: the byte stream for a given input is identical every run",
       a.sent() == b.sent());

    std::string e;
    Image none;
    ok("a non-PE file is refused", !none.loadFromExecutable("README.md", e), e);
    ok("a missing file is refused", !none.loadFromExecutable("no-such.exe", e));

    // THE IDENTITY GUARD, and this is the test that gives it teeth.
    //
    // Updaters 1.04, 1.06 and 1.07 each ship their own FWFILE resource 140.
    // Every one is 66560 bytes, exactly 65 blocks, remainder zero -- so every
    // structural check in the loader passes on all of them. They are
    // wrong-but-well-formed images in the precise sense engineering-rules.md §2 means,
    // and the SHA-256 comparison is the ONLY thing that separates them from
    // the right one. §2: "nothing downstream of us catches a wrong-but-well-
    // formed image. Every guard against flashing the wrong firmware has to be
    // ours, and has to run before the first byte goes out."
    const char* others[] = {
        "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe",
        "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe",
        "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe",
    };
    for (const char* other : others) {
        std::vector<std::uint8_t> raw;
        std::string e1;
        if (!extractResource(other, kResourceType, kResourceName, raw, e1)) {
            ok("sibling updater is readable", false, std::string(other) + ": " + e1);
            continue;
        }
        const bool structurallyFine = raw.size() == kExpectedImageSize &&
                                      raw.size() % kBlockSize == 0 &&
                                      raw.size() / kBlockSize == kExpectedBlockCount;
        Image wrong;
        std::string e2;
        const bool refused = !wrong.loadFromExecutable(other, e2);
        std::string tag = std::string(other).substr(
            std::string(other).find("Updater"));
        ok(("wrong firmware passes EVERY structural check: " + tag).c_str(),
           structurallyFine);
        ok(("...and is refused anyway, on SHA-256 alone: " + tag).c_str(),
           refused, refused ? "" : "ACCEPTED A WRONG IMAGE");
    }

    std::vector<std::uint8_t> synth = synthetic();
    ok("a synthetic image of the right size also hashes differently",
       sha256Hex(synth.data(), synth.size()) != std::string(kExpectedSha256));
}

// Frame layout pinned against the bytes Endgame's updater actually sent, read
// out of windows-run/08-flash.pcapng (notes/flash-wire-observed.md §2, §3).
// These are literals on purpose: an offset that drifts by one still produces a
// well-formed frame with a valid checksum, and every check the DEVICE performs
// would pass on it. Only a fixed reference catches that class of error, and the
// only trustworthy reference is what the vendor sent to this mouse.
static void testObservedFrameLayout() {
    std::printf("\nframe layout, against the captured vendor stream\n");

    const Frame e = enterBootloader();
    ok("enter: a1 3a, magic 5a a5 32 at [5..7]",
       e.size() == 64 && e[0] == 0xA1 && e[1] == 0x3A &&
       e[5] == 0x5A && e[6] == 0xA5 && e[7] == 0x32);
    bool enterRestZero = true;
    for (std::size_t i = 2; i < e.size(); ++i)
        if (i < 5 || i > 7) enterRestZero = enterRestZero && (e[i] == 0);
    ok("enter: every other byte is zero", enterRestZero);

    // The observed start frame, in full: a0 03, then zeros, then 0x41 at [16]
    // and 7d d5 81 00 at [17..20]. 0x41 is 65 -- the block count -- and the
    // four bytes are the 32-bit sum of all 66560 image bytes, 0x0081d57d.
    // Reading [15..18] as one 32-bit checksum gives 0x81d57d41, which looks
    // plausible and is wrong; that misreading is exactly what this pins down.
    const Frame s0 = bootloaderStart(65, 0x0081d57du);
    ok("start: a0 03", s0.size() == 1041 && s0[0] == 0xA0 && s0[1] == 0x03);
    ok("start: block count 0x41 at [16], not [15]",
       s0[16] == 0x41 && s0[15] == 0x00);
    ok("start: whole-image sum 7d d5 81 00 at [17..20]",
       s0[17] == 0x7D && s0[18] == 0xD5 && s0[19] == 0x81 && s0[20] == 0x00);
    bool startRestZero = true;
    for (std::size_t i = 2; i < s0.size(); ++i)
        if (i < 16 || i > 20) startRestZero = startRestZero && (s0[i] == 0);
    ok("start: nothing else is set", startRestZero);

    // One real block from the capture: index 0x34, and the 1024 data bytes.
    //
    // ONE ORIGIN, ALWAYS: offsets are counted on the bytes as transferred, with
    // the report id included where the wire carries one. Under that convention
    // the 1024 data bytes start at 16 in BOTH directions -- the write buffer and
    // the read-back response -- and cfg107 agrees, using frame+0x10 in its
    // writer and its reader alike.
    //
    // This comment previously described a "one-byte stagger" between the two
    // directions. There is none. That claim came from measuring the write
    // without its report-id byte and the read with its leading status byte, and
    // an independent check refuted it from raw file bytes on 2026-09-05, using
    // the vendor's own checksum as the discriminator: sum(buf[16:1040]) & 0xFFFF
    // equals the declared checksum on 65 of 65 blocks in both directions in both
    // captures, and on 0 of 65 at offset 15.
    //
    // The genuine asymmetry is at the START of the buffer and is worth keeping
    // in mind: the device omits the report-id byte from GET_REPORT responses and
    // returns wLength-1 bytes, so an inbound buffer is one shorter and begins
    // with a status byte rather than the report id that was requested.
    std::vector<std::uint8_t> data(kBlockSize, 0);
    data[0] = 0x29;
    const Frame w = writeBlock(0x34, data.data(), kBlockSize);
    ok("write: a0 06, index LE16 at [2..3]",
       w.size() == 1041 && w[0] == 0xA0 && w[1] == 0x06 &&
       w[2] == 0x34 && w[3] == 0x00);
    ok("write: checksum LE16 at [4..5]",
       w[4] == 0x29 && w[5] == 0x00);
    ok("write: ten zero bytes at [6..15]", [&] {
           for (std::size_t i = 6; i < 16; ++i) if (w[i]) return false;
           return true;
       }());
    ok("write: data starts at offset 16, same as the response",
       w[16] == 0x29 && w[15] == 0x00);
    ok("write: [1040] is a zero pad", w[1040] == 0x00);

    const Frame r = readBlock(0x34);
    ok("verify: a0 07, index at [2..3], nothing else",
       r.size() == 1041 && r[0] == 0xA0 && r[1] == 0x07 &&
       r[2] == 0x34 && r[3] == 0x00 && [&] {
           for (std::size_t i = 4; i < r.size(); ++i) if (r[i]) return false;
           return true;
       }());

    const Frame q = wholeImageChecksumQuery(0x74);
    ok("finish: a1 08, first 0x34 and last 0x74 at [2..3]",
       q.size() == 64 && q[0] == 0xA1 && q[1] == 0x08 &&
       q[2] == 0x34 && q[3] == 0x74);

    const Frame c = bootloaderComplete();
    ok("complete: a1 09 and nothing else",
       c.size() == 64 && c[0] == 0xA1 && c[1] == 0x09 && c[2] == 0x00);

    const Frame ps = postSuccess();
    ok("post-success: a1 13 and nothing else",
       ps.size() == 64 && ps[0] == 0xA1 && ps[1] == 0x13 && ps[2] == 0x00);
}


// ---------------------------------------------------------------------------
// §4.4 stage 2. This runs against the ONE mouse, so every failure mode is
// exercised here first. The invariant that matters most is not "it works" --
// it is "it sends exactly one report, and nothing at all when it should not".
// ---------------------------------------------------------------------------
namespace {

struct FakeWorld {
    std::vector<SeenDevice> devices;
    std::vector<std::vector<std::uint8_t>> sent;   // every frame that went out
    bool         sendWorks   = true;
    bool         replyArrives = true;
    std::uint8_t replyStatus = 0x01;
    unsigned     appearAfterMs = 450;              // [O] 448-489 ms
    SeenDevice   appearsAs{kProductIdBootloader, kBootloaderRelease,
                           kUsagePageVendor, kUsageVendor, "Bootloader", "EGG"};
    bool         everAppears = true;
    unsigned     clock = 0;

    EntryEnv env() {
        EntryEnv e;
        e.nowMs   = [this] { return clock; };
        e.sleepMs = [this](unsigned ms) {
            clock += ms;
            // Swap when the thing we are WAITING FOR is not yet present. Keyed
            // on appearsAs, not on a hardcoded PID: this world has to model the
            // transition in both directions, and the first version only worked
            // for entry.
            if (everAppears && clock >= appearAfterMs) {
                bool present = false;
                for (auto& d : devices)
                    if (d.productId == appearsAs.productId) present = true;
                if (!present) {
                    devices.clear();
                    devices.push_back(appearsAs);
                }
            }
        };
        e.enumerate = [this] { return devices; };
        e.exchange  = [this](const std::vector<std::uint8_t>& f,
                             std::vector<std::uint8_t>& reply, bool& readOk) {
            sent.push_back(f);
            if (!sendWorks) return false;
            clock += 2;
            readOk = replyArrives;
            if (replyArrives) { reply.assign(64, 0); reply[1] = replyStatus; }
            return true;
        };
        return e;
    }
};

// EXACTLY what the real mouse publishes, read off it 2026-09-05 with
// `egg-config devices`: SEVEN interfaces on one PID, of which precisely one is
// the vendor collection. The earlier version of this helper had a single
// interface, which is why the first hardware run of stage 2 refused a working
// device while every test passed. A mock that is tidier than the device is not
// a mock, it is a second copy of the code's assumptions.
const std::uint16_t kRealInterfaces[7][2] = {
    {0x0001, 0x06}, {0xff01, 0x02}, {0x000c, 0x01},
    {0xff02, 0x01}, {0xff02, 0x02}, {0x0001, 0x02}, {0x0001, 0x01},
};

void pushRealMouse(FakeWorld& w, std::uint16_t pid, const char* product) {
    for (const auto& i : kRealInterfaces)
        w.devices.push_back({pid, 0x0110, i[0], i[1], product, "Endgame Gear"});
}

FakeWorld appOnly() {
    FakeWorld w;
    pushRealMouse(w, kProductIdApplication, "Endgame Gear OP1 8k v2 Gaming Mouse");
    return w;
}

}  // namespace

static void testStage2Entry() {
    std::printf("\nstage 2 -- bootloader entry, against an adversarial world\n");

    // The frame, byte for byte against what Endgame's tool put on the wire in
    // 08-flash.pcapng. Not against our intent: against the capture.
    {
        FakeWorld w = appOnly();
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        const std::uint8_t want[8] = {0xa1, 0x3a, 0x00, 0x00, 0x00, 0x5a, 0xa5, 0x32};
        bool exact = o.sent.size() == 64 && std::memcmp(o.sent.data(), want, 8) == 0;
        for (std::size_t i = 8; i < o.sent.size(); ++i) if (o.sent[i]) exact = false;
        ok("the frame is a1 3a 00 00 00 5a a5 32 + 56 zeros, exactly as captured", exact);
        ok("happy path confirms", o.result == EntryResult::EnteredAndConfirmed);
        ok("EXACTLY ONE report is ever sent", w.sent.size() == 1,
           "sent " + std::to_string(w.sent.size()));
        ok("it measures the re-enumeration", o.reenumerateMs >= 400 && o.reenumerateMs <= 500,
           std::to_string(o.reenumerateMs) + " ms");
    }

    // THE DEFECT THE FIRST HARDWARE RUN FOUND. Seven interfaces, one device.
    {
        FakeWorld w = appOnly();
        ok("the fake world has the real SEVEN interfaces on one PID",
           w.devices.size() == 7);
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("seven interfaces on one PID read as ONE device, not seven",
           o.result == EntryResult::EnteredAndConfirmed,
           describe(o.result, egg::kProductIdBootloader).c_str());
    }
    // A device with the right PID but NO vendor collection is not openable, so
    // it must not be counted as one either.
    {
        FakeWorld w;
        w.devices.push_back({kProductIdApplication, 0x0110, 0x0001, 0x06,
                             "Endgame Gear OP1 8k v2 Gaming Mouse", "Endgame Gear"});
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("PID present but no 0xff01/0x02 collection -> refuses, sends nothing",
           o.result == EntryResult::NoApplicationDevice && w.sent.empty());
    }

    // Nothing to send to -> nothing sent. This is the one that protects the
    // mouse from a half-understood state, so it is asserted on the SEND COUNT
    // and not merely on the result code.
    {
        FakeWorld w;  // no devices at all
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("no application device -> refuses", o.result == EntryResult::NoApplicationDevice);
        ok("...and sends NOTHING", w.sent.empty());
    }
    {
        FakeWorld w = appOnly();
        pushRealMouse(w, kProductIdApplication, "Endgame Gear OP1 8k v2 Gaming Mouse");
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("TWO application devices -> refuses rather than picking one",
           o.result == EntryResult::NoApplicationDevice);
        ok("...and sends NOTHING", w.sent.empty());
    }
    {
        FakeWorld w;
        pushRealMouse(w, kProductIdBootloader, "Bootloader");
        for (auto& d : w.devices) d.releaseNumber = kBootloaderRelease;
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("already in the bootloader -> confirms without sending A1 3A",
           o.result == EntryResult::EnteredAndConfirmed && w.sent.empty());
    }

    // Identity is checked on all three fields, not just the PID.
    {
        FakeWorld w = appOnly();
        w.appearsAs = {kProductIdBootloader, 0x0007, kUsagePageVendor, kUsageVendor,
                       "Bootloader", "EGG"};
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("wrong bcdDevice on 0x1977 -> REFUSED, not accepted",
           o.result == EntryResult::UnrecognisedIdentity);
        ok("...and it reports what it refused", o.seen.releaseNumber == 0x0007);
    }
    {
        FakeWorld w = appOnly();
        w.appearsAs = {kProductIdBootloader, kBootloaderRelease, kUsagePageVendor,
                       kUsageVendor, "Bootloadr", "EGG"};
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("wrong product string on 0x1977 -> REFUSED",
           o.result == EntryResult::UnrecognisedIdentity);
    }

    // The vendor does not consult resp[1] and neither do we: the PID change is
    // the evidence. These two pin that decision so a later "tidy-up" cannot
    // quietly add a status gate.
    {
        FakeWorld w = appOnly();
        w.replyStatus = 0x00;
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("resp[1]=0x00 but the device re-enumerated -> still confirmed",
           o.result == EntryResult::EnteredAndConfirmed);
        ok("...and the status is still reported", o.status == 0x00 && o.replyRead);
    }
    {
        FakeWorld w = appOnly();
        w.replyArrives = false;
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("no reply at all but the device re-enumerated -> still confirmed",
           o.result == EntryResult::EnteredAndConfirmed && !o.replyRead);
    }

    // Failures, and that they are BOUNDED. §4.2: before erase, abort is right.
    {
        FakeWorld w = appOnly();
        w.sendWorks = false;
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("send failure -> SendFailed and no waiting", o.result == EntryResult::SendFailed);
    }
    {
        FakeWorld w = appOnly();
        w.everAppears = false;
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("device never comes back -> NoReenumeration, does not hang",
           o.result == EntryResult::NoReenumeration);
        ok("...and it gave up at our ceiling, not the vendor's 22.5 s",
           w.clock <= kEntryPollCeilMs + 100,
           std::to_string(w.clock) + " ms");
        ok("...having still sent exactly one report", w.sent.size() == 1);
    }
    {
        FakeWorld w = appOnly();
        w.appearAfterMs = 9000;   // slow, but inside the ceiling
        EntryEnv e = w.env();
        const EntryOutcome o = enterBootloaderAndConfirm(e);
        ok("a slow re-enumeration inside the ceiling is still accepted",
           o.result == EntryResult::EnteredAndConfirmed);
    }
}


static void testStage2Exit() {
    std::printf("\nthe way back -- A1 09, against an adversarial world\n");

    // A world that starts in the bootloader and returns to the application.
    auto blWorld = [] {
        FakeWorld w;
        pushRealMouse(w, kProductIdBootloader, "Bootloader");
        for (auto& d : w.devices) { d.releaseNumber = kBootloaderRelease; d.manufacturer = "EGG"; }
        w.appearsAs = {kProductIdApplication, 0x0110, kUsagePageVendor, kUsageVendor,
                       "Endgame Gear OP1 8k v2 Gaming Mouse", "Endgame Gear"};
        w.appearAfterMs = 880;   // [O] 857-896 ms in the captures
        return w;
    };

    {
        FakeWorld w = blWorld();
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        const std::uint8_t want[4] = {0xa1, 0x09, 0x00, 0x00};
        bool exact = o.sent.size() == 64 && std::memcmp(o.sent.data(), want, 4) == 0;
        for (std::size_t i = 4; i < o.sent.size(); ++i) if (o.sent[i]) exact = false;
        ok("the frame is a1 09 + 62 zeros, exactly as captured", exact);
        ok("it comes back to the application", o.result == EntryResult::EnteredAndConfirmed,
           describe(o.result, egg::kProductIdBootloader).c_str());
        ok("EXACTLY ONE report is ever sent", w.sent.size() == 1);
        ok("and it is the application collection it reports",
           o.seen.productId == kProductIdApplication);
    }
    {
        // Already out. Must not send A1 09 to an application device -- the
        // mirror of the entry path's already-in-bootloader guard.
        FakeWorld w = appOnly();
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        ok("already in application mode -> sends NOTHING",
           o.result == EntryResult::EnteredAndConfirmed && w.sent.empty());
    }
    {
        FakeWorld w;   // nothing attached
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        ok("no device at all -> refuses, sends nothing",
           o.result == EntryResult::NoApplicationDevice && w.sent.empty());
    }
    {
        // The failure that actually matters: the command does nothing. The
        // mouse must be reported as still in the bootloader, not as fixed.
        FakeWorld w = blWorld();
        w.everAppears = false;
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        ok("A1 09 does nothing -> NoReenumeration, reported honestly",
           o.result == EntryResult::NoReenumeration);
        ok("...bounded by the exit ceiling, and still one report",
           w.clock <= kExitPollCeilMs + 200 && w.sent.size() == 1,
           std::to_string(w.clock) + " ms");
    }
    {
        // resp[1] is not a gate here either. The device answered 0x03 to A1 3A
        // where both captures showed 0x01, so gating on an undocumented status
        // is a way to report failure on a success.
        FakeWorld w = blWorld();
        w.replyStatus = 0x03;
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        ok("odd resp[1] but the device came back -> still confirmed",
           o.result == EntryResult::EnteredAndConfirmed && o.status == 0x03);
    }
    {
        FakeWorld w = blWorld();
        w.sendWorks = false;
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        ok("send failure -> SendFailed, no waiting", o.result == EntryResult::SendFailed);
    }
    {
        FakeWorld w = blWorld();
        w.appearAfterMs = 25000;   // slow, inside the 30 s exit ceiling
        EntryEnv e = w.env();
        const EntryOutcome o = leaveBootloaderAndConfirm(e);
        ok("a slow return inside the exit ceiling is accepted",
           o.result == EntryResult::EnteredAndConfirmed);
    }
}


// ---------------------------------------------------------------------------
// §4.4 stage 3 -- the read-back -- and §4.2c's approval token.
// ---------------------------------------------------------------------------
static void testReadBackAndToken() {
    std::printf("\nread-back (A0 07) and the approval token\n");

    const auto img = synthetic();

    // (a) A device that serves its resident image. Read-back must return it
    //     byte-exactly, from the right blocks, in the right order.
    {
        MockBootloader dev;
        dev.preload(img);
        const ReadBack rb = readApplicationRegion(dev);
        ok("reads a resident image back", rb.ok, rb.error);
        ok("all 65 blocks", rb.blocksRead == kBlockCount,
           std::to_string(rb.blocksRead));
        ok("byte-identical to what the device holds", rb.image == img);
        ok("NOTHING was written", dev.writtenIndices().empty() && !dev.started());
    }

    // (b) A device that refuses A0 07 outside a flash session -- the OTHER
    //     hypothesis. The read must ABORT, promptly, and say what it saw.
    //     Before erase, abort is always correct (§4.2); the failure this
    //     guards against is a read-back that spins forever the way the
    //     post-erase phase deliberately does.
    {
        MockBootloader dev;                    // nothing preloaded
        const ReadBack rb = readApplicationRegion(dev);
        ok("a device that refuses A0 07 -> aborts, does not hang", !rb.ok);
        ok("and reports the block it stopped on", rb.error.find("0x34") != std::string::npos,
           rb.error);
        ok("and still writes NOTHING", dev.writtenIndices().empty() && !dev.started());
    }

    // (c) A flaky device inside the retry budget still yields a good backup.
    {
        Faults f; f.rejectRate = 0.25; f.seed = 7;
        MockBootloader dev(f);
        dev.preload(img);
        const ReadBack rb = readApplicationRegion(dev);
        // Not asserting success -- with rejections it may legitimately give up.
        // Asserting the SAFE property: whatever happens, nothing was written.
        ok("under rejections, still no writes and no erase",
           dev.writtenIndices().empty() && !dev.started());
        if (rb.ok) ok("and any image it does return is correct", rb.image == img);
    }

    // ---- The token. §4.2c. -------------------------------------------------
    Image real;
    std::string err;
    if (real.loadFromExecutable(kExe, err)) {
        const std::string t1 = confirmToken(real);
        const std::string t2 = confirmToken(real);
        ok("the token is deterministic", t1 == t2, t1);
        ok("and is 8 hex characters", t1.size() == kConfirmTokenChars);

        // The property that makes it worth having: a DIFFERENT plan must not
        // be approvable with this plan's token.
        const auto frames = plannedFrames(real);

        ok("the plan is enter + start + 2/block + wholesum + complete + reset",
           frames.size() == 5 + real.blockCount() * 2,
           std::to_string(frames.size()));

        // §4.2b: entry is by button, so A1 3A must NOT be in the plan.
        // A1 13 must not be either -- wiping settings is not part of a flash.
        bool hasEnter = false, hasReset = false, allInRange = true;
        for (const auto& f : frames) {
            if (f.size() >= 2 && f[0] == kReportSmall && f[1] == 0x3A) hasEnter = true;
            if (f.size() >= 2 && f[0] == kReportSmall && f[1] == 0x13) hasReset = true;
            if (f.size() >= 3 && f[0] == kReportLarge &&
                (f[1] == 0x06 || f[1] == 0x07)) {
                const unsigned bi = (f[1] == 0x06)
                    ? static_cast<unsigned>(f[2] | (f[3] << 8))
                    : static_cast<unsigned>(f[2]);
                if (bi < kBlockFirst || bi > kBlockLast) allInRange = false;
            }
        }
        // THE property that makes the token worth having: it must be a
        // function of the IMAGE, not just of the command sequence. If
        // plannedFrames ever stopped carrying payloads, every image would
        // produce the same token and --confirm would be theatre. Checked by
        // requiring each block's 1024 bytes to appear, at the right offset, in
        // the frames the token is computed over.
        bool payloadsPresent = true;
        for (std::size_t i = 0; i < real.blockCount() && payloadsPresent; ++i) {
            const Frame& w = frames[2 + i * 2];          // enter, start, then (write,read)*
            if (w.size() != kLargeLen || w[1] != 0x06 ||
                std::memcmp(w.data() + kPayloadOffset, real.block(i), kBlockSize) != 0)
                payloadsPresent = false;
        }
        ok("the plan carries the image bytes at all", payloadsPresent);

        // THE test that matters, and the reason this one exists at all: the
        // version above passed while a mutant that made confirmToken() hash
        // only the FIRST frame SURVIVED. Asserting a property of plannedFrames
        // says nothing about the function that consumes it. §6.2 -- the hole
        // was found by mutation testing, not by review.
        ok("token(all frames) != token(first frame only)",
           confirmTokenForFrames(frames) !=
           confirmTokenForFrames({frames.front()}));

        // And it must move when the IMAGE moves, not merely when the command
        // list does. One flipped payload byte, everything else identical.
        {
            std::vector<Frame> tweaked = frames;
            Frame& firstWrite = tweaked[2];   // enter, start, FIRST WRITE
            firstWrite[kPayloadOffset] =
                static_cast<std::uint8_t>(firstWrite[kPayloadOffset] ^ 0x01);
            ok("one flipped payload byte changes the token",
               confirmTokenForFrames(tweaked) != confirmTokenForFrames(frames));
        }

        ok("confirmToken(img) == confirmTokenForFrames(plannedFrames(img))",
           confirmToken(real) == confirmTokenForFrames(frames));

        // §4.2b as REVISED 2026-09-05: a flash takes the vendor's entry, because
        // their proven sequence begins with it and nothing has ever flashed a
        // button-entered bootloader. The earlier version of this test asserted
        // the opposite; it is inverted rather than deleted so the change of
        // decision is visible in the diff.
        ok("the plan BEGINS with A1 3A, as the vendor's does", hasEnter);
        ok("and A1 3A is literally frame 0",
           frames.front() == enterBootloader());
        // Reversed 2026-09-05. The reason for omitting A1 13 had been
        // aesthetic; the reasons for keeping it are not. It is [D] in two
        // binaries and [O] at 21/21 on this mouse, the undo is verified, and
        // leaving stale settings under new firmware invents a state the
        // vendor's tool never produces (payload +0x71 differs between 1.07 and
        // 1.10). Inverted rather than deleted, so the reversal shows in a diff.
        ok("the plan ENDS with A1 13, as the vendor's does", hasReset);
        ok("and A1 13 is literally the last frame",
           frames.back() == postSuccess());
        ok("every block index in the plan is inside [0x34,0x74]", allInRange);
    } else {
        std::printf("  SKIP  token tests -- %s\n", err.c_str());
    }
}


// ---------------------------------------------------------------------------
// 10. The backup gate. §4.2: "never erase without a saved copy."
//
// `flash` no longer takes the backup -- decided 2026-09-05 -- so this check is
// ONLY thing standing between a bad or absent backup and an erase. It replaced
// an in-flash A0 07 read-back, i.e. it took over a guarantee that used to be
// enforced by actually doing the read. A check that replaces a stronger
// mechanism has to be tested harder than the mechanism it replaced, not less.
//
// Every case below is a file somebody actually ends up with.
// ---------------------------------------------------------------------------
static std::string tmpPath(const char* tag) {
    std::string p = "/tmp/egg-backup-test-";
    p += tag;
    p += ".bin";
    return p;
}

static void writeFile(const std::string& path, const std::vector<std::uint8_t>& b) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { if (b.size()) std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
}

static void testBackupGate() {
    std::printf("\n10. the backup gate (§4.2)\n");

    const std::size_t kFull = kBlockCount * kBlockSize;

    ok("an empty path is refused", !checkBackupFile("").ok);
    ok("...and says so rather than reporting a size",
       checkBackupFile("").reason.find("named") != std::string::npos);

    const std::string missing = tmpPath("definitely-not-here");
    std::remove(missing.c_str());
    ok("a path that does not exist is refused", !checkBackupFile(missing).ok);

    // Length. The interesting one is +1: a reader that asks for exactly kFull
    // bytes and checks the count accepts a too-long file by truncation.
    struct { const char* tag; std::size_t n; } sizes[] = {
        {"empty", 0}, {"short", 100}, {"minus1", kFull - 1},
        {"plus1", kFull + 1}, {"double", kFull * 2},
    };
    bool allSizesRefused = true, plus1NamesRealSize = false;
    for (const auto& c : sizes) {
        std::vector<std::uint8_t> b(c.n);
        for (std::size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<std::uint8_t>(i * 7 + 1);
        const std::string p = tmpPath(c.tag);
        writeFile(p, b);
        const BackupCheck bc = checkBackupFile(p);
        if (bc.ok) allSizesRefused = false;
        if (std::string(c.tag) == "plus1")
            plus1NamesRealSize = bc.reason.find(std::to_string(kFull + 1)) != std::string::npos;
        std::remove(p.c_str());
    }
    ok("every wrong length is refused, including one byte too long", allSizesRefused);
    ok("...and a too-long file is reported at its REAL size, not truncated",
       plus1NamesRealSize);

    // Uniform content of the right length: what a failed read leaves behind.
    bool allUniformRefused = true;
    for (int fill : {0x00, 0xFF, 0x5A}) {
        const std::string p = tmpPath("flat");
        writeFile(p, std::vector<std::uint8_t>(kFull, static_cast<std::uint8_t>(fill)));
        if (checkBackupFile(p).ok) allUniformRefused = false;
        std::remove(p.c_str());
    }
    ok("a right-sized file of one repeated byte is refused as a failed read",
       allUniformRefused);

    // A single differing byte is enough to be "varied" -- deliberately, because
    // this check is about detecting a failed read, not grading firmware. Pinned
    // so that tightening it later is a visible decision rather than a drift.
    {
        std::vector<std::uint8_t> b(kFull, 0xAA);
        b[kFull - 1] = 0xAB;           // the LAST byte, so a loop that stops
        const std::string p = tmpPath("onediff");  // early would miss it
        writeFile(p, b);
        ok("one differing byte, at the very end, counts as varied",
           checkBackupFile(p).ok);
        std::remove(p.c_str());
    }

    // The positive case, and the fields the CLI prints from.
    {
        const std::vector<std::uint8_t> img = synthetic();
        const std::string p = tmpPath("good");
        writeFile(p, img);
        const BackupCheck bc = checkBackupFile(p);
        ok("a varied file of exactly the right length is accepted", bc.ok);
        ok("...with no reason attached", bc.reason.empty());
        ok("...its size reported exactly", bc.size == kFull);
        ok("...and its sha256 is the sha256 OF THE FILE",
           bc.sha256 == sha256Hex(img.data(), img.size()), bc.sha256);

        // The check must read the file, not remember an earlier answer: flip
        // one byte and the digest must move.
        std::vector<std::uint8_t> img2 = img;
        img2[12345] ^= 0xFF;
        writeFile(p, img2);
        const BackupCheck bc2 = checkBackupFile(p);
        ok("a different file gives a different digest", bc2.ok && bc2.sha256 != bc.sha256);
        std::remove(p.c_str());
    }

    // AND THE PROPERTY THE WHOLE CHANGE RESTS ON: checking a backup emits no
    // frames. It is a file read, so this is true by construction -- pinned
    // because "by construction" is exactly the phrase §4.2a bans relying on.
    {
        std::string err;
        Image img;
        if (img.loadFromExecutable(kExe, err)) {
            const auto before = plannedFrames(img);
            const std::string p = tmpPath("good2");
            writeFile(p, synthetic());
            (void)checkBackupFile(p);
            std::remove(p.c_str());
            const auto after = plannedFrames(img);
            ok("checking a backup changes nothing about the plan", before == after);
            bool anyReadBeforeErase = false;
            for (const auto& f : after) {
                if (f.size() > 1 && f[0] == kReportLarge && f[1] == 0x03) break;
                if (f.size() > 1 && f[0] == kReportLarge && f[1] == 0x07)
                    anyReadBeforeErase = true;
            }
            ok("and the plan still emits NO A0 07 before A0 03", !anyReadBeforeErase);
        }
    }
}


// ---------------------------------------------------------------------------
// 11. WHAT THE CODE ACTUALLY SENDS == WHAT THE PLAN SAYS IT WILL.
//
// The gap this closes, found 2026-09-05 by driving the real path and counting.
// Tests/test_golden_vendor.py proves plannedFrames() == Endgame's capture, and
// `egg-flash stream` prints plannedFrames(), so the PLAN was well guarded. But
// nothing checked that preflight() + driveToVerifiedImage() -- the code that
// actually reaches the mouse -- emit those frames. They did not: preflight sent
// a "benign" A1 08 round trip before A0 03, so the wire carried 134 frames
// where the plan had 133, and every existing test passed.
//
// The plan was therefore a SECOND DESCRIPTION of the sequence, and second
// descriptions drift. This test makes the plan and the execution path check
// each other, so the chain is complete:
//
//     what we send  ==  plannedFrames()  ==  the vendor's capture
//        (here)          (golden_vendor, against windows-run/08-flash.pcapng)
//
// Two frames in the plan do not belong to this link and are excluded by name,
// not by position: A1 3A goes to the APPLICATION device before the bootloader
// exists, and A1 13 goes to it again after it comes back (§5.4 steps 1 and 7).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 10a. The provenance gate on restore-firmware, and the bootloader entry
// receipt. Both were decided on 2026-09-06 and both are OFF-WIRE guards --
// §4.2b: "refusing, checking a file, pinning a hash, requiring a token -- cost
// nothing and are always allowed."
//
// §6.2 applies here as much as anywhere: a gate that refuses everything is not
// a gate, it is a broken loader. So the positive case is asserted first and the
// negatives are all ONE MUTATION away from it -- same file, same sidecar, one
// thing changed -- rather than being separately constructed rubbish that would
// fail for reasons unrelated to the property under test.
// ---------------------------------------------------------------------------
static std::vector<std::uint8_t> variedImage(std::uint8_t seed) {
    std::vector<std::uint8_t> b(kBlockCount * kBlockSize);
    for (std::size_t i = 0; i < b.size(); ++i)
        b[i] = static_cast<std::uint8_t>(i * 31u + seed);
    return b;
}

static void testProvenanceGate() {
    std::printf("\n10a. restore provenance and the entry receipt\n");

    const std::string img = tmpPath("prov-image");
    const std::string side = provenancePathFor(img);
    const std::vector<std::uint8_t> bytes = variedImage(0x11);
    writeFile(img, bytes);
    const std::string realSha = sha256Hex(bytes.data(), bytes.size());

    // --- refuse with no sidecar at all --------------------------------------
    std::remove(side.c_str());
    {
        Image im; std::string err;
        const bool loaded = im.loadFromBackup(img, err);
        ok("a 66560-byte image with NO sidecar is refused", !loaded);
        ok("...and the refusal names the .origin file it wanted",
           err.find(".origin") != std::string::npos, err.substr(0, 60));
    }

    // --- the positive case, and it must actually pass ------------------------
    {
        std::string werr;
        const bool wrote = writeProvenance(img, realSha, bytes.size(),
                                           kProductIdBootloader,
                                           kBootloaderRelease,
                                           kBootloaderProduct, "a1-3a", werr);
        ok("writeProvenance writes the sidecar", wrote, werr);
        Image im; std::string err;
        const bool loaded = im.loadFromBackup(img, err);
        // THE PLANTED POSITIVE. If this ever fails, every refusal below is
        // meaningless -- they would all be passing because nothing loads.
        ok("a backup WITH a matching sidecar loads", loaded, err);
        ok("...and its sha256 is the file's own", loaded && im.sha256() == realSha);
        ok("...and it is 65 blocks", loaded && im.blockCount() == kBlockCount);
        ok("...and its checksum is the 32-bit byte sum",
           loaded && im.checksum() == wholeImageChecksum(bytes));
        // And what writeProvenance wrote, readProvenance reads back. The two
        // ends are separate functions and a format drift between them would
        // show up only as an unexplained refusal months later.
        const Provenance p = readProvenance(img);
        ok("the sidecar round-trips: sha256", p.ok && p.sha256 == realSha);
        ok("the sidecar round-trips: size", p.ok && p.size == bytes.size());
        ok("the sidecar round-trips: bcdDevice",
           p.ok && p.bcdDevice == kBootloaderRelease);
        ok("the sidecar round-trips: product",
           p.ok && p.product == std::string(kBootloaderProduct));
        ok("the sidecar round-trips: entry", p.ok && p.entry == "a1-3a");
        ok("the sidecar records a timestamp", p.ok && p.taken.size() >= 20, p.taken);
    }

    // --- ONE BYTE of the image changed, sidecar untouched --------------------
    // The failure this exists for: a backup edited, truncated-and-repadded, or
    // swapped for a different file that happens to sit at the same path.
    {
        std::vector<std::uint8_t> tampered = bytes;
        tampered[40000] ^= 0x01;
        writeFile(img, tampered);
        Image im; std::string err;
        const bool loaded = im.loadFromBackup(img, err);
        ok("ONE flipped bit in the image is refused", !loaded);
        ok("...and the refusal prints both hashes",
           err.find(realSha) != std::string::npos &&
           err.find(sha256Hex(tampered.data(), tampered.size())) != std::string::npos);
        writeFile(img, bytes);   // put it back for the cases below
    }

    // --- a sidecar that is not ours -----------------------------------------
    {
        std::FILE* f = std::fopen(side.c_str(), "wb");
        std::fprintf(f, "some other tool v3\nsha256 %s\nsize %zu\n",
                     realSha.c_str(), bytes.size());
        std::fclose(f);
        Image im; std::string err;
        ok("a sidecar with the wrong magic line is refused",
           !im.loadFromBackup(img, err));
        ok("...and says it is not an egg-flash provenance file",
           err.find("not an egg-flash") != std::string::npos, err.substr(0, 60));
    }

    // --- a sidecar with our magic but no hash in it --------------------------
    // Fails closed either way -- an empty recorded hash never equals a real
    // one -- so this is asserted on the REASON, not just the refusal. A gate
    // that refuses for the wrong reason sends the user looking for an override
    // instead of for the missing line.
    {
        std::FILE* f = std::fopen(side.c_str(), "wb");
        std::fprintf(f, "egg-flash backup provenance v1\nsize %zu\n", bytes.size());
        std::fclose(f);
        Image im; std::string err;
        ok("a sidecar with no sha256 line is refused", !im.loadFromBackup(img, err));
        ok("...and says the sidecar records no hash, not that the image changed",
           err.find("records no sha256") != std::string::npos, err.substr(0, 70));
    }

    // --- a sidecar that disagrees with ITSELF --------------------------------
    // sha256 right, size wrong. Redundant with the hash and checked anyway: a
    // sidecar contradicting itself is corrupt, and noticing costs nothing.
    {
        std::string werr;
        writeProvenance(img, realSha, bytes.size() + 1, kProductIdBootloader,
                        kBootloaderRelease, kBootloaderProduct, "a1-3a", werr);
        Image im; std::string err;
        ok("a sidecar whose size disagrees with the image is refused",
           !im.loadFromBackup(img, err), err.substr(0, 60));
    }

    // --- the structural checks still run, sidecar or no sidecar -------------
    // A VALID sidecar over a file that is not a plausible image. This is the
    // one that matters most: the provenance gate must be an ADDITION to the
    // shape checks, never a way round them. A failed A0 07 loop that got saved
    // and then had a sidecar written for it is exactly this shape.
    {
        const std::string flat = tmpPath("prov-flat");
        std::vector<std::uint8_t> uniform(kBlockCount * kBlockSize, 0xff);
        writeFile(flat, uniform);
        std::string werr;
        writeProvenance(flat, sha256Hex(uniform.data(), uniform.size()),
                        uniform.size(), kProductIdBootloader,
                        kBootloaderRelease, kBootloaderProduct, "a1-3a", werr);
        Image im; std::string err;
        ok("66560 IDENTICAL bytes are refused even with a valid sidecar",
           !im.loadFromBackup(flat, err));
        ok("...and it is named as a failed read, not a hash problem",
           err.find("failed read") != std::string::npos, err.substr(0, 70));

        const std::string shortp = tmpPath("prov-short");
        std::vector<std::uint8_t> shortb(bytes.begin(), bytes.begin() + 1024);
        writeFile(shortp, shortb);
        writeProvenance(shortp, sha256Hex(shortb.data(), shortb.size()),
                        shortb.size(), kProductIdBootloader, kBootloaderRelease,
                        kBootloaderProduct, "a1-3a", werr);
        Image im2; std::string err2;
        ok("a SHORT file is refused even with a valid sidecar",
           !im2.loadFromBackup(shortp, err2));
        ok("...and the size is named", err2.find("1024") != std::string::npos,
           err2.substr(0, 70));

        // AND A TOO-LONG ONE. Short and long are not the same test: a loader
        // that asks "is this at least an image?" refuses the short file and
        // accepts the long one by silently taking its first 66560 bytes. That
        // is how a concatenation, or a firmware image for another product with
        // something appended, becomes a restore. checkBackupFile has the same
        // case for the same reason.
        const std::string longp = tmpPath("prov-long");
        std::vector<std::uint8_t> longb = bytes;
        longb.push_back(0x5a);
        writeFile(longp, longb);
        writeProvenance(longp, sha256Hex(longb.data(), longb.size()),
                        longb.size(), kProductIdBootloader, kBootloaderRelease,
                        kBootloaderProduct, "a1-3a", werr);
        Image im3; std::string err3;
        ok("a file ONE BYTE too long is refused, not truncated",
           !im3.loadFromBackup(longp, err3));
        ok("...and the real size is named",
           err3.find(std::to_string(longb.size())) != std::string::npos,
           err3.substr(0, 70));
        std::remove(provenancePathFor(longp).c_str()); std::remove(longp.c_str());
        std::remove(provenancePathFor(flat).c_str());   std::remove(flat.c_str());
        std::remove(provenancePathFor(shortp).c_str()); std::remove(shortp.c_str());
    }

    std::remove(side.c_str());
    std::remove(img.c_str());

    // --- sameFile: the guard that keeps a restore's two roles distinct -------
    {
        const std::string a = tmpPath("samefile-a");
        std::vector<std::uint8_t> b(16, 0x5a); b[3] = 1;
        writeFile(a, b);
        ok("sameFile: a path equals itself", sameFile(a, a));
        // THE CASE THE LEXICAL COMPARE MISSED. Same file, two spellings.
        const std::string dotted = "/tmp/./" + a.substr(a.rfind('/') + 1);
        ok("sameFile: /tmp/x and /tmp/./x are the same file",
           sameFile(a, dotted), a + "  vs  " + dotted);
        const std::string other = tmpPath("samefile-b");
        writeFile(other, b);
        ok("sameFile: two distinct files with IDENTICAL contents are not the\n"
           "        same file -- this guard is about identity, not bytes",
           !sameFile(a, other));
        ok("sameFile: an empty path is never the same as anything",
           !sameFile("", a) && !sameFile(a, "") && !sameFile("", ""));
        // Neither resolves. They must not be called the same file just because
        // realpath failed on both.
        ok("sameFile: two different unresolvable paths are not the same",
           !sameFile("/no/such/aaa", "/no/such/bbb"));
        std::remove(a.c_str()); std::remove(other.c_str());
    }

    // --- the recovery text must not send someone into the gate ---------------
    // FOUND BY WRITING THE GATE, NOT BY A TEST FAILING. kRecoveryProcedure
    // tells the reader to hold LEFT+RIGHT and "run this command again" -- and
    // the receipt gate refuses exactly that. A guard added anywhere has to be
    // checked against every text that tells someone what to do next, and this
    // is the second time that has bitten in this file's history (the first is
    // recorded at cmdLeaveBootloader's failure path).
    //
    // Pinned as a test because the coupling is invisible: the text lives in
    // WritePhase.cpp, the gate in Provenance.cpp, and nothing but this
    // assertion connects them.
    {
        // THERE USED TO BE TWO CONSTANTS WITH THIS NAME -- the long one
        // egg-flash prints and a short one egg-config printed -- saying the
        // same things in different words. On 2026-09-06 the gate made both
        // wrong and only one was noticed; the COMPILER found the second, by
        // reporting the name as ambiguous right here. That is luck, not a
        // check, and a comment saying "amend both or neither" is not one
        // either.
        //
        // They are collapsed now: egg::kButtonEntrySteps and
        // egg::kButtonEntryReflashNote (Protocol.h) are the only definitions,
        // egg-config prints them directly, and egg::fw::kRecoveryProcedure is
        // BUILT from them. So the property to test is no longer "do the two
        // agree" -- it is that the flasher's text really is composed from the
        // shared pieces rather than having quietly grown its own copy again.
        const std::string rec = egg::fw::kRecoveryProcedure;
        const std::string steps = egg::kButtonEntrySteps;
        const std::string note = egg::kButtonEntryReflashNote;

        ok("the flasher's recovery text CONTAINS the shared button-entry steps,\n"
           "        so amending them cannot reach one printout and miss the other",
           rec.find(steps.substr(0, steps.find('\n'))) != std::string::npos,
           rec.substr(0, 60));
        ok("...and contains the shared re-flash note",
           rec.find(note.substr(0, note.find('\n'))) != std::string::npos);
        ok("the shared steps describe the button entry they are named for",
           steps.find("LEFT and RIGHT") != std::string::npos);
        ok("the shared note names the flag that gets a button-entered\n"
           "        bootloader past the receipt gate -- without it, following\n"
           "        the text to the letter ends in a refusal",
           note.find("--i-know-this-is-button-entered") != std::string::npos);
        ok("...and the flasher's text therefore names it too",
           rec.find("--i-know-this-is-button-entered") != std::string::npos);
        // The composition must actually indent, or the flasher's block would
        // read as unrelated paragraphs glued together.
        ok("...and adds the flasher-only 'run this command again', which the\n"
           "        shared text must NOT carry (egg-config prints that text in a\n"
           "        paragraph where it would mean re-run egg-config)",
           rec.find("run this command again") != std::string::npos &&
           steps.find("run this command again") == std::string::npos &&
           note.find("run this command again") == std::string::npos);
        ok("the flasher indents the shared text into its own block",
           rec.find("\n  Hold LEFT and RIGHT") != std::string::npos ||
           rec.find("  Hold LEFT and RIGHT") != std::string::npos);
    }

    // --- the entry receipt ---------------------------------------------------
    // It lives in the CURRENT DIRECTORY by design, so this chdirs into a temp
    // directory rather than dropping .egg-entry-receipt into the repo root --
    // where it would both pollute the tree and be picked up by the tree_clean
    // test as an uncommitted file.
    {
        // HOME, not the working directory -- the receipt moved there on
        // 2026-09-06 because a Finder-launched .app has cwd "/" and could never
        // write one. So this test redirects HOME rather than chdir-ing, and
        // that difference is the point: if it still passed after a chdir alone,
        // the path would still be cwd-relative and the GUI would still be
        // broken.
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome ? oldHome : "";
        char tmpl[] = "/tmp/egg-receipt-XXXXXX";
        const char* dir = mkdtemp(tmpl);
        ok("receipt: a scratch HOME to run in", dir != nullptr);
        if (dir && setenv("HOME", dir, 1) == 0) {
            ok("no receipt in a fresh directory", !readEntryReceipt().present);
            ok("...and the reason names the absolute path it looked for",
               readEntryReceipt().reason.find(dir) != std::string::npos,
               readEntryReceipt().reason);
            ok("entryReceiptPath() is the receipt in HOME",
               entryReceiptPath() == std::string(dir) + "/" + kEntryReceiptName,
               entryReceiptPath());
            // AND IT DOES NOT MOVE WHEN THE WORKING DIRECTORY DOES. This is the
            // assertion that actually pins the GUI fix: chdir somewhere else
            // and the path must be unchanged.
            {
                char before[4096];
                const bool got = getcwd(before, sizeof before) != nullptr;
                const std::string p1 = entryReceiptPath();
                const bool moved = (chdir("/") == 0);
                ok("the receipt path is INDEPENDENT of the working directory --\n"
                   "        a Finder-launched .app runs with cwd \"/\"",
                   moved && entryReceiptPath() == p1, entryReceiptPath());
                if (got) { if (chdir(before) != 0) {} }
            }

            std::string werr;
            const bool wrote = writeEntryReceipt(kBootloaderRelease,
                                                 kBootloaderProduct, werr);
            ok("writeEntryReceipt writes it", wrote, werr);
            const EntryReceipt r = readEntryReceipt();
            ok("...and it reads back as present", r.present);
            ok("...with the bcdDevice it was given",
               r.present && r.bcdDevice == kBootloaderRelease);
            ok("...with the product string it was given",
               r.present && r.product == std::string(kBootloaderProduct));
            ok("...and a timestamp", r.present && r.sent.size() >= 20, r.sent);

            // A file at the right path that is not ours must not count. The
            // shape this catches: any stray .egg-entry-receipt, including one
            // a user made by hand to get past the gate without reading what
            // the gate is for.
            {
                // entryReceiptPath(), not the bare name: the receipt is
                // HOME-relative now, and writing to the cwd would have this
                // test pass by looking at a file nothing reads.
                std::FILE* f = std::fopen(entryReceiptPath().c_str(), "wb");
                std::fprintf(f, "not a receipt\nbcd-device 0x0006\n");
                std::fclose(f);
                ok("a file with the wrong magic is NOT a receipt",
                   !readEntryReceipt().present);
            }

            writeEntryReceipt(kBootloaderRelease, kBootloaderProduct, werr);
            ok("re-written, it is present again", readEntryReceipt().present);
            clearEntryReceipt();
            ok("clearEntryReceipt removes it -- a completed flash clears the "
               "latch, so the receipt must not outlive it",
               !readEntryReceipt().present);
            clearEntryReceipt();   // idempotent; must not crash or throw
            ok("...and clearing an absent receipt is a no-op",
               !readEntryReceipt().present);

            // --- the gate itself, all four combinations ---------------------
            // Two booleans, so the table is small enough to be exhaustive, and
            // exhaustive is the right standard for a gate: the interesting
            // cases are the two where the answers disagree.
            {
                // no receipt, no override -> REFUSE
                EntryGate g = entryGate(false, kBootloaderRelease, kBootloaderProduct);
                ok("gate: no receipt and no override REFUSES", !g.allowed);
                ok("...and gives a reason naming the path",
                   g.reason.find(kEntryReceiptName) != std::string::npos,
                   g.reason);
                ok("...and does not claim to have been overridden",
                   !g.overridden);

                // no receipt, override -> ALLOW, and say it was overridden
                g = entryGate(true, kBootloaderRelease, kBootloaderProduct);
                ok("gate: no receipt WITH the override allows", g.allowed);
                ok("...and reports that it was overridden", g.overridden);

                std::string gerr;
                writeEntryReceipt(kBootloaderRelease, kBootloaderProduct, gerr);

                // receipt, no override -> ALLOW, NOT as an override
                g = entryGate(false, kBootloaderRelease, kBootloaderProduct);
                ok("gate: a receipt and no override allows", g.allowed);
                ok("...and is NOT reported as an override -- this is the normal\n"
                   "        path and must not print the override warning",
                   !g.overridden);
                ok("...and carries the receipt it found",
                   g.receipt.present &&
                   g.receipt.bcdDevice == kBootloaderRelease);

                // receipt AND override -> ALLOW, and the receipt wins the
                // wording: the override changed nothing, so saying it did
                // would teach the user the flag is needed when it is not.
                g = entryGate(true, kBootloaderRelease, kBootloaderProduct);
                ok("gate: a receipt plus the override still allows", g.allowed);
                ok("...and the receipt wins: not reported as an override",
                   !g.overridden);

                // --- and a receipt about a DIFFERENT bootloader -----------
                // Present, our magic, well-formed -- and describing something
                // else. The shapes this catches are a receipt copied between
                // machines and one hand-edited to get past the gate.
                writeEntryReceipt(kBootloaderRelease, kBootloaderProduct, werr);
                g = entryGate(false, kBootloaderRelease + 1, kBootloaderProduct);
                // NOTE what this does and does not measure. Every OP1 8k v2
                // reports the same bcdDevice and product, so this comparison
                // cannot tell two mice apart; the test feeds a deliberately
                // impossible value to exercise the branch. What it pins is that
                // a receipt failing the comparison is refused -- the catch is a
                // hand-edited or copied file, not device binding.
                ok("gate: a receipt with the wrong bcdDevice REFUSES", !g.allowed);
                ok("...and says it describes a different bootloader, not that\n"
                   "        none was found -- the two send a person elsewhere",
                   g.reason.find("different bootloader") != std::string::npos,
                   g.reason);
                g = entryGate(false, kBootloaderRelease, "Something Else");
                ok("gate: a receipt with the wrong product string REFUSES",
                   !g.allowed);
                g = entryGate(true, kBootloaderRelease + 1, kBootloaderProduct);
                ok("...and the override still gets past it", g.allowed);
                ok("...reported as an override", g.overridden);

                clearEntryReceipt();
            }

            std::remove((std::string(dir) + "/" + kEntryReceiptName).c_str());
            rmdir(dir);
            if (savedHome.empty()) unsetenv("HOME");
            else setenv("HOME", savedHome.c_str(), 1);
        }
    }
}

static void testWireEqualsPlan() {
    std::printf("\n11. the frames sent == the frames planned\n");
    Image img;
    std::string err;
    if (!img.loadFromExecutable(kExe, err)) {
        std::printf("  SKIP  %s\n", err.c_str());
        return;
    }

    const auto plan = plannedFrames(img);
    std::vector<Frame> expected;
    for (const auto& f : plan) {
        const bool entry = f.size() > 1 && f[0] == kReportSmall && f[1] == 0x3A;
        const bool reset = f.size() > 1 && f[0] == kReportSmall && f[1] == 0x13;
        if (!entry && !reset) expected.push_back(f);
    }
    ok("the plan has exactly one A1 3A and one A1 13, sent elsewhere",
       plan.size() == expected.size() + 2);

    // A cooperative device with no faults: any retry would add frames and the
    // comparison is for the happy path, which is the one the capture shows.
    // NOT preloaded. Handing the mock the very image about to be written would
    // let a run that wrote nothing still read back correctly, and this test
    // would pass on a flasher that sends the right frames and stores nothing.
    MockBootloader dev;
    std::string e;
    ok("preflight passes", preflight(img, e), e);
    const Progress p = driveToVerifiedImage(dev, img);
    ok("the run completed", p.imageVerified && p.completeAcked);

    const auto& sent = dev.sent();
    ok("the number of frames sent equals the number planned",
       sent.size() == expected.size(),
       std::to_string(sent.size()) + " sent vs " + std::to_string(expected.size()));

    std::size_t firstBad = expected.size() + 1;
    for (std::size_t i = 0; i < sent.size() && i < expected.size(); ++i)
        if (sent[i] != expected[i]) { firstBad = i; break; }
    std::string detail;
    if (firstBad <= expected.size() && firstBad < sent.size()) {
        detail = "frame " + std::to_string(firstBad) + ": sent " +
                 hex2(sent[firstBad][0]) + " " + hex2(sent[firstBad][1]) +
                 ", planned " + hex2(expected[firstBad][0]) + " " +
                 hex2(expected[firstBad][1]);
    }
    ok("EVERY frame sent is byte-identical to the frame planned, in order",
       sent.size() == expected.size() && firstBad > expected.size(), detail);

    // Said separately, because it is the specific defect this test was written
    // for and a count check alone would not name it.
    bool readBeforeErase = false, sawErase = false;
    for (const auto& f : sent) {
        if (f.size() > 1 && f[0] == kReportLarge && f[1] == 0x03) { sawErase = true; break; }
        readBeforeErase = true;   // ANY frame before A0 03 is one too many
    }
    ok("A0 03 is the FIRST frame this link ever sees", sawErase && !readBeforeErase);
}

// The two defects the 2026-09-05 audit found in driveToVerifiedImage. Both
// were REACHED by the existing suite and neither failed it, because the mock
// could not express the device behaviour that makes them bite. Written after
// the fixes, so each one is checked against the old code by mutation
// (Tests/mutants.sh) rather than by my say-so.
static void testAuditRegressions() {
    std::printf("\nAudit regressions (2026-09-05)\n");
    Image img;
    std::string err;
    if (!img.loadFromExecutable(kExe, err)) { ok("image", false, err); return; }

    // ---- 1. A1 09 is never acknowledged. ---------------------------------
    // [O] the device re-enumerates 857-896 ms after A1 09, so by then the link
    // may be gone. NOTE the measurement that corrected this: the device itself
    // acks in 0.19-0.32 ms (flash-wire-observed.md §2.1a), so this fault models
    // a DEPARTED DEVICE, not the routine case -- an earlier version of this
    // comment claimed the latter and was wrong. The old loop had no exit but
    // a 0x01 status: it spun forever on a correctly-flashed mouse and every 20
    // attempts advised the user to re-enter the bootloader and start again --
    // i.e. to erase a good device. If this test ever hangs, that is the bug
    // back, and a hang is exactly how mutants.sh scores it killed.
    {
        Faults f; f.neverAckComplete = true; f.seed = 7;
        MockBootloader dev(f);
        Progress p = driveToVerifiedImage(dev, img);
        ok("a device that never acks A1 09 does not hang the write phase", true);
        ok("...and the image is still reported VERIFIED", p.imageVerified);
        ok("...and completeAcked is honestly false", !p.completeAcked);
        ok("...and the device did receive A1 09", dev.completed());
        unsigned nine = 0;
        for (const auto& fr : dev.sent())
            if (fr.size() > 1 && fr[0] == kReportSmall && fr[1] == 0x09) ++nine;
        ok("...and it was retried a bounded number of times",
           nine == kCompleteAttempts,
           "sent " + std::to_string(nine) + ", bound " +
           std::to_string(kCompleteAttempts));
    }

    // ---- 2. A block that will not read back is REWRITTEN, not skipped. ----
    // The repair loop used to `continue` on a failed read, declining to repair
    // the one block most likely to be the corruption. The outer loop then
    // re-queried A1 08, got the same mismatch, and repeated -- forever, inside
    // the phase that by design cannot return.
    {
        const std::uint8_t target = 0x40;      // a device index in 0x34..0x74
        Faults f;
        f.lieAboutWholeChecksumTimes = 1;      // force exactly one repair pass
        f.unreadableAfterVerify = target;      // goes bad after the write-loop read
        f.seed = 11;
        MockBootloader dev(f);
        Progress p = driveToVerifiedImage(dev, img);
        ok("an unreadable block does not stall the repair pass", p.imageVerified);

        unsigned writes = 0, others = 0;
        for (const auto& fr : dev.sent()) {
            if (fr.size() < 4 || fr[0] != kReportLarge || fr[1] != 0x06) continue;
            if (fr[2] == target) ++writes; else ++others;
        }
        ok("the unreadable block is rewritten during repair", writes == 2,
           "A0 06 frames for block 0x40: " + std::to_string(writes) +
           " (expect 2: initial write + repair)");
        // The blocks that read back correctly must NOT be rewritten. Otherwise
        // "rewrite on failed read" degenerates into "rewrite everything", which
        // would pass the assertion above while meaning something else entirely.
        ok("...and only that block; the readable ones are left alone",
           others == img.blockCount() - 1,
           "other A0 06 frames: " + std::to_string(others) + ", expect " +
           std::to_string(img.blockCount() - 1));
    }
}


// ---------------------------------------------------------------------------
// The real link's decisions. Added 2026-09-06, closing the gap recorded in
// working-memory.md: HidBootloaderLink.cpp was the only file in the post-erase
// path that mutants.sh could not grade, because nothing but a mouse in
// bootloader mode could reach a line of it. Its DECISIONS are now pure
// functions (HidBootloaderLink.h), so they can be graded here with no device.
//
// What is still untested, said plainly rather than implied by silence: attach(),
// open(), and the two bodies that call these functions. Those need hardware,
// and this file cannot claim otherwise.
// ---------------------------------------------------------------------------
static void testLinkPolicy() {
    std::printf("\nHidBootloaderLink policy (no device required)\n");

    // ---- classifySend -----------------------------------------------------
    ok("send: Ok is Ok whether or not the device is on the bus",
       classifySend(Outcome::Ok, true)  == Io::Ok &&
       classifySend(Outcome::Ok, false) == Io::Ok);
    // The distinction driveToVerifiedImage branches on. Getting this backwards
    // makes the phase retry a departed device forever instead of reconnecting.
    ok("send: TransportFail with the device GONE is Disconnected",
       classifySend(Outcome::TransportFail, false) == Io::Disconnected);
    ok("send: TransportFail with the device PRESENT is SendFailed",
       classifySend(Outcome::TransportFail, true) == Io::SendFailed);
    ok("send: every other outcome is SendFailed, present or not", [] {
           for (Outcome o : {Outcome::BusyTimeout, Outcome::BadStatus,
                             Outcome::ShortRead})
               for (bool present : {true, false})
                   if (classifySend(o, present) != Io::SendFailed) return false;
           return true;
       }());
    ok("send: nothing but TransportFail depends on device presence", [] {
           for (Outcome o : {Outcome::Ok, Outcome::BusyTimeout,
                             Outcome::BadStatus, Outcome::ShortRead})
               if (classifySend(o, true) != classifySend(o, false)) return false;
           return true;
       }());

    // ---- classifyRecv -----------------------------------------------------
    // The load-bearing one. A not-ready or busy answer carries VALID BYTES and
    // a status the caller must see; treating it as a failure here throws the
    // status byte away and the retry logic is built on it.
    ok("recv: BadStatus with a full-length reply is Ok, not a failure",
       classifyRecv(Outcome::BadStatus, 64, 64, true) == Io::Ok);
    ok("recv: BusyTimeout with a full-length reply is Ok, not a failure",
       classifyRecv(Outcome::BusyTimeout, 64, 64, true) == Io::Ok);
    ok("recv: Ok with a full-length reply is Ok",
       classifyRecv(Outcome::Ok, 1041, 1041, true) == Io::Ok);
    ok("recv: an answered-but-short reply is ShortRead in all three arms", [] {
           for (Outcome o : {Outcome::Ok, Outcome::BadStatus,
                             Outcome::BusyTimeout})
               if (classifyRecv(o, 63, 64, true) != Io::ShortRead) return false;
           return true;
       }());
    ok("recv: a LONGER-than-declared reply is ShortRead too, not Ok",
       classifyRecv(Outcome::Ok, 65, 64, true) == Io::ShortRead);
    ok("recv: Transport's own ShortRead stays ShortRead",
       classifyRecv(Outcome::ShortRead, 0, 64, true)  == Io::ShortRead &&
       classifyRecv(Outcome::ShortRead, 0, 64, false) == Io::ShortRead);
    ok("recv: TransportFail with the device GONE is Disconnected",
       classifyRecv(Outcome::TransportFail, 0, 64, false) == Io::Disconnected);
    ok("recv: TransportFail with the device PRESENT is RecvFailed",
       classifyRecv(Outcome::TransportFail, 0, 64, true) == Io::RecvFailed);
    // Length is what decides Ok, so a zero-length "reply" must never be Ok --
    // including the degenerate want==0, which no report has.
    ok("recv: a zero-length reply is never Ok", [] {
           for (Outcome o : {Outcome::Ok, Outcome::BadStatus,
                             Outcome::BusyTimeout, Outcome::ShortRead,
                             Outcome::TransportFail})
               if (classifyRecv(o, 0, 64, true) == Io::Ok) return false;
           return true;
       }());

    // ---- reconnectLoop ----------------------------------------------------
    // The budget, counted rather than assumed. 20000/100 inclusive of zero is
    // 201 attempts; an off-by-one either way changes how long the post-erase
    // phase waits before re-printing the recovery procedure.
    {
        unsigned attempts = 0, slept = 0;
        std::vector<std::string> said;
        const bool back = reconnectLoop(
            kReconnectBudgetMs, kReconnectStepMs,
            [&] { ++attempts; return false; },
            [&](unsigned ms) { slept += ms; },
            [&](const std::string& l) { said.push_back(l); });
        ok("reconnect: a device that never returns gives up, so the PHASE can "
           "decide", !back);
        ok("reconnect: it tries budget/step + 1 times",
           attempts == kReconnectBudgetMs / kReconnectStepMs + 1,
           "attempts " + std::to_string(attempts));
        ok("reconnect: and sleeps the whole budget, one step per attempt",
           slept == attempts * kReconnectStepMs,
           "slept " + std::to_string(slept) + " ms");
        ok("reconnect: it says so exactly once, and says how long it waited",
           said.size() == 1 &&
           said[0].find(std::to_string(kReconnectBudgetMs)) != std::string::npos,
           said.empty() ? "(silent)" : said[0]);
    }
    {
        // Already back: no sleep at all. A reconnect that costs 100 ms when the
        // device never left is 100 ms added to every retry in the post-erase
        // loop, which is the loop with no timeout.
        unsigned attempts = 0, slept = 0;
        std::vector<std::string> said;
        const bool back = reconnectLoop(
            kReconnectBudgetMs, kReconnectStepMs,
            [&] { ++attempts; return true; },
            [&](unsigned ms) { slept += ms; },
            [&](const std::string& l) { said.push_back(l); });
        ok("reconnect: a device already present is taken on the first try",
           back && attempts == 1 && slept == 0,
           "attempts " + std::to_string(attempts) + ", slept " +
           std::to_string(slept));
        ok("reconnect: and it reports 0 ms, not the budget",
           said.size() == 1 && said[0].find("after 0 ms") != std::string::npos,
           said.empty() ? "(silent)" : said[0]);
    }
    {
        // Returning mid-budget must report the ELAPSED time, not the attempt
        // count and not the budget. The number goes in a log a human reads
        // while the mouse is not a mouse.
        unsigned attempts = 0;
        std::vector<std::string> said;
        const bool back = reconnectLoop(
            kReconnectBudgetMs, kReconnectStepMs,
            [&] { return ++attempts == 5; },
            [&](unsigned) {},
            [&](const std::string& l) { said.push_back(l); });
        ok("reconnect: success on attempt 5 is reported as 4 steps of waiting",
           back && said.size() == 1 &&
           said[0].find("after " + std::to_string(4 * kReconnectStepMs) + " ms")
               != std::string::npos,
           said.empty() ? "(silent)" : said[0]);
    }
    {
        // §6.2: a harness that cannot produce a bad result is not evidence.
        // A zero budget must still try once -- the device may already be back --
        // and must not loop forever on a zero step.
        unsigned attempts = 0;
        const bool back = reconnectLoop(
            0, kReconnectStepMs, [&] { ++attempts; return false; },
            [](unsigned) {}, [](const std::string&) {});
        ok("reconnect: a zero budget still tries exactly once",
           !back && attempts == 1, "attempts " + std::to_string(attempts));
    }
}

// ---------------------------------------------------------------------------
// Per-block progress. The write phase must not be silent.
// ---------------------------------------------------------------------------
// Added 2026-09-06. The phase takes ~10-15 s and said nothing for all of it, in
// the exact window where the tool has just announced that Ctrl-C is ignored --
// so "it has hung" and "it is working" looked identical at the one moment the
// difference matters. The GUI inherited the silence, because it only streams
// what the CLI prints.
//
// Tested through the mock's own note log, which is the same path the real link
// uses. Two properties, and the second is the one worth having: the count is
// right on a COOPERATIVE device, and on a HOSTILE one the numbers still count
// verified blocks rather than loop iterations -- a progress line that ran ahead
// of the device would be worse than none, because it would say a block was
// written while it was still being repaired.
static void testWriteProgress() {
    std::printf("\nwrite-phase progress\n");

    Image img;
    std::string err;
    if (!img.loadFromExecutable(kExe, err)) {
        ok("image for the progress runs", false, err);
        return;
    }

    auto verifiedLines = [](const std::vector<std::string>& log) {
        std::vector<std::string> out;
        for (const std::string& l : log)
            if (l.rfind("block ", 0) == 0 &&
                l.find("verified") != std::string::npos)
                out.push_back(l);
        return out;
    };

    ok("a clean run reports every block, once, in order", [&] {
        MockBootloader m;
        const Progress p = driveToVerifiedImage(m, img);
        const auto lines = verifiedLines(m.log());
        if (lines.size() != img.blockCount()) return false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string want = "block " + std::to_string(i + 1) + "/" +
                                     std::to_string(img.blockCount()) +
                                     " verified";
            if (lines[i] != want) return false;
        }
        return p.blocksVerified == img.blockCount();
    }());

    ok("the last line names the total, so a reader can see it finished", [&] {
        MockBootloader m;
        driveToVerifiedImage(m, img);
        const auto lines = verifiedLines(m.log());
        return !lines.empty() &&
               lines.back() == "block " + std::to_string(img.blockCount()) +
                               "/" + std::to_string(img.blockCount()) +
                               " verified";
    }());

    ok("repairs do not inflate the count: one line per block, not per attempt",
       [&] {
        // WHAT THIS CAN AND CANNOT CATCH, stated because the first version of
        // this test was called "the count tracks VERIFIED blocks, not the loop
        // index" and could not have caught that if it were wrong. In the loop
        // as written the counter and the index are equal on every reachable
        // path, so no test distinguishes them (Tests/mutants.sh carries that
        // as an equivalent mutant with the proof).
        //
        // What IS falsifiable, and is the defect worth stopping: emitting the
        // line from inside the retry loop. That would announce a block on
        // every attempt, so a device repairing block 9 four times would report
        // reaching block 12 -- a progress line running ahead of the device is
        // a false statement made at the moment of maximum cost. Hence the
        // exact-count check below, under a fault rate that forces repairs.
        Faults f;
        f.corruptStoreRate = 0.20;      // forces read-back failures and rewrites
        f.seed = 7;
        MockBootloader m(f);
        const Progress p = driveToVerifiedImage(m, img);
        const auto lines = verifiedLines(m.log());
        if (p.rewrites == 0) return false;          // the scenario did nothing
        if (lines.size() != img.blockCount()) return false;
        // Exactly blockCount lines, and each numbered by the verified count.
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i].rfind("block " + std::to_string(i + 1) + "/", 0) != 0)
                return false;
        // Once a rewrite has happened, every later line carries the count.
        bool seen = false;
        for (const std::string& l : lines) {
            const bool has = l.find("rewrite(s) so far") != std::string::npos;
            if (has) seen = true;
            if (seen && !has) return false;
        }
        return seen;
    }());
}

// ---------------------------------------------------------------------------
// NoQuitDuringWrite -- the guard that makes the write phase unstoppable.
// ---------------------------------------------------------------------------
// UNTESTABLE UNTIL 2026-09-06, because it was a class inside
// Sources/egg-flash/main.cpp. Nothing in ctest could construct it, so the only
// thing that had ever checked its signal list was a person reading the code --
// and that is how it came to ignore SIGINT, SIGTERM and SIGHUP while leaving
// SIGPIPE at its default disposition, which TERMINATES THE PROCESS. The phase
// writes progress and every recovery instruction to stderr, so a pipe whose
// reader went away would kill the flash between two blocks with nobody having
// typed anything.
//
// These raise each signal at the running test process. If a disposition is not
// actually ignored the test does not "fail" -- it DIES, and ctest reports the
// signal. Either way it cannot pass, which is what §6.2 asks of a harness.
static void testNoQuitDuringWrite() {
    std::printf("\nNoQuitDuringWrite\n");

    std::size_t n = 0;
    const int* sigs = NoQuitDuringWrite::signals(n);

    ok("SIGPIPE is covered", [&] {
        // Named on its own because it is the one that was missing, and because
        // the reason it belongs is different from the other three: nobody has
        // to do anything for it to arrive.
        for (std::size_t i = 0; i < n; ++i) if (sigs[i] == SIGPIPE) return true;
        return false;
    }());

    ok("engineering-rules.md §3's own two are covered, and SIGHUP with them", [&] {
        bool i_ = false, t = false, h = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (sigs[i] == SIGINT)  i_ = true;
            if (sigs[i] == SIGTERM) t  = true;
            if (sigs[i] == SIGHUP)  h  = true;
        }
        return i_ && t && h;
    }());

    ok("every covered signal is actually ignored inside the guard", [&] {
        // Raised at THIS process. Reaching the next line at all is the result:
        // an un-ignored SIGPIPE or SIGTERM would end the run here.
        NoQuitDuringWrite guard;
        for (std::size_t i = 0; i < n; ++i) {
            if (std::signal(sigs[i], SIG_IGN) != SIG_IGN) return false;
            std::raise(sigs[i]);
        }
        return true;
    }());

    ok("the previous dispositions come back afterwards", [&] {
        // Install something recognisable, let the guard replace it, and require
        // it back. The guard runs for the erase-through-verify unit ONLY --
        // before the erase Ctrl-C must still work, because before the erase
        // abort is always correct (§4.2).
        struct H { static void h(int) {} };
        for (std::size_t i = 0; i < n; ++i) {
            void (*before)(int) = std::signal(sigs[i], &H::h);
            {
                NoQuitDuringWrite guard;
                if (std::signal(sigs[i], SIG_IGN) != SIG_IGN) {
                    std::signal(sigs[i], before);
                    return false;
                }
            }
            const bool back = std::signal(sigs[i], before) == &H::h;
            if (!back) return false;
        }
        return true;
    }());

    ok("nesting two guards still restores the outer disposition", [&] {
        // Not a shape the CLI uses, and cheap to be sure of: a nested guard
        // that saved SIG_IGN and restored it is harmless; one that saved
        // nothing and restored SIG_DFL would silently disarm the outer unit.
        struct H { static void h(int) {} };
        void (*before)(int) = std::signal(SIGPIPE, &H::h);
        {
            NoQuitDuringWrite outer;
            { NoQuitDuringWrite inner; }
            if (std::signal(SIGPIPE, SIG_IGN) != SIG_IGN) {
                std::signal(SIGPIPE, before);
                return false;
            }
        }
        return std::signal(SIGPIPE, before) == &H::h;
    }());
}

// ---------------------------------------------------------------------------
// The release manifest
// ---------------------------------------------------------------------------
// FirmwareManifest.cpp had no mutants until 2026-09-07, and the reason it could
// not have any is worth recording: `Tests/mutants.sh` grades everything outside
// EGGConfigCore with `test-flash`, and `test-flash` never named the manifest.
// Its only checks were in `Tests/test_manifest.py`, which parses the SOURCE
// rather than running it -- so no mutation of the LOOKUP functions could ever
// have been caught, however wrong.
//
// That matters more than the usual coverage argument. `releaseForUpdaterSha`
// is the function that decides WHICH firmware image is about to be written to
// the one mouse there is, from the hash of a file a person handed us. §2:
// "nothing downstream of us catches a wrong-but-well-formed image."
// THE APPLICATION-SIDE FIRMWARE GATE (added 2026-09-08).
//
// The bootloader has always been checked against kBootloaderRelease before an
// erase; the application side was checked for nothing but a 16-bit product id,
// so a mouse on a firmware nobody has seen was erased and overwritten anyway.
// engineering-rules.md 5: a third version is a different device until shown
// otherwise.
//
// Positive controls first, deliberately: a predicate that answered false to
// everything would pass a test that only fed it unknown versions, and that is
// the shape of vacuous check this project has shipped before.
static void testObservedAppReleases() {
    ok("1.10 is recognised: it is the firmware on the one mouse",
       egg::isObservedAppRelease(0x0110), "0x0110");
    ok("1.07 is recognised: everything observed before 2026-09-05 came off it",
       egg::isObservedAppRelease(0x0107), "0x0107");

    ok("a LATER unseen version is refused", !egg::isObservedAppRelease(0x0111),
       "0x0111");
    ok("an EARLIER unseen version is refused", !egg::isObservedAppRelease(0x0106),
       "0x0106");
    ok("zero is refused, which is what a failed enumeration leaves behind",
       !egg::isObservedAppRelease(0x0000), "0x0000");
    ok("the BOOTLOADER's own release is not an application release",
       !egg::isObservedAppRelease(egg::kBootloaderRelease), "0x0006");

    // The set is exactly two. engineering-rules.md 1.2a: "the set is exactly N"
    // is a claim about everywhere you did not look, so pin it rather than
    // assert it in prose, and a third entry has to fail here and be justified.
    std::size_t n = 0;
    for (std::uint16_t r : egg::kObservedAppReleases) { (void)r; ++n; }
    ok("the observed set is exactly the two versions the derivation used",
       n == 2, std::to_string(n));
}

static void testManifest() {
    std::printf("\nFirmwareManifest\n");

    ok("there is at least one release", kReleaseCount >= 1);

    // Exactly one row may claim to have been flashed, and it must be first,
    // because primaryRelease() is kReleases[0] and the flash defaults to it.
    std::size_t proven = 0, provenIdx = kReleaseCount;
    for (std::size_t i = 0; i < kReleaseCount; ++i)
        if (kReleases[i].provenOnDevice) { ++proven; if (provenIdx == kReleaseCount) provenIdx = i; }
    ok("exactly one release is proven on the device", proven == 1,
       "found " + std::to_string(proven));
    ok("and it is the one primaryRelease() returns", provenIdx == 0);
    ok("primaryRelease() is kReleases[0]",
       &primaryRelease() == &kReleases[0]);
    ok("the primary release is the proven one",
       primaryRelease().provenOnDevice);

    // The primary row must agree with the compile-time constants the narrow
    // single-release loader uses. Two places state the same fact; if they ever
    // disagree, `flash` and `flash --version 1.10` would write different bytes.
    ok("the primary image sha matches kExpectedSha256",
       std::string(primaryRelease().imageSha256) == kExpectedSha256);
    ok("the primary resource id matches kResourceName",
       primaryRelease().resourceId == kResourceName);
    ok("the primary image size matches kExpectedImageSize",
       primaryRelease().imageSize == kExpectedImageSize);

    // Labels are exact. `findRelease` has no fuzzy match and no default.
    ok("findRelease finds the primary by its own label",
       findRelease(primaryRelease().label) == &primaryRelease());
    ok("findRelease refuses a label that is not a row",
       findRelease("1.99") == nullptr);
    ok("findRelease refuses a PREFIX of a real label",
       findRelease("1.1") == nullptr);
    ok("findRelease refuses null", findRelease(nullptr) == nullptr);

    // Hashes are exact too, case-insensitively on the hex. A prefix match here
    // would be a way to select a release with a short string.
    {
        const std::string sha = primaryRelease().updaterSha256;
        ok("releaseForUpdaterSha finds the primary",
           releaseForUpdaterSha(sha) == &primaryRelease());

        std::string upper = sha;
        for (char& c : upper)
            if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        ok("...and is case-insensitive on the hex",
           releaseForUpdaterSha(upper) == &primaryRelease());
        ok("...but the uppercase form really was different", upper != sha);

        ok("a 63-character prefix selects nothing",
           releaseForUpdaterSha(sha.substr(0, 63)) == nullptr);
        ok("an over-long hash selects nothing",
           releaseForUpdaterSha(sha + "0") == nullptr);
        ok("an empty hash selects nothing",
           releaseForUpdaterSha("") == nullptr);

        std::string flipped = sha;
        flipped[63] = (flipped[63] == '0') ? '1' : '0';
        ok("one different hex digit selects nothing",
           releaseForUpdaterSha(flipped) == nullptr);
    }

    // Every row is distinct in the two fields that select it, and every row is
    // structurally what the flasher requires. A duplicate hash would make
    // selection depend on table ORDER, which nothing states.
    bool distinctSha = true, distinctLabel = true, wellFormed = true;
    for (std::size_t i = 0; i < kReleaseCount; ++i) {
        if (std::string(kReleases[i].updaterSha256).size() != 64) wellFormed = false;
        if (std::string(kReleases[i].imageSha256).size() != 64)   wellFormed = false;
        if (kReleases[i].imageSize != kExpectedImageSize)         wellFormed = false;
        if (kReleases[i].imageSize % kBlockSize != 0)             wellFormed = false;
        for (std::size_t j = i + 1; j < kReleaseCount; ++j) {
            if (std::string(kReleases[i].updaterSha256) == kReleases[j].updaterSha256)
                distinctSha = false;
            if (std::string(kReleases[i].label) == kReleases[j].label)
                distinctLabel = false;
        }
    }
    ok("no two releases share an updater hash", distinctSha);
    ok("no two releases share a label", distinctLabel);
    ok("every row is 65 whole blocks with two 64-hex hashes", wellFormed);
}

int main() {
    std::printf("EGGFlashCore\n");
    testByteMaps();
    testObservedFrameLayout();
    testChecksums();
    testInvariants();
    testAdversarial();
    testDeterminismAndIdentity();
    testStage2Entry();
    testStage2Exit();
    testReadBackAndToken();
    testBackupGate();
    testProvenanceGate();
    testWireEqualsPlan();
    testAuditRegressions();
    testLinkPolicy();
    testWriteProgress();
    testNoQuitDuringWrite();
    testManifest();
    testObservedAppReleases();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
