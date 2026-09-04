#!/usr/bin/env python3
"""microread.py <tag> [--json PATH] — decide small functions MECHANICALLY.

CLAUDE.md 6.1 (amended) requires every function of updater 1.10 to be read,
library code included. 8,640 of fw110's 9,076 were unread. Sending all of them
to a language model is the least reliable way to do it: most are three
instructions long, an LLM adds no information over the decoder, and every extra
judgement call is another chance to be confidently wrong (6.2).

So this tool absorbs everything that a machine can settle, and the residue --
smaller, and genuinely needing judgement -- is what a reader is spent on.

METHOD
  1. objdump disassembles .text once, linearly. Function starts are checked
     against the sweep's instruction boundaries; any that desynced (19 of 9,076
     in fw110) are re-disassembled individually from their own start, which
     resyncs by construction.
  2. A function's instructions must EXACTLY TILE its body [entry, entry+size).
     Not "mostly" -- a byte left over means the decode is not trusted and the
     function goes to the reading queue regardless of how obvious it looks.
  3. The tiled instruction sequence is matched against templates below. A match
     yields a complete statement of what the function does, derived from the
     bytes, with nothing left unexplained.

WHAT COUNTS AS SETTLED. Only patterns whose semantics are total: every
instruction accounted for, no branch to anywhere the summary does not name. A
template that explains four instructions out of five settles nothing, and this
tool must never say it does -- a partial explanation presented as a whole one is
exactly the failure this project keeps hitting.

This encodes no belief about the protocol: it names instruction shapes, not
meanings (CLAUDE.md 7.1). "Returns constant 0x5444b4" is a fact about bytes.
Whether 0x5444b4 matters is not this tool's business.
"""
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dis.sh")
# The byte column is space-separated but NOT space-terminated when it is full
# width: a 10-byte instruction prints "c7 80 b8 0e 00 00 01 00 00 00\tmovl ...".
# Requiring a trailing space after every pair silently drops exactly the longest
# instructions, which is how 353 function bodies first looked untileable.
LINE = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)*[0-9a-f]{2})\s*\t(.*)$')


def sweep(tag, lo, hi):
    out = subprocess.run([DIS, tag, hex(lo), hex(hi)], capture_output=True,
                         text=True).stdout
    ins = {}
    for line in out.splitlines():
        m = LINE.match(line)
        if m:
            a = int(m.group(1), 16)
            ins[a] = (len(m.group(2).split()), m.group(3).strip())
    return ins


def tile(ins, entry, size):
    """Instructions covering [entry, entry+size).

    Returns (seq, overshoot). overshoot > 0 means the final instruction runs
    past the function's declared end -- i.e. Ghidra's `size` is SHORT, cutting
    the last instruction in half. That is a metadata error, not a decode
    failure, and it is worth separating: 35 fw110 functions are short by 1-4
    bytes, one of them 0x401330, the flasher's own GetFeature wrapper. Anything
    computed from Ghidra's extents -- classify.py's body hashes, gapscan's KNOWN
    class -- is slightly wrong for exactly these.

    Returns (None, 0) when the stream genuinely will not decode.
    """
    seq, a, end = [], entry, entry + size
    while a < end:
        if a not in ins:
            return None, 0
        n, txt = ins[a]
        seq.append((a, txt))
        a += n
    return seq, a - end


IMM = r'\$(-?0x[0-9a-f]+|-?\d+)'

# Mnemonics whose effect is fully stated by naming operands. Deliberately small:
# anything not here sends the function to a reader rather than being guessed at.
PLAIN = {
    "movl", "movb", "movw", "movzbl", "movzwl", "movsbl", "movswl", "leal",
    "addl", "subl", "andl", "orl", "xorl", "incl", "decl", "negl", "notl",
    "shll", "shrl", "sarl", "testl", "testb", "cmpl", "cmpb", "cmpw",
    "pushl", "popl", "sete", "setne", "setg", "setl", "setge", "setle",
    "seta", "setb", "setae", "setbe", "cltd", "cwtl", "nop", "xchgl",
}
XFER = ("jmp", "jmpl", "calll", "call", "ret", "retl")
JCC = re.compile(r'^j(?!mp)[a-z]+\s')


