#!/usr/bin/env python3
"""List and extract the FWFILE resources of an Endgame firmware updater.

WHY THIS IS CHECKED IN. engineering-rules.md §2: "nothing downstream of us catches a
wrong-but-well-formed image", and §6.1 ranks the FWFILE blobs highest because
they are the only place a wrong-image guard could come from. Every updater
carries several images of exactly the same length, and five of the six in 1.10
would be accepted by the device: right size, valid per-block checksums, and they
read back exactly as written. The only thing separating them is which id.

The answer, from two independent static derivations plus the capture: id 140,
chosen by a `push $0x8c` immediate at 0x0040320c feeding FindResourceW. It is a
compile-time constant in the vendor's binary too -- no table, no runtime input --
and the other five ids are unreachable from any code path in the executable.

ONE OPERATIONAL WARNING, and it is the reason this file pins the whole SET
rather than just id 140: updater 1.10 has byte-identical `.text` to 1.06 and
1.07, yet 1.10 added a sixth FWFILE (143) that the earlier releases do not have.
A guard keyed on code identity would have accepted 1.10 without noticing a new
firmware blob had appeared. Pin the resources and their hashes, never the code.

    python3 Tools/pe/fwfile.py <updater.exe> [...]
    python3 Tools/pe/fwfile.py --extract 140 <updater.exe> <out.bin>
"""
import hashlib
import struct
import sys


def _sections(b):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        name = b[o:o + 8].rstrip(b"\0").decode("latin1")
        vsz, va, rsz, ra = struct.unpack_from("<IIII", b, o + 8)
        out.append((name, va, ra))
    return out


def resources(path):
    """[(type, name, lang, file_offset, size)] for every resource data entry."""
    with open(path, "rb") as fh:
        b = fh.read()
    rs = [s for s in _sections(b) if s[0] == ".rsrc"]
    if not rs:
        return b, []
    _, RVA, RAW = rs[0]

    def wname(o):
        n = struct.unpack_from("<H", b, o)[0]
        return b[o + 2:o + 2 + n * 2].decode("utf-16-le")

    out = []

    def walk(o, path_):
        nnamed, nid = struct.unpack_from("<HH", b, o + 12)
        for i in range(nnamed + nid):
            nid_, off = struct.unpack_from("<II", b, o + 16 + 8 * i)
            key = wname(RAW + (nid_ & 0x7FFFFFFF)) if nid_ & 0x80000000 else nid_
            if off & 0x80000000:
                walk(RAW + (off & 0x7FFFFFFF), path_ + [key])
            else:
                drva, dsz = struct.unpack_from("<II", b, RAW + off)[:2]
                out.append(tuple(path_ + [key]) + (RAW + (drva - RVA), dsz))

    walk(RAW, [])
    return b, out


def fwfiles(path):
    """[(id, lang, offset, size, sha256)] for the FWFILE resources, id order."""
    b, res = resources(path)
    out = []
    for r in res:
        if str(r[0]) != "FWFILE":
            continue
        _, name, lang, off, size = r
        out.append((name, lang, off, size,
                    hashlib.sha256(b[off:off + size]).hexdigest()))
    return sorted(out)


def main():
    args = sys.argv[1:]
    if args[:1] == ["--extract"]:
        if len(args) != 4:
            sys.exit(__doc__)
        want = int(args[1])
        with open(args[2], "rb") as fh:
            b = fh.read()
        for name, lang, off, size, h in fwfiles(args[2]):
            if name == want:
                with open(args[3], "wb") as fh:
                    fh.write(b[off:off + size])
                print("%s  %d bytes  sha256 %s" % (args[3], size, h))
                return 0
        print("no FWFILE with id %d in %s" % (want, args[2]))
        return 1
    if not args:
        sys.exit(__doc__)
    for p in args:
        fw = fwfiles(p)
        print("### %s  (%d FWFILE)" % (p.split("/")[-1], len(fw)))
        for name, lang, off, size, h in fw:
            mark = "  <-- the one that is flashed" if name == 140 else ""
            print("   id=%-4d lang=%-5s off=0x%08x size=%-6d %s%s"
                  % (name, lang, off, size, h, mark))
    return 0


if __name__ == "__main__":
    sys.exit(main())
