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
  PTR     outside those, but some 4-byte word ANYWHERE in the image points at
          it. This is how MFC message maps and C++ vtables name their handlers,
          and it is the class 0x00413f90 belongs to -- an E8 scan alone cannot
          see it, which is why an earlier version of this tool reported it as
          absent. Address-taken code is still code.
  PAD     0xCC / 0x90 / 0x00 filler between bodies
  DARK    none of the above. Bytes nothing accounts for.

Structure only. It classifies bytes by provenance and holds no opinion about
what any of them mean (CLAUDE.md 7.1).
"""
import sys, os, json, bisect, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from litscan import TAGS, sections
from coverage import load, AN

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

def text_of(d, secs):
    for nm, va, vsz, ro, rsz in secs:
        if nm == '.text':
            return va, vsz, ro, rsz
    raise SystemExit('no .text')

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
    # address-taken targets: any aligned dword in the whole image pointing into
    # .text at a byte no known body covers. Catches message-map and vtable
    # handlers, which no call-target scan can reach.
    ptrs = set()
    for off in range(0, len(d) - 4, 4):
        v = int.from_bytes(d[off:off + 4], 'little')
        if va <= v < va + n and mark[v - va] == 0:
            ptrs.add(v)
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
    json.dump({'missed_starts': [hex(a) for a in newstarts],
               'ptr_starts': [hex(a) for a in ptrstarts],
               'dark': [[hex(s), hex(e), l] for s, e, l in dark]},
              open(os.path.join(AN, f'gapscan_{tag}.json'), 'w'))
    return newstarts, ptrstarts

if __name__ == '__main__':
    for t in sys.argv[1:]:
        analyse(t); print()
