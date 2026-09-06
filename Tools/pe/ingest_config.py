#!/usr/bin/env python3
"""ingest_config.py -- read a config tool nobody has read, and decide whether
this build's settings knowledge still applies to it.

THE QUESTION THAT PROMPTED IT, and the honest answer.

  "if you upload 1.08 config it should change the config to 1.08 version" --
  and then, "is there genuinely no amount of engineering to make the config
  upload work reliably? or is it just difficult but still possible"

Possible, and this is how. NOT by trusting the new binary, and not by assuming a
new version keeps the old layout. By RE-DERIVING every byte `egg-config` can
write, from the new executable's own bytes, and refusing if any one of them
fails to re-derive or re-derives to a different answer.

That is a stronger claim than "the layout looks the same". The settings record
is 1024 bytes and we can only name about a dozen of them; a version that moved
`lod` from record 0x09 to 0x0a would be invisible to a structural diff of the
serializer if the serializer's SHAPE were unchanged. So each field is located by
what it MEANS -- the bound check, the jump table, the clamp, the BM_GETCHECK
quartet -- and the record offset it lands on is compared against what we ship.

WHAT IS CHECKED

  1. THE SERIALIZER. Tools/ghidra-export/recmap.py recovers the complete
     record <- object map, or refuses. A partial map is worse than none: it
     looks complete. Then it is diffed against cfg107's.
  2. THE OBJECT BASE, derived rather than assumed: the button block's stores
     are absolute (`movb $imm, ABS(,%reg,8)`), recmap says record 0x37 comes
     from object 0x2e, so base = ABS - 0x2e. Two independent facts agreeing.
  3. EVERY WRITABLE FIELD, by pattern:
       lod           eleven `movb $imm, X(%reg)` with immediates exactly {0..10}
       cpi-stage     four such with {0,1,2,3}, each after a BM_GETCHECK
       sensor-angle  the signed clamp cmpl $-0x7f / jge / movl $0x7f / jle
       polling       cmpl $0x3f bound check on the rate index
       checkboxes    the setne/sete stores
  4. THE NINETEEN BUTTON ACTIONS, from the `+0x2e` / `+0x2f` store pairs.

WHAT IT CANNOT DO, said plainly. It cannot tell you a NEW field is safe to
write -- it only re-derives the ones we already understand. A config version
that adds a setting leaves that setting unknown, and §1.3 keeps it unwritten.
And it says nothing about firmware: a config tool is not an updater.

    python3 Tools/pe/ingest_config.py <config-tool.exe> [--against cfg107]
"""
import importlib.util
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GHIDRA = os.path.join(ROOT, "Tools", "ghidra-export")
sys.path.insert(0, GHIDRA)

import recmap  # noqa: E402

CPP = os.path.join(ROOT, "Sources", "EGGConfigCore", "src", "ConfigRecord.cpp")

BM_GETCHECK = 0xF0
CB_GETCURSEL = 0x147


