#!/usr/bin/env python3
"""mkbatches.py <outdir> <tag> [<tag>...] — cut a reading queue into agent batches.

Input per tag is `.analysis/<queue>_<tag>.json`, a JSON list of function entry
addresses (default queue name `unique`, override with --queue). Output is
`<outdir>/batch_NNN.json`, each a JSON array of

    {"id", "size", "ghidra_name", "tag", "disasm": [<objdump lines>]}

METHOD, and the two things it must not get wrong:

  1. `.text` is disassembled ONCE per tag over the span the queue covers, and
     each function is sliced out by [entry, entry+size). A linear sweep desyncs
     silently on embedded jump tables (notes/updater-protocol.md 6.2.2), so a
     slice is a reading aid and must never settle a boundary question. Anything
     that would change a byte we send gets re-disassembled from its own start.

  2. objdump's byte column is space-separated but NOT space-terminated at full
     width, so a regex that assumes a fixed column silently truncates the widest
     instructions. LINE below matches the byte run explicitly; a truncation here
     would hand readers half an instruction and they would summarise it anyway.

Batches are bounded by BOTH line count and function count, because one 29 KB
function and forty 90-byte ones both have to fit in an agent's context.

This records structure only. It encodes no belief about what any function means.
"""
import bisect, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dis.sh")
LINE = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)*[0-9a-f]{2})\s*\t(.*)$')
LINECAP = 2600
FNCAP = 45


def batches_for(tag, queue):
    ents = {}
    with open(f"{ROOT}/.analysis/export/{tag}.jsonl") as fh:
        for line in fh:
            r = json.loads(line)
            ents[int(r["entry"], 16)] = r
    want = [int(a, 16) for a in json.load(open(f"{ROOT}/.analysis/{queue}_{tag}.json"))]
    lo, hi = min(want), max(a + ents[a]["size"] for a in want)
    p = subprocess.run([DIS, tag, hex(lo), hex(hi)], capture_output=True, text=True)
    if p.returncode:
        sys.exit(f"{tag}: dis.sh failed: {p.stderr[:300]}")
    lines = {}
    for ln in p.stdout.splitlines():
        m = LINE.match(ln)
        if m:
            a = int(m.group(1), 16)
            lines[a] = "%08x  %s" % (a, m.group(3).rstrip())
    keys = sorted(lines)
    out = []
    for a in sorted(want):
        sz = ents[a]["size"]
        i = bisect.bisect_left(keys, a)
        j = bisect.bisect_left(keys, a + sz)
        out.append({"id": "0x%08x" % a, "size": sz, "ghidra_name": ents[a]["name"],
                    "tag": tag, "disasm": [lines[k] for k in keys[i:j]]})
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    queue = "unique"
    for a in sys.argv[1:]:
        if a.startswith("--queue="):
            queue = a.split("=", 1)[1]
    if len(args) < 2:
        sys.exit(__doc__)
    outdir, tags = args[0], args[1:]
    os.makedirs(outdir, exist_ok=True)
    fns = []
    for t in tags:
        fns += batches_for(t, queue)
    batches, cur, curl = [], [], 0
    for f in fns:
        n = len(f["disasm"])
        if cur and (curl + n > LINECAP or len(cur) >= FNCAP):
            batches.append(cur)
            cur, curl = [], 0
        cur.append(f)
        curl += n
    if cur:
        batches.append(cur)
    for i, b in enumerate(batches):
        json.dump(b, open(f"{outdir}/batch_{i:03d}.json", "w"))
    empty = sum(1 for b in batches for f in b if not f["disasm"])
    print(f"{outdir}: {len(batches)} batches, {len(fns)} functions, "
          f"{sum(len(f['disasm']) for b in batches for f in b)} lines, "
          f"{empty} with empty disassembly")
    if empty:
        sys.exit("REFUSING: a function with no decoded instructions means the "
                 "slice failed; fix that before spending readers on it.")


if __name__ == "__main__":
    main()
