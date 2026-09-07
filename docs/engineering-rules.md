# OP1 8k v2 — macOS config tool + firmware flasher

Native macOS software for the Endgame Gear OP1 8k v2. Endgame ships Windows-only
tools. The protocol must be derived independently from their `.exe` files.

**This file is rules, decisions, and objectively checkable context. Nothing
else.** No protocol findings (§7), no status, no working notes, no
justification for decisions already made. Those belong in `notes/` or in a
commit message. Editing this file is fine; growing it by default
is not. A rules file that gains a paragraph every session and loses none stops
getting reread, and a rules file nobody rereads enforces nothing. Rewrite and
delete in preference to appending.

## 1. Hard rules

### 1.1 Third-party implementations are OFF LIMITS
Community implementations covering parts of this protocol exist. **None was
opened, fetched, cited, or reasoned from at any point in this project, and none
is named here.** The rule still stands: if a question appears to require one,
say so and stop — the owner decides.

The exclusion is not a judgement on anyone's work. It is what makes this
derivation independent. Every protocol fact here comes from the vendor's own
`.exe` files, or from captures of the vendor's software talking to hardware the
owner owns. Reading someone else's answer first turns verification into
confirmation: the temptation is to defer to it rather than check it, and a
derivation that has deferred once cannot say which of its facts it actually
established.

There is a licensing reason on top of the methodological one. Protocol facts
about hardware are not copyrightable, and an independent implementation from
documented facts is clean — but *code* carries its author's licence, and a
copyleft one would bind this project. Deriving everything here keeps that
question from ever arising.

`OpenNuvoton/*` is the chip vendor, not one of these. Fine to use.

### 1.1a One evidence file is QUARANTINED, and the reason generalises
A running log file kept at the repo root during the early sessions was found to
misdescribe what had actually been done at the device. The owner of the device
said so directly. It is quarantined in full rather than in part, is not
published, and **must not be read, cited, diffed against, or used to label a
capture.**

The failure it represents is this project's characteristic one, which is why
the rule survives the file. It began as an instruction script and accumulated
answers, attributions and `ANSWERED` / `confirmed` tags over many sessions.
Nothing in it marked which statements were observations and which were
inferences written down beside them, so by the third restatement a hedge was
indistinguishable from a measurement — §7.1's contamination pattern exactly, in
a file nobody was treating as a claim.

**What to use instead**, all of which survive the quarantine:
- `windows-run/*.pcapng` — bytes. They cannot misdescribe themselves.
- `windows-run/screenshots/*.png` — **the config screenshots are factory
  defaults**, and that is verified, not assumed: all 16 visible settings match
  the post-factory-reset record byte for byte (`config-wire-observed.md` §7).
  Anchor a capture to the defaults and trace forward.
- the `.exe` files — every `[D]` in `notes/` cites an address.

`windows-run/log.txt` is a separate file and a different question: only its
section 05 was ever filled in. It is not covered by this rule, but it is
single-sourced, so corroborate anything from it before relying on it.

### 1.2 Provenance tags on every protocol claim
- **[O] Observed** — read off the physical device. Reading a capture file is
  `[O]`; the timestamps and bytes in it came off the wire.
- **[D] Derived** — traced to something in an `.exe` that someone else can go
  and check. **Cite it.** Any of: a virtual address; a resource (`RT_DIALOG
  102`, `FWFILE 140`); a file offset or a hash; a command that reproduces it
  (`rabin2 -i`, `Tools/pe/blobcensus.py`); a verbatim quoted literal; or a
  section of a notes file that itself cites one of these. An address is the
  usual form, not the only one — a hash is stronger, because it verifies rather
  than points.
- **[G] Guess** — convention or inference. **Never a basis for a write.**

If it isn't [O] or [D], it does not reach the hardware. Untagged assertions are
bugs. `Tools/ghidra-export/auditclaims.py --strict` enforces all of this
against every `[D]` in `notes/` and runs in `ctest`; it fails on an uncited
claim, an address that is not in the binary named, and any citation to the
quarantined `./log.txt`. This binds everyone working on the project, and the
experienced reader most of all: pattern-matching from general mouse-protocol
knowledge and presenting it with the confidence of a finding is the primary
contamination risk.

