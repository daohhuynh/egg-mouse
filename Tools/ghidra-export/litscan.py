#!/usr/bin/env python3
"""litscan.py <tag> [<literal> ...]  -- count occurrences of integer literals
in a PE's .text, as 2-byte and 4-byte little-endian encodings.

Records structure only. It takes the literals to look for from the command
line and holds no opinion about what any of them mean; it does not rank,
score, or classify. See CLAUDE.md 7.1 for why that matters.
"""
import struct, sys, os

TAGS = {
 "fw110": "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe",
 "fw107": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe",
 "fw106": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe",
 "fw104": "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe",
 "cfg107": "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe",
 "cfg104": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe",
 "cfg101": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe",
 "cfg100": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe",
}

def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        nm = d[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsz, va, rsz, ro = struct.unpack_from('<IIII', d, o + 8)
        out.append((nm, base + va, vsz, ro, rsz))
    return base, out

def main(tag, lits):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
    d = open(os.path.join(root, TAGS[tag]), 'rb').read()
    base, secs = sections(d)
    t = [s for s in secs if s[0] == '.text'][0]
    blob, tva = d[t[3]:t[3] + t[4]], t[1]
    print(f"# {tag}: .text VA {tva:#x}..{tva+t[4]:#x} ({t[4]} bytes)")
    for lit in lits:
        for width in (2, 4):
            if lit >= (1 << (8 * width)):
                continue
            pat = lit.to_bytes(width, 'little')
            hits, st = [], 0
            while True:
                i = blob.find(pat, st)
                if i < 0:
                    break
                hits.append(tva + i)
                st = i + 1
            print(f"{lit:#x}\tw{width}\tn={len(hits)}\t"
                  + " ".join(hex(h) for h in hits[:12])
                  + (" ..." if len(hits) > 12 else ""))

if __name__ == '__main__':
    main(sys.argv[1], [int(x, 0) for x in sys.argv[2:]])
