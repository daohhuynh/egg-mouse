#!/usr/bin/env python3
"""Gate: every Endgame binary's function list stays fully accounted for.

`Tools/ghidra-export/coverage.py` has always been able to fail -- it exits 1 on
a residue -- and until 2026-09-06 NOTHING RAN IT. CLAUDE.md 6 says to
regenerate coverage numbers rather than quote them, and the reason is exactly
what running it after a long gap turned up: updater 1.06 and 1.07 each had a
residue of 7 and 3 functions resting on a Ghidra FID name alone, while
`working-memory.md` said in prose that only `xm1r` had any residue at all.
Nothing had regressed; the prose had simply stopped being true and no gate
noticed. A gate that is not run is a gate that is open (the same sentence is in
CMakeLists.txt above `audit_claims_strict`, for the same reason).

Two properties, both regenerated here, never read out of a file:

  1. UNACCOUNTED == 0.  total - (R|S|M|N|I) is empty, computed in one place.
  2. "resting on Ghidra's name ALONE" == 0.  CLAUDE.md 1.2b: a FunctionID name
     is one signature collision away from a guess, so a function whose ONLY
     evidence is that name is not accounted for in any sense this project
     accepts.  Property 1 alone would pass with N carrying thousands.

`xm1r` is deliberately excluded and the exclusion is asserted, not assumed: it
is the earlier product LINE (XM1r, 2022), it appears in no row of CLAUDE.md
6.1's table, and its residue is a known, stated deferral rather than a
regression.  Asserting that it still HAS a residue means this list cannot
quietly grow to cover a binary that has stopped being checked.
"""
import os, re, subprocess, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COV  = os.path.join(ROOT, "Tools/ghidra-export/coverage.py")

# `.analysis/` is gitignored -- it is Ghidra output, regenerated per machine,
# not source. On a clone that has not run the export yet there is nothing to
# check, and a red test would say "coverage regressed" when it means "this
# machine has no exports". Skip, loudly, with the command that fixes it.
HAVE_EXPORTS = os.path.isdir(os.path.join(ROOT, ".analysis", "export"))
WHY_SKIPPED  = (".analysis/export is absent -- run Tools/ghidra-export/"
                "export_functions.py first. Nothing is being checked here.")

# Every Endgame binary this project reasons from. Firmware updaters first.
ENDGAME = ("fw110", "fw107", "fw106", "fw104",
           "cfg107", "cfg104", "cfg101", "cfg100")


def run(tag):
    p = subprocess.run([sys.executable, COV, tag], cwd=ROOT,
                       capture_output=True, text=True)
    out = p.stdout + p.stderr
    m = re.search(r"UNACCOUNTED \.+ (\d+)", out)
    n = re.search(r"name ALONE \.+ (\d+)", out)
    if not m or not n:
        raise AssertionError(f"coverage.py {tag} printed neither number:\n{out}")
    return int(m.group(1)), int(n.group(1)), p.returncode, out


@unittest.skipUnless(HAVE_EXPORTS, WHY_SKIPPED)
class EveryEndgameBinaryIsAccountedFor(unittest.TestCase):
    def test_no_residue_and_no_name_only_evidence(self):
        for tag in ENDGAME:
            with self.subTest(tag=tag):
                resid, alone, rc, out = run(tag)
                self.assertEqual(resid, 0,
                                 f"{tag} has {resid} unaccounted functions\n{out}")
                self.assertEqual(alone, 0,
                                 f"{tag} has {alone} functions whose only "
                                 f"evidence is a Ghidra FID name\n{out}")
                self.assertEqual(rc, 0, f"coverage.py {tag} exited {rc}")


@unittest.skipUnless(HAVE_EXPORTS, WHY_SKIPPED)
class TheExclusionOfXm1rIsStillDeliberate(unittest.TestCase):
    """If xm1r ever reaches zero, that is good news and this test must be
    updated to fold it into ENDGAME -- not left passing by omission."""

    def test_xm1r_residue_is_a_known_deferral(self):
        resid, alone, rc, _ = run("xm1r")
        self.assertGreater(
            resid + alone, 0,
            "xm1r now has no residue. Move it into ENDGAME and delete this "
            "test rather than leaving a binary silently unchecked.")


@unittest.skipUnless(
    os.path.exists(os.path.join(
        ROOT, "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe")),
    "the vendor .exe files are not present (they are gitignored)")
class IdenticalTextIsProvedBeforeItIsUsed(unittest.TestCase):
    """Evidence set I lets 1.10's reads cover 1.06 and 1.07. That is only sound
    while the three really do share a .text, so check the premise directly off
    the files instead of trusting the tool that depends on it."""

    def test_the_three_updaters_share_one_text_section(self):
        sys.path.insert(0, os.path.join(ROOT, "Tools/ghidra-export"))
        from coverage import text_of                      # noqa: E402
        seen = {t: text_of(t) for t in ("fw110", "fw107", "fw106")}
        for t, v in seen.items():
            self.assertIsNotNone(v, f"no .text recovered for {t}")
        hashes = {t: v[0] for t, v in seen.items()}
        vas    = {t: v[1] for t, v in seen.items()}
        self.assertEqual(len(set(hashes.values())), 1,
                         f".text SHA-256 differs across updaters: {hashes}")
        self.assertEqual(len(set(vas.values())), 1,
                         f".text load VA differs across updaters: {vas}")

    def test_1_04_is_not_swept_in_by_it(self):
        """1.04 is a SECOND CODE BASE (CLAUDE.md 6.1). If it ever hashed the
        same as 1.10 the whole per-binary standard would be wrong, so say so
        here rather than discovering it through a coverage number."""
        sys.path.insert(0, os.path.join(ROOT, "Tools/ghidra-export"))
        from coverage import text_of                      # noqa: E402
        self.assertNotEqual(text_of("fw104")[0], text_of("fw110")[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
