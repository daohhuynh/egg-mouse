#!/usr/bin/env python3
"""dlgref.py <tag> [<tag>...] -- for every RT_DIALOG in a binary, how many
times its id appears as an instruction immediate, and in which function.

WHY. dlgdump.py answers "what does the vendor offer its user"; this answers
"does any code reach it". A dialog template with no reference is a resource the
linker kept and the program cannot show -- a different fact about the product
than a dialog that is merely never seen in practice, and the two must not be
confused.

TWO INDEPENDENT METHODS, because engineering-rules.md 1.2a requires the encoding forms a
negative was searched under to be stated:

  A. objdump immediates. The whole file is disassembled (every section, not
     just .text) and the AT&T immediate `$0xNN` is matched. This sees EVERY
     encoding at once -- `6a 66` push imm8, `68 id` push imm32, `c7 /r id`,
     `b8+r id`, `81 /r id`, the sign-extended `83 /r ib` alu forms -- because
     the disassembler has already normalised them to a printed value. That is
     the whole reason it is used in preference to a byte-pattern scan: the
     first attempt at this question scanned for `68 <id32>` and reported ZERO
     references for dialog 102, whose id is pushed as the two-byte `6a 66`.
     What it cannot see: a value assembled arithmetically, and any region where
     a linear sweep desynchronises.

  B. raw little-endian scan of every section for the id as a dword and as a
     word, with the containing section named. This sees data tables, which
     method A cannot, and it over-reports wildly for small ids -- 0x66 is the
     letter 'f'. It is a floor on where to look, never a count of references.

Neither method is conclusive alone and both are printed. A dialog with 0 in A
and hits only in .rdata/.data string blobs in B is UNREFERENCED BY CODE; that
phrasing is the strongest the evidence supports.
"""
import os, re, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsrc
from litscan import TAGS, sections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.environ.get("DLGREF_CACHE", "/tmp/dlgref-cache")


def disasm(tag):
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, tag + ".full.dis")
    if not os.path.exists(p) or os.path.getsize(p) == 0:
        with open(p, "w") as f:
            subprocess.run(["objdump", "-D", os.path.join(ROOT, TAGS[tag])],
                           stdout=f, stderr=subprocess.DEVNULL, check=False)
    return open(p, errors="replace").read().splitlines()


def dialogs(d):
    base, secs = rsrc.sections(d)
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    rrva = struct.unpack_from("<I", d, pe + 24 + opt - 128 + 16)[0]
    rd = rsrc.rva2off(secs, base, rrva)
    out = []
    rsrc.walk(d, secs, base, rd, rd, 0, [], out)
    return sorted({p[1] for p, _, _, fo in out if p[0] == 5 and fo is not None})


def main():
    tags = [a for a in sys.argv[1:] if not a.startswith("--")]
    for tag in tags:
        d = rsrc.load(tag)
        base, secs = sections(d)
        lines = disasm(tag)
        print(f"\n########## {tag}")
        for did in dialogs(tag and d):
            pat = re.compile(r"\$0x%x(?![0-9a-fA-F])" % did)
            hits = [l for l in lines if pat.search(l)]
            # method A, split: an id used as a resource id is PUSHED or MOVED,
            # never the operand of a sub/and/cmp on esp or a flag byte.
            push = [l for l in hits if re.search(r"\b(push|mov)", l)]
            raw = []
            for nm, va, vsz, ro, rsz in secs:
                blob = d[ro:ro + rsz]
                nd = blob.count(struct.pack("<I", did))
                nw = blob.count(struct.pack("<H", did))
                if nd or nw:
                    raw.append(f"{nm}:{nd}d/{nw}w")
            print(f"  DIALOG {did:<6} A: {len(hits):>3} imm sites "
                  f"({len(push)} push/mov)   B: {' '.join(raw) or '-'}")
            for l in push[:12]:
                print("        ", l.strip())


if __name__ == "__main__":
    main()
