# Scoring the pre-registered predictions

`notes/wire-predictions.md` holds **266 predictions committed before any capture
existed** (commit history proves the order; that is the only thing that makes
this a proof rather than a postdiction). This file is the scoreboard. One entry
per prediction that reality has now touched, `CONFIRMED` / `REFUTED` / `PARTIAL`,
with **what was actually observed** and by whom.

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

## Running tally

| | count |
| --- | --- |
| CONFIRMED | 3 |
| REFUTED | 0 |
| PARTIAL | 0 |
| AMBIGUOUS | 0 |
| **scored so far** | **3 of 266** |

Zero REFUTED at n=3 is not yet informative either way. It becomes a red flag if
it survives the GUI area, which is ~15 predictions and the cheapest to test.

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

### `led-page-unreachable` — **CONFIRMED** (`gui-surface.md` §3)

Observer: the owner, 2026-09-04: *"theres only 4 tabs and you named then all already
basic advanced sensor buttons button mapping, i only see disable LED on lift off
as the mention of LED anywhere in the app."*

DIALOG 137 exists in all four config tools and is reachable in none. Predicted
from a four-entry jump table bounded by `cmpl $0x3` and a single shared hide
call at `0x413c9e`. Consequence: the RGB-looking bytes at wire `0x1d`
(`wire-observed.md` §5.1) **stay [G] permanently** — no capture can move them,
because no UI can change them.

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

- **`button-menu-groups`** (`config-protocol.md` §7.13). Committed 2026-09-05:
  the button dropdown's top-level groups are `MOUSE`, `KEYBOARD KEY`, `CPI`,
  `MEDIA`, `DISABLE`, in that order, and no others. Recovered from the string
  table's declaration order, where submenu items precede their group name.
  **Deliberately withheld from `log.txt`**, which still asks the owner the open
  question — he has already screenshotted the dropdown and every submenu, so
  this is checkable against existing material by an observer who was not primed.
  The membership claim is much stronger than the order claim; score them
  separately.

- **#19 `cfg-basic-page-invisible-buttons`** (`Apply CPI settings` id 1059,
  `Surface Calibration` id 1061). Two questions already sit in `log.txt`'s
  ANSWERS block. This one has real weight: `Surface Calibration` would be a
  device command **outside the four-command set we have derived**, so a refutation
  here means our command set is incomplete.
- **#21 `cfg-advanced-sensor-hidden-controls`** (`Motion Jitter Filter`,
  `Sensor Glass Mode` hidden; slider symmetric −127..+127). Testable on sight.
- **#22 `cfg-combo-cpi-downshift`**, and the Smoothing Tuning three-item
  prediction at `wire-predictions.md:3070`. Both are answered by dropping the
  combo open and reading it, and `log.txt` asks for exactly that.
- Everything in the four **updater** areas (bootloader entry, start command,
  block write loop, read-back) needs `08-flash.pcapng`, which does not exist
  yet. The *launch* area is now partly scored — see below.

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

### CONFIRMED — the `send firmware block data...` status string

`wire-predictions.md:2917` committed the updater's observable sequence after one
click, including that the status becomes exactly **`send firmware block data...`**
while the progress bar advances.

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

And the tally is no longer 4–0. **CONFIRMED 6, REFUTED 1**, all scored the same
day and all from things the owner volunteered rather than from questions he was asked.
The refutation is the one that tells us the scoreboard works.