### 1.2a Absence is a claim, and it needs more evidence than presence
"X is at address A" is checkable by looking. **"X does not exist", "X is unused",
"X is dead code", "the set is exactly N"** are not: they assert something about
everywhere you did not look. These are the claims this project has actually got
wrong, and they are expensive because they *close* lines of enquiry.

Before writing one down:

1. **State the search space and the encoding forms covered — and the ones not.**
   A scan for `push imm32` that finds nothing says nothing about `movl $imm32`.
   That exact mistake was made on 2026-09-03.
2. **Check the user-visible surface before concluding a capability is missing:**
   strings in both encodings, `.rsrc` dialogs and their control captions, menus,
   message maps. A feature the vendor ships has a button somewhere.
3. **Never infer absence of a capability from absence of a name.** "No command is
   called reset" does not mean there is no reset. It meant `A1 13`.

A negative claim that cannot meet 1–3 is **[G]**, however exhaustive the scan
looked. Say "not found by method M, which cannot see form F" — never "does not
exist".

### 1.2b Ghidra's derived views are evidence, never ground truth
The decompiled C, the `callers`/xref lists, and the FunctionID names are all
*products of analysis*, and all three have been caught wrong in this repo: the
decompiler silently drops byte stores into device report buffers; the `callers`
field reported a live function as uncalled (`0x00404720`, plainly called at
`0x413faf`); a FunctionID name is one signature collision away from a guess.

Use them to **find** things. Never to **rule things out**. Any claim that
something is not called, not reachable, or not present must be recovered from raw
bytes — `dis.sh`, an exhaustive literal scan, or a call-edge map built from
`E8`/`E9` targets.

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
split across a fleet of parallel readers. Volume is not rigour: each reader sees
less of the context than one person holding the whole problem, and their
findings come back flattened into a summary thinner than the material.

Splitting work up earns its place in two cases only. The work does not fit in
one head at one time, or independence is the point, as in an adversarial check
on a conclusion about to reach hardware. Reading thousands of decompiled
functions is the first case. Most tasks are neither.

### 1.7 Commit finished work, and keep working memory
Sessions die without warning, and anything held only in a session's head dies
with it.

**Commit when a thing is done, not when it is started.** Done means implemented
and checked, or investigated and concluded. Not "it compiles", not "I have a
theory". The message says what was verified and how, so a later reader can tell
a checked claim from an assumed one without rerunning anything.

**Keep a working-memory file, and treat using it as compulsory.** It is the
working file of whoever is doing the work, not documentation for a user of this
software, so it is deliberately **not tracked by git** and is not published
here. This project outruns anyone's memory of it: without written external
state you will re-derive finished work, contradict an hour-old conclusion, and
lose track of which claims were checked while sounding equally confident
throughout. That is the §7 failure mode. Read it first, update it as you go,
and trust it over recollection when the two disagree.

Untracking it is a deliberate trade with a cost worth naming: the argument above
— anything held in one place only dies with that place — was also the argument
for committing it, and git is no longer the backup. Back it up somewhere, or
accept that.

**Write a gap down the moment you notice it, not once you have resolved it.**
A count that does not reconcile, a number quoted two different ways, a function
you meant to come back to. These feel too small to record when you spot them and
are invisible an hour later, and they are the exact thing this file exists to
catch. If you cannot resolve it now, it goes in the file *before* you do anything
else. A recorded gap is a task. A remembered one is a bug you will ship.

It holds current state, never history; git has the history. Delete each item the
moment it is done, rewrite in place instead of appending, and write for the
stranger who reads it next: name the binary, the address, the command.

**Length is not the target and never was.** The file exists to serve the work,
not to stay short. Trim duplication and finished items ruthlessly; do **not**
trim decisions, open gaps, or hard-won context to hit a line count. The only
test that matters: if this session stops mid-sentence, can a fresh one pick the
work up cold from that file alone?

## 2. Threat model

**The risk is bugs in our own code.** Not power loss, not cable failure, not
user error. Those are out of scope by explicit decision.

### Constraints
- **No spare mouse.** One device.
- **No vendor documentation.** Endgame's config software is Windows-only by
  their own account. Assume no protocol documentation and no support is coming,
  and build as if no answer arrives.
- **No Windows.** No Boot Camp, no VM, no USB capture of the official tool.
  There is no independent check on the derivation other than the device itself.
