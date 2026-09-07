"""Nothing that is COMMITTED may depend on something that is NOT.

WHY THIS EXISTS. On 2026-09-07 `CLAUDE.md` was added to `.gitignore`. Every
test still passed, because the file was sitting on the developer's disk where
it had always been. What had actually broken was invisible from here:

  * 225 of the 440 file-qualified cross-references in `notes/` point at
    `CLAUDE.md`, so a fresh clone's `test_crossrefs.py` fails outright; and
  * `Sources/EGGApp/UpdatesView.swift` located the repository root by looking
    for `CLAUDE.md`, so on a fresh clone the Updates screen would have said
    "not in a repository" forever, standing in a repository.

Neither is exotic. Both are the same mistake, and it is a mistake the whole
existing suite is structurally blind to: a test run in the developer's working
tree cannot see the difference between "this file is here" and "this file is
here BECAUSE IT IS COMMITTED". The tree is the union of what is tracked and
what is ignored, and only one of those halves is published.

So this file asks the one question the others cannot: take the set of files
git would hand a stranger, and check that it is self-sufficient.

WHAT IT CANNOT SEE (CLAUDE.md 1.2a -- state the blind spots):
  * a path assembled at runtime from pieces, e.g. os.path.join(D, name)
  * a path that is data rather than a literal -- in JSON, in a capture, in an
    argument the user supplies
  * a dependency on a DIRECTORY's contents rather than on a named file
  * anything outside the repo entirely (a Homebrew prefix, $HOME)
It sees literal, repo-root-relative filenames, which is the form both of the
2026-09-07 defects took.
"""
import os
import re
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Files that stopped being tracked on 2026-09-07. Named explicitly so this test
# means the same thing on a machine where they were never created at all.
UNTRACKED_NOW = ("CLAUDE.md", "working-memory.md", "log.txt",
                 "baseline-pre-upgrade.txt")

# OUTPUTS, not inputs. A tool that NAMES the file it is about to create is not
# depending on a file it does not have. `egg-config factory-reset` writes the
# pre-reset record here before it writes anything to the device (§4.1's undo).
WRITTEN_BY_THE_TOOLS = ("egg-before-reset.bin",)


def tracked():
    """Exactly what `git archive HEAD` would contain -- the stranger's view."""
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                         capture_output=True, text=True).stdout
    return {p for p in out.split("\0") if p}


def is_ignored(path):
    r = subprocess.run(["git", "check-ignore", "-q", path], cwd=ROOT)
    return r.returncode == 0


