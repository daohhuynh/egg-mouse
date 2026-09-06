#!/usr/bin/env python3
"""test_auditclaims.py -- can the citation audit still say "uncited"?

CLAUDE.md §6.2: "A harness that cannot produce a bad result is not evidence."

This test exists because of HOW the uncited count reached zero on 2026-09-06.
It went 87 -> 0, and only a handful of those were fixed by adding a citation to
a note. The rest were fixed by TEACHING THE TOOL that a resource id, a file
offset, a hash, a reproducible command, a verified quoted literal and a resolved
cross-reference are all citations -- which is a correction to an over-narrow
reading of §1.2, and is also exactly the move that would launder a real problem.

**A checker that was widened until it passed is worth nothing.** So every form
the tool now accepts gets a planted defect here: a claim that carries only that
form, with the form BROKEN, must still be reported. If a plant survives, the
widening went too far and this file says so.

The plants go in a temp copy of notes/; the real ones are never written to.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOL = os.path.join("Tools", "ghidra-export", "auditclaims.py")

FAILS = []


def ok(name, cond, detail=""):
    print(("  PASS  " if cond else "  FAIL  ") + name +
          (("  --  " + detail) if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


def run_with(extra_md):
    """Run the tool over the real notes plus one planted file.

    The planted file goes in the REAL notes directory under a temp name and is
    removed afterwards -- the tool resolves binaries and cross-references
    relative to the repo root, so a copied tree would change what it can see and
    the plants would be testing the copy rather than the checker.
    """
    name = "zzz-planted-%d.md" % os.getpid()
    path = os.path.join(ROOT, "notes", name)
    with open(path, "w") as f:
        f.write(extra_md)
    try:
        r = subprocess.run([sys.executable, TOOL], capture_output=True,
                           text=True, cwd=ROOT)
        return name, r.stdout + r.stderr
    finally:
        os.remove(path)


def planted_is_caught(label, body):
    name, out = run_with(body)
    caught = name in out
    ok(label, caught, "the tool did not report the planted claim\n" + out[-1500:])


def planted_is_accepted(label, body):
    name, out = run_with(body)
    ok(label, name not in out,
       "the tool reported a claim that IS properly cited\n" + out[-1500:])


def main():
    print("auditclaims.py -- planted defects")

    # --- The baseline: is the check awake at all? --------------------------
    planted_is_caught(
        "a bare [D] claim with no citation of any kind is reported",
        "# planted\n\n## 1. A claim\n\nThe device does a thing. [D]\n")

    # --- One plant per accepted citation form, each form BROKEN ------------
    # A hex number that is not an address, not a file offset, and not a hash.
    planted_is_caught(
        "a [D] claim whose only number is a constant, not an address",
        "# planted\n\n## 1. A claim\n\ncfg107 stores 0x80000000 there. [D]\n")

    # A resource-shaped word that is not a resource reference.
    planted_is_caught(
        "a [D] claim mentioning a dialog without naming one",
        "# planted\n\n## 1. A claim\n\nThe dialog is never shown in cfg107. [D]\n")

    # A quoted literal that is NOT in the binary. This is the form the tool
    # VERIFIES, so a fabricated quote must not buy a pass -- otherwise anyone
    # could silence the audit by inventing a plausible string.
    planted_is_caught(
        "a [D] claim quoting a literal that is not in the binary",
        "# planted\n\n## 1. A claim\n\nfw110 contains the string "
        "`NoSuchStringExistsAnywhereInThisBinary12345`. [D]\n")

    # A cross-reference to a section that does not exist.
    planted_is_caught(
        "a [D] claim citing a section that does not exist",
        "# planted\n\n## 1. A claim\n\nSee `updater-protocol.md` §99.99 — [D].\n")

    # A cross-reference to a real section that is ITSELF uncited. A chain of
    # deferrals is how a guess becomes a derivation by restatement.
    planted_is_caught(
        "a [D] claim citing a real but uncited section",
        "# planted\n\n## 1. A claim\n\nSee §2 — [D].\n\n"
        "## 2. The section it points at\n\nNo address anywhere in here.\n")

    # --- And the converse: the accepted forms must still be accepted -------
    # Without these, every plant above could be passing because the tool
    # reports everything.
    planted_is_accepted(
        "a real address in a named binary is accepted",
        "# planted\n\n## 1. A claim\n\ncfg107 serializes at 0x004042d0. [D]\n")
    planted_is_accepted(
        "a literal that really is in the binary is accepted",
        "# planted\n\n## 1. A claim\n\nfw110 carries "
        "`send firmware block data...` [D]\n")
    planted_is_accepted(
        "a resource reference is accepted",
        "# planted\n\n## 1. A claim\n\nfw110 ships FWFILE 140. [D]\n")
    planted_is_accepted(
        "a cross-reference to a cited section is accepted",
        "# planted\n\n## 1. A claim\n\nSee `updater-protocol.md` §2 — [D].\n")

    print("\n%s (%d failure%s)" % ("FAILED" if FAILS else "all passed",
                                   len(FAILS), "" if len(FAILS) == 1 else "s"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
