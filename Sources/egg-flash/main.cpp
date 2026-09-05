// egg-flash -- CLI-first by design (CLAUDE.md §3): no window to close, no Dock
// quit item, and the output is a log by construction.
#include "egg/FlashCommands.h"
#include "egg/Firmware.h"
#include "egg/WritePhase.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace egg::fw;

static int usage() {
    std::printf(
        "egg-flash -- firmware flasher for the Endgame Gear OP1 8k v2\n"
        "\n"
        "  egg-flash image <updater.exe>   validate the firmware image and stop\n"
        "  egg-flash dryrun <updater.exe>  emit the exact byte stream, send nothing\n"
        "  egg-flash stream <updater.exe>  the same stream in full, one frame per\n"
        "                                  line, for diffing against a capture\n"
        "  egg-flash help\n"
        "\n"
        "The image is always FWFILE resource %u of the .exe you name. That is a\n"
        "compile-time constant; there is no way to select a different one, and\n"
        "an image whose SHA-256 is not the pinned value is refused before any\n"
        "byte goes out.\n"
        "\n"
        "There is deliberately no 'flash' verb yet. Staged bring-up (§4.4) puts a\n"
        "real flash last, after a read-back against the device.\n"
        "\n%s\n", kResourceName, kRecoveryProcedure);
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string verb = argv[1];
    if (verb == "help" || verb == "--help" || verb == "-h") return usage();
    if (argc < 3) return usage();

    Image img;
    std::string err;
    if (!img.loadFromExecutable(argv[2], err)) {
        std::printf("REFUSED: %s\n", err.c_str());
        return 1;
    }
    std::printf("image      FWFILE/%u from %s\n", kResourceName, argv[2]);
    std::printf("size       %zu bytes, %zu blocks of %zu\n",
                img.bytes().size(), img.blockCount(), kBlockSize);
    std::printf("sha256     %s  (matches the pinned constant)\n", img.sha256().c_str());
    std::printf("checksum   0x%08x  (FUN_00403580, 32-bit sum of every byte)\n",
                img.checksum());
    std::printf("blocks     device indices 0x%02x..0x%02x\n",
                img.deviceIndex(0), img.deviceIndex(img.blockCount() - 1));

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
    return 0;
}
