"""Tests/test_gui_state.py -- no published state the GUI writes and never reads.

WHAT THIS CAUGHT WHEN IT WAS WRITTEN, 2026-09-07. Two properties, both live in
the shipping app, both invisible to every other test:

  - `FirmwareModel.busy` was set true and false around all five of the Firmware
    screen's operations and read by nothing. Every OTHER screen guards itself
    with its own `busy` (AdvancedView, ConfigView, UpdatesView all carry
    `.disabled(model.busy)`); the Firmware screen's only re-entrancy guard was
    `ToolRunner.writePhaseInProgress`, which is FALSE during bootloader entry,
    during `read-firmware`'s 65-block read, and during both previews. So a
    second press of "Back up firmware" started a second `egg-flash` against the
    same bootloader, writing the same output path.
  - `ToolRunner.running` held the command line of the executing command and was
    read nowhere, so a long run looked exactly like a hung app.

WHY A LINT AND NOT A UNIT TEST. `Tests/test_app_commands.swift` drives
`Commands`, which is pure argument-building, against the real CLIs -- it is the
right test and it cannot see a view. Nothing else in the suite reads the view
layer at all, and SwiftUI state is exactly where "written but never read" hides:
the compiler is happy, the app builds, the screen just quietly does nothing.

SCOPE, so the negative is honest (engineering-rules.md §1.2a). This proves a property is
MENTIONED outside its own declaration. It does not prove it is mentioned
usefully -- `.disabled(m.busy)` and `let _ = m.busy` look the same from here.
What it removes is the case where a guard was written, believed, and wired to
nothing at all, which is the case that actually occurred twice.
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPDIR = os.path.join(ROOT, "Sources", "EGGApp")

DECL = re.compile(
    r"@Published\s+(?:private\(set\)\s+)?var\s+([A-Za-z_][A-Za-z0-9_]*)")


def strip_comments_and_strings(src):
    """CODE ONLY. This is load-bearing, not tidiness.

    The first version of this test scanned the raw source, and its own
    falsification passed: deleting the `RunningBar(command: cmd)` call left the
    doc comment above it saying "`ToolRunner.running` held exactly this string",
    and `.running` matched there. A lint satisfied by a comment ABOUT the thing
    it is checking measures nothing. String literals go too -- "is already
    running." would otherwise count as a use of `running`.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            # Swift block comments nest.
            depth, i = 1, i + 2
            while i < n and depth:
                if src.startswith("/*", i):
                    depth += 1; i += 2
                elif src.startswith("*/", i):
                    depth -= 1; i += 2
                else:
                    i += 1
        elif c == '"':
            triple = src.startswith('"""', i)
            i += 3 if triple else 1
            while i < n:
                if triple and src.startswith('"""', i):
                    i += 3
                    break
                if not triple and src[i] == '"':
                    i += 1
                    break
                # KEEP INTERPOLATIONS. `\\(m.provenLabel)` inside a Text() is a
                # real use of a real property; dropping it made this lint report
                # two false positives the moment strings started being stripped.
                if src.startswith("\\(", i):
                    depth, i = 1, i + 2
                    while i < n and depth:
                        if src[i] == "(":
                            depth += 1
                        elif src[i] == ")":
                            depth -= 1
                            if not depth:
                                i += 1
                                break
                        out.append(src[i])
                        i += 1
                    continue
                i += 2 if src[i] == "\\" else 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def sources():
    """Raw text per file, for the checks that look for a literal spelling."""
    out = {}
    for root, _, files in os.walk(APPDIR):
        for f in sorted(files):
            if f.endswith(".swift"):
                p = os.path.join(root, f)
                with open(p, encoding="utf-8") as fh:
                    out[os.path.relpath(p, ROOT)] = fh.read()
    return out


def _split_top_level(text):
    """Split a Swift argument list on commas at nesting depth zero."""
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(cur)); cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


def _balanced(text, open_at):
    """Text inside the parentheses that start at `open_at`, or None."""
    depth, i = 0, open_at
    while i < len(text):
        if text[i] in "([{":
            depth += 1
        elif text[i] in ")]}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:i]
        i += 1
    return None