- **Do not open the mouse.** Assume a brick is not covered by warranty, and
  assume opening it makes that worse. Do not rely on any specific reading of
  Endgame's warranty terms; none has been verified.
- Must work without root. Detect missing macOS permissions and say so rather
  than failing silently.
- **Assume the device does not validate the image it is given.** Decided
  2026-09-03, for safety, not because it was derived — whether the bootloader
  checks what it receives is `[G]` and untestable without the device. The
  consequence is load-bearing: the vendor's own checksums confirm only that the
  bytes received match the bytes sent, so **nothing downstream of us catches a
  wrong-but-well-formed image.** Every guard against flashing the wrong firmware
  has to be ours, and has to run before the first byte goes out.

## 3. Architecture

**One repo, three targets. Two executables.**

```
Sources/
  EGGCore/        # HID transport, enumeration, logging, device identity,
                  # protocol tables (as DATA), mock device harness
  EGGConfigCore/  # read-modify-write, diff-verify
  EGGFlashCore/   # preflight, chunked write, per-chunk verify, persist-retry
  egg-flash/      # CLI executable
  egg-config/     # CLI executable
Tools/            capture/ (pcapng), pe/ (FWFILE resources),
                  ghidra-export/ (static analysis), device/ (read-only)
notes/ Tests/
```

One repo, because a shared core in a separate repo can drift: correct an opcode,
bump the core, update one consumer, and the other still holds the stale copy —
and the stale one erases flash.

Separate executables, because the two have **contradictory quit semantics**
(§4). Sharing a process makes "can the user quit now?" a conditional on internal
state — the exact bug class being designed out.

**Language: C++ with `hidapi`.** C++ matches decompiler output idiom, reducing
transcription errors on bytes where a transcription error is expensive.
`hidapi` wraps `IOHIDManager`.

**Flasher is CLI-first.** No window to close, no Dock quit item, output is a log
by construction. Trap `SIGINT`/`SIGTERM` explicitly during the write phase.
*(Implemented 2026-09-05 as `NoQuitDuringWrite`, which ignores `SIGINT`,
`SIGTERM` and `SIGHUP` for the erase-through-verified-image unit only. The rule
had been in this file since it was written with no code behind it until an
adversarial audit noticed.)*

**A GUI DOES NOT INHERIT THIS.** If one is built, the contradictory quit
semantics in §4 are the whole reason the two executables are separate: a window
has a close button, a Dock quit item and a force-quit, none of which a
post-erase write phase may honour. A GUI may drive config freely; it must not
own the flash write phase. Shell out to `egg-flash` and let it keep being a
process that cannot be asked to stop.
Protocol constants live in `EGGCore` **as data tables**, not scattered through code.

### 3.1 Tooling on this machine
Ghidra 12.1.3, a Homebrew formula (not a cask). `analyzeHeadless`, `ghidraRun`
and `pyghidraRun` are on `PATH`. It declares a JDK 21 minimum with no maximum
and runs on the Java 24 installed here. Also radare2 / `rabin2`, `objdump`,
`strings`, `xxd`, python3.

`Tools/ghidra-export/` holds the static-analysis toolkit — export, call-edge
mapping, literal and command scanning, coverage accounting, resource and dialog
dumping. **This is the replacement for the script deleted in §7.1, not a return
to it.** The rule it must keep meeting: a tool here records STRUCTURE and must
not encode a belief about what the structure MEANS. The deleted one scored
functions for specific hardcoded constants and pattern-matched one command
shape, so it carried the prior conclusions inside it and handed them to everyone
who read it. If a script here starts ranking by "looks like the protocol",
that is the same defect returning.

## 4. Safety design

### 4.1 Config — near-safe
- **Read-modify-write always.** Never construct a settings blob from scratch.
- Validate a read is structurally plausible before acting on it. Never let a
  reported success stand in for verifying the data itself.
- **Never write after a failed read.**
- Read back and verify after every write; log the diff.
- Save a known-good blob to disk on first connect.
- **Implement factory reset first and confirm it works** before any other write
  path. It is the undo for bad config state. *(Gate cleared 2026-09-05: reset
  and restore both scored 21/21 on the device. The rule still governs any NEW
  write path — an undo comes before the thing it undoes.)*

### 4.2 Flash — two regimes
**Before erase: abort is always correct.** Nothing has changed on the device.
Any single preflight failure aborts. No path proceeds on a partial pass.

