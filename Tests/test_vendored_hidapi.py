"""The vendored hidapi is upstream's bytes, and the README's hashes prove it.

WHY THIS EXISTS. third_party/hidapi/ holds four files copied from hidapi
0.15.0. They are the HID transport: every byte this project puts on the wire
goes through mac/hid.c. Vendored third-party code is the easiest place in a
repository for a quiet local edit to survive, because nobody reads it and no
test covers it -- and here an edit would sit underneath the flasher.

WHAT IT DECIDES. That each vendored file still hashes to the value recorded in
third_party/hidapi/README.md. The hashes are PARSED OUT OF THAT FILE rather
than duplicated here, so the documentation is load-bearing: change a file and
forget the table, and this fails; change both and you have at least stated what
you did, in the file whose job is to say so.

WHAT IT CANNOT SEE (engineering-rules.md 1.2a). It compares against a hash
written down in this repository, so it proves the files have not changed SINCE
they were vendored. It does not re-fetch from upstream and therefore cannot
prove they matched upstream on the day they arrived. That check needs the
network and is the one-liner in third_party/hidapi/README.md. This is
tamper-evidence, not provenance.
"""
import hashlib
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENDOR = os.path.join(ROOT, "third_party", "hidapi")

# Upstream version this project is pinned to. engineering-rules.md 4.4: the HID
# transport is the one layer the mock cannot exercise, so a bump is a device
# question and not a housekeeping one.
PINNED_VERSION = "0.15.0"

# A row is `| `path` | `hash` |`. Only rows whose first cell looks like a file
# path are taken, which skips the tarball row and the header.
ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*`([0-9a-f]{64})`\s*\|\s*$", re.M)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


class VendoredHidapiIsUnmodified(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(VENDOR, "README.md"), encoding="utf-8") as f:
            cls.readme = f.read()
        cls.pinned = {p: h for p, h in ROW.findall(cls.readme)
                      if "/" in p or p.endswith(".txt")}

    def test_the_hasher_can_produce_a_bad_result(self):
        """engineering-rules.md 6.2. Every assertion below is an equality, and a
        hasher that returned a constant would satisfy all of them."""
        a = hashlib.sha256(b"one").hexdigest()
        b = hashlib.sha256(b"two").hexdigest()
        self.assertNotEqual(a, b)
        # Known answer, cross-checked against the system tool rather than
        # recalled: `printf 'hello\n' | shasum -a 256`. The first value written
        # here was wrong from memory, which is the argument for the check.
        self.assertEqual(
            "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03",
            hashlib.sha256(b"hello\n").hexdigest())

    def test_the_readme_table_was_actually_parsed(self):
        """A regex that matched nothing would make the real test vacuous, which
        is how a check like this usually dies."""
        self.assertGreaterEqual(
            len(self.pinned), 4,
            "parsed %d hash rows out of third_party/hidapi/README.md; the table "
            "format changed and this test stopped checking anything: %r"
            % (len(self.pinned), self.pinned))

    def test_every_vendored_file_matches_its_pinned_hash(self):
        wrong = []
        for rel, expected in sorted(self.pinned.items()):
            path = os.path.join(VENDOR, rel)
            if not os.path.isfile(path):
                wrong.append("%s: MISSING" % rel)
                continue
            actual = sha256(path)
            if actual != expected:
                wrong.append("%s:\n      pinned %s\n      actual %s"
                             % (rel, expected, actual))
        self.assertEqual(
            [], wrong,
            "third_party/hidapi no longer matches the hashes in its own "
            "README.md. These files are upstream's and are not ours to edit; "
            "if the version was bumped deliberately, update the table and say "
            "so:\n    " + "\n    ".join(wrong))

    def test_the_pinned_version_is_the_one_that_flashed_the_mouse(self):
        with open(os.path.join(VENDOR, "VERSION"), encoding="utf-8") as f:
            self.assertEqual(PINNED_VERSION, f.read().strip())
        self.assertIn(PINNED_VERSION, self.readme)

    def test_only_the_macos_backend_is_vendored(self):
        """Vendoring a backend this project cannot build is dead weight that
        still has to be audited. Named so a stray copy is noticed."""
        got = set()
        for dirpath, _, files in os.walk(VENDOR):
            for f in files:
                got.add(os.path.relpath(os.path.join(dirpath, f), VENDOR))
        allowed = {"README.md", "LICENSE-bsd.txt", "AUTHORS.txt", "VERSION",
                   os.path.join("hidapi", "hidapi.h"),
                   os.path.join("mac", "hid.c"),
                   os.path.join("mac", "hidapi_darwin.h")}
        self.assertEqual(allowed, got,
                         "unexpected files under third_party/hidapi: %s"
                         % sorted(got - allowed))


if __name__ == "__main__":
    unittest.main(verbosity=2)
