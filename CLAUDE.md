# OP1 8k v2 — macOS config tool + firmware flasher

Native macOS software for the Endgame Gear OP1 8k v2. Endgame ships Windows-only
tools. The protocol must be derived independently from their `.exe` files.

**This file contains rules and decisions only. It deliberately contains no
protocol findings.** See §7.

---

## 1. Hard rules

### 1.1 Third-party projects are OFF LIMITS
Two community projects exist (`qsxcv/UnofficialEGGWebConfig`, AGPL-3.0, which
ships reverse-engineered protocol notes; and `niansa/UnofficialEGGMouseConfig`,
Unlicense, plus its hosted WASM build at `ueggcfg.elinlyze.com` — same
codebase). **Both are config-only. Neither covers firmware flashing.**

**Do not open, fetch, cite, or reason from them.** Not the code, not the notes,
not the hosted build. If a question appears to require them, say so and stop —
the user decides.

These are excluded *because* they are credible, not because they aren't. The
first author writes production firmware for their own mouse built on the same
MCU and sensor as this one, and Endgame's 1.10 changelog credits them for
features in this very firmware. The second repo credits an Endgame employee as a
contributor. So they may contain protocol knowledge that originated inside
Endgame and cannot be derived from the `.exe` files at all.

That cuts both ways. It makes them valuable if we get truly stuck, and it makes
reading them early fatal to an independent derivation — the temptation is to
defer to the more experienced author rather than verify. The user will decide if
and when to consult them.

Two further notes. The second repo marks OP1 8k v2 support as **experimental**
and warns that experimental features can potentially brick the mouse. And the
AGPL project's licence is copyleft: copying its *code* would bind this project.
Protocol facts about hardware are not copyrightable; an independent
implementation from documented facts is clean.

`OpenNuvoton/*` is the chip vendor, not one of these. Fine to use.

### 1.2 Provenance tags on every protocol claim
- **[O] Observed** — read off the physical device.
- **[D] Derived** — traced to a specific location in an `.exe`. Cite the address.
- **[G] Guess** — convention or inference. **Never a basis for a write.**

If it isn't [O] or [D], it does not reach the hardware. Untagged assertions are
bugs. This applies to the AI as much as the user: pattern-matching from general
mouse-protocol knowledge and presenting it with the confidence of a finding is
the primary contamination risk.

### 1.3 Never write a byte whose meaning is [G]
Read-modify-write preserves bytes we don't understand. Do not "clean up", zero,
or normalise unknown bytes.

### 1.4 Firmware image selection is a compile-time constant
Whichever firmware resource the official updater loads, it must be a constant in
our code — never a variable, never selected at runtime, never chosen from a
list. Other resources in the binary may belong to other products.

### 1.5 Do not execute any of the vendor binaries
Static analysis only. Nothing about this project requires running them.

### 1.6 Do straightforward work the straightforward way
Reach for the simplest thing that does the job. A 300-line file gets read, not
delegated. Spawning a fleet of agents to answer a question that one careful pass
would answer is not thoroughness, it is overhead, and it can actively make the
answer worse: each agent sees less context than you do, they re-read the same
file in parallel, and their findings come back flattened into a summary that is
thinner than what you would have seen yourself.

Parallel agents earn their place in exactly two cases. The work genuinely does
not fit in one context, or independence is the point, as in an adversarial check
on a conclusion that is about to reach the hardware. Both cases exist in this
project. Most tasks are neither.

The same instinct that says "use more machinery" is the one §1.2 warns about.
Effort is not evidence, and volume is not rigour.

---

## 1b. Product context

**Removed. See §7.1.**

What stood here was presented as vendor documentation and official changelogs,
but it was unsourced, and the user has confirmed that part of it was fabricated
and described a different mouse. None of it survived. Hardware part numbers,
button behaviour, LED indications, the firmware version history and the
per-version changelog all have to be re-entered from the actual vendor documents
before anything relies on them.

One planning assumption from that section is worth keeping, because it is a
decision rather than a claim: **assume Endgame provides no protocol
documentation and no support for non-Windows tooling.** Build as if no answer is
coming.

---

## 2. Threat model

**The risk is bugs in our own code.** Not power loss, not cable failure, not
user error. Those are out of scope by explicit decision.

