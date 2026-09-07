"""Tests/test_crossrefs.py -- every file-qualified section reference resolves.

CLAUDE.md §1.2 makes a citation load-bearing: a [D] claim is only as good as the
thing a reader can go and check. A reference that points at no section, or at
two different sections, is an uncheckable citation wearing the costume of a
checkable one.

WHAT THIS FOUND WHEN IT WAS WRITTEN, 2026-09-07:

  - Two references to `config-protocol.md` §2.4. That file has had no §2.4 since
    §2 was reorganised; both meant the A1 13 factory-reset finding.
  - `config-protocol.md` numbered THREE sections twice -- 7.3, 7.4 and 7.5 each
    existed as a `###` command finding and as a `##` record-map section. Both
    meanings were live in shipping source: Sources/egg-config/main.cpp cited
    "§7.4" for the A1 13 frame builder, Sources/EGGCore/include/egg/Protocol.h
    cited "§7.4" for the 0x01-0x04 zeroing. Following either citation from the
    other file lands on the wrong section.
  - `bootloader-observed.md` had two §4s, two §5s and two §6s, because a second
    document was appended with its own numbering.

SCOPE, STATED SO THE NEGATIVE IS HONEST (CLAUDE.md §1.2a). This checks
references that NAME THEIR FILE -- "`notes/foo.md` §7.4", "foo.md section 4". It
does NOT check bare "§7.4", because a bare reference in a notes file may mean
that file, CLAUDE.md, or the file under discussion, and no rule distinguishes
them. So a bare reference to a section that does not exist still passes here.
Making bare references checkable would mean adopting a convention this repo does
not have; qualifying the reference is the cheaper fix and is what the two
prediction files got.

It also does not check that the section a reference resolves to is the RIGHT
one. Nothing mechanical can. What it removes is the case where the reader cannot
even tell which section was meant.
"""
import os
import re
import unittest
import collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Files whose headings define section numbers, and which may carry references.
def markdown_files():
    out = {}
    notes = os.path.join(ROOT, "notes")
    if os.path.isdir(notes):
        for f in sorted(os.listdir(notes)):
            # zzz-planted-*.md are the deliberately-defective fixtures
            # Tests/test_auditclaims.py writes into the real notes/ tree to
            # measure itself (.gitignore line 61). They are SUPPOSED to contain
            # a citation that resolves to nothing, and one may be on disk while
            # that test is mid-run, so excluding them is required rather than
            # tidy -- otherwise this test fails at random during a parallel
            # ctest and the failure looks like a real dangling reference.
            if f.endswith(".md") and not f.startswith("zzz-planted-"):
                out[f] = os.path.join(notes, f)
    for f in ("README.md", "CLAUDE.md", "working-memory.md", "CAPTURE-STEPS.md"):
        p = os.path.join(ROOT, f)
        if os.path.exists(p):
            out[f] = p
    return out


# A heading DEFINES a section number when the number is followed by whitespace.
# "### §7.12's prediction, scored" does not define §7.12 -- it discusses it --
# and treating it as a definition made §7.12 look duplicated when it was not.
# The number may be followed by "." or "," before the whitespace: this repo
# writes "## 5. CLOSED ...", "## 7.3 The settings record ..." and
# "## §7.13, the button-action menu". It may NOT be followed by an apostrophe --
# "### §7.12's prediction, scored" DISCUSSES §7.12 and does not define it, and
# counting it as a definition made §7.12 look duplicated when it is not.
HEADING = re.compile(
    r"^#{1,6}[ \t]+§?(\d+[a-z]?(?:\.\d+[a-z]?)*)[.,]?(?=[ \t]|$)", re.M)

# "`notes/foo.md` §7.4", "foo.md §7.4", "foo.md section 4". The stem may carry
# dots (prediction-capture-1.10.md), which an earlier version of this regex got
# wrong and reported as three missing files.
REFERENCE = re.compile(
    r"`?([A-Za-z0-9_.-]+\.md)`?[ \t]*(?:§|[Ss]ection[ \t]+)(\d+[a-z]?(?:\.\d+[a-z]?)*)")


def headings_of(path):
    with open(path, encoding="utf-8") as f:
        return collections.Counter(HEADING.findall(f.read()))


class EveryQualifiedReferenceResolves(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = markdown_files()
        cls.heads = {name: headings_of(p) for name, p in cls.files.items()}
        cls.refs = []
        for name, p in sorted(cls.files.items()):
            with open(p, encoding="utf-8") as f:
                src = f.read()
            for m in REFERENCE.finditer(src):
                cls.refs.append((name, src[:m.start()].count("\n") + 1,
                                 os.path.basename(m.group(1)), m.group(2)))

    def test_the_scan_found_files_and_references(self):
        """A regex that matched nothing would make everything below vacuous --
        which is the failure mode this repo has shipped twice (working-memory
        'the mutation harness has produced a wrong result seven times')."""
        self.assertGreater(len(self.files), 10, "found %d markdown files"
                           % len(self.files))
        self.assertGreater(len(self.refs), 200,
                           "found %d file-qualified references; the repo had "
                           "438 when this test was written" % len(self.refs))
        self.assertGreater(sum(len(h) for h in self.heads.values()), 200)

    def test_every_reference_names_a_file_that_exists(self):
        bad = ["%s:%d -> %s §%s" % r for r in self.refs
               if r[2] not in self.heads]
        self.assertEqual([], bad, "references to markdown files that are not "
                                  "in notes/ or the repo root:\n  " +
                                  "\n  ".join(bad))

    def test_every_reference_resolves_to_a_section_that_exists(self):
        bad = []
        for src, line, tgt, sec in self.refs:
            if tgt in self.heads and self.heads[tgt][sec] == 0:
                bad.append("%s:%d -> %s §%s (no such heading)"
                           % (src, line, tgt, sec))
        self.assertEqual([], bad, "dangling section references:\n  " +
                                  "\n  ".join(bad))

    def test_no_reference_resolves_to_two_different_sections(self):
        bad = []
        for src, line, tgt, sec in self.refs:
            n = self.heads.get(tgt, {}).get(sec, 0)
            if n > 1:
                bad.append("%s:%d -> %s §%s (%d headings claim that number)"
                           % (src, line, tgt, sec, n))
        self.assertEqual(
            [], bad,
            "ambiguous section references. Either renumber the target's "
            "duplicate heading, or -- if the target is a pre-registration "
            "document that must not be edited -- reword the reference to name "
            "the block instead of its number:\n  " + "\n  ".join(bad))


if __name__ == "__main__":
    unittest.main(verbosity=2)