def declared_builders(cmds):
    """`{name(label:label:)}` for every `static func` in Commands.swift.

    A parameter's key is its EXTERNAL label -- `_` when suppressed, otherwise
    the first identifier of the declaration, which is the label a caller types.
    """
    out = set()
    for m in re.finditer(r"static func ([A-Za-z_][A-Za-z0-9_]*)\s*\(", cmds):
        inner = _balanced(cmds, m.end() - 1)
        if inner is None:
            continue
        labels = []
        for part in _split_top_level(inner):
            head = part.strip().split(":")[0].split()
            labels.append(head[0] if head else "_")
        out.add("%s(%s)" % (m.group(1), ":".join(labels)))
    return out


def call_keys(text, prefix):
    """The same key shape, read off call sites in `text`."""
    out = set()
    pat = re.compile(r"%s([A-Za-z_][A-Za-z0-9_]*)\s*\("
                     % re.escape(prefix))
    for m in pat.finditer(text):
        inner = _balanced(text, m.end() - 1)
        if inner is None:
            continue
        labels = []
        for part in _split_top_level(inner):
            part = part.strip()
            lm = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*:", part)
            labels.append(lm.group(1) if lm else "_")
        out.add("%s(%s)" % (m.group(1), ":".join(labels)))
    return out


@unittest.skipUnless(os.path.isdir(APPDIR), "EGGApp target not present")
class NoPublishedStateIsWriteOnly(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = sources()
        cls.all = "\n".join(strip_comments_and_strings(v)
                            for v in cls.src.values())
        cls.props = [(f, m.group(1))
                     for f, s in sorted(cls.src.items())
                     for m in DECL.finditer(s)]

    def test_the_scan_found_the_app(self):
        """A regex that matched nothing would make the test below vacuous --
        the failure mode this repo has shipped more than once."""
        self.assertGreaterEqual(len(self.src), 5,
                                "found %d swift files" % len(self.src))
        self.assertGreaterEqual(len(self.props), 30,
                                "found %d @Published properties; there were 40 "
                                "when this test was written" % len(self.props))

    def test_every_published_property_is_referenced_somewhere(self):
        unread = []
        for f, name in self.props:
            # Count every mention in CODE, then take away the ones that are not
            # reads: the declaration itself, and every assignment to it --
            # `busy = true`, `m.busy = false`, `self.gates = ...`. Whatever is
            # left is a read.
            #
            # An earlier version looked only for `.name` and `$name`, which
            # misses a same-type bare reference: `provenLabel` is read at
            # `version == provenLabel` and was reported unread. The `=(?!=)`
            # is what keeps `==` on the read side of the line.
            code = self.all
            code = re.sub(r"@Published\s+(?:private\(set\)\s+)?var\s+%s\b"
                          % re.escape(name), " ", code)
            code = re.sub(r"(?:\w+\.)?\b%s\b\s*=(?!=)" % re.escape(name),
                          " ", code)
            if not re.search(r"\b%s\b" % re.escape(name), code):
                unread.append("%s: %s" % (f, name))
        self.assertEqual(
            [], unread,
            "@Published properties written but never referenced. Either wire "
            "them up or delete them -- a guard nothing reads is worse than no "
            "guard, because it reads in review as though the case is handled:"
            "\n  " + "\n  ".join(unread))

    def test_the_firmware_screen_disables_on_its_own_busy_flag(self):
        """The specific instance, pinned by name. The general check above would
        pass if `busy` were merely mentioned in a comment.

        The gate is `FirmwareView.busy`, NOT `m.busy`, and the difference is the
        whole point. `RootView` switches screens with a `switch`, so leaving the
        screen tears down the view and its `@StateObject`; returning builds a
        fresh `FirmwareModel` with `busy == false` while the subprocess it was
        guarding is still running, and a second `egg-flash` could then be
        launched against the same bootloader and the same output file. The
        computed property ORs in `runner.running`, which is one object for the
        app's lifetime and therefore survives the round trip. Asserting on
        `m.busy` here would pin the defect in place."""
        fv = strip_comments_and_strings(
            self.src.get("Sources/EGGApp/FirmwareView.swift", ""))
        self.assertIn("@Published var busy", fv)
        self.assertRegex(
            fv, r"private var busy: Bool \{[^}]*\bm\.busy\b[^}]*"
                r"\brunner\.running\b",
            "the Firmware screen's busy gate must outlive navigation: it has to "
            "consult the app-lifetime ToolRunner, not only this view's model, "
            "or leaving and returning mid-operation resets it")
        self.assertGreaterEqual(
            len(re.findall(r"\.disabled\([^)]*(?<!\.)\bbusy\b", fv)), 5,
            "the Firmware screen has five operations and every button that "
            "starts one must be disabled while another is running")
        self.assertNotIn(".disabled(m.busy", fv,
                         "a gate that reads the view-scoped flag directly does "
                         "not survive leaving and re-entering the screen")
        self.assertIn("if busy { return kSomethingRunning }", fv,
                      "the two blocked-reason computations must say WHY, not "
                      "just grey the button out")

    def test_every_command_builder_is_reachable_from_the_app(self):
        """Commands.swift builds argument lists and Tests/test_app_commands.swift
        drives every one of them against the real CLIs. Neither notices when a
        builder is correct, tested, and callable from no screen.

        Two were. `Commands.verbose`, whose doc comment carried the argument for
        why the flag is safe while ToolRunner built `["-v"] + args` inline
        beside it. And `previewHandedness(_:from:)`, the offline handedness
        plan, which no view could construct because `handedness` is not one of
        `set`'s fields -- while test_app_commands.swift asserted a property of
        it in a list whose message names the Advanced screen.

        BY ARGUMENT LABELS, NOT BY NAME. `previewHandedness` is overloaded: the
        device form `(_ side:)` is called from ConfigView, so a name-only check
        sees the name used and passes. It did, on this test's own falsification,
        which is how the label matching below came to exist.
        """
        cmds = strip_comments_and_strings(
            self.src.get("Sources/EGGApp/Commands.swift", ""))
        self.assertTrue(cmds, "Commands.swift is missing")

        decls = declared_builders(cmds)
        self.assertGreaterEqual(
            len(decls), 40,
            "found %d builders; there were 48 when this test was written"
            % len(decls))

        callers = "\n".join(strip_comments_and_strings(v)
                            for k, v in self.src.items()
                            if not k.endswith("Commands.swift"))
        called = call_keys(callers, prefix="Commands.")
        # A builder called only from inside Commands.swift is fine. Its own
        # declarations are removed first: an overloaded name has two `static
        # func` lines and counting those as calls is exactly what let the real
        # unreachable overload through.
        bodies = re.sub(r"static func [A-Za-z_][A-Za-z0-9_]*\s*\(", " ", cmds)
        called |= call_keys(bodies, prefix="")

        unreachable = sorted(k for k in decls if k not in called)
        self.assertEqual(
            [], unreachable,
            "Commands builders that no view or runner calls, and that nothing "
            "inside Commands.swift calls either. Wire them to a control or "
            "delete them -- a capability the CLI has and the app cannot ask "
            "for is a gap, and a tested one reads like coverage:\n  "
            + "\n  ".join(unreachable))

    def test_every_screen_guards_re_entrancy(self):
        """Each screen that runs a tool must disable something on its own busy
        flag. This is the property the Firmware screen was missing while three
        of its four siblings had it."""
        for f in ("ConfigView.swift", "AdvancedView.swift", "UpdatesView.swift",
                  "FirmwareView.swift"):
            s = strip_comments_and_strings(
                self.src.get("Sources/EGGApp/" + f, ""))
            self.assertTrue(s, "%s is missing" % f)
            self.assertRegex(
                s, r"\.disabled\([^)]*\bbusy\b",
                "%s runs tools and never disables anything while one is "
                "running" % f)


if __name__ == "__main__":
    unittest.main(verbosity=2)
