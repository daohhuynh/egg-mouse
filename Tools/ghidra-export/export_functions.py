#!/usr/bin/env python3
"""
Export every function from an already-analysed Ghidra program to JSONL.

Purpose: record STRUCTURE. It deliberately holds no opinion about what any
structure means -- no relevance score, no keyword list, no pattern for any
particular byte, opcode, command shape or constant. See engineering-rules.md 7.1: the
previous extraction script was deleted because its scoring carried a prior
session's conclusions inside it.

Per function it emits: entry address, name, symbol source, body size, the
address range it occupies, callers, callees, every scalar operand the function
mentions, every data address it references together with any string or defined
value at that address, the raw bytes of the body, and the decompiled C.

Usage:
  export_functions.py <project_dir> <project_name> <program_path_in_project> <out.jsonl>
"""
import json, sys, os

import jpype
import pyghidra
pyghidra.start(install_dir=os.environ.get("GHIDRA_INSTALL_DIR"))

from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.symbol import RefType


def main(proj_dir, proj_name, prog_path, out_path):
    from ghidra.base.project import GhidraProject
    proj = GhidraProject.openProject(proj_dir, proj_name, True)
    prog = proj.openProgram("/", prog_path.lstrip("/"), True)
    try:
        run(prog, out_path)
    finally:
        proj.close(prog)
        proj.close()


def run(prog, out_path):
    monitor = ConsoleTaskMonitor()
    fm = prog.getFunctionManager()
    listing = prog.getListing()
    mem = prog.getMemory()
    ref_mgr = prog.getReferenceManager()

    dec = DecompInterface()
    opts = DecompileOptions()
    dec.setOptions(opts)
    dec.openProgram(prog)

    funcs = list(fm.getFunctions(True))
    total = len(funcs)
    sys.stderr.write("functions: %d\n" % total)

    def str_at(addr):
        """Return the defined string at addr, if the listing has one."""
        d = listing.getDataAt(addr)
        if d is None:
            return None
        try:
            if d.hasStringValue():
                return str(d.getValue())
        except Exception:
            pass
        return None

    def defined_at(addr):
        d = listing.getDataAt(addr)
        if d is None:
            return None
        try:
            return {"type": str(d.getDataType().getName()), "repr": str(d.getDefaultValueRepresentation())[:200]}
        except Exception:
            return None

    with open(out_path, "w") as out:
        for i, f in enumerate(funcs):
            if i % 500 == 0:
                sys.stderr.write("  %d/%d\n" % (i, total))
            entry = f.getEntryPoint()
            sym = f.getSymbol()
            rec = {
                "entry": "0x%s" % entry,
                "name": f.getName(),
                "sig": str(f.getSignature()),
                "source": str(sym.getSource()) if sym is not None else None,
                "is_thunk": bool(f.isThunk()),
                "is_external": bool(f.isExternal()),
                "namespace": str(f.getParentNamespace().getName(True)),
                "size": int(f.getBody().getNumAddresses()),
                "body": [[ "0x%s" % r.getMinAddress(), "0x%s" % r.getMaxAddress() ]
                          for r in f.getBody()],
            }
            if f.isThunk():
                t = f.getThunkedFunction(True)
                rec["thunks_to"] = t.getName() if t else None

            # ---- call graph ----
            rec["callees"] = sorted({ "0x%s|%s" % (c.getEntryPoint(), c.getName())
                                      for c in f.getCalledFunctions(monitor) })
            rec["callers"] = sorted({ "0x%s|%s" % (c.getEntryPoint(), c.getName())
                                      for c in f.getCallingFunctions(monitor) })

            # ---- instruction-level facts ----
            scalars = {}
            data_refs = {}
            mnemonics = {}
            ninstr = 0
            for ins in listing.getInstructions(f.getBody(), True):
                ninstr += 1
                m = ins.getMnemonicString()
                mnemonics[m] = mnemonics.get(m, 0) + 1
                for opi in range(ins.getNumOperands()):
                    for obj in ins.getOpObjects(opi):
                        cn = type(obj).__name__
                        if cn == "Scalar":
                            v = obj.getUnsignedValue()
                            k = "0x%x" % v
                            scalars[k] = scalars.get(k, 0) + 1
                for r in ins.getReferencesFrom():
                    if r.getReferenceType().isData():
                        ta = r.getToAddress()
                        key = "0x%s" % ta
                        if key not in data_refs:
                            e = {"addr": key, "reftype": str(r.getReferenceType())}
                            s = str_at(ta)
                            if s is not None:
                                e["string"] = s[:400]
                            else:
                                dv = defined_at(ta)
                                if dv:
                                    e["data"] = dv
                            symb = prog.getSymbolTable().getPrimarySymbol(ta)
                            if symb is not None:
                                e["sym"] = str(symb.getName())
                            data_refs[key] = e
            rec["ninstr"] = ninstr
            rec["scalars"] = scalars
            rec["mnemonics"] = mnemonics
            rec["data_refs"] = list(data_refs.values())

            # ---- raw bytes of the entry block (for cross-version hashing) ----
            try:
                minA = f.getBody().getMinAddress()
                n = min(rec["size"], 65536)
                jbuf = jpype.JArray(jpype.JByte)(n)
                got = mem.getBytes(minA, jbuf)
                rec["bytes_hex"] = bytes(
                    (int(b) & 0xff) for b in jbuf[:got]).hex()
            except Exception as e:
                rec["bytes_hex"] = None
                rec["bytes_err"] = str(e)

            # ---- decompilation ----
            try:
                res = dec.decompileFunction(f, 120, monitor)
                if res.decompileCompleted():
                    rec["c"] = res.getDecompiledFunction().getC()
                else:
                    rec["c"] = None
                    rec["c_err"] = str(res.getErrorMessage())
            except Exception as e:
                rec["c"] = None
                rec["c_err"] = "exception: %s" % e

            out.write(json.dumps(rec) + "\n")

    dec.dispose()
    sys.stderr.write("done: %d functions -> %s\n" % (total, out_path))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
