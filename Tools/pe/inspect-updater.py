#!/usr/bin/env python3
"""Review a NEW Endgame updater before letting egg-flash near it.

WHAT THIS IS FOR. `egg-flash` pins the SHA-256 of the image it will write as a
compile-time constant (CLAUDE.md §1.4, Sources/EGGFlashCore/include/egg/Firmware.h).
That is deliberate: CLAUDE.md §2 says "nothing downstream of us catches a
wrong-but-well-formed image", so the guard has to be ours and it has to run
before the first byte goes out. The consequence is that a new firmware release
does not work automatically -- somebody has to look at the new .exe and decide.

This is what they look at. It prints everything needed for that decision and
then the exact line to paste into Firmware.h. It NEVER touches the device and
NEVER writes to the repo.

WHY LOOKING IS NOT PARANOIA. notes/updater-protocol.md §0.1.1: the binary
Endgame ships as "OP1 8k v2 Firmware Updater 1.10" was linked from a project
named "Endgame Gear EL1 8k Firmware Updater 0.01" -- a different mouse -- and
that same release added a sixth FWFILE resource the earlier ones do not have.
Their build process demonstrably permits one product's updater to carry another
product's project identity. Every FWFILE in these binaries is exactly 66,560
bytes and would produce valid per-block checksums and read back exactly as
written, so the device cannot tell them apart. Only the id does, and the id is
only trustworthy if the binary really is an OP1 8k v2 updater.

    python3 Tools/pe/inspect-updater.py <new-updater.exe>

Exit status is 0 if every check passed, 1 if anything wants a human.
"""
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fwfile  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The id the vendor's own binaries hardcode, in BOTH code bases:
# updater 1.10 `push $0x8c` at 0x0040320c, updater 1.04 at file offset 0x373a.
FLASHED_ID = 140

# [D] notes/updater-protocol.md §4: 65 blocks of 1024 exactly, remainder 0.
EXPECTED_SIZE = 66560
BLOCK = 1024

