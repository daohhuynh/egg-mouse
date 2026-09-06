#!/usr/bin/env python3
"""Score `egg-flash read-firmware` against notes/prediction-read-firmware.md.

WRITTEN AND COMMITTED BEFORE THE RUN, on purpose. The project's scoring
discipline (prediction-factory-reset.md 21/21, prediction-restore.md 21/21,
prediction-postwindows.md 7/7) only means something if the analysis is fixed
before the data exists. A scorer written afterwards can be tuned, however
honestly, to whatever came back.

    python3 Tools/score-read-firmware.py backup.bin [--settings-before F --settings-after G]

It reads a file. It NEVER touches the device and never writes to the repo.
Exit status 0 if every testable prediction HIT, 1 otherwise.
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Tools", "pe"))
import fwfile  # noqa: E402

EXE = os.path.join(ROOT, "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe")
FLASHED_ID = 140
BLOCK = 1024
FIRST, LAST = 0x34, 0x74
COUNT = LAST - FIRST + 1
SIZE = COUNT * BLOCK

# notes/prediction-read-firmware.md prediction 6.
PINNED = "8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0"

hits = []


def score(n, desc, ok, detail=""):
    tag = "HIT " if ok else "MISS"
    hits.append(bool(ok))
    print(f"  {tag}  P{n}: {desc}")
    if detail:
        for line in detail.splitlines():
            print(f"           {line}")


def block_sum16(b):
    """[D] 0x401a00..0x401a34 -- 16-bit sum, truncation is the point."""
    return sum(b) & 0xFFFF


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {}
    argv = sys.argv[1:]
    for i, a in enumerate(argv):
        if a in ("--settings-before", "--settings-after") and i + 1 < len(argv):
            flags[a] = argv[i + 1]
    if not args:
        print(__doc__)
        return 2
    path = args[0]

    if not os.path.exists(path):
        print(f"no such file: {path}")
        print("Did the read abort? If block 0x34 came back status 0x02, that is")
        print("hypothesis B and prediction 5 is REFUTED -- see section 5 of")
        print("notes/prediction-read-firmware.md. It is a finding, not a bug,")
        print("and it does NOT license sending A0 03 to open a session.")
        return 1

    raw = open(path, "rb").read()
    print(f"file       {path}")
    print(f"size       {len(raw)} bytes")
    got_sha = hashlib.sha256(raw).hexdigest()
    print(f"sha256     {got_sha}")
    print()
    print("Scoring notes/prediction-read-firmware.md")
    print()

    # ---- P8: the file is the right size and self-consistent. --------------
    score(8, f"the saved file is exactly {SIZE} bytes",
          len(raw) == SIZE, f"got {len(raw)}")
    if len(raw) != SIZE:
        print("\nNothing further can be scored against a wrong-sized file.")
        return 1

    # ---- P6: the load-bearing one. ----------------------------------------
    ref = None
    if os.path.exists(EXE):
        blob = open(EXE, "rb").read()
        for rid, _lang, off, size, _d in fwfile.fwfiles(EXE):
            if rid == FLASHED_ID:
                ref = blob[off:off + size]
        if ref is not None and hashlib.sha256(ref).hexdigest() != PINNED:
            print("  WARN  FWFILE/140 does not match the pinned sha. Stop and")
            print("        work out why before trusting anything below.")
            ref = None

    if ref is None:
        score(6, "read-back == FWFILE/140 of updater 1.10", False,
              "could not obtain the reference image; UNTESTED, not a miss")
        hits.pop()
    else:
        same = raw == ref
        detail = ""
        if not same:
            diff_blocks = [
                i for i in range(COUNT)
                if raw[i * BLOCK:(i + 1) * BLOCK] != ref[i * BLOCK:(i + 1) * BLOCK]
            ]
            nbytes = sum(1 for a, b in zip(raw, ref) if a != b)
            detail = (f"{len(diff_blocks)} of {COUNT} blocks differ, "
                      f"{nbytes} of {SIZE} bytes\n")
            detail += ("device block indices: " +
                       ", ".join(f"0x{FIRST + i:02x}" for i in diff_blocks[:12]) +
                       (" ..." if len(diff_blocks) > 12 else "") + "\n")
            # Section 5 of the prediction file: name the shape, do not improvise.
            if len(diff_blocks) <= 2:
                detail += ("READING: the firmware writes to its own flash at "
                           "runtime and those blocks hold it. Benign and useful "
                           "-- it localises where settings/calibration live. "
                           "RE-READ to confirm the same blocks differ; a moving "
                           "difference is a different finding.")
            elif len(diff_blocks) == COUNT:
                shifted16 = raw[16:] == ref[:-16]
                shiftedblk = raw[BLOCK:] == ref[:-BLOCK]
                if shifted16:
                    detail += ("READING: the data is the reference shifted by 16 "
                               "bytes. We are reading from the frame start, not "
                               "+0x10. DO NOT FLASH.")
                elif shiftedblk:
                    detail += ("READING: shifted by one whole block. The index "
                               "mapping is off by one. DO NOT FLASH.")
                elif len(set(raw)) <= 2:
                    detail += (f"READING: the region is uniform ({sorted(set(raw))}). "
                               "The bootloader is serving an erased region, not the "
                               "application. This changes the meaning of every "
                               "per-block verify in the flash. STOP AND RETHINK.")
                else:
                    detail += ("READING: every block differs and it is not a simple "
                               "shift. Payload offset or index mapping is wrong. "
                               "DO NOT FLASH.")
            else:
                detail += ("READING: not one of the shapes anticipated in section 5. "
                           "Record it and do not flash until it is understood.")
        score(6, "read-back == FWFILE/140 of updater 1.10", same, detail)

    # ---- P3: the device's own per-block checksum, checkable from the file --
    # Only meaningful if the run logged them; the file alone cannot carry
    # resp[6..7]. State that rather than pretending to score it.
    print("  n/a   P3: resp[6..7] == 16-bit block sum -- needs the run log, not")
    print("           the file. Check with -v output if it was captured.")
    print(f"           (first block's expected sum: "
          f"0x{block_sum16(raw[0:BLOCK]):04x}, "
          f"last: 0x{block_sum16(raw[-BLOCK:]):04x})")

    # ---- P9: settings untouched -------------------------------------------
    b, a = flags.get("--settings-before"), flags.get("--settings-after")
    if b and a and os.path.exists(b) and os.path.exists(a):
        bb, aa = open(b, "rb").read(), open(a, "rb").read()
        n = sum(1 for x, y in zip(bb, aa) if x != y) + abs(len(bb) - len(aa))
        score(9, "settings untouched by the read", n == 0,
              f"{n} differing byte(s) between {b} and {a}")
    else:
        print("  n/a   P9: settings untouched -- rerun with")
        print("           --settings-before ~/.egg-mouse-known-good.bin")
        print("           --settings-after  after-stage3.bin")

    print()
    testable = len(hits)
    good = sum(hits)
    print(f"{good}/{testable} testable predictions HIT")
    if good == testable:
        print("Append this output to notes/prediction-scores.md BEFORE drawing")
        print("any conclusion from the run (CLAUDE.md §7).")
    return 0 if good == testable else 1


if __name__ == "__main__":
    sys.exit(main())
