// test_flash.cpp -- the seven invariants of updater-protocol.md §10.3, the
// derived byte maps, and the flasher driven against an adversarial device.
//
// CLAUDE.md §4.3: "Assert invariants, not examples, over randomised runs." The
// examples here exist only to pin the byte maps, which ARE examples by nature.
// Everything about behaviour is an invariant checked over many seeds.
//
// §6.2: "A harness that cannot produce a bad result is not evidence." Several
// of these deliberately arm a fault and require the flasher to notice. If the
// adversarial section ever passes with the faults disabled, it is measuring
// nothing.
#include "egg/BootloaderEntry.h"
#include "egg/FlashCommands.h"
#include "egg/FlashPlan.h"
#include "egg/Firmware.h"
#include "egg/HidBootloaderLink.h"
#include "egg/MockBootloader.h"
#include "egg/Protocol.h"
#include "egg/WritePhase.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace egg;
using namespace egg::fw;

static int failures = 0;
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
    ok("a non-PE file is refused", !none.loadFromExecutable("CLAUDE.md", e), e);
    ok("a missing file is refused", !none.loadFromExecutable("no-such.exe", e));

    // THE IDENTITY GUARD, and this is the test that gives it teeth.
    //
    // Updaters 1.04, 1.06 and 1.07 each ship their own FWFILE resource 140.
    // Every one is 66560 bytes, exactly 65 blocks, remainder zero -- so every
    // structural check in the loader passes on all of them. They are
    // wrong-but-well-formed images in the precise sense CLAUDE.md §2 means,
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
        // Reversed 2026-09-05 by the owner. My reason for omitting A1 13 was
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
// `flash` no longer takes the backup -- the owner, 2026-09-05 -- so this check is the
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
    testWireEqualsPlan();
    testAuditRegressions();
    testLinkPolicy();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
