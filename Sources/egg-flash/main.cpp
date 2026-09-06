// egg-flash -- CLI-first by design (CLAUDE.md §3): no window to close, no Dock
// quit item, and the output is a log by construction.
#include "egg/BootloaderEntry.h"
#include "egg/Device.h"
#include "egg/FlashCommands.h"
#include "egg/FlashPlan.h"
#include "egg/HidBootloaderLink.h"
#include "egg/Firmware.h"
#include "egg/RecordVault.h"
#include "egg/Transport.h"
#include "egg/WritePhase.h"

#include <chrono>
#include <csignal>
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
        "  egg-flash read-firmware [out.bin]   read the device's CURRENT image\n"
        "                                      back with A0 07 and save it.\n"
        "                                      Read-only. --check just reports\n"
        "                                      whether the bootloader is there.\n"
        "  egg-flash flash <updater.exe> --backup <file>\n"
        "                                      the real thing. Prints the plan\n"
        "                                      and a token; needs --confirm.\n"
        "\n"
        "A FLASH IS TWO RUNS, and that is deliberate (§4.2b, §4.2a):\n"
        "\n"
        "  1. egg-flash enter-bootloader --yes    (A1 3A -- IT LATCHES, see below)\n"
        "     egg-flash read-firmware backup.bin\n"
        "     Reads all 65 blocks out with A0 07 and saves them. The READ is\n"
        "     read-only and abortable; the ENTRY is not. §4.2b requires A1 3A\n"
        "     for anything touching firmware, because a read taken in a\n"
        "     button-entered bootloader may prove nothing about a flash done\n"
        "     in an A1 3A-entered one. From that A1 3A until a flash\n"
        "     completes, the mouse is a bootloader and not a mouse.\n"
        "\n"
        "  2. egg-flash flash <updater.exe> --backup backup.bin --confirm <token>\n"
        "     Enters with A1 3A, the vendor's own way, and then sends EXACTLY\n"
        "     their byte stream:\n"
        "       A1 3A -> A0 03 -> (A0 06, A0 07) x65 -> A1 08 -> A1 09 -> A1 13\n"
        "     Nothing is inserted. Tests/test_golden_vendor.py diffs those frames\n"
        "     against Endgame's own capture of this mouse being flashed.\n"
        "\n"
        "The split is the whole point: the backup is what §4.2 requires before an\n"
        "erase, but taking it inside the flash would have put 65 frames the\n"
        "vendor never sends into the one sequence we have evidence for. So run 1\n"
        "takes it and run 2 only CHECKS it. `flash` refuses without a valid one.\n"
        "\n"
        "A1 3A LATCHES [O]. Once run 2 starts, a power cycle will not return the\n"
        "mouse to normal -- only a completed flash will. Everything that can fail\n"
        "is checked before it is sent, and if a flash aborts anyway, re-running it\n"
        "is the way out. Endgame's Windows updater recovers the same state.\n"
        "\n"
        "`flash` ends with A1 13, the vendor's last step, which resets settings.\n"
        "It refuses to start unless a settings undo already exists.\n"
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
        std::printf("\nFAILED: %s\n", describe(o.result, egg::kProductIdBootloader).c_str());
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
        std::printf("\nFAILED: %s\n", describe(o.result, egg::kProductIdApplication).c_str());
        // NOT kRecoveryProcedure. That text tells the reader to hold LEFT+RIGHT
        // to reach the bootloader -- which is where they already are. Printing
        // it here was a real defect: the one moment a recovery instruction is
        // read closely is the moment it is wrong.
        std::printf(
          "\nThe mouse is still in the bootloader. NOTHING was erased or written\n"
          "by this tool -- no A0 03, no A0 06 -- so the application region is\n"
          "untouched. It is in the wrong mode, not damaged.\n"
          "\n"
          "A power cycle will NOT fix it and neither will running this again;\n"
          "both are [O], twice each (notes/bootloader-observed.md 5b).\n"
          "A1 09 is working: the device resets and re-enumerates in ~292 ms,\n"
          "it simply comes back into the bootloader, because the flag that sends\n"
          "it there lives in NVM and is cleared by a COMPLETED FLASH, not by\n"
          "the exit command.\n"
          "\n"
          "THE WAY OUT IS TO FINISH A FLASH. Two ways to do that:\n"
          "  1. Endgame's Windows updater, on any Windows machine. It treats a\n"
          "     0x1977 device as a supported starting state and goes straight to\n"
          "     flashing (notes/updater-protocol.md 5.1a). No code of ours runs.\n"
          "  2. This tool. `flash` treats an already-bootloadered device as a\n"
          "     supported starting state and skips the A1 3A entry, exactly as\n"
          "     their updater does. It needs a backup file first, and taking one\n"
          "     is read-only:\n"
          "       ./build/egg-flash read-firmware backup.bin\n"
          "       ./build/egg-flash flash <updater.exe> --backup backup.bin\n"
          "\n"
          "Settings are safe either way: the vault is on disk and restore is\n"
          "verified 21/21.\n");
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


