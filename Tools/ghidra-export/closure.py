#!/usr/bin/env python3
"""closure.py [--edges DIR] — device-facing seed and upward closure, per binary.

Answers one question: which functions can reach the device, and which functions
can reach those? Structure only; it holds no belief about what any of them mean.

METHOD. Raw bytes and raw call edges. Ghidra supplies function BOUNDARIES only,
used as intervals, never as edges — CLAUDE.md 1.2b, after its call-graph field
reported cfg107 0x404720 uncalled when 0x413faf is a direct `calll` to it.

  Seed: exhaustive 4-byte scan of every byte of .text for the address of any
  HID/SetupAPI entry-point slot. TWO kinds of slot, and both must be in the set:

    * static IAT slots, from the import directory;
    * GetProcAddress-filled .data slots, found by locating each HidD_/HidP_
      name string, finding the .text site that pushes its address, and taking
      the following `a3` (mov %eax,<abs>) store.

  Keying the dynamic search on the NAME STRING is load-bearing: a
  shape-matched regex found 1 of cfg100's 11. Omitting dynamic slots entirely
  is what once put the config tools' seed at 7 when it is 13.

  A function is in the seed if its body contains such a reference. That
  deliberately includes the resolver, which STORES to the slots without ever
  calling through them; a call-through-only criterion gives 12, not 13.

  Closure: upward over call edges recovered from raw bytes by calledges.py.
  `jmp` edges are included — a tail call is a call, and excluding them
  understated the config-tool closures by 7-8 each.

TWO TRAPS THIS TOOL EXISTS TO AVOID, both of which produced published errors:

  1. The IAT THUNK BLOCK. Unattributed .text holding `jmp *<slot>` stubs gets
     lumped into a single orphan unit that references EVERY import, so it looks
     like one function touching all of them. It is not a function. Seed members
     that are orphan units are excluded and reported separately.
     A function could still reach an import as `call <thunk>`, which no closure
     here would see, so --thunks checks that route directly. As of 2026-09-04
     it is dead in all seven binaries: the XM1r has all 14 HID/SetupAPI thunks
     and ZERO calls into any of them; nothing else has such thunks at all.

  2. ADDRESS KEY FORMAT. calledges.py emits hex(int) unpadded; the function
     export uses 0x%08x. Mixing them makes every caller lookup miss silently
     and the closure collapses to exactly the seed — which looks plausible.
     Everything is normalised through _n() below.

Emits member LISTS, not counts. The artifact this replaces stored counts alone,
which is why an eight-function discrepancy in it could not be audited.
"""
import bisect, json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import calledges as CE

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAGS = ("fw110", "fw104", "cfg107", "cfg104", "cfg101", "cfg100", "xm1r")
BAND = {"fw110": (0x401000, 0x4040AD),
        "fw104": (0x401BC0, 0x405200),
        "xm1r":  (0x643B80, 0x64D000)}
HIDPFX = ("!HidD_", "!HidP_", "!SetupDi")

_n = lambda a: hex(int(a, 16))          # see trap 2 above