def explain_one(txt, imports):
    """One instruction -> a plain statement, or None if it needs judgement."""
    mn = txt.split("\t")[0].split()[0]
    ops = txt.split("\t", 1)[1].strip() if "\t" in txt else ""
    ops = re.sub(r'\s*#.*$', "", ops).strip()
    ops = re.sub(r'\s*<[^>]*>', "", ops).strip()
    if mn in PLAIN:
        return f"{mn} {ops}".strip()
    if mn in ("ret", "retl"):
        return f"return{(' (pops ' + ops + ')') if ops else ''}"
    if mn in ("jmp", "jmpl"):
        m = re.match(r'^\*(0x[0-9a-f]+)', ops)
        if m:
            return f"tail-call through {imports.get(m.group(1), '*'+m.group(1))}"
        m = re.match(r'^(0x[0-9a-f]+)', ops)
        if m:
            return f"tail-call {m.group(1)}"
        return None                      # jmp *%reg -- target unknown, needs a reader
    if mn in ("calll", "call"):
        m = re.match(r'^\*(0x[0-9a-f]+)', ops)
        if m:
            return f"call {imports.get(m.group(1), '*'+m.group(1))}"
        m = re.match(r'^(0x[0-9a-f]+)', ops)
        if m:
            return f"call {m.group(1)}"
        return None                      # call *%reg -- indirect, needs a reader
    return None


def classify(seq, imports):
    """A complete statement, or None if the function needs judgement.

    SETTLED requires all three:
      * every instruction individually explainable from the whitelist above;
      * no conditional branch anywhere -- straight-line code only, because
        "every instruction explained" is NOT the same as "the function is
        understood" once there is control flow;
      * every transfer of control names a concrete target.
    A function with a loop, a jcc, or an indirect jmp/call is not settled here
    however obvious it looks. Partial explanation presented as complete is the
    failure mode this whole tool exists to avoid.
    """
    parts = []
    for k, (a, txt) in enumerate(seq):
        if JCC.match(txt):
            return None
        mn = txt.split("\t")[0].split()[0]
        if mn in ("jmp", "jmpl", "ret", "retl") and k != len(seq) - 1:
            return None                  # an early exit means branching structure
        e = explain_one(txt, imports)
        if e is None:
            return None
        parts.append(e)
    return "; ".join(parts)


def main():
    tag = sys.argv[1]
    recs = [json.loads(l) for l in
            open(os.path.join(ROOT, ".analysis", "export", f"{tag}.jsonl"))]
    sized = [r for r in recs if r["size"]]
    lo = min(int(r["entry"], 16) for r in sized)
    hi = max(int(r["entry"], 16) + r["size"] for r in sized)
    ins = sweep(tag, lo, hi + 16)

    edges = os.path.join(ROOT, ".analysis", "edges", f"{tag}_edges.json")
    imports = json.load(open(edges))["imports"] if os.path.exists(edges) else {}

    settled, needs, untiled, short = {}, [], [], {}
    resync = 0
    for r in sized:
        e, n = int(r["entry"], 16), r["size"]
        seq, over = tile(ins, e, n)
        if seq is None:                      # desynced; re-disassemble from the start
            resync += 1
            seq, over = tile(sweep(tag, e, e + n + 16), e, n)
        if seq is None:
            untiled.append(r["entry"])
            continue
        if over:
            short[r["entry"]] = over
        v = classify(seq, imports)
        if v:
            settled[r["entry"]] = v
        else:
            needs.append({"entry": r["entry"], "size": n, "name": r["name"],
                          "n_ins": len(seq)})

    print(f"=== {tag}: {len(sized)} sized functions")
    print(f"  re-disassembled after a sweep desync ... {resync}")
    print(f"  SETTLED mechanically .................. {len(settled)}")
    print(f"  needs a reader ........................ {len(needs)}")
    print(f"  could not be tiled (decode not trusted) {len(untiled)}")
    if short:
        print(f"  GHIDRA SIZE TOO SHORT ................. {len(short)}"
              f"  (last instruction overshoots by {min(short.values())}-"
              f"{max(short.values())} bytes)")
        for a in sorted(short, key=lambda x: int(x, 16))[:6]:
            print(f"      {a} short by {short[a]}")
    kinds = {}
    for v in settled.values():
        toks = [p.strip().split()[0] for p in v.split(";")]
        k = " ".join(toks[:4]) + (" ..." if len(toks) > 4 else "")
        kinds[k] = kinds.get(k, 0) + 1
    for k, c in sorted(kinds.items(), key=lambda x: -x[1])[:12]:
        print(f"      {k:<52} {c:>5}")
    tb = sum(r["size"] for r in sized)
    sb = sum(next(x["size"] for x in sized if x["entry"] == a) for a in settled)
    print(f"  bytes: {sb} settled of {tb} ({100*sb/tb:.1f}%)")
    if "--json" in sys.argv:
        p = sys.argv[sys.argv.index("--json") + 1]
        json.dump({"tag": tag, "settled": settled, "needs_reader": needs,
                   "untiled": untiled, "ghidra_size_short": short},
                  open(p, "w"), indent=1)
        print("->", p)


if __name__ == "__main__":
    main()
