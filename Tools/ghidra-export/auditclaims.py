#!/usr/bin/env python3
"""
auditclaims.py -- mechanically check every [D] citation in notes/ against the
bytes of the binary it names.

engineering-rules.md 1.2 says a [D] claim is "traced to a specific location in an .exe.
Cite the address." That is a rule nothing has ever enforced. Nearly five hundred
[D] markers accumulated across nineteen note files, written over many sessions,
and no pass has ever confirmed that the addresses in them are real -- that they
land inside the named binary, that they land on an instruction boundary rather
than mid-instruction, or that they exist at all.

WHAT THIS DECIDES, AND WHAT IT CANNOT (engineering-rules.md 1.2a: state the blind spots).

  DECIDED mechanically, from raw bytes, reproducible with objdump and grep:
    - a [D] claim carries no CITATION OF ANY FORM           -> rule violation
    - a cited address is not mapped in any binary           -> the address is wrong
    - it is unmapped in the named binary, real in another,
      and inside the named image's own span                 -> mis-attributed
    - it is outside the named image entirely                -> a constant, or a
      wrong binary name; a script cannot tell, so this is REPORTED AND NOT
      COUNTED as an error. Every instance found so far is a whole-image
      checksum or a Windows style dword.
    - it is in .text but not an instruction boundary        -> suspect
    - a claim's only source is the quarantined log (1.1a)   -> void

  WHAT COUNTS AS A CITATION, which is the part that changed on 2026-09-06.
  engineering-rules.md 1.2 said "[D] ... Cite the address", which is written for a claim
  about code and under-specifies every other kind. Reporting a resource id or a
  SHA-256 as "no citation" was not merely noisy: the obvious way to silence this
  script would have been to bolt a plausible address onto a claim that never
  rested on one, which is worse than no script. The forms, all checkable by
  someone else, and 1.2 now lists the same five:
    - a virtual address in a named binary
    - a resource: RT_DIALOG 102, FWFILE 140, DIALOG 137, a .rsrc RVA
    - a file offset, or a hash of a named artefact
    - a reproducible command on a named binary (rabin2, blobcensus.py, ...)
    - a verbatim quoted literal -- VERIFIED, in both encodings, against the
      binary the block names. A fabricated quote does not buy a pass.
    - a cross-reference to a section of a notes file, resolved ONE LEVEL:
      the section must exist and must itself carry an address. A chain of
      deferrals is how a guess becomes a derivation by being restated.

  Tests/test_auditclaims.py plants a broken instance of every form above and
  requires this script to catch it (6.2: a harness that cannot produce a bad
  result is not evidence). The uncited count went 87 -> 0 on the day those forms
  were added, and that test is the only reason the zero means anything.

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

# ---------------------------------------------------------------------------
# The citation forms that are NOT addresses
# ---------------------------------------------------------------------------
# Added 2026-09-06, after triaging the 87 "uncited" claims this script produced
# and finding that most of them were cited perfectly well -- to a RESOURCE, a
# FILE OFFSET, a HASH, or a reproducible command, none of which is an address.
#
# engineering-rules.md 1.2 said "[D] ... Cite the address", which is written for a claim
# about code and under-specifies every other kind. Reporting those as rule
# violations was not merely noisy: the obvious way to silence this script would
# have been to bolt a plausible address onto a claim that never rested on one,
# which is a worse outcome than no script at all. 1.2 now names the forms; this
# is the same list, and the two must stay in agreement.
#
# Every form here is CHECKABLE BY SOMEONE ELSE, which is the actual requirement.
# A hash is stronger evidence than an address: it verifies, where an address
# only points.
RESOURCE = re.compile(
    r"\bRT_[A-Z]+\b"                       # RT_DIALOG, RT_MENU, RT_STRING
    r"|\bDIALOG\s+\d+"                     # DIALOG 137
    r"|\bFWFILE\s*/?\s*\d+"               # FWFILE 140, FWFILE/140
    r"|\.rsrc\b"
    r"|\bIDD[_ ]?0?x?[0-9A-Fa-f]+\b")
HASH = re.compile(r"\b[0-9a-f]{16,64}\b|\bSHA-?256\b", re.I)
# A command anyone can re-run against a named binary. The script name IS the
# citation -- these all live in Tools/ and are in git.
TOOLCMD = re.compile(
    r"\brabin2\b|\bobjdump\b|\breadpe\b|\bstrings\b|\bxxd\b"
    r"|\b(?:dis|coverage|classify|recmap|rsrc|dlgdump|dlgref|litscan|"
    r"calls|resource_id|ingest|ingest_config|fieldmap|score|fwfile|"
    r"blobcensus|auditclaims|reconcile_scores|pidwatch|bootwatch)\.(?:py|sh)\b")
FILEOFF = re.compile(r"\bfile offset\s+`?0x[0-9a-fA-F]+", re.I)

# A hex literal BELOW the VA window that is a valid offset into the named
# binary. gui-surface.md cites string runs by file offset (`0x1573e8`), which is
# exactly as checkable as a VA and was being reported as no citation at all.
def file_offset_in(tag, block):
    if tag not in BINARIES:
        return False
    try:
        size = os.path.getsize(os.path.join(ROOT, BINARIES[tag]))
    except OSError:
        return False
    lo, _ = va_window()
    for m in re.finditer(r"0x0*([0-9a-fA-F]{4,8})\b", block):
        v = int(m.group(1), 16)
        if 0x1000 <= v < size and v < lo:
            return True
    return False


# A verbatim literal quoted out of the binary. This is the STRONGEST citation
# form in the whole list, and the only one this script can actually VERIFY
# rather than merely recognise: it goes and finds the bytes. `0.1.1 The 1.10
# build's project identity is a different mouse` cites nothing but the PDB path
# it quotes -- and that path either is in fw110 or it is not.
_BLOB = {}


def blob(tag):
    if tag not in _BLOB:
        try:
            with open(os.path.join(ROOT, BINARIES[tag]), "rb") as f:
                _BLOB[tag] = f.read()
        except (OSError, KeyError):
            _BLOB[tag] = b""
    return _BLOB[tag]


def quoted_literal_in(tag, block):
    """A quoted string of 12+ chars that really occurs in the named binary.

    Both encodings, because engineering-rules.md 1.2a's own example of a bad scan is one
    that looked in only one of them.
    """
    b = blob(tag)
    if not b:
        return False
    cands = re.findall(r"`([^`\n]{12,120})`", block)
    for fence in re.findall(r"```[^\n]*\n(.*?)```", block, re.S):
        cands += [l.strip() for l in fence.splitlines() if len(l.strip()) >= 12]
    for c in cands:
        c = c.strip().strip("*_")
        if len(c) < 12 or c.startswith("0x"):
            continue
        try:
            raw = c.encode("latin-1")
        except UnicodeEncodeError:
            continue
        if raw in b or c.encode("utf-16-le") in b:
            return True
    return False


# A cross-reference to a section of another notes file (or this one). Resolved
# ONE LEVEL, never recursively: the named section must exist and must itself
# carry an address. A reference that dangles, or that points at a section as
# uncited as the claim, is not a citation -- it is a deferral, and a chain of
# deferrals is how a [G] becomes a [D] by being restated.
XREF = re.compile(r"(?:`?([a-z][a-z0-9-]*\.md)`?[^.\n]{0,40}?)?"
                  r"§\s*([0-9]+(?:\.[0-9a-z]+)*)")


def xref_resolves(path, block):
    for m in XREF.finditer(block):
        fname, sec = m.group(1), m.group(2)
        target = os.path.join(NOTES, fname) if fname else path
        if not os.path.exists(target):
            continue
        try:
            lines = open(target, encoding="utf-8").read().splitlines()
        except OSError:
            continue
        for i, l in enumerate(lines):
            lv = level(l)
            if not lv:
                continue
            if not re.match(r"^#+\s+%s[\s.:]" % re.escape(sec), l):
                continue
            nxt = next((j for j in range(i + 1, len(lines))
                        if 0 < level(lines[j]) <= lv), len(lines))
            body = "\n".join(lines[i:nxt])
            if addrs_in(body):
                return True
    return False


def cited_otherwise(tag, block, path=None):
    """True if the block carries a checkable citation that is not an address."""
    return bool(RESOURCE.search(block) or HASH.search(block)
                or TOOLCMD.search(block) or FILEOFF.search(block)
                or file_offset_in(tag, block)
                or quoted_literal_in(tag, block)
                or (path and xref_resolves(path, block)))


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
    it reads raw bytes with no disassembler at all (engineering-rules.md 1.2b).
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


def span(tag):
    """(lowest VA, highest VA) of one image, for judging plausibility."""
    t = sect_table(tag)
    return min(x[0] for x in t), max(x[1] for x in t)


# `0x401000`-`0x52c65f` is a RANGE, and its right-hand end is exclusive: one
# past the last mapped byte, so it is unmapped by construction and always will
# be. Two of the ten remaining "mis-attributed" results were that, in notes
# whose whole subject is section boundaries.
RANGE_END = re.compile(
    r"0x[0-9a-fA-F]{4,8}`?\s*[-\u2013\u2014]+\s*`?0x0*([0-9a-fA-F]{4,8})")


def range_ends(text):
    return {int(m.group(1), 16) for m in RANGE_END.finditer(text)}


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


def context_tag(block, path, at=None):
    """The binary a block of prose is talking about.

    THE NEAREST NAME WINS, not the first one. A paragraph that reads "cfg107's
    0x00404830 copies ... cfg100's 0x004050e0 is the mirror image" is about
    cfg100 by the time it reaches the disassembly under it, and taking the
    first match attributed six correct cfg100 addresses to cfg107 (2026-09-06).
    Prose narrows as it goes down; so does this.
    """
    hits = sorted((m.start(), f(m))
                  for rx, f in PROSE_TAG for m in rx.finditer(block))
    if hits:
        # Nearest name BEFORE the address if we know where the address is.
        # "cfg100 0x401000-0x57f957, cfg101 0x401000-0x52c65f, cfg104 ...,
        # cfg107 ..." puts four names and four ranges on two lines; nearest
        # -anywhere picked cfg107 for cfg101's range, which is a name that
        # comes AFTER the address it was applied to.
        before = [h for h in hits if at is None or h[0] < at]
        return (before[-1][1] if before else hits[-1][1]), "prose"
    d = FILE_DEFAULT.get(os.path.basename(path))
    return (d, "file default") if d else (None, "none")


def level(line):
    """Markdown heading depth, or 0 for a non-heading."""
    m = re.match(r"^(#+)\s", line)
    return len(m.group(1)) if m else 0


def claims(path):
    """(line_no, line, block) for every line carrying [D].

    A heading's block runs to the next heading OF THE SAME OR HIGHER LEVEL --
    a section titled "... [D]" is cited by everything beneath it, including its
    own subsections.

    THIS USED TO STOP AT THE NEXT HEADING OF ANY LEVEL, and that alone
    manufactured most of the "uncited" list: `## 7. The complete command set
    [D]` is immediately followed by `### 7.1`, so its block was four lines long
    and the forty addresses under it were invisible. Fixed 2026-09-06 while
    triaging that list; the count went 87 -> the residue below, and the residue
    is the part that was ever worth reading.
    """
    lines = open(path, encoding="utf-8").read().splitlines()
    for i, l in enumerate(lines):
        if "[D]" not in l:
            continue
        lv = level(l)
        if lv:
            nxt = next((j for j in range(i + 1, len(lines))
                        if 0 < level(lines[j]) <= lv), len(lines))
            yield i + 1, l, "\n".join(lines[i:nxt])
        else:
            # A claim in prose is cited by ITS SECTION, which is how a reader
            # actually finds the evidence: "Two HID feature report IDs,
            # distinguished by length [D]" is followed eight lines later by
            # `### FUN_004012a0 @ 0x004012a0`, and nobody reading it would call
            # that uncited. The old +-3/+6 window called it uncited, which is
            # most of what was left on the list after the heading fix.
            #
            # THIS IS DELIBERATELY THE WEAKER TEST, and the residue is what
            # makes it worth running: a [D] claim whose ENTIRE SECTION contains
            # no address anywhere is a real §1.2 violation and there is nothing
            # to argue about.
            start = max((j for j in range(i, -1, -1) if level(lines[j])),
                        default=0)
            lv = level(lines[start]) or 1
            nxt = next((j for j in range(start + 1, len(lines))
                        if 0 < level(lines[j]) <= lv), len(lines))
            # From the SECTION HEADING, not from i-3. The heading itself often
            # carries the address -- "### FUN_00401330 @ 0x00401330" -- and
            # starting three lines above the claim skipped it, which is how
            # "Status byte values seen: 0x01 = ready, 0x04 = busy [D]" got
            # reported as uncited while sitting inside the section named after
            # the function that reads those exact bytes.
            yield i + 1, l, "\n".join(lines[min(start, max(0, i - 3)):
                                             max(nxt, i + 6)])


def main():
    if "--addr" in sys.argv:
        a = int(sys.argv[sys.argv.index("--addr") + 1], 16)
        for t in BINARIES:
            print("  %-7s %s" % (t, classify(t, a) or "unmapped"))
        return 0

    strict = "--strict" in sys.argv
    uncited, logsourced = [], []
    unmapped, misattr, misaligned, outside = [], [], [], []
    seen = set()
    nclaims = 0

    for name in sorted(os.listdir(NOTES)):
        if not name.endswith(".md"):
            continue
        path = os.path.join(NOTES, name)

        for lineno, line, block in claims(path):
            nclaims += 1
            where = "%s:%d" % (name, lineno)
            btag, _ = context_tag(block, path)
            if not addrs_in(block) and not cited_otherwise(btag, block, path):
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
                # THE LINE FIRST, then the surrounding window. In a
                # cross-binary comparison table every row names a different
                # binary -- `| cfg100 | 0x004038a0 | 0x5dcb70 |` -- and a
                # 15-line window picks whichever was named first, so every row
                # but one was reported as citing an address that is unmapped in
                # a binary the row never mentions. That was 24 of the 24
                # "mis-attributed" results, i.e. the whole category was an
                # artefact of reading the wrong scope (2026-09-06).
                # Three scopes, narrowest first, each accepted only if it
                # actually maps the address. The LINE, then the PARAGRAPH (back
                # to the previous blank line), then a 15-line window.
                #
                # The paragraph step is not cosmetic: `0x5814a0` sits in a
                # bullet whose FIRST line reads "**cfg100: 47 hits.**" and
                # whose third line holds the address. Line scope misses it,
                # window scope picks up the cfg107 bullet above it, and the
                # tool reported a correct cfg100 address as unmapped in cfg107.
                para = next((j for j in range(i, max(0, i - 12), -1)
                             if not lines[j].strip()), max(0, i - 12))
                col = l.lower().find("%x" % a)
                if col < 0:
                    col = l.lower().find("%06x" % a)
                pre = "\n".join(lines[para:i])
                scopes = [(l, col if col >= 0 else None),
                          (pre + "\n" + l, None if col < 0 else len(pre) + 1 + col),
                          ("\n".join(lines[max(0, i - 12):i + 3]), None)]
                tag, how = None, "none"
                for sc, at in scopes:
                    t, h = context_tag(sc, path, at)
                    if t and classify(t, a) is not None:
                        tag, how = t, h
                        break
                    if t and not tag:
                        tag, how = t, h
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
                    # An exclusive range END is unmapped by construction --
                    # one past the last byte -- and always will be. Judge the
                    # last byte instead of complaining forever.
                    if a in range_ends(l) and classify(tag, a - 1):
                        continue
                    lo, hi = span(tag)
                    other = [t for t in BINARIES if classify(t, a)]
                    if not other:
                        unmapped.append(("%s:%d" % (name, i + 1), tag, a,
                                         l.strip()[:70]))
                    elif lo <= a < hi:
                        # Inside the named image's own span but in no section:
                        # it really looks like an address there and is not one.
                        # THIS is the interesting case.
                        misattr.append(("%s:%d" % (name, i + 1), tag, how, a,
                                        other, l.strip()[:60]))
                    else:
                        # Outside the named image entirely. It is a constant
                        # that happens to fall inside a LARGER image's range --
                        # every one found so far is a whole-image checksum or a
                        # Windows style dword -- or the note names the wrong
                        # binary. A script cannot tell those apart, so this is
                        # its own bucket and is NOT reported as an error.
                        outside.append(("%s:%d" % (name, i + 1), tag, a,
                                        other, l.strip()[:60]))
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
    show("HARD: cited to the quarantined log (engineering-rules.md 1.1a)", logsourced,
         lambda r: "%-40s %s" % (r[0], r[1]))
    show("MIS-ATTRIBUTED: unmapped in the named binary, real in another", misattr,
         lambda r: "%-40s says %-7s (%s) 0x%08x -> %s | %s" %
                   (r[0], r[1], r[2], r[3], ",".join(r[4]), r[5]))
    show("OUTSIDE THE NAMED IMAGE: a constant, or the wrong binary name. "
         "NOT an error", outside,
         lambda r: "%-40s %-7s 0x%08x  only in %s | %s" %
                   (r[0], r[1], r[2], ",".join(r[3]), r[4]))
    show("SUSPECT (low precision -- see boundaries()): not decoded by the sweep",
         misaligned,
         lambda r: "%-40s %-7s 0x%08x  %s" % (r[0], r[1], r[2], r[3]))
    show("RULE VIOLATION: [D] with no address in its block (engineering-rules.md 1.2)",
         uncited, lambda r: "%-40s %s" % (r[0], r[1]))

    hard = len(unmapped) + len(logsourced)
    print("\nhard: %d   mis-attributed: %d   outside-image: %d   suspect: %d"
          "   uncited: %d"
          % (hard, len(misattr), len(outside), len(misaligned), len(uncited)))
    # --strict fails on UNCITED too. It did not, which made the flag useless
    # for the one category §1.2 actually legislates. The outside-image bucket
    # is excluded on purpose: a script cannot tell a constant from a wrong
    # binary name, and failing a build on a judgement call trains people to
    # pass --no-strict.
    return 1 if (strict and (hard or misattr or uncited)) else 0


if __name__ == "__main__":
    sys.exit(main())