def sections(raw):
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<H", raw, pe + 6)[0]
    opt = struct.unpack_from("<H", raw, pe + 20)[0]
    base = struct.unpack_from("<I", raw, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        nm = raw[o:o + 8].rstrip(b"\0").decode(errors="replace")
        vs, va, rs, ro = struct.unpack_from("<IIII", raw, o + 8)
        out.append((nm, va, vs, ro, rs))
    return base, out


def dynamic_slots(raw, base, secs):
    """HID entry points resolved at runtime, keyed on the name string."""
    import re
    names = {}
    for nm, va, vs, ro, rs in secs:
        b = raw[ro:ro + rs]
        for m in re.finditer(rb"(HidD_|HidP_)[A-Za-z0-9_]{2,40}\x00", b):
            names[base + va + m.start()] = m.group(0)[:-1].decode()
    text = [s for s in secs if s[0] == ".text"][0]
    _, tva, _, tro, trs = text
    tb = raw[tro:tro + trs]
    slots = {}
    for j in range(len(tb) - 5):
        if tb[j] != 0x68:                                  # push imm32
            continue
        p = struct.unpack_from("<I", tb, j + 1)[0]
        if p not in names:
            continue
        for k in range(j + 5, min(j + 40, len(tb) - 5)):
            if tb[k] == 0xA3:                              # mov %eax,<abs>
                slots["0x%x" % struct.unpack_from("<I", tb, k + 1)[0]] = names[p]
                break
    return slots


def thunk_route(raw, base, secs, hid_slots, own):
    """Is anything reaching an import via `call <thunk>`? Returns (thunks, hits)."""
    text = [s for s in secs if s[0] == ".text"][0]
    _, tva, _, tro, trs = text
    tb = raw[tro:tro + trs]
    tlo = base + tva
    thunks = {}
    for j in range(len(tb) - 6):
        if tb[j] == 0xFF and tb[j + 1] == 0x25:
            slot = "0x%x" % struct.unpack_from("<I", tb, j + 2)[0]
            if slot in hid_slots:
                thunks[tlo + j] = hid_slots[slot]
    hits = []
    for j in range(len(tb) - 5):
        if tb[j] != 0xE8:
            continue
        t = tlo + j + 5 + struct.unpack_from("<i", tb, j + 1)[0]
        if t in thunks:
            hits.append((hex(tlo + j), own(tlo + j), thunks[t]))
    return thunks, hits


def analyse(tag, edir):
    d = json.load(open(os.path.join(edir, f"{tag}_edges.json")))
    raw = open(os.path.join(ROOT, CE.TAGS[tag]), "rb").read()
    base, secs = sections(raw)

    stat = {s: n for s, n in d["imports"].items() if any(p in n for p in HIDPFX)}
    dyn = dynamic_slots(raw, base, secs)
    hid_slots = dict(stat); hid_slots.update(dyn)
    slotvals = {int(s, 16) for s in hid_slots}

    recs = [json.loads(l) for l in open(os.path.join(ROOT, ".analysis", "export", f"{tag}.jsonl"))]
    iv = [(int(r["entry"], 16), int(r["entry"], 16) + r["size"], _n(r["entry"]))
          for r in recs if r["size"]]
    orph = {_n(a) for a, b in d["orphan_units"]}
    ov = [(int(a, 16), int(b, 16), _n(a)) for a, b in d["orphan_units"]]
    allv = sorted(iv + ov)
    starts = [x[0] for x in allv]

    def own(va):
        i = bisect.bisect_right(starts, va) - 1
        return allv[i][2] if i >= 0 and allv[i][0] <= va < allv[i][1] else None

    text = [s for s in secs if s[0] == ".text"][0]
    _, tva, _, tro, trs = text
    tb = raw[tro:tro + trs]
    tlo = base + tva
    seed, orphan_refs = set(), set()
    for j in range(len(tb) - 3):
        if struct.unpack_from("<I", tb, j)[0] in slotvals:
            o = own(tlo + j)
            if o is None:
                continue
            (orphan_refs if o in orph else seed).add(o)

    callers = d["callers"]
    clo, fr = set(seed), list(seed)
    while fr:
        for site, o, kind, sync in callers.get(_n(fr.pop()), []):
            if o and _n(o) not in clo:
                clo.add(_n(o)); fr.append(_n(o))

    thunks, hits = thunk_route(raw, base, secs, hid_slots, own)
    lo, hi = min(int(a, 16) for a in clo), max(int(a, 16) for a in clo)
    b = BAND.get(tag)
    return {"tag": tag, "n_static_slots": len(stat), "n_dynamic_slots": len(dyn),
            "seed": sorted(seed, key=lambda x: int(x, 16)),
            "closure": sorted(clo, key=lambda x: int(x, 16)),
            "span": [hex(lo), hex(hi)],
            "orphan_unit_slot_refs": sorted(orphan_refs),
            "n_hid_thunks": len(thunks), "calls_into_thunk": hits,
            "in_band": (sum(1 for a in clo if b[0] <= int(a, 16) <= b[1]) if b else None)}


def main():
    edir = (sys.argv[sys.argv.index("--edges") + 1] if "--edges" in sys.argv
            else os.path.join(ROOT, ".analysis", "edges"))
    if not os.path.isdir(edir):
        sys.exit(f"no edge dir {edir}; run: calledges.py <tag> --out {edir}/<tag>_edges.json")
    out = {}
    for tag in TAGS:
        r = analyse(tag, edir)
        out[tag] = r
        print("%-7s slots %2d static + %2d dynamic | seed %2d | closure %2d %s"
              % (tag, r["n_static_slots"], r["n_dynamic_slots"], len(r["seed"]),
                 len(r["closure"]),
                 "(in band %d/%d)" % (r["in_band"], len(r["closure"]))
                 if r["in_band"] is not None else ""))
        if r["orphan_unit_slot_refs"]:
            print("        orphan-unit slot refs (IAT thunk block, NOT functions): %s"
                  % r["orphan_unit_slot_refs"])
        if r["n_hid_thunks"]:
            print("        HID/SetupAPI thunks %d, calls into them %d"
                  % (r["n_hid_thunks"], len(r["calls_into_thunk"])))
    p = os.path.join(ROOT, ".analysis", "device_closure_rawedges.json")
    json.dump(out, open(p, "w"), indent=1)
    print("->", p)


if __name__ == "__main__":
    main()
