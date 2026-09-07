#!/usr/bin/env python3
"""ingest.py -- read an Endgame firmware updater nobody has read, and propose a
manifest row for it.

THE PROBLEM THIS SOLVES, stated so the safety argument is checkable.

engineering-rules.md §1.4 says the firmware resource "must be a constant in our code --
never a variable, never selected at runtime, never chosen from a list. Other
resources in the binary may belong to other products." Updater 1.10 carries SIX
FWFILE resources of identical length, and five of them would be accepted by the
device: right size, valid per-block checksums, and they read back exactly as
written. §2 assumes the device validates nothing, so a wrong-but-well-formed
image is a brick with no downstream check.

Supporting future firmware versions therefore cannot mean "let the user pick a
resource". It means: a human ingests a new .exe ONCE, this tool reports
everything mechanically recoverable from it, and the result is committed as a
source change. At run time the id still comes from a compile-time table, keyed
by the SHA-256 of the exact .exe. An updater not in that table cannot be
flashed at all.

So §1.4 survives intact. What changes is only that the table has more than one
row, and that adding a row is reviewable work rather than a guess.

WHAT IS RECOVERED MECHANICALLY
  - SHA-256 of the .exe                    the manifest key
  - the FWFILE id the vendor's OWN code loads  (Tools/pe/resource_id.py: the
    FindResourceW call site, its operands read from raw bytes)
  - every FWFILE resource, with size and SHA-256
  - the selected image's size, SHA-256 and 32-bit whole-image checksum
  - VS_FIXEDFILEINFO's version quadruple

WHAT IS NOT, AND WHY NOT
  - THE MARKETING LABEL. The four updaters we hold report 1.0.4.0, 1.0.6.0,
    1.0.7.0 and 1.1.0.0 for what Endgame calls 1.04, 1.06, 1.07 and 1.10. A rule
    fitting all four exists ("%d.%d%d" % major, minor, build) but it is fitted to
    four points and nothing constrains the next release to obey it. The label is
    cosmetic; the hash is what protects the mouse. A person types the label.
  - WHETHER THE IMAGE IS FOR THIS MOUSE. Nothing here can tell an OP1 8k v2
    image from another product's. That is what the id derivation is for, and it
    is why an updater whose id cannot be recovered is REFUSED rather than
    guessed at.

    python3 Tools/pe/ingest.py <updater.exe> [--label 1.11]
"""
import hashlib
import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

_spec = importlib.util.spec_from_file_location(
    "resource_id", os.path.join(HERE, "resource_id.py"))
rid = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rid)

_spec2 = importlib.util.spec_from_file_location(
    "fwfile", os.path.join(HERE, "fwfile.py"))
fwfile = importlib.util.module_from_spec(_spec2)
_spec2.loader.exec_module(fwfile)

BLOCK = 1024
EXPECTED_SIZE = 66560          # 65 blocks; see egg/Firmware.h
EXPECTED_BLOCKS = 65


# The §8.5a lineage fingerprint (notes/updater-protocol.md).
#
# WHY THIS IS HERE AND NOT ONLY IN THE NOTES. For an updater already in the
# manifest, the pinned whole-image SHA-256 settles everything. For a BRAND-NEW
# one there is no row yet, so the pinned hash cannot help by construction --
# and that is precisely the moment §1.4's "other resources in the binary may
# belong to other products" is live. 1.10 carries SIX FWFILE resources of
# identical length, five of which the device would accept: right size, valid
# per-block checksums, and they read back exactly as written. §2 assumes the
# device validates nothing, so a wrong-but-well-formed image is a brick with
# nothing downstream to catch it.
#
# What the fingerprint is: chunks 29-63 of every FWFILE blob are one 1024-byte
# chunk repeated 35 times, and that chunk's value is CONSTANT PER RESOURCE NAME
# across all four updaters we hold -- 21 blobs, six distinct values, zero
# collisions between names. So it identifies the resource NAME independently of
# the id the code asks for, which is the one thing a new .exe could change
# without announcing it.
#
# What it is NOT: an argument about which physical mouse the image is for
# (that is §8.2), and not proof against a vendor who deliberately re-fills the
# run. It is a mechanical check that the blob the new .exe's own code selects
# sits in the same lineage as the image that has actually been flashed onto
# this mouse. That is worth having and it costs nothing.
FILLER_FIRST = 29
FILLER_LAST = 63
FILLER_SHA_140 = ("cefe77fb6c23f0d4cb19fc709232bed0"
                  "035ba41cc1413d46688172d3e6c6ffda")


