#!/usr/bin/env python3
"""Query the exported function records.

  qfunc.py <tag> <addr|name> [...]     print full records
  qfunc.py <tag> --range LO HI         list functions with entry in [LO,HI)
  qfunc.py <tag> --grep REGEX          list functions whose decompilation matches
  qfunc.py <tag> --list                list every function: entry, size, ns, name

<tag> is one of fw110 fw107 fw106 fw104 cfg107 cfg104 cfg101 cfg100.
Reads .analysis/export/<tag>.jsonl. Prints facts only; it ranks nothing.
"""
import json, sys, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def load(tag):
    p = os.path.join(ROOT, ".analysis", "export", f"{tag}.jsonl")
    return [json.loads(l) for l in open(p)]

def show(r, full=True):
    print("=" * 100)
    print(f"{r['entry']}  {r['name']}  size={r['size']} ninstr={r['ninstr']} ns={r['namespace']}")
    print(f"  sig: {r['sig']}")
    print(f"  source={r['source']} thunk={r['is_thunk']}"
          + (f" thunks_to={r.get('thunks_to')}" if r['is_thunk'] else ""))
    print(f"  CALLERS ({len(r['callers'])}): {r['callers']}")
    print(f"  CALLEES ({len(r['callees'])}): {r['callees']}")
    sc = sorted(r['scalars'].items(), key=lambda kv: (-kv[1], kv[0]))
    print(f"  SCALARS: {sc}")
    print(f"  MNEMONICS: {sorted(r['mnemonics'].items(), key=lambda kv:-kv[1])}")
    dr = [d for d in r['data_refs'] if not d['addr'].startswith('0xStack')]
    if dr:
        print("  DATA REFS:")
        for d in dr:
            print(f"    {d['addr']} {d['reftype']}"
                  + (f" sym={d.get('sym')}" if d.get('sym') else "")
                  + (f" STRING={d['string']!r}" if 'string' in d else "")
                  + (f" data={d['data']}" if 'data' in d else ""))
    if full:
        print(f"  BYTES: {r.get('bytes_hex')}")
        print("-" * 100)
        print(r.get('c') or f"<<no decompilation: {r.get('c_err')}>>")

def main():
    tag = sys.argv[1]; args = sys.argv[2:]
    recs = load(tag)
    by = {}
    for r in recs:
        by[r['entry']] = r
        by[r['entry'].replace('0x00', '0x')] = r
        by[r['name']] = r
    if args and args[0] == '--list':
        for r in sorted(recs, key=lambda x: int(x['entry'], 16)):
            print(f"{r['entry']} {r['size']:>6} {r['ninstr']:>6}  {r['namespace'][:28]:<28} {r['name']}")
    elif args and args[0] == '--range':
        lo, hi = int(args[1], 16), int(args[2], 16)
        for r in sorted(recs, key=lambda x: int(x['entry'], 16)):
            if lo <= int(r['entry'], 16) < hi:
                print(f"{r['entry']} {r['size']:>6} {r['ninstr']:>6}  {r['namespace'][:28]:<28} {r['name']}")
    elif args and args[0] == '--grep':
        rx = re.compile(args[1])
        for r in sorted(recs, key=lambda x: int(x['entry'], 16)):
            if rx.search(r.get('c') or '') or rx.search(json.dumps(r['data_refs'])):
                print(f"{r['entry']} {r['size']:>6}  {r['namespace'][:24]:<24} {r['name']}")
    else:
        for a in args:
            k = a if a in by else ('0x' + a.lstrip('0x').zfill(8))
            if k in by: show(by[k])
            else: print(f"!! not found: {a}")

main()
