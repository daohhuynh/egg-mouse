// egg-flash -- CLI-first by design (CLAUDE.md §3): no window to close, no Dock
// quit item, and the output is a log by construction.
#include "egg/BootloaderEntry.h"
#include "egg/Device.h"
#include "egg/FlashCommands.h"
#include "egg/Firmware.h"
#include "egg/RecordVault.h"
#include "egg/Transport.h"
#include "egg/WritePhase.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace egg::fw;

// ---------------------------------------------------------------------------
// The settings undo. working-memory.md, open gaps:
//
//   "A1 13 is both the Factory Reset button and the updater's last command. If
//    it means the same in both places, flashing wipes the user's settings. The
//    flasher must save a blob first and say so."
//
// It is [G] whether the two mean the same thing, and it is not testable without
// the device -- section 07 of the capture run sent A1 13 from a record that was
// already at defaults, so before == after proved nothing. §2's standing
// instruction for exactly this shape of unknown is to build as if the bad
// reading were true.
//
// Compounding it, and also from the open gaps: there is no evidence any config
// write persists across a power cycle, and no persist command has been seen. So
// settings lost here may not be recoverable by asking the device again.
//
// Hence: this tool states, every time, whether an undo exists. It cannot make
// one itself -- reading settings is `egg-config`'s job and lives in the other
// executable by §3 -- but it can refuse to let the question go unasked.
static std::string defaultVaultPath() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.egg-mouse-known-good.bin"
                : std::string(".egg-mouse-known-good.bin");
}

// Returns true if an undo exists. Prints either way; the silent case is the one
// this function exists to prevent.
//
// ON STDERR, DELIBERATELY, and Tests/test_flash_undo.sh caught why. `stream`
// exists so the whole outbound byte sequence can be diffed against a capture of
// the vendor's tool doing the same flash -- that is the strongest check §4.3
// has, and it only works if stdout carries nothing but frames. A warning on
// stdout would have made the diff depend on whether a backup file happened to
// exist. Stderr keeps it in front of a terminal user and out of every pipe.
static bool reportSettingsUndo(const std::string& vaultPath) {
    egg::cfg::FileRecordVault v(vaultPath);
    if (v.holds()) {
        std::fprintf(stderr, "settings   undo present: %s\n", v.where().c_str());
        return true;
    }
    std::fprintf(stderr,
        "settings   *** NO SETTINGS UNDO EXISTS ***\n"
        "           This tool's last command is A1 13, which is byte-for-byte\n"
        "           the config tool's Factory Reset. Whether it means the same\n"
        "           thing in both places is a GUESS and is not testable without\n"
        "           flashing. Nor is there any evidence that settings survive a\n"
        "           power cycle, so a wipe may not be undoable from the device.\n"
        "           Run `egg-config read` first. It saves a known-good record to\n"
        "           %s automatically and never overwrites it.\n",
        vaultPath.c_str());
    return false;
}

static int usage() {
    std::printf(
        "egg-flash -- firmware flasher for the Endgame Gear OP1 8k v2\n"
        "\n"
        "  egg-flash image <updater.exe>   validate the firmware image and stop\n"
        "  egg-flash dryrun <updater.exe>  emit the exact byte stream, send nothing\n"
        "  egg-flash stream <updater.exe>  the same stream in full, one frame per\n"
        "                                  line, for diffing against a capture\n"
        "  egg-flash enter-bootloader --yes\n"
        "                                  §4.4 stage 2: send ONE report (A1 3A),\n"
        "                                  confirm the mouse came back as the\n"
        "                                  bootloader, send it nothing. No erase,\n"
        "                                  no write. Exit by unplugging it.\n"
        "  egg-flash leave-bootloader --yes\n"
        "                                  the way back: ONE report (A1 09), the\n"
        "                                  vendor's own exit. No erase, no write.\n"
        "  egg-flash help\n"
        "\n"
        "The image is always FWFILE resource %u of the .exe you name. That is a\n"
        "compile-time constant; there is no way to select a different one, and\n"
        "an image whose SHA-256 is not the pinned value is refused before any\n"
        "byte goes out.\n"
        "\n"
        "There is deliberately no 'flash' verb yet. Staged bring-up (§4.4) puts a\n"
        "real flash last, after a read-back against the device.\n"
        "\n"
        "  --vault F   where egg-config keeps the known-good settings record.\n"
        "              Default ~/.egg-mouse-known-good.bin. This tool's last\n"
        "              command is A1 13, the same byte as Factory Reset, so it\n"
        "              reports whether that undo exists before doing anything.\n"
        "\n%s\n", kResourceName, kRecoveryProcedure);
    return 2;
}

