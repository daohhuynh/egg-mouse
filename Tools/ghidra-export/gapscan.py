#!/usr/bin/env python3
"""Account for every BYTE of .text, not every function Ghidra found.

  gapscan.py <tag> [...]

WHY THIS EXISTS (2026-09-03). coverage.py proves a partition over the function
list Ghidra produced. That is a partition of *what Ghidra found*, not of the
code. Ghidra missed cfg107 0x00413f90 entirely -- the Factory Reset button's
handler -- so it was in no evidence class, appeared in no residue, and was
invisible to a tool whose whole job is to make invisibility impossible. About
10% of .text in every one of these binaries lies outside every known function
body.

Counting functions cannot detect a missed function. Counting bytes can.

Classes, and note that only the last one is a hole:
  KNOWN   inside a function body Ghidra reported
  CALLED  outside those, but is the target of a direct E8/E9 -- definitely code,
          definitely a function start, and definitely missed by Ghidra
  PTR     outside those, but the RELOCATION TABLE proves some absolute address
          there points into .text. This is how MFC message maps and C++ vtables
          name their handlers, and it is the class 0x00413f90 belongs to -- an
          E8 scan alone cannot see it, which is why an earlier version of this
          tool reported it as absent. Address-taken code is still code.

AUTHORITATIVE SOURCES ONLY (the point of the rewrite, 2026-09-03). The first
version of this class used a heuristic: scan every 4-aligned dword in the file
and take those landing in .text. Checked against .reloc, that heuristic MISSED
752 code pointers in fw110, 839 in cfg107 and 745 in fw104. A heuristic cannot
support a completeness claim, and the earlier one silently understated the hole.
Everything here now comes from structures the OS LOADER depends on, so the
linker cannot have omitted an entry:
  .reloc         every 32-bit absolute address in the image (HIGHLOW)
  export table   every externally reachable entry point
  TLS directory  every callback that runs BEFORE the entry point
  LoadConfig     the SEH handler table, i.e. every valid exception handler
  entry point    from the optional header
These are ground truth. Ghidra's function list is not.
  PAD     0xCC / 0x90 / 0x00 filler between bodies
  DARK    none of the above. Bytes nothing accounts for.

Structure only. It classifies bytes by provenance and holds no opinion about
what any of them mean (engineering-rules.md 7.1).

STATUS: FINDING-AID for the DARK class, EVIDENCE for the rest (2026-09-04).

The KNOWN/CALLED/PTR/PAD figures are a byte partition and stand as evidence.
The DARK figure does NOT stand on its own: it is "bytes this tool could not
account for", which is a statement about the tool. What those bytes actually are
is settled by darkclass.py, which re-disassembles from each function's own start
and resolves every DARK run in all six PE binaries to a known function's tail or
an instruction interior. Cite darkclass.py for that, not the DARK percentage.

Also note KNOWN is computed from Ghidra's function extents, and those extents are
SHORT for 29-71 functions per binary (microread.py measures the overshoot), so
KNOWN slightly understates and DARK slightly overstates. The direction is safe
but the numbers are not exact.
"""
import sys, os, json, bisect, collections, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from litscan import TAGS, sections
from coverage import load, AN

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

def text_of(d, secs):
    for nm, va, vsz, ro, rsz in secs:
        if nm == '.text':
            return va, vsz, ro, rsz
    raise SystemExit('no .text')

