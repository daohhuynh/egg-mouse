#!/usr/bin/env python3
"""ingest.py -- read an Endgame firmware updater nobody has read, and propose a
manifest row for it.

THE PROBLEM THIS SOLVES, stated so the safety argument is checkable.

CLAUDE.md §1.4 says the firmware resource "must be a constant in our code --
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
    with open(path, "rb") as f:
        d = f.read()
    out = {"path": path, "ok": False, "problems": []}
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
    return """    // %s
    // Ingested %s by Tools/pe/ingest.py. Resource id recovered from the
    // FindResourceW call site, NOT from the resource table.
    {"%s",
     {%d, %d, %d, %d},
     "%s",
     %d,
     %d,
     "%s",
     0x%08xu,
     "%s"},""" % (
        os.path.basename(r["path"]),
        "<date>",
        r["label"],
        r["version"][0], r["version"][1], r["version"][2], r["version"][3],
        r["exe_sha256"],
        r["id"],
        r["image"]["size"],
        r["image"]["sha256"],
        r["checksum"],
        "Tools/pe/ingest.py; %d FWFILE resources present" % len(r["resources"]))


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
        if r["problems"]:
            print("\n  NOT READY TO INGEST:")
            for q in r["problems"]:
                print("    - %s" % q)
            bad = 1
        if r["ok"]:
            print("\n  Proposed row for Sources/EGGFlashCore/src/FirmwareManifest.cpp:")
            print(emit_row(r))
            print("\n  Paste it, set the date, rebuild, and run `ctest`. Adding a")
            print("  row is a SOURCE CHANGE on purpose: it is reviewable, and the")
            print("  resource id still reaches the flasher as a compile-time")
            print("  constant selected by the .exe's own hash (§1.4).")
        print()
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