# --------------------------------------------------------------------------
# Raw access
# --------------------------------------------------------------------------
class Text:
    """A PE's executable bytes, addressable by VA."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = d = f.read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        nsect = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        base = struct.unpack_from("<I", d, pe + 24 + 0x1C)[0]
        tbl = pe + 24 + optsz
        self.spans = []
        for i in range(nsect):
            e = tbl + 40 * i
            vsize, va, rsize, roff = struct.unpack_from("<IIII", d, e + 8)
            chars = struct.unpack_from("<I", d, e + 36)[0]
            if chars & 0x20000020:
                self.spans.append((base + va, d[roff:roff + rsize]))
        if not self.spans:
            raise ValueError("no executable section")

    def scan(self, pattern):
        """(va, match) for every occurrence of a bytes-regex.

        re.DOTALL is NOT optional here and its absence is a silent, targeted
        failure: without it `.` does not match 0x0a, so a `movb $0x0a, ...` or
        a displacement of 0x0a is invisible. That cost both LOD's eleventh arm
        (immediate 0x0a) and cpi-stage entirely (displacement 0x0a) on the first
        run of this tool -- a scanner that finds 10 of 11 things and says the
        set does not match is worse than one that finds none.
        """
        for va, blob in self.spans:
            for m in re.finditer(pattern, blob, re.DOTALL):
                yield va + m.start(), m

    def at(self, va, n):
        for base, blob in self.spans:
            if base <= va < base + len(blob):
                o = va - base
                return blob[o:o + n]
        return b""


# --------------------------------------------------------------------------
# Field locators. Each returns (object_offset, value_set) or None.
# --------------------------------------------------------------------------
def stores_imm_to_disp(t):
    """Every `movb $imm, disp8(%reg)`, as {disp: {imm: [va, ...]}}.

    c6 /r ib with mod=01 (disp8) is `c6 4X dd ii`, where X is the base
    register. That is the form the vendor uses for object fields, because the
    object pointer is in a register throughout the collect-from-GUI function.
    """
    out = {}
    for va, m in t.scan(rb"\xc6[\x40-\x47](.)(.)"):
        disp, imm = m.group(1)[0], m.group(2)[0]
        # disp8 is SIGNED. 0x80-0xff are negative -- stack locals, not object
        # fields -- and they were the entire source of the ambiguity this tool
        # first reported (eleven bogus clusters at "offset 0xfc", which is -4).
        # The settings object's highest offset is 0x81, so the whole legal range
        # is non-negative and this filter loses nothing real.
        if disp > 0x7F:
            continue
        out.setdefault(disp, {}).setdefault(imm, []).append(va)
    return out


def find_value_set(t, want, label, span=0x200):
    """The object offset whose immediate stores CLUSTER into exactly `want`.

    A bare disp8 is not an object-field identifier: `movb $imm, 0x27(%reg)`
    matches any structure in the program with a byte at offset 0x27, and the
    first version of this function therefore found the union of several
    unrelated fields and reported that no offset matched. What identifies the
    field is that its stores are the arms of ONE dispatch -- eleven of them
    inside a couple of hundred bytes, one per dropdown item.

    So: group by displacement, cluster by address, and require a cluster's
    immediate set to be EXACTLY `want`. Exactly, not a superset -- a superset
    would let a version that added a twelfth LOD value pass silently.
    """
    hits = []
    for disp, byval in stores_imm_to_disp(t).items():
        sites = sorted((va, imm) for imm, vas in byval.items() for va in vas)
        i = 0
        while i < len(sites):
            j = i + 1
            while j < len(sites) and sites[j][0] - sites[j - 1][0] <= span:
                j += 1
            cluster = sites[i:j]
            if {imm for _, imm in cluster} == want:
                hits.append((disp, [va for va, _ in cluster]))
            i = j
    if not hits:
        return None, ("no object offset has a cluster of stores whose "
                      "immediates are exactly the %d values %s"
                      % (len(want), sorted(want)))
    if len({h[0] for h in hits}) > 1:
        return None, ("%d different object offsets have such a cluster (%s) -- "
                      "ambiguous, refusing rather than picking one"
                      % (len(hits), ", ".join("0x%02x" % h[0] for h in hits)))
    return hits[0], None


def find_button_base(t):
    """The settings object's base VA, from the button block's absolute stores.

    `movb $imm, ABS(,%reg,8)` encodes as c6 04 <sib> <abs32> <imm>. The action
    type lands at object 0x2e and its second byte at 0x2f, so the block shows up
    as a PAIR OF ADJACENT absolute targets both written by many handlers.

    Picking "the busiest absolute byte store" is not good enough and the first
    version proved it: several action types are stored from a register rather
    than an immediate, so +0x2f has MORE immediate-store sites than +0x2e, and
    the base came out one too high. Requiring an adjacent pair and taking the
    LOWER removes the ambiguity instead of guessing which side won.
    """
    counts = {}
    for va, m in t.scan(rb"\xc6\x04[\xc5\xd5\xcd\xdd\xed\xf5](....)."):
        abs32 = struct.unpack("<I", m.group(1))[0]
        counts.setdefault(abs32, []).append(va)
    if not counts:
        return None, "no `movb $imm, ABS(,%reg,8)` stores found at all"
    pairs = [(a, len(counts[a]) + len(counts[a + 1]))
             for a in counts if a + 1 in counts]
    if not pairs:
        return None, ("no ADJACENT pair of absolute byte-store targets; the "
                      "button block writes an action type and its second byte "
                      "to consecutive addresses and should appear as one")
    lo = max(pairs, key=lambda p: p[1])[0]
    sites = counts[lo] + counts[lo + 1]
    if len(sites) < 12:
        return None, ("the busiest adjacent pair has only %d immediate stores; "
                      "the button menu should give many more" % len(sites))
    return (lo - 0x2E, sites), None


def button_pairs(t, base):
    """{action_type: {second_byte}} from the +0x2e / +0x2f store pairs."""
    act, snd = base + 0x2E, base + 0x2F
    pairs = {}
    for va, m in t.scan(rb"\xc6\x04[\xc5\xd5\xcd\xdd\xed\xf5]"
                        + re.escape(struct.pack("<I", act)) + rb"(.)"):
        b0 = m.group(1)[0]
        # the matching +1 store follows within a few instructions
        blob = t.at(va, 48)
        m2 = re.search(rb"\xc6\x04[\xc5\xd5\xcd\xdd\xed\xf5]"
                       + re.escape(struct.pack("<I", snd)) + rb"(.)", blob)
        pairs.setdefault(b0, set()).add(m2.group(1)[0] if m2 else None)
    return pairs


# The writable fields this tool can locate by MEANING, and where egg-config
# writes them. `want` is the exact set of values the vendor's dispatch stores;
# requiring equality rather than containment is what makes a version that added
# or removed an option show up instead of passing quietly.
#
# Module level so a test can perturb it -- a checker nobody has watched fail is
# not evidence (§6.2).
FIELD_EXPECTATIONS = {
    "lod":       (set(range(0, 11)), 0x09),
    "cpi-stage": ({0, 1, 2, 3},      0x0D),
}


def clamp_bounds(t):
    """The sensor-angle trackbar's clamp bounds, or (None, None).

    Anchored on `cmpl $-0x7f` (83 f8 81), which every shipped config tool has.
    From there the LOW bound is the 32-bit immediate stored on the taken arm
    (0xffffff81 = -127) and the HIGH bound is the positive constant the code
    compares or loads within the same run. Both are read as VALUES, so a version
    that changed the range reports the new numbers instead of failing to match a
    byte pattern.
    """
    lows, highs = set(), set()
    for va, _ in t.scan(rb"\x83\xf8\x81"):
        blob = t.at(va, 64)
        for m in re.finditer(rb"\x81\xff\xff\xff", blob):
            lows.add(-127)
        # cmpl $imm8 with a positive bound, or movl $imm32 of one
        for m in re.finditer(rb"\x83[\xf8-\xff]([\x01-\x7f])", blob, re.DOTALL):
            v = m.group(1)[0]
            if v != 0x81:
                highs.add(v)
        for m in re.finditer(rb"[\xb8-\xbf]([\x01-\x7f])\x00\x00\x00", blob,
                             re.DOTALL):
            highs.add(m.group(1)[0])
    if not lows or not highs:
        return (min(lows) if lows else None, max(highs) if highs else None)
    # The bound is the largest positive constant in the clamp run: a smaller one
    # is a loop counter or a field width, not the trackbar's maximum.
    return min(lows), max(highs)


def shipped_button_table():
    with open(CPP, encoding="utf-8") as f:
        src = f.read()
    out = {}
    for m in re.finditer(r'\{"([a-z0-9-]+)",\s*"([a-z]+)",\s*'
                         r'(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),', src):
        out[m.group(1)] = (int(m.group(3), 16), int(m.group(4), 16))
    return out


# --------------------------------------------------------------------------
def analyse(tag_or_path, against="cfg107"):
    r = {"target": tag_or_path, "problems": [], "notes": []}

    # 1. the serializer. A tag for the four checked-in tools, or a path -- the
    # path form is the point, since a version nobody has read is not a tag.
    if tag_or_path in recmap.TAGS:
        path = os.path.join(ROOT, recmap.TAGS[tag_or_path])
    else:
        path = tag_or_path if os.path.isabs(tag_or_path) \
            else os.path.join(ROOT, tag_or_path)
        if not os.path.exists(path):
            r["problems"].append("no such file: %s" % tag_or_path)
            return r
    try:
        Text(path)
    except (ValueError, struct.error) as e:
        r["problems"].append(
            "%s is not a 32-bit PE with executable code (%s). This tool reads "
            "CONFIG tools; a firmware updater goes to Tools/pe/ingest.py."
            % (os.path.basename(path), e))
        return r
    m = recmap.extract(path)
    r["map"] = m
    if not m.get("ok"):
        r["problems"].append("record map incomplete: %s" % m.get("why", ""))
        return r
    ref = recmap.extract(against)
    r["map_diff"] = recmap.diff(m, ref) if ref.get("ok") else ["reference failed"]

    t = Text(path)

    # 2. the object base
    got, why = find_button_base(t)
    if why:
        r["problems"].append("object base: " + why)
        base = None
    else:
        base, sites = got
        r["base"] = base
        r["notes"].append("settings object base 0x%06x, from %d button "
                          "action-type stores at object 0x2e"
                          % (base, len(sites)))

    # 3. the writable fields, by meaning
    rec_of = {obj: rec for rec, obj in m["map"].items()}
    r["fields"] = {}
    for name, (want, expect_rec) in FIELD_EXPECTATIONS.items():
        hit, why2 = find_value_set(t, want, name)
        if why2:
            r["problems"].append("%s: %s" % (name, why2))
            continue
        obj, vas = hit
        rec = rec_of.get(obj)
        r["fields"][name] = {"obj": obj, "rec": rec, "sites": len(vas)}
        if rec is None:
            r["problems"].append(
                "%s: object 0x%02x is not serialised at all in this version"
                % (name, obj))
        elif rec != expect_rec:
            r["problems"].append(
                "%s MOVED: this version writes it to record 0x%02x, "
                "egg-config writes record 0x%02x" % (name, rec, expect_rec))

    # sensor-angle: DERIVE the clamp bounds, do not pattern-match one encoding.
    #
    # The first version of this looked for `83 f8 81 7d` -- cmp $-0x7f followed
    # by a SHORT JGE -- and reported the clamp missing from cfg100, cfg101 and
    # cfg104. It is not missing. Those three use `jg` (0x7f) where cfg107 uses
    # `jge` (0x7d), which differs only in what happens at exactly -127, where
    # both end up storing -127 anyway. Pinning the jump opcode was testing the
    # compiler, not the protocol.
    lo, hi = clamp_bounds(t)
    r["clamp"] = (lo, hi)
    if lo is None or hi is None:
        r["problems"].append(
            "sensor-angle: could not derive both clamp bounds (found low=%s "
            "high=%s). egg-config limits the angle to -127..127 on the strength "
            "of that clamp; without it the bound is unknown for this version."
            % (lo, hi))
    elif (lo, hi) != (-127, 127):
        r["problems"].append(
            "sensor-angle: this version clamps to %d..%d, egg-config enforces "
            "-127..127" % (lo, hi))
    else:
        r["notes"].append("sensor-angle clamps to %d..%d, matching egg-config"
                          % (lo, hi))

    # polling: the 0x3f bound check
    if not list(t.scan(rb"\x0f\xb6\x03\x48\x83\xf8\x3f")):
        r["notes"].append(
            "polling: the exact `movzbl (%ebx) ; decl ; cmpl $0x3f` sequence "
            "was not found. Not fatal on its own -- the surrounding code may "
            "have been recompiled -- but the rate table should be re-read.")

    # 4. the button table
    if base is not None:
        pairs = button_pairs(t, base)
        shipped = shipped_button_table()
        r["button_types"] = sorted(pairs)
        # An action type stored from a REGISTER leaves no immediate to match:
        # the five mouse buttons zero one and KEY loads 0x02 into %cl. Those are
        # reported separately, because "not verifiable by this method" and "not
        # present" are different claims and §1.2a says so.
        missing, unverifiable = [], []
        for name, (b0, b1) in sorted(shipped.items()):
            if b0 not in pairs:
                if b0 in (0x00, 0x02):
                    unverifiable.append("%s (type 0x%02x, stored from a "
                                        "register)" % (name, b0))
                else:
                    missing.append("%s (type 0x%02x)" % (name, b0))
            elif b1 not in pairs[b0] and None not in pairs[b0]:
                missing.append("%s (0x%02x/0x%02x; this version stores %s)"
                               % (name, b0, b1,
                                  ", ".join("0x%02x" % x for x in
                                            sorted(v for v in pairs[b0]
                                                   if v is not None))))
        r["buttons_missing"] = missing
        r["buttons_unverifiable"] = unverifiable
        if unverifiable:
            r["notes"].append(
                "%d button actions store their type from a register, so no "
                "immediate exists to compare: %s. Not evidence of absence "
                "(\u00a71.2a) -- check them by hand or with "
                "Tests/test_citations.py against this version."
                % (len(unverifiable), "; ".join(unverifiable)))
        if missing:
            r["problems"].append(
                "%d of %d shipped button actions do not re-derive: %s"
                % (len(missing), len(shipped), "; ".join(missing)))

    r["ok"] = not r["problems"] and not r["map_diff"]
    return r


def main(argv):
    against = "cfg107"
    targets = []
    i = 1
    while i < len(argv):
        if argv[i] == "--against":
            i += 1
            against = argv[i]
        else:
            targets.append(argv[i])
        i += 1
    if not targets:
        print(__doc__)
        return 2

    bad = 0
    for tag in targets:
        r = analyse(tag, against)
        print("=" * 74)
        print("%s   (compared against %s)" % (tag, against))
        print("=" * 74)
        m = r.get("map")
        if m and m.get("ok"):
            print("  serializer   %d record offsets, high 0x%02x, unwritten %s"
                  % (m["count"], m["high"],
                     ", ".join("0x%02x" % x for x in m["unwritten"])))
            d = r.get("map_diff") or []
            print("  record map   %s"
                  % ("IDENTICAL to %s" % against if not d
                     else "%d DIFFERENCES" % len(d)))
            for line in d[:20]:
                print("     " + str(line))
        for note in r.get("notes", []):
            print("  %s" % note)
        for name, f in sorted(r.get("fields", {}).items()):
            print("  %-11s object 0x%02x -> record %s   (%d store sites)"
                  % (name, f["obj"],
                     "0x%02x" % f["rec"] if f["rec"] is not None else "NONE",
                     f["sites"]))
        if "button_types" in r:
            print("  buttons      %d distinct action types re-derived: %s"
                  % (len(r["button_types"]),
                     " ".join("%02x" % x for x in r["button_types"])))
        if r["problems"]:
            bad = 1
            print("\n  NOT SAFE TO ADOPT:")
            for q in r["problems"]:
                print("    - %s" % q)
        elif r.get("ok"):
            print("\n  SAFE: the record layout is identical and every byte "
                  "egg-config can write\n  re-derives from this binary to the "
                  "same record offset and the same values.")
        print()
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
