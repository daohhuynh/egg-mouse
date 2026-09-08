"""No em dash reaches the two files a stranger actually reads.

WHY THIS IS A TEST AND NOT A NOTE. The owner asked twice for em dashes to be
gone from `README.md` and `engineering-rules.md`, and asked for it to be
"written down somewhere so you remember". A note is not a mechanism: prose
rules decay silently, and the character is easy to reintroduce by hand, by
paste, or by an editor's smart-punctuation substitution. This makes the rule
decidable, so remembering is not required.

SCOPE, STATED SO THE NEGATIVE IS HONEST (engineering-rules.md 1.2a). This
checks exactly two files. Nothing else is covered: `notes/` held 2,221 em
dashes across 21 files when this was written, and another 18 tracked files
outside it hold at least one. Those are the derivation and the tooling rather
than the front door, and rewording them has never been asked for. So this test
says nothing whatever about them and must not be quoted as though it did.
Regenerate those counts (`git grep -o '—' -- 'notes/*.md' | wc -l`) rather than
trusting this sentence; it was already wrong once.
If that changes, add the paths to FILES rather than widening a glob, so the
scope stays something a reader can see.

U+2014 EM DASH only. The en dash in "meet 1-3" and the U+2212 MINUS SIGN in
engineering-rules.md 6's coverage formula are different characters doing
different jobs, and both were kept on purpose.
"""
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EM_DASH = "—"

# Named one by one, not globbed. A glob would silently start or stop covering
# a file as the tree changes, and the point of this test is a scope you can
# read off the page.
FILES = ("README.md", "engineering-rules.md")


def count_em_dashes(text):
    return text.count(EM_DASH)


class NoEmDashes(unittest.TestCase):
    def test_the_detector_can_actually_fire(self):
        """engineering-rules.md 6.2: a harness that cannot produce a bad result
        is not evidence. Every assertion below is `== 0`, and `== 0` is exactly
        what a broken detector returns for everything."""
        self.assertEqual(3, count_em_dashes("a" + EM_DASH + "b" + EM_DASH + EM_DASH))
        self.assertEqual(0, count_em_dashes("a - b, an en dash – and a minus −"))

    def test_the_files_are_present_and_substantial(self):
        """A missing or truncated file would pass the real check vacuously."""
        for name in FILES:
            path = os.path.join(ROOT, name)
            self.assertTrue(os.path.isfile(path), "%s is missing" % name)
            with open(path, encoding="utf-8") as f:
                text = f.read()
            self.assertGreater(len(text), 5000,
                               "%s is only %d bytes; too small to be the real "
                               "file, so the em dash check below would be "
                               "vacuous" % (name, len(text)))

    def test_no_em_dash_in_the_published_prose(self):
        offenders = []
        for name in FILES:
            with open(os.path.join(ROOT, name), encoding="utf-8") as f:
                for lineno, line in enumerate(f, 1):
                    if EM_DASH in line:
                        offenders.append("%s:%d  %s" % (name, lineno, line.rstrip()))
        self.assertEqual(
            [], offenders,
            "%d line(s) contain an em dash (U+2014). Reword the sentence rather "
            "than swapping the character for a comma; a colon, a full stop or a "
            "pair of parentheses is almost always the right replacement:\n  %s"
            % (len(offenders), "\n  ".join(offenders[:40])))


if __name__ == "__main__":
    unittest.main(verbosity=2)
