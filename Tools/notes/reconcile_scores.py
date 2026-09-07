#!/usr/bin/env python3
"""reconcile_scores.py -- make prediction-scores.md's tally a computation.

engineering-rules.md §6: "a number written in prose survives a context compaction while the
computation behind it does not, so it returns looking like established fact:
regenerate numbers, never quote them." That rule was written about coverage
accounting and this file is the same failure one directory over. The scoreboard
carried "266 predictions" for weeks; the register held 271, and nobody could say
where 266 came from because nothing recomputed it.

WHAT THIS CHECKS, and each is a way the scoreboard has actually been wrong:

  1. The register size, four independent ways. If the four disagree, the
     register is malformed and NO tally from it means anything.
  2. Every slug the scoreboard scores exists in the register -- or is declared,
     in the scoreboard itself, as belonging to another scope with the file that
     holds it. A slug in neither is a name that cannot be grepped, and a name
     that cannot be grepped cannot be de-duplicated: two entries scoring one
     prediction under two names inflate the tally silently.
  3. No prediction is scored twice, under its own name or an alias.
  4. The tally printed in the scoreboard matches the entries actually present.

It exits non-zero on any of those. It does not read the predictions or judge
them -- it only checks that the bookkeeping is arithmetic rather than memory.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REGISTER = os.path.join(ROOT, "notes", "wire-predictions.md")
SCORES = os.path.join(ROOT, "notes", "prediction-scores.md")

# A register entry: "#### 11. `slug` — [D]". The number restarts per section
# (there are eight #20s), so the slug is the only identifier.
ENTRY = re.compile(r"^#### \d+\.\s+`([^`]+)`", re.M)
CITE = re.compile(r"^- \*\*Cite:\*\*", re.M)
REFIF = re.compile(r"^- \*\*REFUTED IF:\*\*", re.M)

# A scoreboard entry, in either shape the file uses:
#   ### #20 `slug` — **CONFIRMED**
#   ### CONFIRMED — `slug` (...)
#   ### REFUTED — `slug` (...)
SCORED = [
    re.compile(r"^### #?\d*\s*`([^`]+)`\s*[-—]+\s*\*\*(CONFIRMED|REFUTED|PARTIAL|AMBIGUOUS)\*\*", re.M),
    re.compile(r"^### (CONFIRMED|REFUTED|PARTIAL|AMBIGUOUS)\s*[-—]+\s*`([^`]+)`", re.M),
]

# The scoreboard's own declaration of a slug that is NOT a register entry:
#   | `slug` | OUT OF REGISTER: <where the claim lives> |
# Declaring it is what makes it auditable; an undeclared stranger is an error.
OUT = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*OUT OF REGISTER:", re.M)
# And the scoreboard's declaration of a rename:
#   | `old-slug` | ALIAS OF: `register-slug` |
ALIAS = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*ALIAS OF:\s*`([^`]+)`", re.M)

# The printed tally rows this script regenerates. ALL THREE, because checking
# one row and trusting the other two is how the register row came to be right
# while a second tally in the same file said something else.
#   | <scope> | 3 | 0 | 2 | 271 |
TALLY = re.compile(
    r"^\|\s*(?P<scope>[^|]*?)\s*\|\s*(?P<c>\d+)\s*\|\s*(?P<r>\d+)\s*\|"
    r"\s*(?P<p>\d+)\s*\|\s*(?P<d>\d+|not enumerated)\s*\|", re.M)
SCOPE_REGISTER = "`wire-predictions.md` register"
SCOPE_OTHER = "pre-registered elsewhere"
SCOPE_FILES = "own pre-registration files"


def scored_entries(text):
    """[(slug, verdict)] for every scoreboard heading, either shape."""
    out = []
    for m in SCORED[0].finditer(text):
        out.append((m.group(1), m.group(2)))
    for m in SCORED[1].finditer(text):
        out.append((m.group(2), m.group(1)))
    return out


def main():
    reg = open(REGISTER).read()
    sc = open(SCORES).read()
    problems = []

    # ---- 1. the register's own size, four ways -----------------------------
    slugs = ENTRY.findall(reg)
    counts = {
        "#### headings": len(slugs),
        "distinct slugs": len(set(slugs)),
        "Cite: bullets": len(CITE.findall(reg)),
        "REFUTED IF: bullets": len(REFIF.findall(reg)),
    }
    print("register: " + ", ".join("%s %d" % (k, v) for k, v in counts.items()))
    if len(set(counts.values())) != 1:
        problems.append("the four register counts disagree: %r. The register is "
                        "malformed and no tally computed from it means anything."
                        % counts)
    n = counts["#### headings"]
    known = set(slugs)

    # ---- 2 + 3. every scored slug resolves, and resolves once --------------
    aliases = dict(ALIAS.findall(sc))
    declared_out = set(OUT.findall(sc))
    for old, new in sorted(aliases.items()):
        if new not in known:
            problems.append("ALIAS OF: `%s` -> `%s`, but `%s` is not a register "
                            "slug either." % (old, new, new))
        if old in known:
            problems.append("`%s` is declared an alias but IS a register slug." % old)
    for slug in sorted(declared_out):
        if slug in known:
            problems.append("`%s` is declared OUT OF REGISTER but IS in the "
                            "register." % slug)

    resolved = {}          # register slug -> [names it was scored under]
    files = {}             # own-file scorings -> verdict
    for slug, verdict in scored_entries(sc):
        target = aliases.get(slug, slug)
        if target in known:
            resolved.setdefault(target, []).append((slug, verdict))
        elif slug.endswith(".md") or "/" in slug:
            # A device run scored from its own pre-registration file. The
            # filename IS a resolvable name -- it greps, and git dates it -- so
            # it needs no table row. It is a THIRD scope and must never be added
            # to the register's numerator (§6: never do arithmetic between
            # separately-scoped numbers).
            if not os.path.exists(os.path.join(ROOT, slug)):
                problems.append("`%s` is scored as an own-file prediction but "
                                "that file does not exist." % slug)
            files.setdefault(slug, []).append(verdict)
        elif slug in declared_out:
            pass           # scored, but not against the register denominator
        else:
            problems.append(
                "`%s` is scored but is neither a register slug, nor declared "
                "ALIAS OF a register slug, nor declared OUT OF REGISTER. A name "
                "that does not resolve cannot be de-duplicated." % slug)

    for target, hits in sorted(resolved.items()):
        if len(hits) > 1:
            problems.append("`%s` is scored %d times, as %s. One prediction, one "
                            "entry." % (target, len(hits),
                                        ", ".join("`%s`" % h[0] for h in hits)))

    # ---- 4. the printed tally against the entries actually present ---------
    got = {"CONFIRMED": 0, "REFUTED": 0, "PARTIAL": 0, "AMBIGUOUS": 0}
    for target, hits in resolved.items():
        got[hits[0][1]] += 1
    print("register-scoped verdicts: " +
          ", ".join("%s %d" % (k, v) for k, v in sorted(got.items())))
    print("out-of-register slugs declared: %d" % len(declared_out))
    print("own-file scorings: %d (%s)" %
          (len(files), ", ".join(sorted(files)) or "none"))
    for f, vs in sorted(files.items()):
        if len(vs) > 1:
            problems.append("`%s` is scored %d times." % (f, len(vs)))

    def tally_of(verdicts):
        t = {"CONFIRMED": 0, "REFUTED": 0, "PARTIAL": 0, "AMBIGUOUS": 0}
        for v in verdicts:
            t[v] += 1
        return t

    out_verdicts = [v for slug, v in scored_entries(sc) if slug in declared_out]
    file_verdicts = [vs[0] for vs in files.values()]

    rows = {}
    for m in TALLY.finditer(sc):
        rows[m.group("scope")] = m
    if not rows:
        problems.append("no tally table found -- nothing to check the entries "
                        "against.")

    def check_row(marker, t, denom):
        hit = [k for k in rows if marker in k]
        if not hit:
            problems.append("no tally row whose scope contains %r." % marker)
            return
        if len(hit) > 1:
            problems.append("%d tally rows match %r: %r" % (len(hit), marker, hit))
            return
        m = rows[hit[0]]
        have = (int(m.group("c")), int(m.group("r")), int(m.group("p")),
                m.group("d"))
        want = (t["CONFIRMED"], t["REFUTED"], t["PARTIAL"], denom)
        if have != want:
            problems.append(
                "tally row %r reads %r; the entries in this file say %r "
                "(CONFIRMED, REFUTED, PARTIAL, denominator)." % (hit[0], have, want))
        if t["AMBIGUOUS"]:
            problems.append("row %r has %d AMBIGUOUS entries and the table has "
                            "no column for them." % (hit[0], t["AMBIGUOUS"]))

    check_row(SCOPE_REGISTER, got, str(n))
    check_row(SCOPE_OTHER, tally_of(out_verdicts), "not enumerated")
    check_row(SCOPE_FILES, tally_of(file_verdicts), str(len(files)))

    # A number in the prose that is not in the table is how the third tally got
    # in. Only the table may carry counts.
    body = sc.split("## Running tally", 1)[-1]
    for stray in re.finditer(r"\*\*CONFIRMED (\d+), REFUTED (\d+)\*\*", body):
        problems.append("a second tally is written into the prose at %r. Counts "
                        "live in the table only." % stray.group(0))

    # ---- 5. NO PRE-REGISTRATION FILE GOES UNCOUNTED ------------------------
    # Checks 2-4 all start from what the scoreboard already names, so every one
    # of them was satisfied while three device runs were missing from the file
    # entirely -- including the two that hold this project's only device-run
    # REFUTATIONS. The tally was internally consistent and the denominator was
    # 3 where it should have been 6, which is the worst shape a scoreboard can
    # have: it read as a clean sheet BECAUSE the misses were absent.
    #
    # So this check starts from the DIRECTORY instead. A prediction file either
    # gets an entry here, or gets declared UNSCORED here with a reason. Never
    # neither. Found 2026-09-07.
    for name in sorted(os.listdir(os.path.join(ROOT, "notes"))):
        if not name.startswith("prediction-") or not name.endswith(".md"):
            continue
        rel = "notes/" + name
        if rel == "notes/prediction-scores.md":
            continue
        if rel in files:
            continue
        if re.search(r"^\|\s*`%s`\s*\|\s*UNSCORED:" % re.escape(rel),
                     sc, re.M):
            continue
        problems.append(
            "`%s` exists and is neither scored in prediction-scores.md nor "
            "declared there as `| `%s` | UNSCORED: <why> |`. A pre-registration "
            "that nothing counts cannot lower the tally, only raise it."
            % (rel, rel))

    if problems:
        print("\nNOT RECONCILED:")
        for p in problems:
            print("  - " + p)
        return 1
    print("\nreconciled: %d register predictions, %d of them scored, "
          "%d out-of-register slugs, %d own-file runs."
          % (n, sum(got.values()), len(declared_out), len(files)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