// ---------------------------------------------------------------------------
// §4.4 stage 3, and now also CLAUDE.md §4.2's precondition for ANY erase:
// "Never erase without a saved copy of what is being erased."
//
// Read-only. A0 07 carries a block index and nothing else -- no payload, no
// length. Before erase, so §4.2's first regime applies without qualification:
// every failure aborts, because a partial read is not a backup.
// ---------------------------------------------------------------------------
static bool bootloaderIsPresent(std::size_t* bootColl, std::size_t* appColl) {
    const auto seen = seeAll();
    const std::size_t b = countVendorCollections(seen, egg::kProductIdBootloader);
    const std::size_t a = countVendorCollections(seen, egg::kProductIdApplication);
    if (bootColl) *bootColl = b;
    if (appColl)  *appColl  = a;
    return b == 1 && a == 0;
}

// The instruction, in one place, so every caller says the same thing.
//
// CHANGED 2026-09-05 by the owner, overruling a split I had written into §4.2b
// without asking: the firmware read-back now enters the SAME way the flash
// does. His reasoning, and it is right -- the flash reads blocks back inside
// an A1 3A-entered bootloader, so a read-back done in a button-entered one
// proves nothing about the flash unless the two are identical, and that is
// [G]. I had already conceded that a button-mode refusal would be
// "inconclusive about the flash" without noticing that this makes half the
// test's outcomes worthless.
static const char* const kBootloaderEntryHelp =
    "Get into the bootloader THE WAY THE FLASH DOES (§4.2b), so that what this\n"
    "read proves also applies to the flash:\n"
    "  ./build/egg-flash enter-bootloader --yes\n"
    "  ./build/egg-flash read-firmware --check\n"
    "\n"
    "READ THIS BEFORE YOU RUN IT. A1 3A LATCHES [O]. Once sent, the mouse stops\n"
    "being a mouse and a power cycle does NOT undo it -- only a completed flash\n"
    "does. So from here until you finish a flash, it is a bootloader.\n"
    "That is recoverable, not a brick: Endgame's Windows updater accepts a\n"
    "0x1977 device as a starting state, and so does `flash`.\n"
    "\n"
    "The buttons (hold LEFT+RIGHT while plugging in) reach the same mode and\n"
    "unplug back out of it, which is safer -- but a read done there may not\n"
    "tell you anything about the flash, which is why it is not the default.\n";

static int saveAndVerify(const std::vector<std::uint8_t>& image,
                         const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("REFUSED: cannot open %s for writing\n", path.c_str()); return 1; }
    const std::size_t wrote = std::fwrite(image.data(), 1, image.size(), f);
    const bool closed = std::fclose(f) == 0;
    if (wrote != image.size() || !closed) {
        std::printf("REFUSED: wrote %zu of %zu bytes to %s\n",
                    wrote, image.size(), path.c_str());
        return 1;
    }
    // §4.2 says "and the file re-read and checked". Re-reading is the whole
    // point: a backup that was never read back is a belief, not a backup.
    std::FILE* g = std::fopen(path.c_str(), "rb");
    if (!g) { std::printf("REFUSED: cannot re-open %s to verify it\n", path.c_str()); return 1; }
    std::vector<std::uint8_t> back(image.size());
    const std::size_t got = std::fread(back.data(), 1, back.size(), g);
    std::fclose(g);
    if (got != image.size() || back != image) {
        std::printf("REFUSED: %s does not read back as written\n", path.c_str());
        return 1;
    }
    return 0;
}