class TheTrackedSetIsSelfSufficient(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tracked = tracked()

    def setUp(self):
        if not os.path.isdir(os.path.join(ROOT, ".git")):
            self.skipTest("not a git checkout; there is no tracked set to check")

    def test_git_is_actually_answering(self):
        """A `git ls-files` that returned nothing would make everything below
        vacuously true -- the shape this repo has shipped more than once."""
        self.assertGreater(len(self.tracked), 100,
                           "git ls-files returned %d paths" % len(self.tracked))
        self.assertIn("CMakeLists.txt", self.tracked)

    def test_every_markdown_cross_reference_resolves_inside_the_tracked_set(self):
        """The `notes/` tree must be readable by someone who only has the repo.

        This is `test_crossrefs.py`'s check with the corpus cut down to what is
        actually published. That test passes in the working tree and fails in a
        fresh clone, which is precisely the gap.
        """
        import sys
        sys.path.insert(0, os.path.join(ROOT, "Tests"))
        import test_crossrefs

        names = {os.path.basename(p) for p in self.tracked if p.endswith(".md")}
        dangling = []
        for path in sorted(self.tracked):
            if not path.endswith(".md"):
                continue
            with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                src = f.read()
            for m in test_crossrefs.REFERENCE.finditer(src):
                target = os.path.basename(m.group(1))
                if target not in names:
                    line = src[:m.start()].count("\n") + 1
                    dangling.append("%s:%d -> %s §%s"
                                    % (path, line, target, m.group(2)))
        self.assertEqual([], dangling,
                         "%d reference(s) in COMMITTED files point at a file "
                         "that is not committed, so they dangle for everyone "
                         "but this machine:\n  %s"
                         % (len(dangling), "\n  ".join(dangling[:40])))

    def test_no_tracked_source_file_keys_off_an_untracked_root_file(self):
        """The `CLAUDE.md`-as-root-marker defect, made mechanical.

        Looks for string literals naming a file that sits at the repo root and
        is ignored. A marker that is not published is not a marker.
        """
        roots = {p for p in os.listdir(ROOT)
                 if os.path.isfile(os.path.join(ROOT, p)) and not p.startswith(".")}
        untracked_roots = {p for p in roots if p not in self.tracked}
        self.assertTrue(untracked_roots,
                        "this test is vacuous unless something at the root is "
                        "ignored; if that is genuinely true, delete it")

        # The candidate set must NOT come from os.listdir alone: that is
        # whatever happens to be lying in this developer's tree today, so the
        # test would mean something different on every machine. UNTRACKED_NOW
        # names the files that actually left the tracked set, so the regression
        # is caught even on a clone where they were never created.
        untracked_roots |= {n for n in UNTRACKED_NOW if n not in self.tracked}

        # ABSENT BY DESIGN, and the distinction is the whole point of the test.
        # These are Endgame Gear's proprietary Windows binaries. .gitignore has
        # excluded them since the initial commit -- they are the INPUT to this
        # project, not part of it, and publishing them is not ours to do. Every
        # tool that names one cannot run without the user supplying it, says so,
        # and exits; that is a documented precondition rather than a latent
        # breakage. notes/binaries.md pins the SHA-256 of each.
        #
        # A name goes on this list only when a stranger's clone is EXPECTED to
        # lack it. `CLAUDE.md` never qualified: nothing tells you it is missing,
        # and the failure is silent.
        expected_absent = {n for n in untracked_roots
                           if n.endswith(".exe") or n in WRITTEN_BY_THE_TOOLS}
        # GUARDED OPTIONAL USES. Each is wrapped in an existence check, handles
        # absence, and only narrows what gets cross-checked -- so it is not a
        # dependency. Listed as (file, name) pairs so a NEW unguarded use of the
        # same name still fails.
        guarded = {("Tests/test_crossrefs.py", "working-memory.md")}
        must_be_present = untracked_roots - expected_absent
        self.assertTrue(must_be_present,
                        "nothing at the root is both ignored and expected to "
                        "be there; if that is genuinely true, delete this test")

        exts = (".swift", ".cpp", ".h", ".py", ".sh", ".cmake", ".txt")
        offenders = []
        for path in sorted(self.tracked):
            if not path.endswith(exts):
                continue
            if path.startswith("Tests/test_freshclone"):
                continue
            with open(os.path.join(ROOT, path), encoding="utf-8",
                      errors="replace") as f:
                src = f.read()
            for name in sorted(must_be_present):
                # The WHOLE literal must be the root-relative name. Matching a
                # substring flagged "$TMP/backup.bin" -- a runtime temp path
                # that has nothing to do with a repo file of the same basename.
                for m in re.finditer(r'["\'](\./)?%s["\']' % re.escape(name), src):
                    line = src[:m.start()].count("\n") + 1
                    if (path, name) in guarded:
                        continue
                    offenders.append("%s:%d looks up %r, which is not committed"
                                     % (path, line, name))
        self.assertEqual([], offenders,
                         "committed code depends on an uncommitted file:\n  "
                         + "\n  ".join(offenders))

    def test_the_gitignore_entries_added_on_20260907_really_are_ignored(self):
        """Falsifies the premise. If these were tracked after all, the two
        tests above would be asserting nothing interesting."""
        for p in ("CLAUDE.md", "working-memory.md", "log.txt"):
            if os.path.exists(os.path.join(ROOT, p)):
                self.assertTrue(is_ignored(p), "%s is not ignored" % p)
                self.assertNotIn(p, self.tracked, "%s is still tracked" % p)


if __name__ == "__main__":
    unittest.main(verbosity=2)