### Constraints
- **No spare mouse.** One device.
- **No Windows.** No Boot Camp, no VM, no USB capture of the official tool.
  There is no independent check on the derivation other than the device itself.
- **Do not open the mouse.** Assume a brick is not covered by warranty, and
  assume opening it makes that worse. Do not rely on any specific reading of
  Endgame's warranty terms; none has been verified.
- Must work without root. Detect missing macOS permissions and say so rather
  than failing silently.

---

## 3. Architecture

**One repo, three targets. Two executables.**

```
egg-mouse/
  CLAUDE.md
  notes/
  Sources/
    EGGCore/        # HID transport, enumeration, logging, device identity,
                    # protocol tables (as DATA), mock device harness
    EGGConfigCore/  # read-modify-write, diff-verify
    EGGFlashCore/   # preflight, chunked write, per-chunk verify, persist-retry
    egg-flash/      # CLI executable
    egg-config/     # CLI executable (GUI later, if ever)
  Tools/exe-diff/
  Tests/
```

One repo, because a shared core in a separate repo can drift: correct an opcode,
bump the core, update one consumer, and the other still holds the stale copy —
and the stale one erases flash.

Separate executables, because the two have **contradictory quit semantics**
(§4). Sharing a process makes "can the user quit now?" a conditional on internal
state — the exact bug class being designed out.

**Language: C++ with `hidapi`.** IOKit is a C API; every language calls it
equally well. C++ matches decompiler output idiom, reducing transcription errors
on bytes where a transcription error is expensive. `hidapi` wraps `IOHIDManager`.

**Flasher is CLI-first.** No window to close, no Dock quit item, output is a log
by construction. Trap `SIGINT`/`SIGTERM` explicitly during the write phase.

Protocol constants live in `EGGCore` **as data tables**, not scattered through
code.

### 3.1 Tooling available on this machine
- **Ghidra 12.1.3**, installed via Homebrew. `analyzeHeadless` is on `PATH`
  (symlinked to `/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless`);
  `ghidraRun` and `pyghidraRun` are also on `PATH`.
- radare2 / `rabin2`, `objdump`, `strings`, `xxd`, Java 24, python3.
- `.claude/settings.json` allowlists these so they run without prompting.

No extraction or triage script is checked in. The previous one was deleted; see
§7.1.

---

## 4. Safety design

### 4.1 Config — near-safe
- **Read-modify-write always.** Never construct a settings blob from scratch.
- Validate a read is structurally plausible before acting on it. Never let a
  reported success stand in for verifying the data itself.
- **Never write after a failed read.**
- Read back and verify after every write; log the diff.
- Save a known-good blob to disk on first connect.
- **Implement factory reset first and confirm it works** before any other write
  path. It is the undo for bad config state.

### 4.2 Flash — two regimes
**Before erase: abort is always correct.** Nothing has changed on the device.
Any single preflight failure aborts. No path proceeds on a partial pass.

Preflight: validate image size and identity; if entering the bootloader
re-enumerates the device, confirm device identity again afterwards; round-trip a
benign query and confirm a structurally plausible response; if the bootloader
reports a version or an identity of any kind, refuse anything unrecognised.

**After erase: quitting is the bug.** The device has no valid application;
exiting cleanly guarantees the bad outcome. Retry the failed chunk, re-establish
the connection if it drops, keep driving to a valid resident image. No cancel
button, no timeout that gives up. The erase-through-completion sequence is one
non-abortable unit; no code path returns from it without either a verified image
or an explicit, loud unrecoverable state.

**Mirror whatever verification the vendor protocol provides, exactly.** Free
safety is the best kind.

### 4.3 What actually protects against AI-written bugs
Language safety features do not. The dangerous failures — wrong chunk boundary,
wrong opcode, trusting a reported success — compile clean in any language.

- **Mock bootloader with failure injection.** Adversarial, not cooperative: it
  rejects commands, returns malformed responses, reports false success, goes
  silent mid-write, disconnects and reappears, stalls past every timeout.
  Highest-value artefact in the project. Needs no hardware.
