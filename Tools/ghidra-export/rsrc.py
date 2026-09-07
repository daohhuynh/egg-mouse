#!/usr/bin/env python3
"""Enumerate every PE resource in a vendor binary.

Belief-free by construction (engineering-rules.md 7.1). It walks the resource directory
and reports what is there: type, name, language, RVA, size, SHA-256, Shannon
entropy and a byte preview. It does not score, rank, filter, or label any
resource as "the firmware". Which resource matters is a conclusion to be
derived and cited, not something this tool asserts.

  rsrc.py <tag> [<tag>...]                    census
  rsrc.py --extract <tag> <type> <name> <out> write one resource to a file
"""
import sys, os, struct, hashlib, math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from litscan import TAGS, sections

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

# Standard RT_* names. Presentation only -- a numeric id is always also shown.
RT = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',
      7:'FONTDIR',8:'FONT',9:'ACCELERATOR',10:'RCDATA',11:'MESSAGETABLE',
      12:'GROUP_CURSOR',14:'GROUP_ICON',16:'VERSION',17:'DLGINCLUDE',
      19:'PLUGPLAY',20:'VXD',21:'ANICURSOR',22:'ANIICON',23:'HTML',
      24:'MANIFEST'}

def load(tag):
    return open(os.path.join(ROOT, TAGS[tag]), 'rb').read()

def rva2off(secs, base, rva):
    """rva is image-relative; litscan.sections() reports absolute VAs."""
    rva += base
    for nm, va, vsz, ro, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            o = rva - va
            return ro + o if o < rsz else None
    return None

def entropy(b):
    if not b: return 0.0
    c = [0]*256
    for x in b: c[x] += 1
    n = len(b)
    return -sum((k/n)*math.log2(k/n) for k in c if k)

def walk(d, secs, base, rd_off, off, level, path, out):
    """rd_off = file offset of resource dir root; off = this dir's offset."""
    nnamed, nid = struct.unpack_from('<HH', d, off + 12)
    for i in range(nnamed + nid):
        e = off + 16 + 8*i
        nameval, offval = struct.unpack_from('<II', d, e)
        if nameval & 0x80000000:
            so = rd_off + (nameval & 0x7fffffff)
            ln = struct.unpack_from('<H', d, so)[0]
            nm = d[so+2:so+2+2*ln].decode('utf-16-le', 'replace')
        else:
            nm = nameval
        if offval & 0x80000000:
            walk(d, secs, base, rd_off, rd_off + (offval & 0x7fffffff),
                 level+1, path + [nm], out)
        else:
            do = rd_off + offval
            drva, dsz = struct.unpack_from('<II', d, do)
            fo = rva2off(secs, base, drva)
            out.append((path + [nm], drva, dsz, fo))

def census(tag):
    d = load(tag); base, secs = sections(d)
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    ddoff = pe + 24 + (96 if magic == 0x10b else 112)
    rrva, rsize = struct.unpack_from('<II', d, ddoff + 8*2)   # entry 2 = resource
    print(f"### {tag}: {TAGS[tag]}")
    if not rrva:
        print("  no resource directory"); return []
    ro = rva2off(secs, base, rrva)
    out = []
    walk(d, secs, base, ro, ro, 0, [], out)
    out.sort(key=lambda r: (str(r[0][0]), str(r[0][1] if len(r[0])>1 else '')))
    print(f"  resource dir RVA {rrva:#x} size {rsize:#x}, {len(out)} leaves")
    print(f"  {'type':<14}{'name':<10}{'lang':<6}{'rva':>10}{'size':>10}"
          f"{'ent':>6}  sha256[:16]      first bytes")
    for path, drva, dsz, fo in out:
        t = path[0]
        tn = f"{RT.get(t,t)}({t})" if isinstance(t, int) else str(t)
        nm = path[1] if len(path) > 1 else ''
        lg = path[2] if len(path) > 2 else ''
        if fo is None or fo + dsz > len(d):
            print(f"  {tn:<14}{str(nm):<10}{str(lg):<6}{drva:>10x}{dsz:>10}"
                  f"   ----  <not in file>")
            continue
        b = d[fo:fo+dsz]
        h = hashlib.sha256(b).hexdigest()[:16]
        print(f"  {tn:<14}{str(nm):<10}{str(lg):<6}{drva:>10x}{dsz:>10}"
              f"{entropy(b):>6.2f}  {h}  {b[:12].hex(' ')}")
    print()
    return out

def extract(tag, typ, name, outp):
    d = load(tag); base, secs = sections(d)
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    ddoff = pe + 24 + (96 if magic == 0x10b else 112)
    rrva, _ = struct.unpack_from('<II', d, ddoff + 8*2)
    ro = rva2off(secs, base, rrva)
    out = []
    walk(d, secs, base, ro, ro, 0, [], out)
    for path, drva, dsz, fo in out:
        if str(path[0]) == typ and str(path[1]) == name:
            open(outp, 'wb').write(d[fo:fo+dsz])
            print(f"{tag} {typ}/{name}: {dsz} bytes -> {outp}")
            return 0
    print("not found", file=sys.stderr); return 1

if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--extract':
        sys.exit(extract(a[1], a[2], a[3], a[4]))
    for t in a: census(t)