Preflight: validate image size and identity; if entering the bootloader
re-enumerates the device, confirm device identity again afterwards; if the
bootloader reports a version or an identity of any kind, refuse anything
unrecognised.

**Preflight sends nothing.** This clause used to also say "round-trip a benign
query and confirm a structurally plausible response". It was written before the
vendor's sequence was known, and it directly caused a defect: `preflight()` put
an `A1 08` on the wire immediately before `A0 03`, a frame the vendor never
sends, at the moment of maximum cost — found 2026-09-05, by counting what the
code emitted (134) against what it planned (133). The rule below about byte
streams settles the conflict against it, and `preflight` now takes no link, so
it *cannot* send rather than merely being told not to. A link that cannot carry
a frame fails at `A0 03` instead, which is a round trip the vendor actually
performs, and a frame that never went out has erased nothing.

The general lesson, because this is twice in one day: **a rule of ours that
adds a frame is a deviation, and being a safety rule does not exempt it.**

**After erase: quitting is the bug.** The device has no valid application;
exiting cleanly guarantees the bad outcome. Retry the failed chunk, re-establish
the connection if it drops, keep driving to a valid resident image. No cancel
button, no timeout that gives up. The erase-through-completion sequence is one
non-abortable unit; no code path returns from it without either a verified image
or an explicit, loud unrecoverable state.

**Mirror whatever verification the vendor protocol provides, exactly.** Free
safety is the best kind.

**Never erase without a saved copy of what is being erased.** No `A0 03` until
the current application region has been read back block by block, written to
disk, and the file re-read and checked. Read-back is not merely a way to
validate address arithmetic — it is how a failed flash stops being permanent,
because a saved image can be rewritten. If read-back does not work on this
device, that is a finding that changes the plan, not a step to skip.

**The backup is taken in its own run, never inside the flash.** The first
implementation obeyed the rule above by reading all 65
blocks immediately before `A0 03`, which put 65 `A0 07` frames in front of the
erase — something the vendor never does. That is the rule eating itself: a
safety rule of ours was inserted into the one byte sequence there is a capture
of, so the flash no longer matched the only evidence we have that any of it
works.

Two runs. `read-firmware` takes the backup and is read-only and abortable;
`flash` requires that file, validates it, and **sends nothing extra**. The
accepted cost is that a backup can go stale, which requires deliberately
flashing between the two runs.

The general rule this is an instance of: **a safety measure that changes the
byte stream is not free.** Weigh it against the derived sequence, and prefer the
form that leaves the wire alone. Off-wire guards — refusing, checking a file,
pinning a hash, requiring a token — cost nothing and are always allowed.

### 4.2a The necessity gate — answer in writing BEFORE proposing any send

Added after a day in which every command sent to the device was byte-perfect
and two of the three should never have been sent at all. **The frame's contents
were never the risk. The device's reaction was, and that is not derivable from
the frame.** "It writes nothing by construction,
so it is safe" was asserted three times in one session about `A1 3A`, `A1 09`
and `A0 07`. It is not a safety argument. It is a category error, and it is now
a banned sentence.

Before ANY byte reaches the device, these are answered in the pre-registration:

1. **What question does this answer?** If it answers none, it is not sent.
2. **Can that question be answered without touching the device?** Static
   analysis, a capture, the mock, the kernel log. If yes — **do that instead.**
3. **Is there a hardware-forced alternative?** If yes — **use it.** A physical
   action the operator performs cannot latch, cannot misfire, and needs no undo.
4. **If the effect is unpredictable, what is the recovery, and is that recovery
   still available afterwards?** A recovery path that the command itself might
   consume is not a recovery path.

Gate 3 alone would have prevented today: button entry was `[O]`, already
exercised three times, and does not latch.

### 4.2b Everything that touches firmware enters by `A1 3A`

This replaced an earlier rule, on the day that one was written. The earlier
rule said button entry always, because `A1 3A` latches and had just cost a day.
That was reactive, and it traded a KNOWN risk for an UNKNOWN one.

**The vendor's flash always begins from an `A1 3A`-entered bootloader.** Both
captures are that sequence end to end, on this mouse. There is no precedent
anywhere — not in the captures, not in either code base — for flashing a
bootloader reached by the buttons. `A1 3A` demonstrably sets persistent state,
and whether `A0 03` requires it is `[G]`. Entering the vendor's way deletes that
question instead of carrying it past the point of no return.