static int cmdReadFirmware(const std::string& outPath, bool checkOnly, bool verbose) {
    std::size_t boot = 0, app = 0;
    const bool ready = bootloaderIsPresent(&boot, &app);
    std::printf("preflight, right now:\n"
                "  %zu bootloader  vendor collection(s)  (PID 0x%04x)\n"
                "  %zu application vendor collection(s)  (PID 0x%04x)\n",
                boot, egg::kProductIdBootloader, app, egg::kProductIdApplication);
    if (!ready) {
        std::printf("  -> NOT ready. Nothing was sent.\n\n%s", kBootloaderEntryHelp);
        return 1;
    }
    std::printf("  -> ready.\n");
    if (checkOnly) return 0;

    egg::Log log(verbose);
    auto link = HidBootloaderLink::open(log);
    if (!link) { std::printf("REFUSED: could not open the bootloader.\n"); return 1; }

    std::printf("\nreading blocks 0x%02x..0x%02x with A0 07 (read-only)...\n",
                kBlockFirst, kBlockLast);
    const ReadBack rb = readApplicationRegion(*link);
    if (!rb.ok) {
        std::printf("\nFAILED after %zu of %zu blocks: %s\n",
                    rb.blocksRead, kBlockCount, rb.error.c_str());
        std::printf(
          "\nIf the status was a value other than 0x01, the bootloader may only\n"
          "accept A0 07 inside a flash session it started with A0 03. That is a\n"
          "FINDING, not a bug to work around: A0 03 erases, and §4.2 forbids\n"
          "erasing without the backup this command exists to make.\n");
        return 1;
    }
    std::printf("read       %zu blocks, %zu bytes\n", rb.blocksRead, rb.image.size());
    std::printf("sha256     %s\n", sha256Hex(rb.image.data(), rb.image.size()).c_str());
    std::printf("checksum   0x%08x  (32-bit sum, the value A0 03 declares)\n",
                wholeImageChecksum(rb.image));
    if (saveAndVerify(rb.image, outPath) != 0) return 1;
    std::printf("saved      %s, re-read and byte-identical\n", outPath.c_str());
    std::printf(
      "\nTHE MOUSE IS STILL IN THE BOOTLOADER and stays there until a flash\n"
      "completes. That is expected, not a fault. The flash below will find it\n"
      "already there and skip its own A1 3A -- 134 frames instead of 135, which\n"
      "is the path Endgame's updater takes for a 0x1977 device (§5.1a).\n"
      "\nTHIS FILE IS THE BACKUP A FLASH REQUIRES (§4.2). Pass it with:\n"
      "  ./build/egg-flash flash <updater.exe> --backup %s\n"
      "The flash CHECKS it and takes no read-back of its own, so that run sends\n"
      "exactly the vendor's byte stream and nothing else.\n"
      "\nSCORE IT against the pre-registered predictions before drawing any\n"
      "conclusion from it (§7). This extracts the reference image itself and\n"
      "names the reading for whatever shape of mismatch comes back:\n"
      "  python3 Tools/score-read-firmware.py %s\n"
      "A match proves the block arithmetic of §3.8 AND that the application is\n"
      "intact. A mismatch is worth understanding BEFORE anything is written --\n"
      "notes/prediction-read-firmware.md §5 says what each shape means, written\n"
      "down before the data existed so it cannot be reasoned backwards from.\n"
      "\nOr compare against the reference directly:\n"
      "  python3 Tools/pe/fwfile.py --extract 140 \"<updater.exe>\" /tmp/fw140.bin\n"
      "  cmp %s /tmp/fw140.bin\n",
      outPath.c_str(), outPath.c_str(), outPath.c_str());
    return 0;
}

// The backup is CHECKED here, never taken. the owner's call, 2026-09-05: the
// read-back used to run inside the flash, which meant 65 A0 07 frames went out
// before A0 03 -- something the vendor never does, inserted into the one
// sequence we have a capture of. It now comes from a separate `read-firmware`
// run, so the flash run puts exactly the vendor's byte stream on the wire.
//
// The tradeoff, stated rather than hidden: a backup taken in an earlier session
// could in principle be stale. Firmware does not change on its own, so the only
// way to make it stale is to flash between the two runs -- which is deliberate,
// not accidental. The file's sha256 is printed here and by `read-firmware`, so
// the two runs can be confirmed to be talking about the same bytes.
//
// This runs BEFORE A1 3A. A refusal here must not leave the mouse latched in a
// bootloader it was put into for a flash that then did not happen.
static bool checkBackup(const std::string& path, const Image& img) {
    // The DECISION is egg::fw::checkBackupFile, in the library, where
    // Tests/mutants.sh can plant bugs in it. Everything here is presentation.
    const BackupCheck bc = checkBackupFile(path);
    if (!bc.ok) {
        std::printf("\nREFUSED: %s.\n", bc.reason.c_str());
        std::printf("§4.2 forbids erasing without a saved copy of what is being\n"
                    "erased. Take one first -- it is read-only and sends no write\n"
                    "of any kind:\n"
                    "  ./build/egg-flash read-firmware %s\n",
                    path.empty() ? "device-firmware.bin" : path.c_str());
        return false;
    }
    std::printf("\nbackup     %s\n", path.c_str());
    std::printf("           %zu bytes, sha256 %s\n", bc.size, bc.sha256.c_str());
    if (bc.sha256 == img.sha256())
        std::printf("           identical to the image about to be written: this\n"
                    "           is a re-flash of what is already there. The vendor\n"
                    "           does this too (capture 09-flash-again).\n");
    return true;
}

