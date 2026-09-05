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
| CONFIRMED | 2 |
| REFUTED | 0 |
| PARTIAL | 0 |
| AMBIGUOUS | 0 |
| **scored so far** | **2 of 266** |

Zero REFUTED at n=2 is not yet informative either way. It becomes a red flag if
it survives the GUI area, which is ~15 predictions and the cheapest to test.

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
- Everything in the four **updater** areas (launch, bootloader entry, start
  command, block write loop, read-back) needs `08-flash.pcapng`, which does not
  exist yet.