def lineage(b):
    """Return (verdict, detail) for §8.5a's filler-chunk fingerprint.

    verdict is one of "match", "mismatch", "unusable". "unusable" means the
    image is not the shape the fingerprint is defined over -- it is reported
    separately from a mismatch because the two mean different things and the
    size problem is already raised elsewhere.
    """
    if len(b) != EXPECTED_SIZE:
        return "unusable", ("image is %d bytes, not %d, so the 35-chunk filler "
                            "run is not defined over it" % (len(b),
                                                            EXPECTED_SIZE))
    chunks = [b[i * BLOCK:(i + 1) * BLOCK] for i in range(len(b) // BLOCK)]
    run = chunks[FILLER_FIRST:FILLER_LAST + 1]
    odd = [FILLER_FIRST + i for i, c in enumerate(run) if c != run[0]]
    if odd:
        return "mismatch", ("chunks %s differ from chunk %d; in all 21 blobs of "
                            "all four updaters this run is one chunk repeated "
                            "%d times"
                            % (", ".join(str(x) for x in odd), FILLER_FIRST,
                               len(run)))
    got = hashlib.sha256(run[0]).hexdigest()
    if got != FILLER_SHA_140:
        return "mismatch", ("filler chunk is %s; FWFILE 140 has been %s in "
                            "1.04, 1.06, 1.07 and 1.10. A different value means "
                            "this blob is a different resource NAME, whatever "
                            "id the code asked for -- or the vendor re-filled "
                            "the run, which nobody has ever seen them do."
                            % (got[:16] + "...", FILLER_SHA_140[:16] + "..."))
    return "match", "filler chunk %s..., the 140 lineage" % FILLER_SHA_140[:16]


def whole_image_checksum(b):
    """The value A0 03 declares. [D] updater 1.10 FUN_00403580: the four
    accumulators are a 4-way unroll of one sum, so it is the plain 32-bit sum
    of every byte. Reimplemented here rather than shelled out to, so ingest
    needs no build."""
    return sum(b) & 0xFFFFFFFF


def file_version(d):
    m = re.search(re.escape(struct.pack("<I", 0xFEEF04BD)), d)
    if not m:
        return None
    o = m.start()
    a, b, c, e = struct.unpack_from("<HHHH", d, o + 8)
    return (b, a, e, c)          # MS hi, MS lo, LS hi, LS lo


def report(path, label):
    # A TYPO'D PATH IS A FINDING, NOT A TRACEBACK. `UpdatesView` shows this
    # tool's output raw, so an unhandled OSError put a Python stack trace in
    # the GUI's pane where a sentence belonged, and the exit status was 1 for
    # a reason nobody could read.
    out = {"path": path, "ok": False, "problems": [], "resources": []}
    try:
        with open(path, "rb") as f:
            d = f.read()
    except OSError as e:
        out["problems"].append("cannot read %s: %s" % (path, e.strerror or e))
        out["exe_sha256"] = None
        out["exe_size"] = 0
        out["version"] = None
        out["id"] = None
        out["sites"] = []
        return out
    out["exe_sha256"] = hashlib.sha256(d).hexdigest()
    out["exe_size"] = len(d)
    out["version"] = file_version(d)

    r = rid.derive(path)
    out["sites"] = r["sites"]
    out["id"] = r["id"]
    if r["id"] is None:
        out["problems"] += r["why"]

    try:
        blobs = fwfile.fwfiles(path)
    except Exception as e:                      # noqa: BLE001 -- report, not crash
        out["problems"].append("cannot enumerate FWFILE resources: %s" % e)
        blobs = []
    # fwfiles() yields (id, lang, file_offset, size, sha256); the bytes are
    # sliced here so the checksum is computed from the same span it hashed.
    out["resources"] = [
        {"id": name, "size": size, "sha256": sha,
         "bytes": d[off:off + size]}
        for (name, lang, off, size, sha) in blobs]

    if r["id"] is not None:
        sel = [x for x in out["resources"] if x["id"] == r["id"]]
        if not sel:
            out["problems"].append(
                "the code loads FWFILE id %d but no such resource is present -- "
                "the id derivation and the resource table disagree, and that "
                "disagreement is exactly what must never be resolved by "
                "guessing" % r["id"])
        else:
            img = sel[0]
            out["image"] = img
            if img["size"] != EXPECTED_SIZE:
                out["problems"].append(
                    "image is %d bytes; every image this project has seen is %d "
                    "(%d blocks of %d). A different size means the block "
                    "arithmetic in FlashPlan was derived against something else."
                    % (img["size"], EXPECTED_SIZE, EXPECTED_BLOCKS, BLOCK))
            elif img["size"] % BLOCK:
                out["problems"].append("image is not a whole number of blocks")
            else:
                out["checksum"] = whole_image_checksum(img["bytes"])
            verdict, detail = lineage(img["bytes"])
            out["lineage"] = (verdict, detail)
            if verdict == "mismatch":
                out["problems"].append(
                    "the selected image FAILS §8.5a's lineage fingerprint: %s "
                    "This is the only mechanical wrong-image check that works "
                    "on an updater nobody has read, so it refuses rather than "
                    "warns. If Endgame really has changed the filler, that is a "
                    "finding to write up in notes/updater-protocol.md §8.5a "
                    "before any row is added -- not something to wave through."
                    % detail)
            dupes = [x["id"] for x in out["resources"]
                     if x["id"] != img["id"] and x["sha256"] == img["sha256"]]
            if dupes:
                out["problems"].append(
                    "resources %s are byte-identical to the selected image; "
                    "the id is still what the code asks for, but note it"
                    % ", ".join(str(x) for x in dupes))

    out["label"] = label
    if label is None:
        out["problems"].append(
            "no --label given. The version resource says %s, which this tool "
            "will not turn into a marketing label on its own -- see the "
            "docstring." % (".".join(str(x) for x in out["version"])
                            if out["version"] else "nothing"))
    out["ok"] = not out["problems"] and out.get("image") is not None
    return out


def emit_row(r):
    """The row a person pastes into FirmwareManifest.cpp.

    IT MUST COMPILE AS WRITTEN, and until 2026-09-06 it did not: `provenOnDevice`
    was missing, so the provenance string landed in the bool's slot and clang
    refused it as a narrowing conversion. Failing loudly is the good direction,
    but this tool told the user to "paste it, rebuild" and the build then broke
    with nothing pointing at the row.

    It went unnoticed because the DERIVATION is tested to the letter by
    Tests/test_manifest.py while the ARTEFACT a human copies was not tested at
    all. Tests/test_ingest_row.py now compiles this output against the real
    struct, and needs no .exe to do it.

    `provenOnDevice` is emitted as `false` and this tool cannot make it anything
    else. engineering-rules.md 5: a release is proven when it has been flashed onto the one
    mouse and verified, which is a fact about the past that no file can
    establish about itself.
    """
    return """    // %s
    // Ingested %s by Tools/pe/ingest.py. Resource id recovered from the
    // FindResourceW call site, NOT from the resource table.
    //
    // provenOnDevice is false, and ingest.py cannot emit anything else: this
    // image has not been flashed onto the mouse and verified (engineering-rules.md 5).
    {"%s",
     {%d, %d, %d, %d},
     "%s",
     %d,
     %d,
     "%s",
     0x%08xu,
     false,
     "%s"},""" % (
        os.path.basename(r["path"]),
        _today(),
        r["label"],
        r["version"][0], r["version"][1], r["version"][2], r["version"][3],
        r["exe_sha256"],
        r["id"],
        r["image"]["size"],
        r["image"]["sha256"],
        r["checksum"],
        "Tools/pe/ingest.py; %d FWFILE resources present" % len(r["resources"]))


def _today():
    import datetime
    return datetime.date.today().isoformat()


def main(argv):
    label, paths = None, []
    i = 1
    while i < len(argv):
        if argv[i] == "--label":
            i += 1
            label = argv[i] if i < len(argv) else None
        else:
            paths.append(argv[i])
        i += 1
    if not paths:
        print(__doc__)
        return 2

    bad = 0
    for p in paths:
        r = report(p, label)
        print("=" * 74)
        print(os.path.basename(p))
        print("=" * 74)
        if r["exe_sha256"] is None:
            # Unreadable. Everything below would print "None" and "0 bytes",
            # which reads like findings about the file rather than the absence
            # of one.
            print("\n  NOT READY TO INGEST:")
            for q in r["problems"]:
                print("    - %s" % q)
            print()
            bad = 1
            continue
        print("  exe SHA-256   %s" % r["exe_sha256"])
        print("  exe size      %d bytes" % r["exe_size"])
        print("  version rsrc  %s" % (".".join(str(x) for x in r["version"])
                                      if r["version"] else "ABSENT"))
        print("  FWFILE id     %s" %
              ("%d (0x%x), from the FindResourceW call site" % (r["id"], r["id"])
               if r["id"] is not None else "NOT RECOVERED"))
        print("  resources     %d" % len(r["resources"]))
        for x in r["resources"]:
            mark = "  <-- loaded by the vendor's code" if x["id"] == r["id"] else ""
            print("     id=%-4d size=%-7d %s%s"
                  % (x["id"], x["size"], x["sha256"], mark))
        if "checksum" in r:
            print("  A0 03 whole-image checksum  0x%08x" % r["checksum"])
        if "lineage" in r:
            verdict, detail = r["lineage"]
            print("  §8.5a lineage  %-9s %s" % (verdict.upper(), detail))
        if r["problems"]:
            print("\n  NOT READY TO INGEST:")
            for q in r["problems"]:
                print("    - %s" % q)
            bad = 1
        if r["ok"]:
            print("\n  Proposed row for Sources/EGGFlashCore/src/FirmwareManifest.cpp:")
            print(emit_row(r))
            print("\n  Adding a row is a SOURCE CHANGE on purpose: it is")
            print("  reviewable, and the")
            print("  resource id still reaches the flasher as a compile-time")
            print("  constant selected by the .exe's own hash (§1.4).")
            print("")
            print("  THE FULL CHECKLIST, because the row alone is not enough.")
            print("  Every step below announces itself when you forget it --")
            print("  step 2's silent half was removed on 2026-09-06:")
            print("    1. paste the row into")
            print("       Sources/EGGFlashCore/src/FirmwareManifest.cpp")
            print("    2. Tests/test_manifest.py: add this .exe to its EXE path")
            print("       map. It is the one edit no tool can make for you --")
            print("       the vendor's filenames are inconsistent, so the path")
            print("       cannot be derived from the label. The suite names the")
            print("       missing label if you forget. (The expected row COUNT")
            print("       used to need bumping here too; it is derived now.)")
            print("    3. leave provenOnDevice false until this image has actually")
            print("       been flashed onto the mouse and verified. Recording that")
            print("       later means moving the row to index 0 and updating")
            print("       test_manifest.py's proven-row check -- engineering-rules.md §5.")
            print("    4. rebuild, then run `ctest`.")
        print()
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
