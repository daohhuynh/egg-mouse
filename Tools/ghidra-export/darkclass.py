#!/usr/bin/env python3
"""darkclass.py — account for every DARK byte gapscan.py reports.

gapscan.py partitions .text into KNOWN/CALLED/PTR/PAD/DARK, where DARK is
"nothing accounts for these bytes". That residue is ~0.1% of .text (1,195 bytes
in 158 runs for fw110) and it is the only part of the code section with no
evidence attached, so it is exactly where something unknown could hide. This
settles what it is.

RESULT, all six PE binaries, zero exceptions: every DARK run is part of a
function Ghidra already knows about. ~80% lie past the function's declared end
(its extent is short); the rest are interior bytes of a real instruction. No
DARK run in any binary is code Ghidra missed entirely.

WITHOUT relying on the whole-.text linear sweep.

The sweep desyncs on data islands -- switch jump tables embedded in .text -- and
a desync is silent. Checked by hand: fw110 0x40b6cc is a real MSVC prologue
(8b ff / 55 / 8b ec) that the sweep straddles because the 11-entry jump table at
0x40b6a0 threw it out of phase. So a sweep disagreement is evidence about the
SWEEP, not about Ghidra.

Here each DARK run is judged by re-disassembling from the preceding function's
OWN start, which resyncs by construction.
"""
import json,bisect,re,subprocess,sys
R='<repo>'
LINE=re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)*[0-9a-f]{2})\s*\t(.*)$')
def dis(tag,a,b):
    out=subprocess.run([f'{R}/Tools/ghidra-export/dis.sh',tag,hex(a),hex(b)],
                       capture_output=True,text=True).stdout
    d={}
    for l in out.splitlines():
        m=LINE.match(l)
        if m: d[int(m.group(1),16)]=len(m.group(2).split())
    return d
print("%-7s %5s %8s %11s %9s %12s"%("tag","runs","covered","interior","after-end","UNEXPLAINED"))
for tag in ("fw110","fw104","cfg107","cfg104","cfg101","cfg100","xm1r"):
    recs=[json.loads(l) for l in open(f'{R}/.analysis/export/{tag}.jsonl')]
    iv=sorted((int(r['entry'],16),int(r['entry'],16)+r['size']) for r in recs if r['size'])
    st=[x[0] for x in iv]
    g=json.load(open(f'{R}/.analysis/gapscan_{tag}.json'))
    c={'covered':0,'interior':0,'after':0,'un':0}; bad=[]
    for s,e,l in g['dark']:
        s=int(s,16); e=int(e,16)
        i=bisect.bisect_right(st,s)-1
        if i<0: c['un']+=1; bad.append(hex(s)); continue
        fs,fe=iv[i]
        d=dis(tag,fs,max(e,fe)+24)          # from the function's own start
        p=fs; hit=None
        while p<e:
            if p not in d: break
            if p<=s<p+d[p]: hit=(p==s); break
            p+=d[p]
        if hit is True: c['covered' if s<fe else 'after']+=1
        elif hit is False: c['interior']+=1
        else: c['un']+=1; bad.append(hex(s))
    print("%-7s %5d %8d %11d %9d %12d %s"%(tag,len(g['dark']),c['covered'],
          c['interior'],c['after'],c['un'],(bad[:4] if bad else "")))