// CLAUDE.md §3: "Trap SIGINT/SIGTERM explicitly during the write phase."
//
// NEVER IMPLEMENTED UNTIL 2026-09-05, when an adversarial audit noticed the
// rule had no code behind it. It is not hypothetical: earlier the same day the owner
// watched a command sit silent and asked "uhh my terminal froze do i control c
// this?" -- of a read-only command, where the answer was yes. During the write
// phase the answer is no, and the difference cannot be left to a judgement call
// made under stress.
//
// §4.2: after the erase the device has no valid application, so exiting
// cleanly GUARANTEES the bad outcome. A default-disposition Ctrl-C terminates
// the process between two chunks and leaves exactly that.
//
// SIG_IGN, not a flag a loop polls: the loops here are deliberately unbounded,
// so a "please stop" flag would either be ignored anyway or become the early
// exit §4.2 forbids. The handler is installed for the erase-through-verify unit
// only and restored immediately after, so before the erase Ctrl-C still works
// normally -- which is correct, because before the erase abort is always right.
//
// This is not an inescapable process. SIGKILL cannot be trapped, so `kill -9`
// still works for a genuinely wedged tool. It is not offered in the message,
// because someone reaching for it should have to decide to.
class NoQuitDuringWrite {
public:
    NoQuitDuringWrite() {
        int_  = std::signal(SIGINT,  SIG_IGN);
        term_ = std::signal(SIGTERM, SIG_IGN);
        // SIGHUP too: closing the terminal is the same mistake with less
        // deliberation behind it than Ctrl-C.
        hup_  = std::signal(SIGHUP,  SIG_IGN);
    }
    ~NoQuitDuringWrite() {
        if (int_  != SIG_ERR) std::signal(SIGINT,  int_);
        if (term_ != SIG_ERR) std::signal(SIGTERM, term_);
        if (hup_  != SIG_ERR) std::signal(SIGHUP,  hup_);
    }
    NoQuitDuringWrite(const NoQuitDuringWrite&) = delete;
    NoQuitDuringWrite& operator=(const NoQuitDuringWrite&) = delete;
private:
    void (*int_)(int)  = SIG_ERR;
    void (*term_)(int) = SIG_ERR;
    void (*hup_)(int)  = SIG_ERR;
};

