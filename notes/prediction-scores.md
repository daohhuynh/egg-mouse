# Scoring the pre-registered predictions

`notes/wire-predictions.md` holds a register of predictions **committed before
any capture existed** (commit history proves the order; that is the only thing
that makes this a proof rather than a postdiction). This file is the scoreboard.
One entry per prediction that reality has now touched, `CONFIRMED` / `REFUTED` /
`PARTIAL`, with **what was actually observed** and by whom.

**The register's size is not written here on purpose.** Run
`python3 Tools/notes/reconcile_scores.py`; it counts four independent ways and
fails the build if they disagree. This sentence said "266" until 2026-09-07 —
the correction was already 28 lines below it and had been for a day, and `266`
was even listed there among "three numbers that are now gone". It was not gone.
It was the first factual claim a reader met, which is the only position in the
file where a stale number does real damage.

**Rules for this file, and they are the point of it.**

1. **Score against the `REFUTED IF` line as written**, never against a softened
   restatement. If the observation is ambiguous under the original wording, that
   is `AMBIGUOUS` and the prediction was badly written — record that too, it is
   a fault in the register.
2. **Zero REFUTED is a red flag, not a result** (§6.2). If a whole area comes
   back clean, the likely explanations in order are: the predictions were
   written vague enough to be unfalsifiable, the observer was told the expected
   answer, or the observation did not actually test them. Say which.
3. An entry needs the **observer and the date**. the owner reading a screen is `[O]`.
   Me reading a capture file is `[O]`. Me reasoning about a binary is not an
   observation and cannot score anything.

## Running tally — REGENERATED, never quoted

**Run `Tools/notes/reconcile_scores.py`.** It recounts the register four
independent ways, resolves every slug scored in this file, refuses a name that
resolves to nothing, refuses a prediction scored twice, and checks the table
below against the entries that are actually present. It exits non-zero on any
of those. If you are about to type a number into this file, run it instead.

**The register does not hold 266 predictions**, which is what this file said
for weeks. The four counts above agree with each other and with the denominator
in the table below; `266` agreed with nothing and could not be sourced. It had
been restated here as fact — the §7.1 failure mode, in the file whose whole job
is to resist it. The count itself is deliberately not repeated in this
paragraph: the tool prints it, the table below carries it, and the tool fails
the build if the two disagree.

A single tally would also be arithmetic across scopes, which §6 forbids. Three
scopes, and **nothing is ever added across the rows**:

| scope | CONFIRMED | REFUTED | PARTIAL | denominator |
| --- | --- | --- | --- | --- |
| `wire-predictions.md` register | 6 | 0 | 2 | 271 |
| pre-registered elsewhere (bare slugs, declared below) | 2 | 1 | 1 | not enumerated |
| device runs, own pre-registration files | 3 | 0 | 3 | 6 |