// ---------------------------------------------------------------------------
// §4.4 stage 2. One command out, then nothing but watching.
//
// It takes no image and loads none: an image path here would be a path by which
// a mistyped verb could start a flash, and there is no reason for this rung to
// know what firmware even is.
// ---------------------------------------------------------------------------
static std::vector<SeenDevice> seeAll() {
    std::vector<SeenDevice> out;
    for (const auto& m : egg::enumerateAll())
        out.push_back({m.productId, m.releaseNumber, m.usagePage, m.usage,
                       m.product, m.manufacturer});
    return out;
}

static int cmdEnterBootloader(bool yes, bool verbose) {
    if (!yes) {
        std::printf(
          "enter-bootloader sends ONE 64-byte report (A1 3A + the 5A A5 32\n"
          "magic) to the application device and then only watches.\n"
          "\n"
          "It does not erase, does not write, and sends the bootloader nothing\n"
          "at all. The mouse will disappear and come back as PID 0x1977.\n"
          "To get out again: UNPLUG IT AND PLUG IT BACK IN. A power cycle is\n"
          "the observed exit and it has been done on this mouse before.\n"
          "\n"
          "Re-run with --yes.\n");

        // Show the preflight rather than only describing it. Same predicate the
        // send path uses -- a prompt that cannot see what the send path sees is
        // a prompt that reassures you about the wrong thing.
        const auto seen = seeAll();
        const std::size_t apps = countVendorCollections(seen, egg::kProductIdApplication);
        const std::size_t bls  = countVendorCollections(seen, egg::kProductIdBootloader);
        std::printf("\npreflight, right now:\n"
                    "  %zu interface(s) on VID 0x3367 in total\n"
                    "  %zu application vendor collection(s)  (PID 0x%04x, 0xff01/0x02)\n"
                    "  %zu bootloader  vendor collection(s)  (PID 0x%04x)\n",
                    seen.size(), apps, egg::kProductIdApplication,
                    bls, egg::kProductIdBootloader);
        if (apps == 1 && bls == 0)
            std::printf("  -> ready. Exactly one mouse to switch.\n");
        else if (bls > 0)
            std::printf("  -> already in the bootloader; --yes would send nothing.\n");
        else
            std::printf("  -> NOT ready: --yes would refuse and send nothing.\n");
        return 2;
    }

    egg::Log log(verbose);
    auto dev = egg::Device::open(egg::kProductIdApplication, log);
    if (!dev) return 1;
    egg::Transport t(*dev, egg::kUpdaterBusy, log);

    const auto t0 = std::chrono::steady_clock::now();
    EntryEnv env;
    env.nowMs = [t0] {
        return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    };
    env.sleepMs = [](unsigned ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    };
    env.enumerate = seeAll;
    env.exchange = [&t](const std::vector<std::uint8_t>& frame,
                        std::vector<std::uint8_t>& reply, bool& readOk) {
        // The vendor sleeps i*2 ms after the send and before the read, i = 1 on
        // the first pass (updater 1.10 0x004037b3). Two milliseconds.
        const egg::Reply r = t.exchange(frame, egg::kReportSmall,
                                        "A1 3A enter bootloader", 2);
        readOk = !r.buf.empty();
        reply  = r.buf;
        return r.outcome != egg::Outcome::TransportFail;
    };

    const EntryOutcome o = enterBootloaderAndConfirm(env);

    if (!o.sent.empty()) {
        std::printf("\nsent       ");
        for (std::size_t i = 0; i < 8; ++i) std::printf("%02x ", o.sent[i]);
        std::printf("... (%zu bytes, rest zero)\n", o.sent.size());
        std::printf("ack        %u ms, reply %s",
                    o.ackMs, o.replyRead ? "read" : "NOT read");
        if (o.replyRead) std::printf(", resp[1]=0x%02x", o.status);
        std::printf("\n           (resp[1] is logged, not gated on -- the vendor\n"
                    "            does not check it either; the PID change is the evidence)\n");
    }

    if (o.sawBootloaderPid) {
        std::printf("appeared   %u ms after the reply\n", o.reenumerateMs);
        std::printf("identity   PID 0x%04x  bcdDevice 0x%04x  \"%s\"  (manufacturer \"%s\")\n",
                    o.seen.productId, o.seen.releaseNumber, o.seen.product.c_str(),
                    o.seen.manufacturer.c_str());
    }

    if (o.result != EntryResult::EnteredAndConfirmed) {
        std::printf("\nFAILED: %s\n", describe(o.result));
        if (o.result == EntryResult::UnrecognisedIdentity)
            std::printf("Refused it rather than proceeding. Expected bcdDevice 0x%04x and \"%s\".\n",
                        egg::kBootloaderRelease, egg::kBootloaderProduct);
        if (o.result == EntryResult::NoReenumeration)
            std::printf("Nothing was erased and nothing was written. If the mouse still\n"
                        "works normally, the command simply did not take; run it again.\n");
        std::printf("\n%s\n", kRecoveryProcedure);
        return 1;
    }

    std::printf("\nCONFIRMED: in the bootloader, all three identity fields matched.\n"
                "Nothing was sent to it and nothing will be.\n"
                "\n"
                "TO GET OUT: unplug the mouse, wait a moment, plug it back in.\n"
                "Then `egg-config devices` should show PID 0x1978 (application).\n");
    return 0;
}