So, by risk regime rather than by preference:

**EVERYTHING THAT TOUCHES FIRMWARE ENTERS BY `A1 3A`** — overruling an earlier
split in this table that sent read-only work in by the buttons.

| work | entry | why |
| --- | --- | --- |
| **flash** | **`A1 3A`** | the vendor's proven path; no unknown at the moment of maximum cost |
| **firmware read-back** | **`A1 3A`** | see below |
| identity / enumeration only (nothing sent) | button or neither | no command is issued at all |

That split said read-only work enters by BUTTON, because abort is free there.
The objection to it is correct: **a test whose result does not transfer is not
worth its risk.** The flash reads blocks back inside an
`A1 3A`-entered bootloader. A read-back performed in a button-entered one
therefore proves nothing about the flash unless the two are identical, and
whether they are is `[G]` — the only fields we can compare (PID `0x1977`,
bcdDevice `0x0006`, product `Bootloader`) are `[O]`-identical, but an internal
flag set by `A1 3A` would not show in any of them.

The load-bearing half of this had already been conceded without anyone noticing
what it implied: a button-mode refusal was written down as "inconclusive about
the flash". That makes half the test's outcomes worthless, which is the whole
argument against paying anything for it.

**THE ACCEPTED COST, stated so it is a decision and not a surprise.**
Anything that aborts after `A1 3A` leaves the mouse latched in the bootloader,
and the only thing that clears the latch is a completed flash. That is
recoverable, not a brick: Endgame's updater treats PID `0x1977` as a supported
starting state (`updater-protocol.md` §5.1a, verified forward from raw bytes),
and so does this tool.

Two consequences of putting the read-back on this side of the line:

1. The latched window now opens at the READ, not at the erase. From the moment
   the read-back begins until a flash completes, the mouse is not a mouse.
2. The flash that follows finds the device already in the bootloader and
   **skips its own `A1 3A` — 134 frames, not 135.** Endgame's updater takes
   that same path for a device already at `0x1977` (§5.1a), so it is a
   supported sequence, but it is no longer byte-identical to the capture, and
   the capture was the whole basis for trusting the sequence. Weigh that
   before treating the golden test as covering what actually goes out.

**Therefore: do everything that can fail BEFORE sending `A1 3A`.** Image load,
hash check, device count, backup validation, approval token — all of it happens
with the mouse in application mode, so the latched window contains only the
commands that have to be there.

### 4.2c An approval must be bound to the exact bytes it approves

`--yes` on its own is a reflex, and a reflex is not a decision. Any verb that
writes to the device requires `--confirm <token>`, where the token is derived
from the exact byte stream of THE PLAN. The dry run prints it. If the plan
changes by one byte, the token changes and the old approval is void.

