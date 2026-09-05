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
#include "egg/FlashCommands.h"
#include "egg/Firmware.h"
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
    ok("preflight passes against a cooperative device",
       preflight(dev, img, e2), e2);
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

    // 6: no write is emitted unless preflight passed.
    Faults never;                       // a device that answers nothing useful
    never.rejectRate = 1.0;
    MockBootloader bad(never);
    std::string e3;
    const bool pf = preflight(bad, img, e3);
    ok("inv 6: preflight FAILS against a device that rejects everything", !pf);
    bool anyWrite = false;
    for (const auto& fr : bad.sent()) if (fr.size() > 1 && fr[1] == 0x06) anyWrite = true;
    ok("inv 6: and no write frame was emitted", !anyWrite);
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

int main() {
    std::printf("EGGFlashCore\n");
    testByteMaps();
    testObservedFrameLayout();
    testChecksums();
    testInvariants();
    testAdversarial();
    testDeterminismAndIdentity();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
