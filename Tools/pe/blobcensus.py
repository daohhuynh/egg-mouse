#!/usr/bin/env python3
"""blobcensus.py -- the block census updater-protocol.md §8 rests on, as a
command anyone can rerun.

§8 says three things about the FWFILE blobs, and the flasher's wrong-image guard
(§8.5a) rests on all three:

  1. Every blob is 65 blocks of 1024 bytes with 31 distinct values, the same
     35-long run of one repeated block at the same indices in every one.
  2. The transform is deterministic and position-independent -- one plaintext
     block gives identical ciphertext at all 35 positions, so there is no IV, no
     chaining, no dependence on index or address.
  3. No 1024-byte block is shared between any two of the six resources, while
     the same resource name across releases shares its filler exactly.

None of those was reproducible. They were computed once, written down as prose,
and cited to nothing -- which is how a number survives a context compaction
looking like established fact (engineering-rules.md §6). This recomputes all of it from the
.exe files in one pass and prints what §8 claims, so the claim and the check are
the same command.

  blobcensus.py <exe> [<exe> ...]     census + cross-resource sharing
  blobcensus.py --check               every .exe in the repo, and assert §8

--check exits non-zero if any of the three claims fails, so it can be a test.
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fwfile import fwfiles  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BLOCK = 1024

UPDATERS = [
    "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe",
    "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe",
    "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe",
    "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe",
]


def blocks(data):
    return [data[i:i + BLOCK] for i in range(0, len(data), BLOCK)]


def census(data):
    bs = blocks(data)
    counts = {}
    for i, b in enumerate(bs):
        counts.setdefault(b, []).append(i)
    runs = sorted((len(v), v[0], v[-1]) for v in counts.values() if len(v) > 1)
    return {
        "blocks": len(bs),
        "distinct": len(counts),
        "runs": runs,
        "hashes": [hashlib.sha256(b).hexdigest() for b in bs],
        "counts": counts,
    }


def read(path):
    out = {}
    with open(path, "rb") as fh:
        raw = fh.read()
    for name, _lang, off, size, _h in fwfiles(path):
        out[name] = raw[off:off + size]
    return out


def main(argv):
    check = "--check" in argv
    paths = [a for a in argv[1:] if not a.startswith("--")]
    if check or not paths:
        paths = [os.path.join(ROOT, p) for p in UPDATERS]

    per_file = {}
    for p in paths:
        if not os.path.exists(p):
            print("missing: %s" % p)
            return 2
        per_file[os.path.basename(p)] = read(p)

    problems = []

    # ---- 1 + 2. per-blob census ------------------------------------------
    print("=== per-resource census (65 x 1024) ===")
    shape = {}
    for fname, res in sorted(per_file.items()):
        for name, data in sorted(res.items()):
            c = census(data)
            run = max(c["runs"]) if c["runs"] else (0, 0, 0)
            print("  %-46s id=%-4d blocks=%-3d distinct=%-3d "
                  "longest run=%d at [%d..%d]"
                  % (fname[:46], name, c["blocks"], c["distinct"],
                     run[0], run[1], run[2]))
            shape.setdefault((name,), []).append((fname, c["blocks"],
                                                  c["distinct"], run))
            if c["blocks"] != 65:
                problems.append("%s id=%d has %d blocks, not 65"
                                % (fname, name, c["blocks"]))
            if c["distinct"] != 31:
                problems.append("%s id=%d has %d distinct blocks, not 31"
                                % (fname, name, c["distinct"]))
            if run[:1] != (35,):
                problems.append("%s id=%d longest repeated run is %d, not 35"
                                % (fname, name, run[0]))
            # Position independence: the repeated block must occupy a
            # CONTIGUOUS span, and identically in every resource. A transform
            # with chaining could not repeat one ciphertext block at all.
            idxs = [v for v in c["counts"].values() if len(v) == run[0]]
            if idxs and idxs[0] != list(range(idxs[0][0], idxs[0][0] + run[0])):
                problems.append("%s id=%d: the 35-run is not contiguous"
                                % (fname, name))

    same_span = {(r[3][1], r[3][2]) for v in shape.values() for r in v}
    print("\n  the 35-run occupies the same index span everywhere: %s  %r"
          % ("YES" if len(same_span) == 1 else "NO", sorted(same_span)))
    if len(same_span) != 1:
        problems.append("the 35-run sits at different indices in different "
                        "resources: %r" % sorted(same_span))

    # ---- 3. sharing between resource NAMES, and within one name ----------
    print("\n=== block sharing ===")
    by_name = {}
    for fname, res in per_file.items():
        for name, data in res.items():
            by_name.setdefault(name, {})[fname] = set(census(data)["hashes"])

    names = sorted(by_name)
    cross = 0
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            for fa, sa in by_name[a].items():
                for fb, sb in by_name[b].items():
                    if sa & sb:
                        cross += 1
                        problems.append(
                            "id %d (%s) and id %d (%s) share %d block(s)"
                            % (a, fa, b, fb, len(sa & sb)))
    print("  distinct resource ids that share ANY 1024-byte block: %d "
          "(§8 says 0)" % cross)

    for name in names:
        files = sorted(by_name[name])
        if len(files) < 2:
            continue
        common = set.intersection(*(by_name[name][f] for f in files))
        print("  id %-4d across %d releases: %d block(s) in common"
              % (name, len(files), len(common)))
        if not common:
            problems.append("id %d shares NO block across releases, but §8 "
                            "says its filler is identical" % name)

    if problems:
        print("\n§8 DOES NOT HOLD:")
        for p in problems:
            print("  - " + p)
        return 1
    print("\n§8 holds: 65 blocks, 31 distinct, one 35-long contiguous run at "
          "the same indices in every resource; no block shared between resource "
          "ids; every id shares blocks across its own releases.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