// ---------------------------------------------------------------------------
// §4.4 stage 4. The real thing.
// ---------------------------------------------------------------------------
static int cmdFlash(const Image& img, const std::string& vaultPath,
                    const std::string& confirmArg, const std::string& backupPath,
                    bool verbose) {
    const std::string token = confirmToken(img);

    // THE PLAN IS PRINTED FIRST, AND UNCONDITIONALLY.
    //
    // The first version of this checked device readiness before showing the
    // plan, so the only way to read what the tool would do was to already have
    // the mouse in the bootloader. That is backwards: reviewing the plan is
    // read-only and should happen calmly, BEFORE the device is put into a state
    // anyone is nervous about. The token depends only on the image, so there is
    // nothing about the device it needs in order to be correct.
    if (confirmArg.empty()) {
        std::printf(
          "\nWHAT WOULD HAPPEN, in order:\n"
          "  A1 3A  enter the bootloader, the vendor's own way (§4.2b)\n"
          "  A0 03  erase and declare %zu blocks, checksum 0x%08x  <-- POINT OF NO RETURN\n"
          "  A0 06  write block, then A0 07 read it back and compare, x%zu\n"
          "  A1 08  whole-image checksum, compared against the host's\n"
          "  A1 09  complete, then the device re-enumerates as the application\n"
          "  A1 13  factory reset -- the vendor's last step. YOUR SETTINGS GO.\n"
          "         Restore them afterwards with `egg-config restore`; that path\n"
          "         is verified 21/21. This command refuses to run at all if no\n"
          "         settings undo exists.\n"
          "\nIF THE MOUSE IS ALREADY IN THE BOOTLOADER, the A1 3A above is\n"
          "SKIPPED and the other 134 frames are sent unchanged. The vendor does\n"
          "the same -- their updater treats a 0x1977 device as a supported\n"
          "starting state (§5.1a). The token still covers all 135 frames, because\n"
          "it identifies the PLAN and must be reviewable before the mouse is in\n"
          "any particular mode; the preflight line below says which case you are\n"
          "in, and it is printed before anything is sent.\n"
          "\nA1 3A LATCHES. Once it is sent, a power cycle will NOT return the\n"
          "mouse to normal -- only a completed flash will. Everything that can\n"
          "fail is checked BEFORE it is sent, so the latched window is the flash\n"
          "itself. If the flash aborts anyway, re-run this command; Endgame's\n"
          "Windows updater also recovers a 0x1977 device (updater-protocol.md\n"
          "§5.1a). It is not a brick.\n"
          "\nAfter A0 03 this tool DOES NOT STOP. No cancel, no timeout that gives\n"
          "up: with the application erased, exiting cleanly guarantees the bad\n"
          "outcome. It retries, reconnects, and keeps driving to a verified image.\n"
          "\nThis is EXACTLY the vendor's byte stream, in their order, with nothing\n"
          "added -- the golden_vendor test diffs it against Endgame's own capture\n"
          "of this mouse being flashed, and that is what makes the claim checkable\n"
          "rather than asserted.\n"
          "\nA BACKUP MUST ALREADY EXIST (§4.2). Take it in its own run, which is\n"
          "read-only and sends no write:\n"
          "  ./build/egg-flash read-firmware <file>\n"
          "then pass it here with --backup <file>. It is checked, not taken, so\n"
          "that this run inserts nothing into their sequence.\n"
          "\nTo proceed:\n"
          "  ./build/egg-flash flash <updater.exe> --backup <file> --confirm %s\n"
          "\nThat token is SHA-256 over the exact frames listed above. Change the\n"
          "image and it changes, so an approval cannot outlive what it approved.\n",
          img.blockCount(), img.checksum(), img.blockCount(), token.c_str());
        // NOT another reportSettingsUndo() here. main() already printed it
        // before the plan and the gate below prints it again on the way to
        // deciding; a third copy is noise, and noise is how a real warning
        // stops being read.
    }

    // THE HOST-SIDE CHECKS COME FIRST, ahead of even looking at the device.
    //
    // Not cosmetic. Two of these three refusals -- the settings undo and the
    // backup -- are decisions about files sitting on this machine, and putting
    // them ahead of enumeration means they can be exercised, and regression-
    // tested, with no mouse plugged in at all. Tests/test_flash_backup.sh does
    // exactly that. A guard that only runs when the hardware is present is a
    // guard nothing checks.
    //
    // §4.2b: they are all before A1 3A, which latches.

    // A1 13 is in the plan, so this flash WILL reset settings. §4.2's "never
    // erase without a saved copy" is not specific to firmware: refuse rather
    // than warn. reportSettingsUndo already prints where to get one.
    if (!reportSettingsUndo(vaultPath)) {
        std::printf("\nREFUSED: this flash ends with A1 13 (factory reset) and\n"
                    "there is no settings undo. Run `egg-config read` first.\n"
                    "NOTHING WAS SENT.\n");
        return 1;
    }

    if (!checkBackup(backupPath, img)) {
        std::printf("NOTHING WAS SENT.\n");
        return 1;
    }

    // EVERYTHING THAT CAN FAIL HAPPENS BEFORE A1 3A IS SENT. §4.2b: the entry
    // latches, so the window in which an abort leaves the mouse in the
    // bootloader must contain nothing but the flash itself.
    std::size_t boot = 0, app = 0;
    const auto seen0 = seeAll();
    boot = countVendorCollections(seen0, egg::kProductIdBootloader);
    app  = countVendorCollections(seen0, egg::kProductIdApplication);
    std::printf("\npreflight, right now:\n"
                "  %zu application vendor collection(s)  (PID 0x%04x)\n"
                "  %zu bootloader  vendor collection(s)  (PID 0x%04x)\n",
                app, egg::kProductIdApplication, boot, egg::kProductIdBootloader);

    const bool fromApp  = (app == 1 && boot == 0);
    const bool fromBoot = (app == 0 && boot == 1);
    if (!fromApp && !fromBoot) {
        std::printf("  -> NOT ready: need exactly one mouse, in exactly one mode.\n"
                    "     NOTHING WAS SENT.\n");
        return 1;
    }
    // THE ALREADY-IN-BOOTLOADER PATH MUST CONFIRM IDENTITY TOO.
    //
    // Found by adversarial audit, 2026-09-05, and it matters far more than it
    // did an hour ago: with the read-back now entering by A1 3A (§4.2b, the owner's
    // call), the mouse is ALWAYS latched by the time `flash` runs, so fromBoot
    // is no longer the rare recovery path -- it is the normal one.
    //
    // fromApp gets a full identity check for free, because
    // enterBootloaderAndConfirm requires PID + bcdDevice + product string
    // before it returns EnteredAndConfirmed. fromBoot had none: one 16-bit PID
    // and we erase. §4.2 says "if the bootloader reports a version or an
    // identity of any kind, refuse anything unrecognised", and it reports both.
    if (fromBoot) {
        bool known = false;
        SeenDevice bl{};
        for (const auto& d : seen0) {
            if (d.productId != egg::kProductIdBootloader) continue;
            bl = d;
            if (d.releaseNumber == egg::kBootloaderRelease &&
                d.product == egg::kBootloaderProduct) { known = true; break; }
        }
        if (!known) {
            std::printf("  -> REFUSED: PID 0x%04x is present but its identity is\n"
                        "     not one we recognise.\n"
                        "       bcdDevice 0x%04x, expected 0x%04x\n"
                        "       product   \"%s\", expected \"%s\"\n"
                        "     §4.2: refuse anything unrecognised. NOTHING WAS SENT.\n",
                        egg::kProductIdBootloader, bl.releaseNumber,
                        egg::kBootloaderRelease, bl.product.c_str(),
                        egg::kBootloaderProduct);
            return 1;
        }
        std::printf("  -> bootloader identity confirmed: bcdDevice 0x%04x, \"%s\"\n",
                    bl.releaseNumber, bl.product.c_str());
    }

    std::printf(fromApp ? "  -> ready. Will enter the bootloader with A1 3A.\n"
                        : "  -> ready. Already in the bootloader; no entry needed.\n");


    if (confirmArg.empty()) return 2;

    if (confirmArg != token) {
        std::printf("\nREFUSED: --confirm %s does not match this plan's token %s.\n"
                    "NOTHING WAS SENT. Re-run without --confirm to see the plan.\n",
                    confirmArg.c_str(), token.c_str());
        return 1;
    }

    egg::Log log(verbose);

    // ---- The entry. The vendor's own, and the plan's first frame. ---------
    if (fromApp) {
        std::printf("\n[1/4] entering the bootloader (A1 3A)\n");
        auto appDev = egg::Device::open(egg::kProductIdApplication, log);
        if (!appDev) {
            std::printf("REFUSED: could not open the application device.\n"
                        "NOTHING WAS SENT.\n");
            return 1;
        }
        egg::Transport t(*appDev, egg::kUpdaterBusy, log);
        const auto t0 = std::chrono::steady_clock::now();
        EntryEnv env;
        env.nowMs = [t0] {
            return static_cast<unsigned>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count());
        };
        env.sleepMs = [](unsigned ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        };
        env.enumerate = seeAll;
        env.exchange = [&t](const std::vector<std::uint8_t>& frame,
                            std::vector<std::uint8_t>& reply, bool& readOk) {
            const egg::Reply r = t.exchange(frame, egg::kReportSmall,
                                            "A1 3A enter bootloader", 2);
            readOk = !r.buf.empty();
            reply  = r.buf;
            return r.outcome != egg::Outcome::TransportFail;
        };
        const EntryOutcome o = enterBootloaderAndConfirm(env);
        if (o.result != EntryResult::EnteredAndConfirmed) {
            std::printf("\nABORTED: %s\n",
                        describe(o.result, egg::kProductIdBootloader).c_str());
            std::printf("NOTHING HAS BEEN ERASED OR WRITTEN.\n");
            if (o.sawBootloaderPid)
                std::printf("The mouse MAY be in the bootloader. Check with:\n"
                            "  ./build/egg-flash read-firmware --check\n");
            return 1;
        }
        // appDev is closed here, before the bootloader link opens: the
        // application handle refers to a device that no longer exists.
        std::printf("      in the bootloader after %u ms  (PID 0x%04x, \"%s\")\n",
                    o.reenumerateMs, o.seen.productId, o.seen.product.c_str());
    }

    auto link = HidBootloaderLink::open(log);
    if (!link) {
        std::printf("REFUSED: could not open the bootloader.\n"
                    "Nothing has been erased or written. If the mouse is latched\n"
                    "in the bootloader, re-running this command is the way out.\n");
        return 1;
    }

    // ---- Preflight proper. Any failure aborts and nothing has changed. ----
    std::printf("\n[2/4] preflight\n");
    std::string err;
    if (!preflight(img, err)) {
        std::printf("\nABORTED: %s\nNOTHING HAS BEEN ERASED OR WRITTEN.\n", err.c_str());
        return 1;
    }
    std::printf("      image and block range pass. NOTHING WAS SENT to check\n"
                "      this -- preflight has no link and cannot send (§4.2).\n");

    // ---- The point of no return. ------------------------------------------
    std::printf("\n[3/4] writing. FROM HERE THIS TOOL DOES NOT STOP.\n");
    std::printf("      Ctrl-C is now IGNORED until the image is resident.\n");
    std::fflush(stdout);
    const Progress p = [&] {
        NoQuitDuringWrite guard;      // §3, and see the comment on the class
        return driveToVerifiedImage(*link, img);
    }();

    std::printf("\nDONE. The image is verified resident on the device.\n"
                "  write acks       %zu   (>65 means blocks were rewritten)\n"
                "  blocks verified  %zu\n"
                "  rewrites         %zu\n  reconnects       %zu\n"
                "  send failures    %zu\n  whole-image sum  %s\n  A1 09 acked      %s\n",
                p.blocksWritten, p.blocksVerified, p.rewrites, p.reconnects,
                p.sendFailures, p.imageVerified ? "MATCHED" : "not matched",
                p.completeAcked ? "yes" : "no");
    // ---- [4/4] The vendor's own tail: wait for 0x1978, then A1 13. --------
    //
    // §5.4 steps 6-7. The bootloader link is finished with; A1 13 goes to the
    // APPLICATION device, which does not exist yet at this point. The vendor
    // waits with Sleep(800..16000) summing to 168 s; we poll for the same
    // 168 s. Matched rather than shortened (audit, 2026-09-05): giving up here
    // skips A1 13 and leaves stale settings under new firmware, and a wait
    // costs nothing on the wire.
    std::printf("\n[4/4] waiting for the application to come back "
                "(up to %u s, the vendor's own budget)\n", kPostFlashWaitMs / 1000);
    SeenDevice appAgain{};
    bool back = false;
    for (unsigned waited = 0; waited <= kPostFlashWaitMs; waited += 100) {
        for (const auto& d : seeAll())
            if (d.productId == egg::kProductIdApplication &&
                d.usagePage == egg::kUsagePageVendor && d.usage == egg::kUsageVendor) {
                appAgain = d; back = true; break;
            }
        if (back) { std::printf("      back after ~%u ms  (PID 0x%04x, bcdDevice 0x%04x, \"%s\")\n",
                                waited, appAgain.productId, appAgain.releaseNumber,
                                appAgain.product.c_str());
                    break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!back) {
        // The image is verified resident, so this is not a failure of the
        // flash. It is the vendor's own gate (c) at 0x403cc3 not being met,
        // and their code takes a path that reports success without sending
        // A1 13 (§5.4a). Say so precisely rather than alarmingly.
        std::printf(
          "      it did not. THE IMAGE IS VERIFIED RESIDENT -- this is not a\n"
          "      failed flash. Unplug and replug. A1 13 was NOT sent, so your\n"
          "      settings are untouched; the vendor's tool reaches the same\n"
          "      state via 0x403e6f and also reports success (§5.4a).\n");
        std::printf("\nThe pre-flash image is in %s.\n", backupPath.c_str());
        return 0;
    }

    std::printf("\n      sending A1 13 (factory reset), the vendor's last step\n");
    {
        auto appDev = egg::Device::open(egg::kProductIdApplication, log);
        if (!appDev) {
            std::printf("      could not open it. A1 13 NOT sent; settings are\n"
                        "      untouched. The flash itself is complete.\n");
        } else {
            egg::Transport t(*appDev, egg::kConfigBusy, log);
            // The vendor sleeps 900 ms and then reads one 64-byte report which
            // it never inspects (§5.4a). The config tool sleeps 1100 and DOES
            // require resp[1]==0x01. We use the config tool's timing and report
            // what came back without gating on it -- the updater does not gate,
            // and a status gate on a command whose effect we can verify
            // directly would only manufacture false failures.
            const egg::Reply r = t.exchange(postSuccess(), egg::kReportSmall,
                                            "A1 13 factory reset", 1100);
            std::printf("      resp[1]=0x%02x (%s)\n", r.status,
                        r.ok() ? "acknowledged" : egg::describe(r.outcome));
        }
    }

    std::printf(
      "\nSETTINGS ARE NOW AT FACTORY DEFAULTS. To put yours back:\n"
      "  ./build/egg-config restore %s\n"
      "\nAnd to confirm the reset landed where the vendor's does:\n"
      "  ./build/egg-config read --save after-flash.bin\n"
      "  ./build/egg-config diff %s after-flash.bin\n"
      "Expect exactly the 21 bytes of notes/prediction-factory-reset.md.\n"
      "\nThe pre-flash image is in %s if anything needs putting back.\n",
      vaultPath.c_str(), vaultPath.c_str(), backupPath.c_str());
    return 0;
}