**"The plan", not "what goes on the wire", and the difference is one frame.**
The token covers all 135 frames including `A1 3A`. If the mouse is already in
the bootloader that frame is skipped and the other 134 are sent unchanged
(§4.2b; the vendor's updater does the same, `updater-protocol.md` §5.1a). The
token is deliberately the same in both cases, because it identifies what is
being approved and has to be reviewable BEFORE the mouse is in any particular
mode — a token that changed depending on which mode the device happened to be
in could not be printed by a dry run at all. Which case you are in is printed
by the preflight line, before anything is sent. *(Wording corrected
2026-09-06: this clause said "the exact byte stream that would be sent", which
the code has never matched and should not.)*

The point is not to slow anyone down. It is that **no approval can be given for
something the approver has not been shown**, including when the approver is
tired, in a hurry, or has typed the same command four times already.

### 4.3 What actually protects against the bugs that matter here
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

### 4.4 Staged bring-up — ALL FOUR STAGES COMPLETE, 2026-09-05

Read-only round trip, bootloader entry/exit, flash read-back, real flash. Both
tools have been driven end to end against the device. Details belong in the
commit messages and the working-memory file, not here.

**The rule this leaves behind, which governs every later capability** — the GUI,
new firmware images, anything that reaches the device:

**Order new work so each step removes untested code from the next one, and make
the cheapest irreversible step come last.** A read-only round trip before a
write; a zero-write address check before an erase; a reference to compare
against before trusting arithmetic. If a step cannot be made to prove something
the next step depends on, it is ceremony — cut it rather than pay for it (§4.2's
rule that a safety measure changing the byte stream is not free).

## 5. Device-gated work — what is STILL open

Everything else on this list was answered on 2026-09-05 and has been removed:
report descriptors, product IDs, macOS permissions, inter-command timing,
factory reset, and both bootloader entry mechanisms.

- **Recovery behaviour with the application region blank.** Never observed, and
  deliberately so — it needs a *failed* flash to produce. Assume it is not
  survivable evidence you have; the button entry (§4.2b) is the only recovery
  that is `[O]`.
- **Deliberate error states and malformed input.** The device has only ever been
  sent well-formed frames. How it answers a bad one is `[G]`, and the mock's
  hostile behaviours are invented rather than observed. **Do not "verify" this
  by sending the device something malformed** — §4.2a's gate applies, and there
  is no question here worth the risk.
- **`restore-firmware` on hardware — CLOSED BY DECISION, NOT BY EVIDENCE.**
  The device's owner declined to spend a flash cycle proving it, which is the
  right call. It is implemented, and every part of it that can be checked
  without a device is (`Tests/test_flash_restore.sh`, the mock, the mutants,
  the shared write phase it uses unchanged). What is untested is the only part a test
  cannot reach: how the device behaves. **Do not propose spending a flash cycle
  on it, and do not record it as a gap again.** The reason is not squeamishness
  — the test IS the risk. Exercising it means an erase, and if the restore path
  is wrong the erase has already happened. So: it stays `[G]` on the device, it
  is documented as such, and it is used only when it is the best remaining
  option, which is exactly the situation it exists for.
- **Anything a new firmware version changes.** The `[O]` facts here span TWO
  firmware versions on the one mouse, not one: everything observed before
  2026-09-05 07:53 local is **1.07**, everything after is **1.10**, because the
  vendor's own updater flashed it mid-run (dated from capture timestamps in
  `bootloader-observed.md` §5b.2). The only measured difference is one
  factory-default byte, record `0x71`, `01` → `00`, out of 1024. So the record
  LAYOUT is stable across 1.07→1.10 by measurement rather than assumption, and
  DEFAULTS are not: **never hardcode a default vector.** A third version is a
  different device until shown otherwise.

## 6. Reading policy for the binaries

Read **every** candidate function, meaning everything not positively identified
as statically-linked library code. A relevance score may set reading ORDER; it
must never decide what goes unread. If the candidate set is too large to read
exhaustively, **say so and give the number**. Do not silently sample.

**State coverage as a partition, never as arithmetic between separately-scoped
numbers.** `total − (union of every evidence set) == 0`, computed in one place,
with the residue enumerated. Two pipelines whose scopes overlap will otherwise
look like a shortfall — that is exactly how a phantom "40 missing functions"
appeared on 2026-09-03 when nothing was unread. And a number written in prose
survives a context compaction while the computation behind it does not, so it
returns looking like established fact: regenerate numbers, never quote them.
`Tools/ghidra-export/coverage.py <tag>` enforces this and exits non-zero on any
residue. It also refuses a reference binary that shares >90% of bodies with the
target, because 1.06/1.07/1.10 have byte-identical `.text` and self-matching
manufactures confidence.

### 6.1 Not every binary gets the same standard of proof
Updater **1.10 is the binary whose bytes reach the one mouse that exists.** The
others are corroboration. Treat them accordingly, and do not let "we understand
the family" stand in for understanding 1.10.

| binary | standard |
| --- | --- |
| **updater 1.10** | **Everything is read. Every function, library included** — a cross-binary body match is no longer an alternative to reading it (superseding the earlier "match or read" rule; see below). Plus every byte of the file accounted for, not just `.text`. |
| updaters 1.06, 1.07 | `.text` is byte-identical to 1.10 (same SHA-256), so 1.10's work covers their code by construction. They still get an exhaustive diff of everything that is *not* code — `.data`, PE headers, all resources. Identical code does not mean identical file. |
| updater 1.04 | Second code base. Band read. Drive its residue down by the same positive accounting, then read what survives. |
| config tools | Transport settled for 1.07; §4.1's gate is cleared. Internals still largely unread — the settings record is fully mapped and mostly unnamed, which is why `set` is a short list. |
| `FWFILE` blobs | All six names, all four versions. Ranked high: it is the only place a wrong-image guard could come from that works on an updater we have not read. |