Zero REFUTED **in the register** at n=8 is still not informative, and the two
added on 2026-09-06 (#13, #14) make it *less* informative rather than more.
Both were scored by me reading files with a parser — the cheapest and most
mechanical kind of check there is, on predictions that were essentially
transcriptions of what those files contain. A register that only ever gets
scored where scoring is easy will read as vindicated no matter what is in it.

What would actually be informative, in order of cost: the **GUI area**, ~15
predictions, answerable by looking at the screenshots already in the repo and
by an observer who is not me; and the **updater areas**, now unblocked because
`08-flash.pcapng` exists, where a refutation would mean the derived command set
is incomplete. #19 is still the single highest-value one for that reason.

**Where the refutations actually are, corrected 2026-09-07.** This paragraph
used to end "The one REFUTED so far came from outside the register", and that
was the most damaging sentence in the file: it is the one Rule 2 exists to
guard, and it was understating the project's real refutation rate by a factor
of three. There are **four** refuted predictions on record, and three of them
came off the device:

| where | prediction | outcome |
| --- | --- | --- |
| out-of-register slug | (the one this sentence used to mean) | REFUTED |
| `prediction-bootloader-entry.md` #3 | `resp[1] == 0x01` | **REFUTED — `0x03`** |
| `prediction-bootloader-entry.md` #9 | returns to `0x1978` on a power cycle | **REFUTED — it latches**, and this is the single most consequential fact in the flash design (CLAUDE.md §4.2b) |
| `prediction-capture-1.10.md`, the `16-keys` block | Left Shift records `+1 = 00` | **REFUTED — `02`**, the dialog sets modifiers from live keyboard state |

They were invisible here because the scoreboard simply had no entry for the
files that hold them. That is now checked rather than remembered:
`reconcile_scores.py` fails if any `notes/prediction-*.md` is neither scored in
this file nor declared unscored below.

**Three numbers that were in this file and are now gone**, recorded because the
pattern matters more than the arithmetic: `266` (a denominator nobody could
source), `4–0` in the table above, and `CONFIRMED 6, REFUTED 1` written into the
prose of a later section. All three were quoted rather than computed, all three
disagreed, and every one of them read as established fact. That is why the
counts above are now generated by a script that fails the build when they drift.

### Slug declarations — RECONCILED 2026-09-06, and machine-checked from here on

Every name this file scores under must resolve, because **a name that does not
grep cannot be de-duplicated**, and two entries scoring one prediction under two
names inflate the tally silently. Each row below is read by
`Tools/notes/reconcile_scores.py`; the marker text is load-bearing, not
decoration.

| name used here | |
| --- | --- |
| `updater-idle-window` | ALIAS OF: `upd-idle-state` — the register entry at `wire-predictions.md:2908`, whose `REFUTED IF` this file quotes verbatim. Same prediction, renamed by accident. |
| `updater-ui-four-controls` | ALIAS OF: `upd-window-one-dialog` — the four-control claim at `wire-predictions.md:2872`. Declared so the name resolves; it is **not scored** (see below — the owner named three controls and was not counting). |
| `led-page-unreachable` | ALIAS OF: `cfg-led-page-never-appears` — `wire-predictions.md:3023`. Scored PARTIAL; the register entry contradicts another register entry, see there. |
| `cfg-basic-page-invisible-buttons` | ALIAS OF: `cfg-hidden-buttons-stay-hidden` — `wire-predictions.md:3032`. Not yet scored. |
| `led-liftoff-inverted` | OUT OF REGISTER: `config-protocol.md` §7.8. **Not** an alias of the register's `disable-led-liftoff-inverted`, and the distinction is the whole point of rule 1 — that entry's `REFUTED IF` is about the value of report byte `0x18`, and nobody has read that byte. What was confirmed is the *semantics*, behaviourally. |
| `rgb-block-tag-is-cpi-stage` | OUT OF REGISTER: `config-protocol.md` §7.3. |
| `button-menu-groups` | OUT OF REGISTER: `config-protocol.md` §7.13, scored 4/4 further down this file. |
| `keyboard-modifier-byte` | OUT OF REGISTER: `config-protocol.md` §7.12, scored 3/4 further down this file. |

**Two things the reconciliation turned up that the gap note did not predict.**

1. **Two of the seven were wrongly said to be absent from the register.** The
   old note claimed `led-liftoff-inverted` "is not a `####` entry" and
   `led-page-unreachable` was "in no notes file but this one". Both are `####`
   entries, under `disable-led-liftoff-inverted` (line 2266) and
   `cfg-led-page-never-appears` (line 3023). The note was written by searching
   for the exact string; the register uses a prefix. That is §1.2a's rule — a
   scan that finds nothing says nothing about the forms it cannot see — applied
   to our own notes rather than to a binary.
2. **A register prediction was being scored under no slug at all**, in the
   `send firmware block data...` entry below. It has one now
   (`upd-click-sequence-app-mode`), and it is PARTIAL rather than CONFIRMED,
   because the entry's own text says it does not score the surrounding
   sequence.

Also note the register numbers **restart per section** — there are eight `#20`s.
`#N` alone identifies nothing here; always carry the slug.

**Watch this specifically:** all three confirmations so far are the *same kind of
claim* — "a control shipped in the resource is hidden by code at page-init, so
it is not on screen", read off `ShowWindow(…, SW_HIDE)` calls in `OnInitDialog`.
Three hits from one technique is one technique working, not three independent
successes, and it should not be reported as though the register is 3-for-3 on
unrelated ground.

---

## GUI surface

### #20 `cfg-basic-page-hidden-controls` — **CONFIRMED**

Observer: the owner, at the machine, 2026-09-05, mid-capture and unprompted — he went
looking for Ripple Control because `log.txt` line 9 told him to click it, not
because he was asked whether it was there.

- **Predicted:** "the 'Ripple Control' checkbox is present in the resource but
  hidden by code at page-init, so it is not on screen." REFUTED IF visible.
- **Observed:** *"ripple control isnt a tick box anywhere."* Not visible.
- **Mechanism predicted and now supported:** page-135 `OnInitDialog` at
  `0x0040bbd0` calls `ShowWindow(this+0xf78, SW_HIDE)` at `0x40bbd9`, and the
  control's `BN_CLICKED` handler is the bare `ret` at `0x00413d90`.

Three sub-claims in the same prediction also held: the owner has separately confirmed
`Angle Snapping`, `Disable LED on Lift-Off` and `X/Y Settings` are all present
on that page — and he found `X/Y Settings` himself, which I had left out of
`log.txt` entirely, so that one is a confirmation I could not have fitted.

**Why this one matters beyond its own line.** It is the second independently
confirmed case of the vendor shipping a control it hides at init, the first
being the whole LED page (`gui-surface.md` §3). That pattern is the evidence for
a claim that governs our writes: **the record contains fields this firmware no
longer honours**, so §1.3's read-modify-write is not pedantry — there are known
bytes behind dead UI, and we must carry them through untouched.

### #21 `cfg-advanced-sensor-hidden-controls` — **CONFIRMED**

Observer: the owner, 2026-09-05, mid-capture, prompted only by `log.txt` telling him
to click them.

- **Predicted:** on the Advanced Sensor page, `Motion Jitter Filter` and
  `Sensor Glass Mode` are hidden by code and do not appear. REFUTED IF either
  is visible.
- **Observed:** *"motion jitter filter and sensor glass mode are not seen
  anywhere on advanced sensor"*. Neither is visible.
- **Cite that held:** page-140 `OnInitDialog` `0x00411980` calls
  `ShowWindow(this+0x1a4, 0)` at `0x411a19` and `ShowWindow(this+0x130, 0)` at
  `0x411a26`, DDX-bound to control ids 1030 and 1031.

The rest of that prediction is **not yet scored**: it also says the Sensor Angle
Tuning slider runs −127..+127 symmetric, and the `REFUTED IF` covers that too.
The owner has confirmed the default is 0 but not the range. Do not mark this fully
confirmed until the maximum is read off the screen.

### `led-page-unreachable` — **PARTIAL** (`gui-surface.md` §3)

Observer: the owner, 2026-09-04: *"theres only 4 tabs and you named then all already
basic advanced sensor buttons button mapping, i only see disable LED on lift off
as the mention of LED anywhere in the app."*

**Was scored CONFIRMED. Downgraded 2026-09-06 while reconciling the slugs, and
the downgrade is the finding.** Rule 1 of this file: score against the
`REFUTED IF` as written, never against a softened restatement. Resolving the
alias to `cfg-led-page-never-appears` put the original wording in front of the
observation for the first time, and they do not fit:

| the register entry says | the owner observed |
| --- | --- |
| "The config tool 1.07 exposes **NO LED controls anywhere**." | "i only see **disable LED on lift off** as the mention of LED anywhere in the app." |
| REFUTED IF: "**Any** LED control, or the string 'Singel color', is visible anywhere in the config tool." | an LED control, visible |

Read as written, that is a refutation. Read as intended — DIALOG 137's own
controls (`LED On / Off`, the `LED effect` combo, `Scroll led`, `Logo led`,
`DPI led`, the RGB edits, `Apply led settings`, and the `Singel color` typo) —
it is a clean confirmation, and the owner's answer covers every one of those by name.

**The register contradicts itself, and that is worth more than the point.**
`cfg-led-page-never-appears` (#18) says no LED control is visible anywhere.
`disable-led-liftoff-inverted` (#11) predicts, in detail, the behaviour of an
LED control that IS visible — page 135, control 1066, the very one the owner named.
Both are `[D]`, both were committed in the same file, and each was written
without reading the other. Neither derivation is wrong; the word "anywhere" is.

So: **PARTIAL**, and the fault is recorded against the register per rule 1. What
is confirmed is the load-bearing half — DIALOG 137 exists in all four config
tools and is reachable in none, predicted from a four-entry jump table bounded
by `cmpl $0x3` and a single shared hide call at `0x413c9e`, so there is no
unread LED command family. Consequence: the RGB-looking bytes at wire `0x1d`
(`wire-observed.md` §5.1) **stay [G] permanently** — no capture can move them,
because no UI can change them.

**The lesson for writing the remaining 266.** A `REFUTED IF` that says "any X
anywhere" is almost never the claim being made, and it converts a correct
derivation into a scoreboard argument. Bound the negative to the search space it
was derived from — here, "any control from DIALOG 137".

---

## Observed, and deliberately NOT scored

Things reality has settled that **were not pre-registered**. They belong here
rather than in the tally, because counting them would inflate a number whose
only value is that it was fixed before the data existed.

- **`X/Y Settings` writes nothing** (`gui-surface.md` §6). the owner found the control
  himself — it was not in `log.txt` and not in the register — and then found
  that ticking it does not enable APPLY. I had claimed from the wire that the
  X/Y lock is host-side only, and this agrees with that claim. **It is still not
  a scored prediction**: I wrote that claim down *after* he told me the control
  existed, so it is a successful postdiction and nothing more.
- **The APPLY button is gated on dirty state** (`gui-surface.md` §5). Not
  predicted at all. It is load-bearing for the capture method, which is why it
  is written up, but it scores nothing.
- **The mouse reports firmware 1.07, VID `0x3367`, PID `0x1978`** in the info
  reply (`wire-observed.md` §2.2). All three were known independently before the
  capture, so decoding them confirms the *decode*, not a prediction.

The distinction matters more than it looks. Three plausible-sounding hits that
do not count is exactly the pressure a scoreboard has to resist to stay worth
keeping.

## Not yet scored, and what it will take

- ~~**`button-menu-groups`**~~ — **SCORED 4/4 on 2026-09-06**, see §7.13 at the
  end of this file. Left here with its original framing because the framing was
  right and is worth keeping: (`config-protocol.md` §7.13). Committed 2026-09-05:
  the button dropdown's top-level groups are `MOUSE`, `KEYBOARD KEY`, `CPI`,
  `MEDIA`, `DISABLE`, in that order, and no others. Recovered from the string
  table's declaration order, where submenu items precede their group name.
  **Deliberately withheld from `log.txt`**, which still asks the owner the open
  question — he has already screenshotted the dropdown and every submenu, so
  this is checkable against existing material by an observer who was not primed.
  The membership claim is much stronger than the order claim; score them
  separately.

- **#19 `cfg-basic-page-invisible-buttons`** (`Apply CPI settings` id 1059,
  `Surface Calibration` id 1061). **THE ROUTE IS VOID.** It was "two questions
  in `log.txt`'s ANSWERS block", and `./log.txt` is quarantined as evidence
  (CLAUDE.md §1.1a). The replacement route needs no new capture and no new
  question: `windows-run/screenshots/*.png` are verified factory defaults
  (`config-wire-observed.md` §7) and show the basic page. Score it by looking,
  or ask the owner once, cleanly. This one has real weight: `Surface Calibration` would be a
  device command **outside the four-command set we have derived**, so a refutation
  here means our command set is incomplete.
- ~~**#21 `cfg-advanced-sensor-hidden-controls`**~~ — **ALREADY SCORED
  CONFIRMED** earlier in this file. It was left in this "not yet scored" list
  after being scored, which is the drift this file's own tally script exists to
  catch and which the script cannot see: it counts verdicts, not stale prose.
  Kept struck through rather than deleted, as the record of that.
- **#22 `cfg-combo-cpi-downshift`**, and the Smoothing Tuning three-item
  prediction at `wire-predictions.md:3070`. **THE ROUTE WAS VOID** for the same
  reason — it said "`log.txt` asks for exactly that". It is now partly answered
  from material that survives the quarantine: the screenshots give the CPI
  Downshift list as Force Off / Light Timer Only / Medium Timer Only / Default
  and Smoothing as Force Off / Ripple Control Off / Ripple Control On, which is
  what the decoders were checked against on 2026-09-06 (`Tests/test_show.sh`).
  Score the two predictions against those lists as written, rather than
  treating the decoder work as having scored them.
- The four **updater** areas (bootloader entry, start command, block write
  loop, read-back) were blocked on `08-flash.pcapng`. **That file has existed
  since 2026-09-05** and this bullet went on saying it did not. Two of them are
  scored from it now — see "Updater areas" below — and the rest are reachable
  the same way: the captures are in the repo, the parser is in
  `Tools/capture/usbpcap.py`, and nothing about them needs the mouse.
  The *launch* area is partly scored — see below.

## Updater areas — scored from the flash captures, 2026-09-06

The bullet below used to say these needed `08-flash.pcapng`, "which does not
exist yet". **It exists** (`windows-run/08-flash.pcapng` and
`09-flash-again.pcapng`, both of the vendor updater flashing this mouse), and
has since 2026-09-05. Two register predictions are scored here off those
captures and the checked-in binaries; both are mechanical, both are pinned in
`ctest` so they cannot silently rot.

### #13 `alternate-updater-payloads` — **CONFIRMED**

Observer: me, reading capture files and .exe files with the committed parsers,
2026-09-06. No reasoning about a binary is doing any work here — every number
is a byte read out of a file.

- **Predicted:** all four updaters' `FWFILE`/140 are 66,560 bytes / 65 blocks,
  each with its own sha256, whole-image sum32 and first 16 bytes; and the
  `A0 03` checksum in a capture identifies which .exe was run **with no
  filesystem access**. REFUTED IF the `A0 03` checksum bytes in the capture
  match none of the four.
- **Observed, .exe side:** all four reproduce exactly with `Tools/pe/fwfile.py`
  — 1.10 `0x0081D57D`, 1.07 `0x0081D408`, 1.06 `0x0081F40B`, 1.04 `0x0081F627`,
  sizes and first-16 as predicted, including the stated quirk that 1.06 and
  1.04 share a first block and differ only in the whole-image value.
- **Observed, capture side:** both flash captures carry exactly one `A0 03`
  SET_REPORT, and both read
  `a0030000000000000000000000000000 41 7d d5 81` — LE32 `0x0081D57D`, which is
  **1.10 and only 1.10**. The prediction's whole point, that the capture alone
  names the running executable, holds.
- **Pinned:** `Tests/test_fwfile_set.py::PredictionThirteenAlternateUpdaterPayloads`.

### #14 `config-tool-sends-no-firmware` — **CONFIRMED**

Observer: me, 2026-09-06, same standard.

- **Predicted:** the four config tools expose 74 resource leaves each and ZERO
  of type `FWFILE`; no leaf is 66,560 bytes; the UTF-16 string `FWFILE` occurs
  0 times. REFUTED IF any config-tool capture contains an `A0 06` frame or any
  run of 1024 high-entropy payload bytes.
- **Observed, .exe side:** 74 leaves in every one of the four, zero `FWFILE`,
  no 66,560-byte leaf, zero UTF-16 `FWFILE`. The same check run against two
  updaters objects on all four counts, so it is not vacuous.
- **Observed, capture side:** **sixteen** config-tool captures
  (`windows-run/00-07`, `10`, `windows-capture/11-17`) — every capture in the
  repo that is not one of the two flash runs. `A0 06` frames: **0**. Runs of
  1024 payload bytes with Shannon entropy > 7 bits/byte: **0**. The vendor
  command set observed across all sixteen is exactly
  `A0 11`, `A1 01`, `A1 02`, `A1 12`, `A1 13` plus standard descriptor
  requests — no flash command appears anywhere.
- **Why it is load-bearing rather than trivia:** CLAUDE.md §3's two-executable
  split rests on the config tool having no flash path. If it had one, the
  contradictory-quit-semantics argument would need revisiting.
- **Pinned:** `Tests/test_fwfile_set.py::PredictionFourteenConfigToolsCannotFlash`.

**Neither of these was scored against a softened restatement** (rule 1): both
`REFUTED IF` lines are about captures, and both were checked against captures,
not against the .exe half that was easier to reach.

## Scored 2026-09-05

### CONFIRMED — `updater-idle-window` (`wire-predictions.md:2908`)

> "At launch, before any click, the status box is empty, the progress bar is at
> 0, and the 'Update Firmware' button is enabled."
> REFUTED IF: "The updater shows any status text, **a firmware version**, or a
> non-zero progress bar before the button is clicked, or the button starts
> greyed."

The owner, 2026-09-05, unprompted: *"the firmware version is not shown by the updater
before clicking anything, its just what i told you earlier, a text input box, a
bar, and a start button."*

Observer: the owner. Method: looked at the running updater on the Windows laptop.
This scores because the `REFUTED IF` names the firmware version explicitly and
he addressed exactly that, without being asked the question in that form — he
raised it while querying a different line in `log.txt`. The derivation behind it
was that fw110's `OnInitDialog` sets two window icons and returns, enumerating
nothing.

**Not** scored from the same sentence: `updater-ui-four-controls`
(`wire-predictions.md:2872`) predicts *four* controls — progress bar, button,
`Status:` static, edit. the owner named three and did not mention the `Status:` label.
A small static caption is the likeliest thing to go unlisted in an informal
description, so this is **corroboration, not a confirmation**, and it stays
unscored until someone counts controls deliberately. Scoring it off a sentence
that was not answering that question is how a scoreboard starts lying.

### REFUTED — `rgb-block-tag-is-cpi-stage` (`config-protocol.md` §7.3)

> Predicted, before asking: the four 5-byte colour records are the CPI-stage
> indicator, and the trailing `01 0N` byte says which stage each colour belongs
> to — so stage 1 yellow, 2 blue, 3 red, 4 green.
> REFUTED IF: "the observed colour at any stage is not the one predicted, or the
> colours do not follow the stage order."

The owner, 2026-09-05: *"the official cpi level numbering blue is CPI 1, green is CPI
2, yellow i CPI 3, and red is CPI 4"*.

Observer: the owner, from Endgame's own level numbering plus the indicator on the
underside of his mouse. **The colour set matches exactly and the order does
not** — tag→stage is `1→3, 2→1, 3→4, 4→2`. The prediction was written with the
`REFUTED IF` naming the order explicitly, so it fails on its own terms and no
re-reading rescues it.

**This is the first REFUTED entry in the project**, which matters more than the
three CONFIRMED ones. §6.2 says a harness that never produces a bad result is
measuring nothing, and until today this scoreboard had never produced one. It
can. What it caught was not an arithmetic slip but a `[G]` that had been
restated until it read as a finding — the failure §7.1's closing line names.

Salvage, stated so the refutation is not quietly widened either: the block's
*shape* survives and is now `[O]` (four × `[3 colour bytes][0x01][position]`,
stride 5, records `0x0f`–`0x22`, bounded by the CPI block at `0x23`). What died
is the tail's meaning, and with it the claim that this block is the DPI
indicator at all.

### CONFIRMED — `led-liftoff-inverted` semantics (`config-protocol.md` §7.8)

Derived, from `sete` (not `setne`) at cfg107 `0x40ecd2`: the "Disable LED on
Lift-Off" box stores the **inverse** of its tick, so record `0x08` means "LED
enabled on lift-off" and `1` is the normal state.

The owner, 2026-09-05: *"when i disabled it and then applied and lifted the mouse the
LED color CPI wasnt there anymore so thats what it does."*

Observer: the owner, on the device. This is a **behavioural** confirmation of a
polarity derived from one instruction, and it is the strongest kind available
short of a capture, because it is the direction of the effect rather than a byte
value — a `setne`-vs-`sete` mix-up would have shown as the LED going out with
the box *unticked*.

It also settles what "the LED" refers to at all: the underside DPI indicator,
not a separate light. Note the sequencing — the owner first reported that the setting
did **not** affect the indicator, then corrected himself once he had pressed
APPLY. The first report is what the earlier draft of §7.3 briefly rested on, and
it is a good reminder that an observation taken before the write lands is not an
observation of the write.

### PARTIAL — `upd-click-sequence-app-mode`, the `send firmware block data...` string

**This entry had no slug until 2026-09-06**, so nothing could tell that it was
scoring a register prediction at all — it was invisible to any de-duplication
and absent from every count. It scores `upd-click-sequence-app-mode`
(`wire-predictions.md:2917`), which committed the updater's whole observable
sequence after one click, including that the status becomes exactly
**`send firmware block data...`** while the progress bar advances.

**PARTIAL, not CONFIRMED**, and only because the entry's own last paragraph
already said so: one clause of a seven-step prediction is confirmed and the
`REFUTED IF` covers the rest. The verdict now matches the text.

The owner, 2026-09-05, mid-flash and unprompted: *"while the firmware was downloading
and the progress bar was progressing it said something like 'send firmware block
data...'"* — he flagged it as possibly mistyped. It is not: the string sits at
file offset `0x143960` in updater 1.10 as UTF-16, character for character
including the three dots.

Observer: the owner, on the running updater. Scored narrowly: this confirms the
**string** and that it appears during the block phase with the bar moving. It
does **not** yet score the surrounding sequence, the 2% jump, or the timing,
which need `08-flash.pcapng` and the screen order.

Worth recording as method rather than as a hit: he was about to spend effort
re-deriving a string that was already in the binary. Pulling all 11 status
strings out of updater 1.10 (`0x1437d0`–`0x143ab4`) converted the rest of that
section from transcription into ticking boxes, and turned "anything else on
screen" into a positive finding — a string not in that list would mean text from
code that has not been read.

### The technique-diversity caveat now partly lifts

Every prior confirmation used the same technique (`ShowWindow(SW_HIDE)` in
`OnInitDialog`), which the earlier note flagged as a weakness — three hits from
one method is close to one hit. This one is a different method on a different
binary: reading what `OnInitDialog` *omits* rather than what it hides.

All of these were scored the same day, and all from things the owner volunteered
rather than from questions he was asked. The refutation is the one that tells us
the scoreboard works.

*(A tally stood here — "CONFIRMED 6, REFUTED 1" — that added the register and
non-register scopes together, which §6 forbids, and disagreed with the table at
the top of this file. Removed 2026-09-06. Counts live in one place now, and a
script regenerates them.)*


---

## Device runs, 2026-09-05 — scored from their own pre-registration files

These are not in the register at all. Each has its own file, committed to git
**before** the command ran, which is the only thing that makes them proofs
rather than postdictions; the commit is named in each.

**Six files, not three, and the three added on 2026-09-07 are the ones that
matter most.** This section carried `prediction-factory-reset.md`,
`prediction-restore.md` and `prediction-read-firmware.md` and stopped there —
so `prediction-bootloader-entry.md`, `prediction-postwindows.md` and
`prediction-capture-1.10.md` were scored in their own files, by their own
tables, and counted nowhere. Two of the three carry this project's only
device-run REFUTATIONS. A scoreboard whose denominator silently omits the runs
that refuted things is the exact failure Rule 2 names, and it had been sitting
here for two days looking like a clean sheet.

### PARTIAL — `notes/prediction-bootloader-entry.md`, **9 of 11, and the two misses are the finding**

`egg-flash enter-bootloader` on the real device, committed at `2094d98` before
the frame went out. Full table in that file; the two that missed:

- **#3 `resp[1] == 0x01` — REFUTED, it came back `0x03`.** A value in neither
  capture. The pre-registration said explicitly it was *not gated on*; had the
  tool required `0x01`, it would have reported failure on a **successful**
  entry and the natural next move would have been to send `A1 3A` again. The
  general lesson is in that file and it is worth more than the prediction: a
  status gate on an undocumented byte manufactures false failures.
- **#9 `returns to 0x1978 on a power cycle` — REFUTED, it latches.** This is
  the most consequential refutation in the project. CLAUDE.md §4.2b's accepted
  cost, `egg-flash`'s help text, and the whole shape of the enter →
  read-firmware → flash sequence exist because of it.

Two more (#10, #11) were **UNTESTABLE** until the device was back, which is a
fault in the predictions rather than a result — they were written to be scored
in a state the run itself made unreachable.

Observer: the owner at the machine, 2026-09-05 19:35:40.

### CONFIRMED — `notes/prediction-postwindows.md`, **7 of 7**

The post-Windows-flash read-back, committed at `b746d0f` before it ran. Scored
mechanically rather than by eye: the 21 predicted rows were re-parsed out of
`prediction-factory-reset.md` and set-compared against the actual byte deltas
between the vault and `after-windows-flash.bin` — zero predicted-but-unmoved,
zero moved-but-unpredicted, zero wrong values.

**7 of 7 with zero refuted is exactly the shape Rule 2 says to distrust**, so
the honest qualifier: #6 is a HIT *by transitivity*, not by direct measurement,
and that file says so at the row rather than in a footnote. The other six are
direct.

### PARTIAL — `notes/prediction-capture-1.10.md`, the 2026-09-06 Windows session

Committed at `8a25286` before any capture file was opened. Scored in
`config-wire-observed.md` §8, which is where the detail lives; the summary:

| block | outcome |
| --- | --- |
| `13-cpi-stage34` — stage 4's flag follows **stage 3's** boxes | **HIT**, and observed from both ends (§8.1). The project's only prediction that asserts the vendor's own tool computes something incorrectly |
| `15-media` — all eight MEDIA actions | **HIT** on the action set (§8.2) |
| `14-fixed-cpi` — X and Y independent, argument order | **HIT** (§8.3), and the normaliser confirmed on the wire at 2505 → 2510 |
| `16-keys` — keypad `+` reaches the record as usage `0x57` | **HIT, and the good branch** (§8.4) |
| `16-keys` — Left Shift records `+1 = 00` | **REFUTED — `02`** (§8.4). The dialog sets modifier bits from live keyboard state, independently of the key it records |

**And one thing this run got wrong that was mine, not the capture's.** §8.2's
first reading of `15-media` concluded that `+2`–`+5` stay zero. The capture says
BROWSER and EXPLORER write `01` at `+2`, and that misreading reached the code —
`egg-config map <btn> browser` emitted a byte pattern the vendor never produces
for about a week. Recorded here and not only in §8.2, because a scoreboard that
lists only the predictions and not the misreadings of the evidence is scoring
half the process.


### CONFIRMED — `notes/prediction-factory-reset.md`, **21 / 21**

`egg-config factory-reset --yes` on the real device, committed at `47f5959`
first. 21 payload bytes predicted to move, to named values; 21 moved, to exactly
those values, and nothing else moved. The device afterwards is byte-identical to
`10-postflash-baseline` across all 1039 comparable bytes — our `a1 13` and
Endgame's own produce the same record.

Observer: the owner at the machine. **This cleared CLAUDE.md §4.1's gate**, which had
blocked every config write since the captures showed section 07's reset test was
vacuous.

### CONFIRMED — `notes/prediction-restore.md` stage A, **21 / 21**

`egg-config restore ~/.egg-mouse-known-good.bin --yes`, committed at `c433007`
first. The first 1041-byte `SET_REPORT` this machine has ever sent. 21 predicted
rows, 21 observed, **0 mismatches and 0 extras**, scored by comparing the two
tables as maps rather than by eye. The factory-reset table with its value columns
swapped is *equal as a map* to what restore produced, so the two runs are the
same 21 bytes traversed in opposite directions — which is what they had to be if
both readings were right.

Observer: the owner at the machine; the map comparison is mine, mechanical.

**Its stage B is deliberately NOT scored** and must not be counted here. No
prediction was offered on persistence, on purpose, so the observation that the
record survived a power cycle (0 of 1024 payload bytes differ after 8.6 s
unplugged) is a finding, not a hit. Counting it would be exactly the inflation
the "Observed, and deliberately NOT scored" section above exists to refuse.

### The technique-diversity caveat lifts further

The earlier warning was that every confirmation used one technique
(`ShowWindow(SW_HIDE)` in `OnInitDialog`). These two are a fourth kind: a
byte-level prediction about what a *write* does to device state, scored against
the device rather than against a screen, with 42 individually falsifiable byte
values across the two runs and a cross-check between them that neither could
have passed alone.

**And they are the first predictions in this project scored by a script rather
than by reading.** Every earlier entry rests on me comparing prose to a
sentence. §6.2 says a reader that cannot be trusted must not be load-bearing;
these two are the first entries where it isn't.

---

## §4.4 stage 3 — `read-firmware`, scored 2026-09-05

### PARTIAL — `notes/prediction-read-firmware.md`, **1/2 testable, and the MISS is the useful one**

**Given this heading 2026-09-06.** It is a device run scored from its own
pre-registration file, exactly like the two above, and it was sitting under a
`##` where nothing counted it — the same defect as the unslugged status-string
entry. It is PARTIAL and always was: P8 hit, P6 missed.

`python3 Tools/score-read-firmware.py backup.bin`, scorer written and committed
BEFORE the run (`Tools/score-read-firmware.py`, `notes/prediction-read-firmware.md`).

| P | prediction | result |
| --- | --- | --- |
| P8 | the saved file is exactly 66,560 bytes | **HIT** |
| P6 | read-back == FWFILE/140 of updater 1.10 | **MISS** — 1 of 65 blocks differs |
| P3 | `resp[6..7]` == 16-bit block sum | n/a, needs the run log |
| P9 | settings untouched by the read | n/a, not captured |

**The miss is a single block: device index `0x74`, the last one, 1022 of its
1024 bytes.** The other 64 are byte-identical to the vendor's image. Read twice;
`backup.bin` and `backup2.bin` are **identical byte-for-byte**
(sha256 `46e4dd15d14c44382d6e6ec07218df23f3a51aa84849eea1ecb29d03ab9cac89`), the
same block differs both times with the same bytes. So it is stable stored data,
not a stale buffer or a bad read — which is precisely the discriminator §5 of
the prediction file pre-registered.

**WHAT P6's MISS DOES NOT MEAN, and this is why the prediction was written to be
falsifiable in a specific direction.** It is not evidence against the block
arithmetic. A wrong payload offset, a wrong index mapping or a wrong `+0x34`
base produces garbage in *every* block; 64 of 65 matching a reference we never
sent to the device is strong positive evidence that all three are right. §3.8's
addressing is confirmed, and the application region is confirmed to hold
firmware 1.10.

**THE PRE-REGISTERED READING IS REFUTED IN ITS DETAIL AND SURVIVES IN ITS
CONCLUSION.** §5 named ≤2 differing blocks as "the firmware writes to its own
flash at runtime and those blocks hold it — benign, and it localises where
settings/calibration live." The second half is **wrong**: the settings record is
not there. No 16-byte run of `~/.egg-mouse-known-good.bin` occurs anywhere in
block `0x74`, and the block's entropy (7.80 bits/byte) is indistinguishable from
every other block in the image (mean 7.80, range 7.76–7.84 — the whole FWFILE is
encrypted or compressed, so nothing in it is plaintext settings).

What survives is "the firmware writes that block at runtime", now with a dated
chain behind it rather than as a guess:

1. the owner's Windows updater flashed FWFILE 140 to this mouse earlier on 2026-09-05,
   writing all 65 blocks including `0x74` (both captures show 65 `A0 06` frames
   for `0x34`–`0x74`).
2. The mouse was then used normally.
3. Block `0x74` now differs from that written image, stably, at the same entropy.

**Ruled out by search, with the space stated (§1.2a).** Device `0x74` is not the
last block of FWFILE 133/135/137/140/142/143; it does not occur anywhere in the
1.10 updater `.exe` at any offset; and it is not FWFILE 140's `0x74` shifted by
±1, ±16 or ±512 bytes. Not searched: the other three updater versions' files,
and the config tool. So "it came from somewhere else in the vendor's shipped
data" is not excluded in general, only for 1.10.

**Consequence for the flash: none, and the reason is `[O]` rather than
reasoned.** Our flash writes FWFILE 140's `0x74` — byte-identical to what
Endgame's updater wrote to this same mouse hours earlier, in a run that
completed and left it working. Whatever the firmware later put in that block,
the vendor's own procedure overwrites it and the device rebuilds it.

**Consequence for the backup: stated so it is not a surprise later.**
`backup.bin` is NOT a pristine factory image. 64 of 65 blocks are exact; block
`0x74` is this device's runtime state as of 23:03 on 2026-09-05. As a recovery
artefact that is what the device actually had, which is the right thing to be
able to put back — but do not describe it as "the same as the `.exe`".

---

## `prediction-factory-reset.md` re-scored after OUR flash, 2026-09-05. **21/21**

Not a duplicate of the earlier 21/21. That one scored a standalone
`egg-config factory-reset`. This scores the `A1 13` that `egg-flash flash`
sends as the vendor's last step, on a device our own tool had just written.

Mechanically checked, table parsed from `prediction-factory-reset.md` itself
rather than read by eye:

```
predicted 21 bytes, observed 21 bytes
21/21 predicted byte transitions confirmed EXACTLY (offset and both values)
bytes that moved but were NOT predicted: none
```

**Why this is the strongest end-to-end result in the project.** The "expected
after reset" column was never derived from the owner's device — it came from
`windows-run/10-postflash-baseline.pcapng`, Endgame's own capture of this mouse
immediately after *their* updater flashed it. So after a flash performed
entirely by our tool, the settings record is byte-identical to the state
Endgame's software leaves behind. The firmware write, the completion, the
re-enumeration and the reset all land where the vendor's do.

Reproduce:

```
./build/egg-config read --save after-flash.bin
./build/egg-config diff ~/.egg-mouse-known-good.bin after-flash.bin
```

---

## §7.12, the keyboard-key modifier byte

### PARTIAL — `keyboard-modifier-byte`, **3/4 + 1 bonus**, scored 2026-09-06

Committed in `notes/config-protocol.md` §7.12 on 2026-09-05, from
`FUN_00405180` alone, before `05-buttonmapping` was diffed.

**RE-ANCHORED 2026-09-06, and it needed to be.** The original scoring line read
*"scored against `windows-run/05-buttonmapping.pcapng` writes 9–11 (`log.txt`
lines 10–12)"* — a `log.txt` citation, written the day before the quarantine
(CLAUDE.md §1.1a) and still standing after it. It is removed, and the score is
re-derived from material that survives.

The capture's own bytes, no labels anywhere. Every distinct button-entry state
in `05-buttonmapping.pcapng` (14 of them, across the baseline read and all 12
`A0 11` writes) that begins with action type `0x02`:

```
02 00 04 00 00 00
02 01 04 00 00 00
02 03 04 00 00 00
```

That set alone carries three of the four predictions: the varying byte is at
`+1`, not `+2`–`+5`; the keycode `0x04` sits at `+2` and does not move; and the
observed modifier values are exactly `{0x00, 0x01, 0x03}`, where `0x03` is the
bitwise OR of the other two — a bitfield, not an enum, since an enum would have
no reason to skip `0x02`.

**What the re-anchoring costs, stated rather than glossed:** which physical
tick-box produced `0x01` is no longer attested by an observer. That `0x01` is
CTRL and `0x02` is SHIFT is `[D]` from `FUN_00405180`, not `[O]`. So rows 2 and
3 below now score the *shape* — a bitfield whose values compose by OR — against
the capture, and take the bit *assignment* from the binary. That is weaker than
it read before, and it is what the evidence actually supports.

| # | prediction | outcome |
| --- | --- | --- |
| 1 | modifier is `0x00` with nothing ticked | **HIT** |
| 2 | modifier is `0x01` with CTRL | **HIT** |
| 3 | CTRL+SHIFT is the bitwise OR, `0x03` | **HIT** — so it is a bitfield, not an enum |
| 4 | the modifier lives in records `0x4e`–`0x51` (entry `+2`–`+5`) | **MISS** — it is at `+1` |

Bonus, predicted in the same section and not numbered: `A` encodes as HID usage
`0x04`, not `VK_A` `0x41`. **HIT.**

**The miss is the useful part.** §7.12 reasoned that the keyboard path zeroes
`obj[0x30+8k]` and `obj[0x32+8k]`, so the modifier had to be among the bytes
those map to. True, and it never considered `+1`, because §7.3 had named `+1`
"the button mask" and that name was reused as though it were a type. `+1` is
discriminated on `+0`: mask for MOUSE, modifier for KEYBOARD, Consumer usage for
MEDIA. A label learned from one action type concealed four others.

*(A fourth running tally stood here, listing six runs across three different
scopes on one line. Removed 2026-09-06 — see the table at the top of this file,
which is regenerated by `Tools/notes/reconcile_scores.py`. This one had lasted a
day.)*

---

## §7.13, the button-action menu

### CONFIRMED — `button-menu-groups`, **4/4 exact**, scored 2026-09-06

Committed in `notes/config-protocol.md` §7.13 on 2026-09-05 from **one UTF-16
string run in cfg107** (`0x155cde`–`0x155e78`) plus one rule: submenu items are
declared before the group name they belong to. Scored against
`windows-run/screenshots/button-mapping{,-mouse,-CPI,-media}.png`, which the owner
took before any of this was derived and was never told the expected answer
(§6.2).

| # | prediction | observed | outcome |
| --- | --- | --- | --- |
| 1 | five top-level groups | five | **HIT** |
| 2 | `MOUSE`, `KEYBOARD KEY`, `CPI`, `MEDIA`, `DISABLE`, in that order | identical, in that order | **HIT** — including the order §7.13 flagged as its weakest clause |
| 3 | MOUSE = LEFT CLICK, RIGHT CLICK, MIDDLE CLICK, FORWARD, BACK, SCROLL UP, SCROLL DOWN | identical, in order | **HIT** |
| 4 | CPI = CPI LOOP, FIXED CPI; MEDIA = PLAY/PAUSE, NEXT, PREVIOUS, MUTE, VOLUME UP, VOLUME DOWN, BROWSER, EXPLORER | identical, in order | **HIT** |

§7.13 explicitly hedged that "the string table's order is the order the items
were declared, which is usually but not necessarily the order they are added to
the menu." It was the declaration order, in all four menus, with no exceptions.

**This is the cheapest correct prediction in the project** — a complete UI
vocabulary, ordered, from a single string run and no device interaction at all.

## §7.22, the multiclick / SPDT byte

### CONFIRMED — `multiclick-and-spdt-byte`, **4/4**, scored 2026-09-06

`wire-predictions.md` #24, written from the packer. Scored against
`windows-run/04-buttons.pcapng`, which was captured on 2026-09-05 — **before
the prediction was written**, so the observation could not have been shaped by
it, and the capture was re-read structurally (a large GET is a settings record
only when the SET before it was `A1 12`) rather than by trusting an earlier
reading of it.

The sixteen records, per button, in order:

| button | record `0x3d + 7n` |
| --- | --- |
| left | `8 8 8 0 25 12 12 12 12 12 f0 f1 8 8 8 8` |
| right | `8 8 8 8 8 8 25 25 25 25 25 25 25 f0 f1 8` |
| middle | `8 8 8 8 8 8 8 25 25 25 25 25 25 25 25 25` |
| forward | `8 8 8 8 8 8 8 8 25 25 25 25 25 25 25 25` |
| back | `8 8 8 8 8 8 8 8 8 25 25 25 25 25 25 25` |

| # | prediction | observed | outcome |
| --- | --- | --- | --- |
| 1 | the trailing byte of each of the first five button records is the per-button Multiclick Filter | left moves through 0, 25 and 12; each of the five moves independently and nothing else moves with it | **HIT** |
| 2 | `GX Speed Mode` sets `0xF1`, `GX Safe Mode` sets `0xF0` | both bytes appear, on left and on right | **HIT** |
| 3 | left is report `0x4D`, right `0x54`, then `0x5B`/`0x62`/`0x69` | record `0x3d`/`0x44`/`0x4b`/`0x52`/`0x59`, i.e. wire `0x4d`/`0x54`/`0x5b`/`0x62`/`0x69` | **HIT** |
| 4 | **middle/forward/back have trackbars only and no SPDT combo** | those three carry `8` and `25` and never `0xf0` or `0xf1` | **HIT** |

Row 4 is the one worth having. It is a **negative** prediction — that two
specific bytes never appear at three specific offsets — and §1.2a says a
negative needs more evidence than a positive. It got it: the same capture that
produced `0xf0` and `0xf1` twice each on the other two buttons produced neither,
in any of sixteen records, on these three. That is the clause
`egg-config multiclick` refuses on, so it is load-bearing rather than
decorative.

**Reproduce:** `python3 -m unittest Tests.test_handedness`.
