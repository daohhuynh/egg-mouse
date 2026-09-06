#!/usr/bin/env python3
"""
auditclaims.py -- mechanically check every [D] citation in notes/ against the
bytes of the binary it names.

CLAUDE.md 1.2 says a [D] claim is "traced to a specific location in an .exe.
Cite the address." That is a rule nothing has ever enforced. Nearly five hundred
[D] markers accumulated across nineteen note files, written over many sessions,
and no pass has ever confirmed that the addresses in them are real -- that they
land inside the named binary, that they land on an instruction boundary rather
than mid-instruction, or that they exist at all.

WHAT THIS DECIDES, AND WHAT IT CANNOT (CLAUDE.md 1.2a: state the blind spots).

  DECIDED mechanically, from raw bytes, reproducible with objdump and grep:
    - a [D] claim cites no address at all                   -> rule violation
    - a cited address is not mapped in the named binary     -> the address is wrong
    - it is unmapped there but real in another binary       -> mis-attributed
    - it is in .text but not an instruction boundary        -> suspect
    - a claim's only source is the quarantined log (1.1a)   -> void

  NOT DECIDED. Whether the instruction at the address MEANS what the note says.
  That is a reading, and a script cannot check a reading. This narrows the set a
  person must re-read; it does not replace the re-reading.

  THE SWEEP'S BLIND SPOT. Instruction boundaries come from objdump's linear
  disassembly. Linear sweep desynchronises after data embedded in code and
  resynchronises later, so "not a boundary" is A REASON TO LOOK, never a proof
  of error. "Boundary" is sound: the sweep really did decode an instruction.

  WHAT IS NOT EVEN TESTED, deliberately. A bare hex run with no 0x prefix
  (a SHA-256 head, a checksum) and any value outside the images' VA window are
  not treated as citations. An earlier version of this script did treat them as
  addresses and reported 178 "hard failures" that were HRESULTs, magic division
  reciprocals and hash prefixes. A checker that cries wolf on constants would be
  ignored within a week, which is worse than not existing.

Usage:
  auditclaims.py                 report everything
  auditclaims.py --strict        exit non-zero if any hard failure survives
  auditclaims.py --addr 0x4042d0 explain one address against every binary
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NOTES = os.path.join(ROOT, "notes")

# Same tag -> file mapping as dis.sh, duplicated rather than parsed out of a
# shell script.
BINARIES = {
    "fw110":  "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe",
    "fw107":  "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater 1.07.exe",
    "fw106":  "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.06.exe",
    "fw104":  "old-firmware-executables/Endgame Gear OP1 8k v2 Firmware Updater v1.04.exe",
    "cfg107": "Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe",
    "cfg104": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe",
    "cfg101": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe",
    "cfg100": "old-config-executables/Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe",
    "xm1r":   "XM1r_Flash_Upgrade_1.9.46.exe",
}

# Prose spells the versions several ways. Longest/most specific first.
PROSE_TAG = [
    (re.compile(r"\bcfg1(00|01|04|07)\b"),                 lambda m: "cfg1" + m.group(1)),
    (re.compile(r"\bfw1(04|06|07|10)\b"),                  lambda m: "fw1" + m.group(1)),
    (re.compile(r"\bconfig(?:uration)?[ -]*tool[ v]*1\.(00|01|04|07)\b", re.I),
     lambda m: "cfg1" + m.group(1)),
    (re.compile(r"\bupdater[ v]*1\.(04|06|07|10)\b", re.I), lambda m: "fw1" + m.group(1)),
    (re.compile(r"\bxm1r\b", re.I),                        lambda m: "xm1r"),
]

FILE_DEFAULT = {
    "config-protocol.md":            "cfg107",
    "config-wire-observed.md":       "cfg107",
    "gui-surface.md":                "cfg107",
    "wire-observed.md":              "cfg107",
    "updater-protocol.md":           "fw110",
    "wire-predictions.md":           "fw110",
    "flash-wire-observed.md":        "fw110",
    "bootloader-observed.md":        "fw110",
    "prediction-read-firmware.md":   "fw110",
    "prediction-bootloader-entry.md": "fw110",
    "prediction-postwindows.md":     "fw110",
    "prediction-scores.md":          "fw110",
    "device-predictions.md":         "fw110",
    "windows-session.md":            "fw110",
    "xm1r-flasher.md":               "xm1r",
    "build-design.md":               "fw110",
    "binaries.md":                   "fw110",
}

# A CITATION, not a constant. Requires an explicit 0x or a Ghidra-style symbol
# prefix, and six to eight hex digits so record offsets and byte values do not
# qualify. The VA window is checked separately against the real images.
ADDR = re.compile(r"\b(?:FUN|LAB|DAT|SUB|PTR|UNK|thunk_FUN)_0*([0-9a-fA-F]{6,8})\b"
                  r"|0x0*([0-9a-fA-F]{6,8})\b")

LOGTXT = re.compile(r"(?<!\w)log\.txt", re.I)

_SECT, _BOUNDARY = {}, {}


def sect_table(tag):
    """[(va_start, va_end, name, is_code)] for one binary, from the PE headers.

    VIRTUAL size, not raw size. objdump -h reports the size on disk, and a
    section whose virtual extent is larger -- .data with a BSS tail -- then
    looks unmapped past its raw end. Every global cfg107 keeps above 0x578e00
    lives there, including the HID import slots at 0x57f160 and the settings
    object at 0x57f210, so a raw-size table calls the project's most-cited
    addresses "not in this binary". It did: 157 of them, until 2026-09-06.
    """
    if tag in _SECT:
        return _SECT[tag]
    with open(os.path.join(ROOT, BINARIES[tag]), "rb") as f:
        d = f.read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("auditclaims: %s is not a PE image" % tag)
    nsect = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 0x1C)[0]
    tbl = pe + 24 + optsz
    got = []
    for i in range(nsect):
        e = tbl + 40 * i
        name = d[e:e + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize = struct.unpack_from("<III", d, e + 8)
        chars = struct.unpack_from("<I", d, e + 36)[0]
        # IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE
        code = bool(chars & 0x00000020) or bool(chars & 0x20000000)
        got.append((base + va, base + va + max(vsize, rsize), name, code))
    if not got:
        raise SystemExit("auditclaims: no sections in %s" % tag)
    _SECT[tag] = got
    return got


def boundaries(tag):
    """Every instruction address one linear sweep of the code sections decodes.

    ONE sweep, and its limitation is not fixable here. Linear disassembly
    desynchronises after data embedded in code and stays wrong until it happens
    to resynchronise, so it reports real instructions as "not a boundary" -- it
    does so for `movb $0x2, %cl` at cfg107 0x40865a, which sets the KEYBOARD
    action type. The usual remedy is to union sweeps begun a byte apart, which
    desynchronise in different places. That is not available on this machine:
    llvm-objdump snaps --start-address down to the nearest symbol (asking for
    0x401001 disassembles from 0x401000), and it has no -b binary to
    disassemble a raw slice with. Both were tried on 2026-09-06.

    So SUSPECT is a low-precision signal by construction -- a reason to look,
    never a verdict -- and nothing safety-critical rests on it.
    Tests/test_citations.py checks the addresses that can reach the device, and
    it reads raw bytes with no disassembler at all (CLAUDE.md 1.2b).
    """
    if tag in _BOUNDARY:
        return _BOUNDARY[tag]
    out = subprocess.run(["objdump", "-d", os.path.join(ROOT, BINARIES[tag])],
                         capture_output=True, text=True).stdout
    _BOUNDARY[tag] = {int(m.group(1), 16) for m in
                      re.finditer(r"(?m)^\s*([0-9a-f]{4,8}):\s", out)}
    return _BOUNDARY[tag]


# The union VA window over every image. Anything outside is a constant, not a
# citation, and is never reported -- see the docstring.
def va_window():
    lo = min(s[0] for t in BINARIES for s in sect_table(t))
    hi = max(s[1] for t in BINARIES for s in sect_table(t))
    return lo, hi


def classify(tag, addr):
    """'code', 'code-misaligned', '<section>', or None if unmapped."""
    for lo, hi, name, code in sect_table(tag):
        if lo <= addr < hi:
            if not code:
                return name
            return "code" if addr in boundaries(tag) else "code-misaligned"
    return None


def addrs_in(text):
    lo, hi = va_window()
    out = set()
    for m in ADDR.finditer(text):
        v = int(m.group(1) or m.group(2), 16)
        if lo <= v < hi:
            out.add(v)
    return out


def context_tag(block, path):
    for rx, f in PROSE_TAG:
        m = rx.search(block)
        if m:
            return f(m), "prose"
    d = FILE_DEFAULT.get(os.path.basename(path))
    return (d, "file default") if d else (None, "none")


def claims(path):
    """(line_no, line, block) for every line carrying [D].

    A heading's block runs to the next heading -- a section titled "... [D]" is
    cited by the lines beneath it, not by its own title.
    """
    lines = open(path, encoding="utf-8").read().splitlines()
    heads = [i for i, l in enumerate(lines) if l.startswith("#")]
    for i, l in enumerate(lines):
        if "[D]" not in l:
            continue
        if l.startswith("#"):
            nxt = next((h for h in heads if h > i), len(lines))
            yield i + 1, l, "\n".join(lines[i:nxt])
        else:
            # A table row's citation is often the row above it.
            yield i + 1, l, "\n".join(lines[max(0, i - 3):i + 6])


def main():
    if "--addr" in sys.argv:
        a = int(sys.argv[sys.argv.index("--addr") + 1], 16)
        for t in BINARIES:
            print("  %-7s %s" % (t, classify(t, a) or "unmapped"))
        return 0

    strict = "--strict" in sys.argv
    uncited, logsourced = [], []
    unmapped, misattr, misaligned = [], [], []
    seen = set()
    nclaims = 0

    for name in sorted(os.listdir(NOTES)):
        if not name.endswith(".md"):
            continue
        path = os.path.join(NOTES, name)

        for lineno, line, block in claims(path):
            nclaims += 1
            where = "%s:%d" % (name, lineno)
            if not addrs_in(block):
                uncited.append((where, line.strip()[:100]))
            if LOGTXT.search(block) and not re.search(
                    r"1\.1a|quarantin|void|OFF LIMITS|not deleted", block, re.I):
                logsourced.append((where, line.strip()[:100]))

        # Address checking is per DISTINCT (file, address): a heading block
        # repeats every address under it, and reporting one wrong address forty
        # times buries the other thirty-nine.
        lines = open(path, encoding="utf-8").read().splitlines()
        for i, l in enumerate(lines):
            for a in addrs_in(l):
                tag, how = context_tag(
                    "\n".join(lines[max(0, i - 12):i + 3]), path)
                if not tag or (name, a, tag) in seen:
                    continue
                seen.add((name, a, tag))
                k = classify(tag, a)
                if k == "code":
                    continue
                if k == "code-misaligned":
                    misaligned.append(("%s:%d" % (name, i + 1), tag, a,
                                       l.strip()[:70]))
                elif k is None:
                    other = [t for t in BINARIES if classify(t, a)]
                    if other:
                        misattr.append(("%s:%d" % (name, i + 1), tag, how, a,
                                        other, l.strip()[:60]))
                    else:
                        unmapped.append(("%s:%d" % (name, i + 1), tag, a,
                                         l.strip()[:70]))
                # a data section is a legitimate citation; nothing to report

    def show(title, rows, fmt):
        print("\n%s  (%d)" % (title, len(rows)))
        print("-" * 78)
        for r in rows:
            print("  " + fmt(r))
        if not rows:
            print("  none")

    lo, hi = va_window()
    print("[D] claims: %d    distinct (file, address, binary) checked: %d"
          % (nclaims, len(seen)))
    print("VA window treated as citable: 0x%06x-0x%06x" % (lo, hi))

    show("HARD: address is mapped in NO vendor binary", unmapped,
         lambda r: "%-40s %-7s 0x%08x  %s" % (r[0], r[1], r[2], r[3]))
    show("HARD: cited to the quarantined log (CLAUDE.md 1.1a)", logsourced,
         lambda r: "%-40s %s" % (r[0], r[1]))
    show("MIS-ATTRIBUTED: unmapped in the named binary, real in another", misattr,
         lambda r: "%-40s says %-7s (%s) 0x%08x -> %s | %s" %
                   (r[0], r[1], r[2], r[3], ",".join(r[4]), r[5]))
    show("SUSPECT (low precision -- see boundaries()): not decoded by the sweep",
         misaligned,
         lambda r: "%-40s %-7s 0x%08x  %s" % (r[0], r[1], r[2], r[3]))
    show("RULE VIOLATION: [D] with no address in its block (CLAUDE.md 1.2)",
         uncited, lambda r: "%-40s %s" % (r[0], r[1]))

    hard = len(unmapped) + len(logsourced)
    print("\nhard: %d   mis-attributed: %d   suspect: %d   uncited: %d"
          % (hard, len(misattr), len(misaligned), len(uncited)))
    return 1 if (strict and (hard or misattr)) else 0


if __name__ == "__main__":
    sys.exit(main())