The reason for the asymmetry is a `p99` argument, stated plainly: **missing one
important thing ends the project regardless of how much of the rest was
right.** Speed and coverage elsewhere do not compensate.

**Amended 2026-09-04.** An earlier version of this section said to *prefer*
positive accounting over reading, on the grounds that a cross-binary byte match
is a proof while thousands of function-reads carry their own error rate. That
reasoning is still true about the error rate, and it is the wrong conclusion for
1.10. Two corrections:

1. **The match is a hypothesis, not a proof, until something tests it.**
   `classify.py`'s premise — that a body appearing in an unrelated product is
   library — is an assumption. Reading the matched set is how that assumption
   gets *tested*, and a single vendor function that byte-matches something
   elsewhere would invalidate the same inference in every other binary. So
   reading library code is not wasted effort; it is the only empirical check on
   the classifier the whole accounting rests on.
2. **The error rate of reading is an engineering problem, not a reason to skip
   reading.** If a reader is unreliable, do not conclude "read less" — build a
   harness that *measures* the reader and makes the safety-critical property
   decidable without trusting it. §6.2.

Scope note: "9,076 functions" was always Ghidra's count of the functions it
found, which is about 89.5% of `.text` by bytes. Never state a coverage number
without the scope in the same sentence.

### 6.2 A reader you cannot trust must not be load-bearing
Agents reading thousands of functions will skim, pattern-match, and tell you what
they think you want. That is a property of the reader, and the answer is to stop
resting safety on its judgement:

- **Decide the safety-critical question mechanically.** Whether a function can
  touch the device is answerable by exhaustive literal scan — IAT slots, device
  globals, protocol constants, raw call edges. Those are script-computed,
  reproducible by anyone with `objdump` and `grep`, and cannot be faked by a
  reader. Agent reading adds meaning *on top*; it never clears a function the
  mechanical filter flags.
- **Never state the expected answer in the prompt.** Telling readers "these are
  almost certainly all library" produced 436/436 and 112/112 unanimous verdicts.
  That is a leading question, and the unanimity is evidence of the prompt, not of
  the code.
- **Plant known positives.** Seed each batch with functions that *are*
  device-facing, unlabelled. A batch that calls one of them library is discarded
  and re-run. This measures the false-negative rate instead of assuming it.
- **Require machine-checkable facts.** Every verdict must carry claims a script
  can verify against the bytes — size, call targets, referenced literals. Wrong
  verifiable facts invalidate the judgement that came with them.
- **A harness that cannot produce a bad result is not evidence.** If the planted
  positives never fail and no verdict is ever rejected, the harness is measuring
  nothing. Report its failure rate; a rate of zero is a red flag, not a pass.

## 7. Why there are no protocol findings in this file

A prior analysis session produced a large body of protocol findings. They are
withheld deliberately.

That analysis was **wrong at least three times** — each error stated with
confidence, each caught only by redoing the work more carefully, never by
review. Handing those conclusions over as a starting point would propagate the
errors and turn verification into confirmation.

**Derive everything independently first.** The prior material is released for
comparison only once the independent derivation is complete and committed.
Disagreements are expected. They mean one of: the prior analysis was wrong, the
new analysis is wrong, or the behaviour is version-dependent. **The tiebreaker
is the device, never authority or seniority.**

### 7.1 The contamination reached this repo, not just the notes
Withholding the findings was not enough. Two leaks were found and removed.

**The tooling.** A Ghidra extraction script scored functions for specific
hardcoded constants and pattern-matched a specific command shape, so it carried
the prior conclusions inside it and anyone reading it inherited them. Deleted,
not repaired. A replacement must record structure without encoding a belief
about what the structure means.

**The prose.** A "product context" section asserted vendor documentation and
preferences that were never sourced, part of it confirmed false, and §4.2 and
§4.4 asserted protocol details in a file that claims to hold none. Gone. §1.1
was reviewed and kept as a rule, then narrowed further: it describes only the
obligation to derive independently, and names no third-party project at all.

**Contamination hides in whatever is not treated as a claim**: a scoring
heuristic, a variable name, a section that reads as background rather than
analysis, a hedge that has become an assertion by its third restatement.