# What 1.10 holds, so a new release can be diffed against a known point rather
# than judged in isolation. Regenerate with Tools/pe/fwfile.py, never by hand.
KNOWN_110 = {
    133: "09ba52978e0b72a4b4b65f640d177de61530636eed5fa5bf97b1ef431f5e62cf",
    135: "4fb2c174736683e6a265c60f4875dd7f8c26472c72be50d57ec00ebcb399cc6f",
}
PINNED_IMAGE_SHA = "8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def strings16(blob, min_len=6):
    """UTF-16LE strings. The version resource and the PDB path are both here."""
    out, cur = [], bytearray()
    for i in range(0, len(blob) - 1, 2):
        ch = blob[i] | (blob[i + 1] << 8)
        if 0x20 <= ch < 0x7F:
            cur.append(ch)
        else:
            if len(cur) >= min_len:
                out.append(cur.decode("ascii"))
            cur = bytearray()
    if len(cur) >= min_len:
        out.append(cur.decode("ascii"))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    raw = open(path, "rb").read()

    problems = []
    notes = []

    print(f"file       {path}")
    print(f"size       {len(raw)} bytes")
    print(f"sha256     {sha(raw)}")
    print()

    # ---- 1. Is it even an OP1 8k v2 updater? ------------------------------
    # Checked against the whole file in UTF-16LE, which is how PE version
    # resources and the CodeView PDB path are both stored. §1.2a: this cannot
    # see an ASCII-encoded identity, and it is stated rather than assumed.
    s16 = strings16(raw)
    ascii_s = re.findall(rb"[\x20-\x7e]{6,}", raw)
    ident = [t for t in s16 if "OP1" in t or "EL1" in t or "Endgame" in t]
    pdb = [t for t in ascii_s if t.endswith(b".pdb")]

    print("identity strings (UTF-16LE):")
    for t in sorted(set(ident))[:12]:
        print(f"  {t}")
    if not ident:
        problems.append("no Endgame/OP1 identity string found in UTF-16LE")
    if not any("OP1" in t for t in ident):
        problems.append("NO STRING MENTIONS 'OP1' -- this may not be an OP1 updater")
    print()

    print("CodeView PDB path (ASCII):")
    for t in pdb[:4]:
        d = t.decode("ascii", "replace")
        print(f"  {d}")
        # §0.1.1: 1.10's PDB says EL1. Report it; do not fail on it, because
        # every other identifier in 1.10 says OP1 and the accepted reading is a
        # copied project directory. It still has to be seen by a person.
        if "EL1" in d:
            notes.append("PDB path names a DIFFERENT product (EL1). 1.10 does this "
                         "too -- see notes/updater-protocol.md §0.1.1 -- but check "
                         "the other identifiers agree before accepting it.")
    if not pdb:
        notes.append("no PDB path; nothing to cross-check the build provenance with")
    print()

    # ---- 2. The resource-set fingerprint. §0.1.1 requires this. -----------
    try:
        # [(id, lang, offset, size, sha256)], id order.
        entries = fwfile.fwfiles(path)
    except Exception as e:                                    # noqa: BLE001
        print(f"REFUSED: could not read FWFILE resources: {e}")
        return 1

    print(f"FWFILE resource set ({len(entries)} entries):")
    by_id = {}
    for rid, _lang, off, size, digest in entries:
        by_id[rid] = (off, size, digest)
        mark = "  <-- the one that gets flashed" if rid == FLASHED_ID else ""
        known = ""
        if rid in KNOWN_110:
            known = "  same as 1.10" if digest == KNOWN_110[rid] else "  CHANGED vs 1.10"
        print(f"  id {rid:<4} {size:>7} bytes  {digest}{known}{mark}")
        if size != EXPECTED_SIZE:
            notes.append(f"id {rid} is {size} bytes, not the usual {EXPECTED_SIZE}")
    print()

    if FLASHED_ID not in by_id:
        print(f"REFUSED: there is no FWFILE/{FLASHED_ID} in this file.")
        return 1

    # ---- 3. The image itself. ---------------------------------------------
    off, size, img_sha = by_id[FLASHED_ID]
    img = raw[off:off + size]
    print(f"image      FWFILE/{FLASHED_ID}")
    print(f"size       {len(img)} bytes")
    if len(img) != EXPECTED_SIZE:
        problems.append(f"image is {len(img)} bytes, not {EXPECTED_SIZE}; "
                        "egg-flash refuses anything but 65 whole blocks")
    elif len(img) % BLOCK:
        problems.append("image is not a whole number of 1024-byte blocks")
    else:
        print(f"blocks     {len(img)//BLOCK} of {BLOCK}  "
              f"-> device indices 0x34..0x{0x34 + len(img)//BLOCK - 1:02x}")
    # FUN_00403580: plain 32-bit sum of every byte. This is a value we WRITE
    # (into the A0 03 start frame), so it is printed for the reviewer.
    print(f"checksum   0x{sum(img) & 0xFFFFFFFF:08x}  (32-bit sum of every byte)")
    print(f"sha256     {img_sha}")
    if img_sha == PINNED_IMAGE_SHA:
        print("           == the image egg-flash already pins. This is 1.10's; "
              "nothing to add.")
    print()

    # ---- 4. The verdict. ---------------------------------------------------
    for n in notes:
        print(f"NOTE     {n}")
    for p in problems:
        print(f"PROBLEM  {p}")
    print()

    if problems:
        print("DO NOT ADD THIS IMAGE. Resolve the problems above first.")
        return 1

    if img_sha == PINNED_IMAGE_SHA:
        print("Already pinned. No change needed.")
        return 0

    print("If the identity above is genuinely an OP1 8k v2 updater, add this to")
    print("Sources/EGGFlashCore/include/egg/Firmware.h and rebuild:")
    print()
    print(f'    // FWFILE/{FLASHED_ID} from "{os.path.basename(path)}"')
    print(f'    //   file sha256 {sha(raw)}')
    print(f'    inline constexpr const char* kExpectedSha256 =')
    print(f'        "{img_sha}";')
    print()
    print("Then re-run Tests/test_fwfile_set.py, which pins the whole resource")
    print("set and will fail loudly if any OTHER blob changed at the same time.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