def _dirs(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    return pe + 24 + (96 if magic == 0x10b else 112)

def authoritative_text_pointers(d, base, secs, va, n):
    """Every address in .text that a loader-critical structure names.

    Sources are structures the Windows loader itself consumes, so they cannot
    be incomplete without the binary failing to run. See the module docstring.
    """
    def off(rva):
        rva += base
        for nm, v, vsz, ro, rsz in secs:
            if v <= rva < v + max(vsz, rsz):
                k = rva - v
                return ro + k if k < rsz else None
        return None
    out = set()
    dd = _dirs(d)

    # entry point
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    ep = struct.unpack_from('<I', d, pe + 24 + 16)[0]
    if ep: out.add(base + ep)

    # .reloc HIGHLOW -- every 32-bit absolute address in the image
    rva, sz = struct.unpack_from('<II', d, dd + 8 * 5)
    if rva:
        p = off(rva); end = p + sz if p else 0
        while p and p < end:
            pgrva, blk = struct.unpack_from('<II', d, p)
            if blk < 8: break
            for i in range((blk - 8) // 2):
                e = struct.unpack_from('<H', d, p + 8 + 2 * i)[0]
                if (e >> 12) != 3: continue
                fo = off(pgrva + (e & 0xfff))
                if fo is None or fo + 4 > len(d): continue
                v = int.from_bytes(d[fo:fo + 4], 'little')
                if va <= v < va + n: out.add(v)
            p += blk

    # exports
    rva, sz = struct.unpack_from('<II', d, dd + 8 * 0)
    if rva:
        e = off(rva)
        if e:
            nFunc = struct.unpack_from('<I', d, e + 20)[0]
            fo = off(struct.unpack_from('<I', d, e + 28)[0])
            for i in range(nFunc):
                v = base + struct.unpack_from('<I', d, fo + 4 * i)[0]
                if va <= v < va + n: out.add(v)

    # TLS callbacks -- run before the entry point
    rva, sz = struct.unpack_from('<II', d, dd + 8 * 9)
    if rva:
        t = off(rva)
        if t:
            cb = struct.unpack_from('<I', d, t + 12)[0]
            co = off(cb - base) if cb else None
            while co:
                v = struct.unpack_from('<I', d, co)[0]
                if not v: break
                if va <= v < va + n: out.add(v)
                co += 4

    # LoadConfig SEH handler table -- every valid exception handler
    rva, sz = struct.unpack_from('<II', d, dd + 8 * 10)
    if rva:
        lc = off(rva)
        if lc and sz >= 72:
            seh = struct.unpack_from('<I', d, lc + 64)[0]
            cnt = struct.unpack_from('<I', d, lc + 68)[0]
            so = off(seh - base) if seh else None
            if so and cnt < 100000:
                for i in range(cnt):
                    v = base + struct.unpack_from('<I', d, so + 4 * i)[0]
                    if va <= v < va + n: out.add(v)
    return out

def analyse(tag):
    d = open(os.path.join(ROOT, TAGS[tag]), 'rb').read()
    base, secs = sections(d)
    va, vsz, ro, rsz = text_of(d, secs)
    n = min(vsz, rsz)
    recs = load(tag)

    mark = bytearray(n)          # 0 dark, 1 known, 2 called, 3 pad
    for r in recs:
        s = int(r['entry'], 16); e = s + r['size']
        for i in range(max(0, s - va), min(n, e - va)):
            mark[i] = 1

    # direct call/jmp targets anywhere in .text
    targets = set()
    for off in range(n - 5):
        b = d[ro + off]
        if b not in (0xE8, 0xE9):
            continue
        rel = int.from_bytes(d[ro + off + 1:ro + off + 5], 'little', signed=True)
        dst = va + off + 5 + rel
        if va <= dst < va + n:
            targets.add(dst)
    ptrs = set(authoritative_text_pointers(d, base, secs, va, n))
    newstarts = sorted(t for t in targets if mark[t - va] == 0)
    ptrstarts = sorted(p for p in ptrs if p not in targets)

    # extend each recovered start to the next marked byte or a terminator run
    known = sorted(set([int(r['entry'], 16) for r in recs] + newstarts))
    for s in newstarts:
        i = s - va
        while i < n and mark[i] == 0:
            mark[i] = 2
            i += 1
    for s in ptrstarts:
        i = s - va
        while i < n and mark[i] == 0:
            mark[i] = 4
            i += 1

    for i in range(n):
        if mark[i] == 0 and d[ro + i] in (0xCC, 0x90, 0x00):
            mark[i] = 3

    cnt = collections.Counter(mark)
    dark = []
    i = 0
    while i < n:
        if mark[i] == 0:
            j = i
            while j < n and mark[j] == 0:
                j += 1
            dark.append((va + i, va + j, j - i))
            i = j
        else:
            i += 1
    dark.sort(key=lambda x: -x[2])
    print(f"=== {tag}: .text {n} bytes, {len(recs)} functions Ghidra found")
    for k, lbl in ((1, 'KNOWN '), (2, 'CALLED'), (4, 'PTR   '), (3, 'PAD   ')):
        print(f"    {lbl} {cnt[k]:>9}  {100*cnt[k]/n:6.2f}%")
    print(f"    DARK   {cnt[0]:>9}  {100*cnt[0]/n:6.2f}%   in {len(dark)} runs")
    print(f"    recovered starts Ghidra missed: {len(newstarts)} by call target, "
          f"{len(ptrstarts)} by address-taken pointer")
    if dark[:5]:
        print("    largest dark runs:")
        for s, e, ln in dark[:5]:
            print(f"      {s:#010x}-{e:#010x}  {ln}")
    json.dump({'text_bytes': n,
               'KNOWN_pct':  round(100*cnt[1]/n, 4),
               'CALLED_pct': round(100*cnt[2]/n, 4),
               'PTR_pct':    round(100*cnt[4]/n, 4) if len(cnt) > 4 else None,
               'PAD_pct':    round(100*cnt[3]/n, 4),
               'DARK_pct':   round(100*cnt[0]/n, 4),
               'n_dark_runs': len(dark),
               'missed_starts': [hex(a) for a in newstarts],
               'ptr_starts': [hex(a) for a in ptrstarts],
               'dark': [[hex(s), hex(e), l] for s, e, l in dark]},
              open(os.path.join(AN, f'gapscan_{tag}.json'), 'w'))
    return newstarts, ptrstarts

if __name__ == '__main__':
    for t in sys.argv[1:]:
        analyse(t); print()
