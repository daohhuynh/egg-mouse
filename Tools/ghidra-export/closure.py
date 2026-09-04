#!/usr/bin/env python3
"""closure.py [--edges DIR] — device-facing seed and upward closure, per binary.

Answers one question: which functions can reach the device, and which functions
can reach those? Structure only; it holds no belief about what any of them mean.

METHOD. Raw bytes and raw call edges. Ghidra supplies function BOUNDARIES only,
used as intervals, never as edges — CLAUDE.md 1.2b, after its call-graph field
reported cfg107 0x404720 uncalled when 0x413faf is a direct `calll` to it.

  Seed, part 1: exhaustive 4-byte scan of every byte of .text for the address
  of any HID/SetupAPI entry-point slot. TWO kinds of slot, and both must be in
  the set:

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

  Seed, part 2 -- THE HID IMPORTS ARE NOT THE ONLY ROUTE TO THE DEVICE.
  Added 2026-09-04, after part 1 alone was found to miss a live read channel.
  The config tools ship a vendor-patched hidapi whose read/write/feature paths
  go through kernel32 on the device handle, not through HidD_*:

      hid_write             WriteFile
      hid_read_timeout      ReadFile / CancelIo
      hid_get_feature_report DeviceIoControl(0xB0192 = IOCTL_HID_GET_FEATURE)
      hid_close             CancelIo

  A HID-only seed cannot see any of them, so cfg107's poll thread 0x412c60 --
  which reads 8-byte input reports off the device every 80 ms -- sat outside the
  closure while the function that starts it sat inside. Part 2 therefore also
  seeds on kernel32 handle-I/O slots, CONFINED TO THE HIDAPI SPAN (the address
  range between the lowest and highest part-1 seed member). The confinement is
  what keeps the CRT's _read/_write and MFC's CFile out; they are generic file
  I/O on handles that never come from HID enumeration, and every one of them
  lands outside the span in all seven binaries.

  Stated blind spot: a device-I/O call sited outside BOTH the hidapi span and
  the HID-only closure would not be seeded by part 2. Every rejected owner is
  printed per binary (`devio_out_of_span`) so the residue is enumerated rather
  than assumed away.

  A SECOND HAZARD, recorded because it is not fixed here and cannot be:
  a function whose address is only ever TAKEN -- a thread proc, a callback
  stored to a .data slot -- has no incoming call edge, so no call-graph closure
  in either direction can reach it. 0x412c60 is reachable here only because it
  CALLS a seeded function. Its sibling 0x412c40 (the event callback, stored to
  *0x57f298 and invoked indirectly) has no such edge and is invisible to this
  method. The `addrtaken_devio_not_in_closure` field audits for the dangerous
  case -- an address-taken device-I/O function absent from the closure -- and
  is expected empty; `addrtaken_devio_out_of_span` carries the CRT/ATL file
  classes, which are address-taken because they sit in vtables.

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
# kernel32 entry points that can perform I/O on an already-open device handle.
# Seeded only inside the hidapi span -- see "Seed, part 2" above.
KIO = ("KERNEL32.dll!DeviceIoControl", "KERNEL32.dll!ReadFile",
       "KERNEL32.dll!WriteFile", "KERNEL32.dll!CancelIo",
       "KERNEL32.dll!GetOverlappedResult")

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

    def upward(start):
        c, fr = set(start), list(start)
        while fr:
            for site, o, kind, sync in callers.get(_n(fr.pop()), []):
                if o and _n(o) not in c:
                    c.add(_n(o)); fr.append(_n(o))
        return c

    # ---- seed part 2: kernel32 handle I/O on a device handle ----
    # Two passes, because one is not enough. Pass A closes over the HID-only
    # seed. A kernel32 handle-I/O function then counts as device I/O if it is
    # inside the hidapi span OR already in pass A's closure; the second arm is
    # what catches the XM1r's 0x643b80, which does ReadFile, sits BELOW the
    # span, and is reached by a call edge from a seeded function. Everything
    # else is generic file I/O and is enumerated, not discarded silently.
    clo_a = upward(seed)
    kslots = {int(sl, 16): n for sl, n in d["imports"].items() if n in KIO}
    span_lo = min(int(a, 16) for a in seed)
    span_hi = max(int(a, 16) for a in seed)
    kseed, kresidue = {}, {}
    for j in range(len(tb) - 3):
        v = struct.unpack_from("<I", tb, j)[0]
        if v not in kslots:
            continue
        o = own(tlo + j)
        if o is None or o in orph:
            continue
        devicey = (span_lo <= int(o, 16) <= span_hi) or o in clo_a
        (kseed if devicey else kresidue).setdefault(o, set()).add(kslots[v].split("!")[1])
    seed |= set(kseed)
    clo = upward(seed)

    # ---- audit: address-taken functions that touch a device-I/O slot ----
    # A function whose address is only TAKEN has no incoming call edge, so no
    # closure in either direction reaches it. This reports the case that would
    # matter: one that is address-taken, references a device-I/O slot, and is
    # nevertheless absent from the closure. Expected empty; if it is ever not,
    # the closure is understated and the method needs a third seed part.
    entries = {int(r["entry"], 16) for r in recs if r["size"]}
    taken = set()
    for j in range(len(raw) - 3):
        v = struct.unpack_from("<I", raw, j)[0]
        if v in entries:
            taken.add("0x%x" % v)
    # In-span is the alarm: an address-taken hidapi-band function outside the
    # closure would mean a device path with no call edge into it at all.
    addrtaken_gap = sorted((taken & (seed | set(kseed))) - clo,
                           key=lambda x: int(x, 16))
    # Out-of-span is informational and must be ENUMERATED, not summarised away
    # (CLAUDE.md 1.2a). In all seven binaries it is the CRT/ATL file classes,
    # whose handles come from CreateFile* on filesystem paths.
    addrtaken_outofspan = sorted(taken & set(kresidue), key=lambda x: int(x, 16))

    thunks, hits = thunk_route(raw, base, secs, hid_slots, own)
    lo, hi = min(int(a, 16) for a in clo), max(int(a, 16) for a in clo)
    b = BAND.get(tag)
    return {"tag": tag, "n_static_slots": len(stat), "n_dynamic_slots": len(dyn),
            "hidapi_span": [hex(span_lo), hex(span_hi)],
            "seed_kernel_devio": {k: sorted(v) for k, v in
                                  sorted(kseed.items(), key=lambda x: int(x[0], 16))},
            "devio_out_of_span": {k: sorted(v) for k, v in
                                  sorted(kresidue.items(), key=lambda x: int(x[0], 16))},
            "addrtaken_devio_not_in_closure": addrtaken_gap,
            "addrtaken_devio_out_of_span": addrtaken_outofspan,
            "seed": sorted(seed, key=lambda x: int(x, 16)),
            "closure": sorted(clo, key=lambda x: int(x, 16)),
            "span": [hex(lo), hex(hi)],
            "orphan_unit_slot_refs": sorted(orphan_refs),
            "n_hid_thunks": len(thunks), "calls_into_thunk": hits,
            "in_band": (sum(1 for a in clo if b[0] <= int(a, 16) <= b[1]) if b else None)}


# A planted positive, per CLAUDE.md 6.2: a harness that cannot produce a bad
# result is not evidence. These are the functions that caused the retracted
# factory-reset conclusion -- 0x413f90 is the Factory Reset button's handler and
# 0x404720 is the A1 13 sender it calls. NEITHER EXISTS IN GHIDRA'S EXPORT
# (0x413f90 is reachable only through an MFC message map, so no function node was
# ever created and 0x404720's exported `callers` field is empty). Any closure
# method built on that export misses both and concludes, wrongly, that the config
# tool has no factory reset. This method finds them by attributing call sites to
# orphan units, so if it ever stops finding them the method has regressed to the
# one that was wrong.
SELFTEST = {"cfg107": ["0x413f90", "0x404720", "0x4035f0",
                       # added 2026-09-04: the input-report read path. A
                       # HID-only seed misses both -- 0x4031c0 reaches the
                       # device through kernel32 ReadFile, and 0x412c60 is the
                       # 80 ms poll thread that calls it. If either drops out,
                       # seed part 2 has regressed.
                       "0x4031c0", "0x412c60"]}


def selftest(out):
    bad = []
    for tag, musts in SELFTEST.items():
        if tag not in out:
            continue
        have = set(out[tag]["closure"])
        for m in musts:
            if m not in have:
                bad.append(f"{tag}: {m} missing from closure")
    return bad


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
        if r["seed_kernel_devio"]:
            print("        + kernel32 device I/O (hidapi span %s, or in the HID-only closure): %s"
                  % ("-".join(r["hidapi_span"]),
                     ", ".join("%s(%s)" % (k, "/".join(v))
                               for k, v in r["seed_kernel_devio"].items())))
        if r["addrtaken_devio_not_in_closure"]:
            print("        !! ADDRESS-TAKEN device-I/O function OUTSIDE the closure: %s"
                  % r["addrtaken_devio_not_in_closure"])
        if r["devio_out_of_span"]:
            print("        out-of-span handle I/O (generic file I/O, not the device): %s"
                  % ", ".join("%s(%s)" % (k, "/".join(v))
                              for k, v in r["devio_out_of_span"].items()))
        if r["n_hid_thunks"]:
            print("        HID/SetupAPI thunks %d, calls into them %d"
                  % (r["n_hid_thunks"], len(r["calls_into_thunk"])))
    bad = selftest(out)
    if bad:
        print("\nSELFTEST FAILED -- this method has regressed to the one that was wrong:")
        for b in bad:
            print("   ", b)
        sys.exit(1)
    print("selftest: cfg107 closure contains the message-map-only Factory Reset "
          "handler 0x413f90 and its A1 13 sender 0x404720 (both absent from "
          "Ghidra's export), and the kernel32-route input-report path "
          "0x4031c0 / 0x412c60 -- OK")
    p = os.path.join(ROOT, ".analysis", "device_closure_rawedges.json")
    json.dump(out, open(p, "w"), indent=1)
    print("->", p)


if __name__ == "__main__":
    main()