// ---------------------------------------------------------------------------
// The way back out, added after stage 2 established that a software entry
// LATCHES and a power cycle does not undo it. See BootloaderEntry.h for the
// full provenance -- the short version is [D] on what the command is and what
// the vendor does after it, [G] on what it does to a bootloader that was never
// flashed, and nothing was erased here so there is an intact application.
// ---------------------------------------------------------------------------
static int cmdLeaveBootloader(bool yes, bool verbose) {
    const auto seen = seeAll();
    const std::size_t apps = countVendorCollections(seen, egg::kProductIdApplication);
    const std::size_t bls  = countVendorCollections(seen, egg::kProductIdBootloader);

    if (!yes) {
        std::printf(
          "leave-bootloader sends ONE 64-byte report (A1 09) to the bootloader.\n"
          "\n"
          "It is the vendor's own exit: after it, updater 1.10 searches for the\n"
          "application PID 0x1978 (fw110 0x00403c7c), and in both captures the\n"
          "mouse came back as 0x1978 about 880 ms later. Updater 1.04, a separate\n"
          "code base, builds the identical frame.\n"
          "\n"
          "WHAT IS A GUESS: Endgame only ever sends A1 09 to a bootloader it has\n"
          "just finished flashing. Nothing has been flashed or erased here, so\n"
          "there is an intact application to hand back to -- but the device's\n"
          "behaviour in this exact state is not derivable from their binaries.\n"
          "\n"
          "It does not erase and does not write. The frame carries no parameters.\n"
          "\n"
          "IF IT DOES NOTHING, nothing is lost: the mouse stays in the bootloader\n"
          "and Endgame's Windows updater still recovers it (it treats a 0x1977\n"
          "device as a supported starting state and flashes it directly).\n");
        std::printf("\npreflight, right now:\n"
                    "  %zu application vendor collection(s)  (PID 0x%04x)\n"
                    "  %zu bootloader  vendor collection(s)  (PID 0x%04x)\n",
                    apps, egg::kProductIdApplication, bls, egg::kProductIdBootloader);
        if (apps >= 1)      std::printf("  -> already in application mode; --yes would send nothing.\n");
        else if (bls == 1)  std::printf("  -> ready. Re-run with --yes.\n");
        else                std::printf("  -> NOT ready: --yes would refuse and send nothing.\n");
        return 2;
    }

    egg::Log log(verbose);
    auto dev = egg::Device::open(egg::kProductIdBootloader, log);
    if (!dev) return 1;
    egg::Transport t(*dev, egg::kUpdaterBusy, log);

    const auto t0 = std::chrono::steady_clock::now();
    EntryEnv env;
    env.nowMs = [t0] {
        return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    };
    env.sleepMs   = [](unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    env.enumerate = seeAll;
    env.exchange  = [&t](const std::vector<std::uint8_t>& frame,
                         std::vector<std::uint8_t>& reply, bool& readOk) {
        // The vendor sleeps 50 ms after this send before reading (0x00403bfa),
        // which is longer than the 2 ms it allows after A1 3A.
        const egg::Reply r = t.exchange(frame, egg::kReportSmall,
                                        "A1 09 leave bootloader", 50);
        readOk = !r.buf.empty();
        reply  = r.buf;
        return r.outcome != egg::Outcome::TransportFail;
    };

    const EntryOutcome o = leaveBootloaderAndConfirm(env);

    if (!o.sent.empty()) {
        std::printf("\nsent       ");
        for (std::size_t i = 0; i < 4; ++i) std::printf("%02x ", o.sent[i]);
        std::printf("... (%zu bytes, rest zero)\n", o.sent.size());
        std::printf("ack        %u ms, reply %s", o.ackMs, o.replyRead ? "read" : "NOT read");
        if (o.replyRead) std::printf(", resp[1]=0x%02x", o.status);
        std::printf("\n");
    }

    if (o.result != EntryResult::EnteredAndConfirmed) {
        std::printf("\nFAILED: %s\n", describe(o.result));
        std::printf(
          "\nThe mouse is still in the bootloader. NOTHING was erased or written,\n"
          "so it is not damaged -- it is in the wrong mode.\n"
          "Endgame's own Windows updater recovers this: it treats a 0x1977 device\n"
          "as a supported starting state and goes straight to flashing\n"
          "(notes/updater-protocol.md 5.1a).\n\n%s\n", kRecoveryProcedure);
        return 1;
    }

    std::printf("\nBACK IN APPLICATION MODE.\n"
                "  PID 0x%04x  bcdDevice 0x%04x  \"%s\"\n",
                o.seen.productId, o.seen.releaseNumber, o.seen.product.c_str());
    if (o.reenumerateMs) std::printf("  came back %u ms after the reply\n", o.reenumerateMs);
    std::printf("\nNow check the settings survived:\n"
                "  ./build/egg-config read --save after-stage2.bin\n"
                "  ./build/egg-config diff ~/.egg-mouse-known-good.bin after-stage2.bin\n");
    return 0;
}

int main(int argc, char** argv) {
    std::string vaultPath = defaultVaultPath();
    std::string args[3];
    bool yes = false, verbose = false;
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--vault" && i + 1 < argc) { vaultPath = argv[++i]; continue; }
        if (a == "--yes") { yes = true; continue; }
        if (a == "-v" || a == "--verbose") { verbose = true; continue; }
        if (n < 3) args[n++] = a;
    }

    if (n < 1) return usage();
    const std::string verb = args[0];
    if (verb == "help" || verb == "--help" || verb == "-h") return usage();

    // Before the image is loaded, because this rung has no image and must not
    // acquire one by accident.
    if (verb == "enter-bootloader") return cmdEnterBootloader(yes, verbose);
    if (verb == "leave-bootloader") return cmdLeaveBootloader(yes, verbose);

    if (n < 2) return usage();
    const std::string exePath = args[1];

    Image img;
    std::string err;
    if (!img.loadFromExecutable(exePath.c_str(), err)) {
        std::printf("REFUSED: %s\n", err.c_str());
        return 1;
    }
    std::printf("image      FWFILE/%u from %s\n", kResourceName, exePath.c_str());
    std::printf("size       %zu bytes, %zu blocks of %zu\n",
                img.bytes().size(), img.blockCount(), kBlockSize);
    std::printf("sha256     %s  (matches the pinned constant)\n", img.sha256().c_str());
    std::printf("checksum   0x%08x  (FUN_00403580, 32-bit sum of every byte)\n",
                img.checksum());
    std::printf("blocks     device indices 0x%02x..0x%02x\n",
                img.deviceIndex(0), img.deviceIndex(img.blockCount() - 1));
    reportSettingsUndo(vaultPath);

    if (verb == "image") return 0;

    // "stream" prints every frame in full, one per line, so the whole outbound
    // byte sequence can be diffed against a capture of the vendor's tool doing
    // the same flash. That comparison is worth more than the frozen golden file
    // §4.3 asks for, because the reference is not something we produced -- it is
    // what Endgame's own updater actually sent to this exact mouse.
    if (verb == "stream") {
        auto emit = [](const char* what, const Frame& f) {
            std::printf("%s ", what);
            for (std::size_t i = 0; i < f.size(); ++i) std::printf("%02x", f[i]);
            std::printf("\n");
        };
        emit("enter", enterBootloader());
        emit("start", bootloaderStart(
            static_cast<std::uint8_t>(img.blockCount()), img.checksum()));
        for (std::size_t i = 0; i < img.blockCount(); ++i) {
            const std::uint8_t idx = img.deviceIndex(i);
            emit("write", writeBlock(idx, img.block(i), kBlockSize));
            emit("verify", readBlock(idx));
        }
        emit("wholesum",
             wholeImageChecksumQuery(img.deviceIndex(img.blockCount() - 1)));
        emit("complete", bootloaderComplete());
        emit("postsuccess", postSuccess());
        return 0;
    }

    if (verb != "dryrun") return usage();

    std::printf("\nthe exact byte stream, first 16 bytes of each frame:\n");
    auto show = [](const char* what, const Frame& f) {
        std::printf("  %-22s len=%-5zu ", what, f.size());
        for (std::size_t i = 0; i < 16 && i < f.size(); ++i)
            std::printf("%02x ", f[i]);
        std::printf("\n");
    };
    show("A1 3A enter-bootloader", enterBootloader());
    show("A0 03 start", bootloaderStart(
        static_cast<std::uint8_t>(img.blockCount()), img.checksum()));
    for (std::size_t i = 0; i < img.blockCount(); ++i) {
        const std::uint8_t idx = img.deviceIndex(i);
        if (i < 2 || i + 2 >= img.blockCount()) {
            show(("A0 06 write " + hex2(idx)).c_str(),
                 writeBlock(idx, img.block(i), kBlockSize));
            show(("A0 07 verify " + hex2(idx)).c_str(), readBlock(idx));
        } else if (i == 2) {
            std::printf("  ... %zu more write/verify pairs ...\n",
                        img.blockCount() - 4);
        }
    }
    show("A1 08 whole checksum",
         wholeImageChecksumQuery(img.deviceIndex(img.blockCount() - 1)));
    show("A1 09 complete", bootloaderComplete());
    show("A1 13 post-success", postSuccess());

    // Repeated at the END as well as the top, because the top of a long dry-run
    // scrolls away and this is the frame the warning is about.
    std::printf("\n");
    reportSettingsUndo(vaultPath);
    return 0;
}