- **Dry-run mode** emitting the exact byte stream that would be sent.
- **Golden file**: freeze a trusted byte stream, diff every future run against it.
- **Assert invariants, not examples**, over randomised runs. At minimum: no write
  is emitted unless preflight passed; no return from the post-erase phase without
  a verified image or explicit unrecoverable state; the byte stream for a given
  input is identical every run; the firmware resource identifier is never
  anything but the single hardcoded constant.
- **Keep the write phase small and in one file.** ~100 lines in one place can be
  audited by eye. Spread across eight files with clever abstractions, it cannot.

### 4.4 Staged bring-up

**The flasher is the priority. It is not gated on the config tool and does not
wait for it.** Both tools ship on the same timeline.

Stages 1 and 2 are *transport validation for the flasher*, not config work. If
the two tools turn out to share a transport, proving it once proves it for both,
but that has to be established rather than assumed. Step 1 uses a read-only
command because that is the cheapest possible way to prove the layer works — a
bootloader-mode query serves equally well if preferred, and skips config
entirely.

1. **One read-only command round trip.** Exercises enumeration, channel open,
   report framing, response parsing, timing. Nothing is written; nothing to undo.
2. **Bootloader entry and exit only.** No erase, no write. Exercises whatever
   mode switch exists, any USB re-enumeration it causes, and the search across
   however many identities the device presents — the parts unique to flashing.
3. **Flash read-back** if the bootloader supports it. Compare against whatever
   reference image can be obtained. Validates address/chunk arithmetic with zero
   writes.
4. **A real flash.**

By stage 4 the only untested code is erase and write.

Config work — read-modify-write, factory reset as the undo, settings semantics —
proceeds in parallel on the same shared transport. Factory reset must still be
working before any *config* write; that gate does not apply to flashing.

---

## 5. Device-gated work (requires the physical device)
- Report descriptor → confirm report IDs and exact lengths [O].
- Which product IDs the device presents, and which appears in normal
  operation [O].
- Whether macOS hands over the vendor interface without Input Monitoring.
- Device response behaviour, error states, malformed-input handling.
- Real inter-command timing vs the vendor's padding.
- Factory reset confirmation.
- Recovery behaviour with the application region blank.

Everything else — transport, framing, state machines, mock harness, dry-run,
invariants, the `.exe` differ — is buildable now.

---

## 6. Reading policy for the binaries

Read **every** candidate function — everything not positively identified as
statically-linked library code. A relevance score may set reading ORDER; it must
never decide what goes unread.

If the candidate set is too large to read exhaustively, **say so and give the
number**. Do not silently sample.

---

## 7. Why there are no protocol findings in this file

A prior analysis session produced a large body of protocol findings. They are
withheld deliberately.

That analysis was **wrong at least three times** — each error stated with
confidence, each caught only by redoing the work more carefully, never by
review. Handing those conclusions over as a starting point would propagate the
errors and turn verification into confirmation.

**Derive everything independently first.** When the independent derivation is
complete and committed, the user will release the prior material for comparison.
Disagreements are expected. They mean one of: the prior analysis was wrong, the
new analysis is wrong, or the behaviour is version-dependent. **The tiebreaker
is the device, never authority or seniority.**

### 7.1 The prior session's contamination reached this repo

Withholding the findings was not enough. Two leaks were found and removed.

**The tooling.** A Ghidra extraction script (`Tools/ghidra_scripts/MineAll.java`)
carried the prior session's conclusions inside it: its relevance scorer awarded
points for specific hardcoded constants, and its pattern matching assumed a
specific command shape. A tool that scores for the answer is not a neutral
instrument, and anyone who reads it inherits the answer. It was deleted rather
than repaired. Any replacement must extract structure without encoding a belief
about what the structure means.

**The prose.** §1b asserted vendor documentation and user preferences that were
not sourced, and part of it was confirmed false. §4.2 and §4.4 asserted protocol
details in a file that opens by claiming it holds none. All of that is gone.

§1.1 was reviewed and kept in full. It describes the two community projects
themselves — their names, licences, scope, authorship — and never reproduces
anything found inside them. Repository metadata is not protocol knowledge, and
knowing what to avoid requires knowing what it is.

The lesson generalises. **Contamination hides in whatever is not treated as a
claim**: a scoring heuristic, a variable name, a section that sounds like
background rather than analysis, a hedge that has quietly become an assertion by
the third time it is restated. §1.2 applies to all of it.
