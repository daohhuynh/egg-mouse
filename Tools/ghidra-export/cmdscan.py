"""Every instruction in .text that STORES an immediate whose low byte is a
report id (0xA0/0xA1) to memory. Whole section, not the vendor band.

objdump prints these signed -- movb $0xa1 shows as $-0x5f -- which is exactly how
a text scan for "$0xa1" misses them. Normalise before comparing.
"""
import json,re,subprocess,sys,bisect,os
# The repo root, derived rather than hardcoded: this was an absolute path
# under one developer's home, which leaked the account name and made the
# script unrunnable for anyone else.
R=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
tag=sys.argv[1]
LINE=re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)*[0-9a-f]{2})\s*\t(.*)$')
recs=[json.loads(l) for l in open(f'{R}/.analysis/export/{tag}.jsonl')]
iv=sorted((int(r['entry'],16),int(r['entry'],16)+r['size'],r['entry']) for r in recs if r['size'])
st=[x[0] for x in iv]
def own(a):
    i=bisect.bisect_right(st,a)-1
    return iv[i][2] if i>=0 and iv[i][0]<=a<iv[i][1] else None
lo,hi=iv[0][0],max(x[1] for x in iv)
out=subprocess.run([f'{R}/Tools/ghidra-export/dis.sh',tag,hex(lo),hex(hi+64)],
                   capture_output=True,text=True).stdout
MEMDST=re.compile(r'^(mov[blwq]?)\s+\$(-?0x[0-9a-f]+|-?\d+),\s*(-?0x[0-9a-f]+\(%\w+\)|\(%\w+\)|-?\d+\(%\w+\))')
hits=[]
for l in out.splitlines():
    m=LINE.match(l)
    if not m: continue
    txt=m.group(3).replace('\t',' ')
    mm=MEMDST.match(txt)
    if not mm: continue
    mn,imm,dst=mm.groups()
    v=int(imm,16) if imm.startswith(('0x','-0x')) else int(imm)
    width={'movb':1,'movw':2,'movl':4,'mov':4}.get(mn,4)
    v&=(1<<(8*width))-1
    if (v&0xFF) in (0xA0,0xA1):
        hits.append((int(m.group(1),16),mn,hex(v),dst,own(int(m.group(1),16))))
print("%s: %d instructions in ALL of .text store an immediate with low byte 0xA0/0xA1 to memory\n"%(tag,len(hits)))
for a,mn,v,dst,o in sorted(hits):
    print("   %#010x  %-5s %-10s -> %-18s  in %s"%(a,mn,v,dst,o))