int main(int argc, char** argv) {
    std::string vaultPath = defaultVaultPath();
    std::string args[3];
    std::string confirmArg, backupPath;
    bool yes = false, verbose = false, checkOnly = false;
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--vault" && i + 1 < argc) { vaultPath = argv[++i]; continue; }
        if (a == "--yes") { yes = true; continue; }
        if (a == "--confirm" && i + 1 < argc) { confirmArg = argv[++i]; continue; }
        if (a == "--backup" && i + 1 < argc) { backupPath = argv[++i]; continue; }
        if (a == "--check") { checkOnly = true; continue; }
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
    // Takes an OUTPUT path, not a vendor .exe, so it must be dispatched before
    // the image load below -- and it must never load an image, because reading
    // the device back has nothing to do with what we might later write.
    if (verb == "read-firmware")
        return cmdReadFirmware(n >= 2 ? args[1] : std::string("device-firmware.bin"),
                               checkOnly, verbose);

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

    if (verb == "flash") {
        // No default path. When this command TOOK the backup a generated name
        // was right; now that it CHECKS one, inventing a name would either
        // refuse against a file the user never named or, worse, silently accept
        // an unrelated file that happened to be sitting there.
        return cmdFlash(img, vaultPath, confirmArg, backupPath, verbose);
    }

    // "stream" prints every frame in full, one per line, so the whole outbound
    // byte sequence can be diffed against a capture of the vendor's tool doing
    // the same flash. That comparison is worth more than the frozen golden file
    // §4.3 asks for, because the reference is not something we produced -- it is
    // what Endgame's own updater actually sent to this exact mouse.
    if (verb == "stream") {
        // STREAMS plannedFrames(), NOT a second copy of the sequence.
        //
        // This used to build the list inline, which meant the golden vendor
        // diff -- the strongest check in the project, because its reference is
        // Endgame's own capture of THIS mouse being flashed -- was validating a
        // list that nothing else sent. The flash sends plannedFrames(). Two
        // independently-written copies of "the byte stream" is the drift this
        // repo has already been bitten by twice (MUTABLE's two lists, and the
        // FWFILE-selection duplicate). Now the capture diff covers the bytes
        // that actually go out.
        //
        // They became identical when A1 13 went back into the plan on
        // 2026-09-05. Before that the plan was deliberately a subset, and this
        // unification would have been wrong.
        const auto frames = plannedFrames(img);
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const char* what;
            if (i == 0)                       what = "enter";
            else if (i == 1)                  what = "start";
            else if (i + 3 == frames.size())  what = "wholesum";
            else if (i + 2 == frames.size())  what = "complete";
            else if (i + 1 == frames.size())  what = "postsuccess";
            else                              what = ((i - 2) & 1) ? "verify" : "write";
            std::printf("%s ", what);
            for (std::size_t j = 0; j < frames[i].size(); ++j)
                std::printf("%02x", frames[i][j]);
            std::printf("\n");
        }
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
