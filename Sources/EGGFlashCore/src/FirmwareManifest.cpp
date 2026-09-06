// FirmwareManifest.cpp -- the table. See FirmwareManifest.h for why this is a
// source file and not a config file.
//
// Every row here was produced by `python3 Tools/pe/ingest.py <exe> --label X`
// and pasted unmodified except for the date. Do not hand-edit a hash: if a
// value here is wrong, the flash refuses (which is the good case) or proceeds
// on a wrong-but-well-formed image (which is a brick, §2).
#include "egg/FirmwareManifest.h"

#include <cstring>

namespace egg::fw {

const Release kReleases[] = {
    // Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe
    // Ingested 2026-09-06 by Tools/pe/ingest.py. Resource id recovered from the
    // FindResourceW call site at 0x00403213, NOT from the resource table.
    //
    // THE ONLY ROW PROVEN ON HARDWARE. egg-flash drove this image end to end on
    // 2026-09-05: 65 blocks written, 65 verified, 0 rewrites, the device's own
    // whole-image checksum matched, and the application came back in ~900 ms.
    // Every [O] in this project was taken from the firmware it installs.
    //
    // Note this .exe has SIX FWFILE resources where 1.06 and 1.07 have five,
    // despite all three having byte-identical .text. A guard keyed on code
    // identity would not have noticed a new blob appearing; this table is keyed
    // on the whole file for exactly that reason.
    {"1.10",
     {1, 1, 0, 0},
     "97cd23d8294461b412bcd02f4a2cadc23fb255dbea874d641d324729aa814144",
     140,
     66560,
     "8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0",
     0x0081d57du,
     true,
     "Tools/pe/ingest.py 2026-09-06; 6 FWFILE resources; id also hand-derived "
     "2026-09-05 from the `push $0x8c` at 0x0040320c"},

    // Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe
    // Ingested 2026-09-06. Call site 0x00403213, same as 1.10 -- .text is
    // byte-identical across 1.06/1.07/1.10 (same SHA-256), so the derivation
    // lands in the same place; the RESOURCES differ, which is the point.
    //
    // NOT PROVEN ON DEVICE. Flashing this is a downgrade to the firmware the
    // windows-run captures were taken against. It is a supported vendor
    // operation -- Endgame ships the .exe -- but nothing here has tested it,
    // and §5 says a different version is a different device until shown
    // otherwise. Note record 0x71's factory default differs between 1.07 and
    // 1.10 (config-wire-observed.md §7), so a downgrade changes settings
    // behaviour, not just a version string.
    {"1.07",
     {1, 0, 7, 0},
     "d355346d2abefff094188cef927588c1fc7fa4f209a151f6684d57d189ba98e0",
     140,
     66560,
     "3922b14157eff4268ca28c8fe7f284ba1ec0880dae2d9137d01eb1c42e297940",
     0x0081d408u,
     false,
     "Tools/pe/ingest.py 2026-09-06; 5 FWFILE resources present"},

    // Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe
    // Ingested 2026-09-06. Call site 0x00403213. NOT PROVEN ON DEVICE.
    {"1.06",
     {1, 0, 6, 0},
     "221c1c49b6ee55a91a5fb12c0690031654035018875207d5f4886649af40a05d",
     140,
     66560,
     "9f01d732913fcfc6799d699d19a92878c97a5f4dc76ed18b4a307125b35514dd",
     0x0081f40bu,
     false,
     "Tools/pe/ingest.py 2026-09-06; 5 FWFILE resources present"},

    // Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe
    // Ingested 2026-09-06. NOT PROVEN ON DEVICE.
    //
    // The interesting row. 1.04 is a SECOND CODE BASE: its FindResourceW call
    // is at 0x00404347, not 0x00403213, and it imports through a different IAT
    // slot (0x0056e29c, not 0x0051b230). The id derivation recovered 140 there
    // anyway, matching the hand derivation in updater-protocol.md §4 -- which
    // is the evidence that Tools/pe/resource_id.py generalises past the one
    // binary it was written against, rather than pattern-matching 1.10.
    {"1.04",
     {1, 0, 4, 0},
     "0fd2016ff89e0ee4bce0d7eedc9b90ca696189090a15ce09ace4570fe8380f9f",
     140,
     66560,
     "41d5397ec84c4f167e6dce4d05f649f425e5c38ee066b8931721898d5dc013c3",
     0x0081f627u,
     false,
     "Tools/pe/ingest.py 2026-09-06; 5 FWFILE resources; second code base, "
     "call site 0x00404347"},
};

const std::size_t kReleaseCount = sizeof(kReleases) / sizeof(kReleases[0]);

const Release* findRelease(const char* label) {
    if (!label) return nullptr;
    for (std::size_t i = 0; i < kReleaseCount; ++i)
        if (std::strcmp(kReleases[i].label, label) == 0) return &kReleases[i];
    return nullptr;
}

const Release* releaseForUpdaterSha(const std::string& sha256) {
    // Case-insensitive on the hex, exact on the value. No prefix matching:
    // a short hash that happens to be unique today is a collision waiting for
    // the next row.
    if (sha256.size() != 64) return nullptr;
    for (std::size_t i = 0; i < kReleaseCount; ++i) {
        const char* want = kReleases[i].updaterSha256;
        std::size_t j = 0;
        for (; j < 64; ++j) {
            char a = sha256[j], b = want[j];
            if (a >= 'A' && a <= 'F') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'F') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == 64) return &kReleases[i];
    }
    return nullptr;
}

const Release& primaryRelease() {
    // Index 0 by construction, and asserted by Tests/test_manifest.py rather
    // than trusted: the primary must be the one and only provenOnDevice row.
    return kReleases[0];
}

}  // namespace egg::fw
