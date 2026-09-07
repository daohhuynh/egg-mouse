# The 2026-09-06 capture session: full UI inventory, and what to capture

**Written and committed BEFORE the capture.** Predictions below are predictions.
Score by diffing afterwards; disagreements are disagreements (§7's tiebreaker is
the device).

Built the way the owner asked: **start from the screenshots** — everything the vendor's
own software exposes — and check each control off, rather than starting from our
record map, which can only list what we already found. That reframing is what
found §7.32.

---

## 1. The complete vendor surface, control by control

Sources: `windows-run/screenshots/*.png`, `dlgdump.py`, `gui-surface.md`.
"captured" = a vendor `A0 11` in `windows-run/` demonstrates it.

### Info bar (all tabs)

| control | ours | captured | note |
| --- | --- | --- | --- |
| Firmware Version (display) | `info` | yes | `A1 02` |
| Software Version (display) | n/a | n/a | host-side only |
| **Factory Reset** button | `factory-reset` | yes | proven on device 21/21 |

### Basic tab

| control | ours | captured | note |
| --- | --- | --- | --- |
| Polling Rate | `set polling` | **7 of 7 values** | complete |
| LOD | `set lod` | **11 of 11 values** | complete |
| CPI Levels | `set cpi-levels` | **4 of 4** | complete |
| Angle Snapping | `set angle-snapping` | both | complete |
| Disable LED on Lift-Off | `set led-on-liftoff` | both | stored inverted |
| **X/Y Settings** checkbox | — | n/a | **writes nothing** (§7.8): it only ungreys the Y boxes. Not a setting |
| CPI 1–4 radio (active stage) | `set cpi-stage` | **4 of 4** | complete |
| CPI 1–4 colour swatch | — | constant | display only. No control writes it; see §4 below |
| CPI **1** X/Y | `cpi 1 X [Y]` | yes | |
| CPI **2** X/Y | `cpi 2 X [Y]` | values yes, flag no | `0x28` never moved |
| CPI **3** X/Y | `cpi 3 X [Y]` | **NEVER** | **capture A** |
| CPI **4** X/Y | `cpi 4 X [Y]` | **NEVER** | **capture A** |

### Advanced Sensor tab

| control | ours | captured | note |
| --- | --- | --- | --- |
| CPI Downshift Tuning | `set cpi-downshift` | **4 of 4** | complete |
| Smoothing Tuning | `set smoothing` | **3 of 3** | complete |
| Motion Sync | `set motion-sync` | both | complete |
| Force max Sensor fps | `set force-max-fps` | both | complete |
| Sensor Angle Tuning | `set sensor-angle` | 4 values of 255 | encoding settled (two's complement, ±127). More values add nothing |
| *Motion Jitter Filter* | `set motion-jitter-filter` | **impossible** | **the control is not on this page on this model** — confirmed by the screenshot. The BIT is `[D]` (cfg107 `0x411e7a`/`0x411aeb`); its effect is unobserved and unobservable through the vendor |
| *Sensor Glass Mode* | withheld | **impossible** | same: absent from the page (§7.25). cfg107 only reads `0x6f` |

### Buttons tab

| control | ours | captured | note |
| --- | --- | --- | --- |
| Slamclick Filter | `set slamclick-filter` | both | complete |
| Multiclick ×5 (L/R/M/Fwd/Back) | `multiclick` | L: 0,8,12,25 · others: 8,25 | encoding is one byte 0–25; ends and a middle seen |
| SPDT ×2 (L/R only) | `multiclick ... gx-speed\|gx-safe` | both, both buttons | complete. Three options: OFF / GX Speed / GX Safe |
| "I understand…" checkbox | `0x72`, withheld | yes | a UI acknowledgement, not a mouse setting (§7.23) |
| *no slider for wheel/CPI button* | — | — | five sliders, not eight — matches the record |

### Button Mapping tab

| control | ours | captured | note |
| --- | --- | --- | --- |
| Left-handed Mode | `handedness` | yes | a MOVE plus a reset (§7.20) |
| six rows (R/M/Fwd/Back/WhUp/WhDn) | `map` | yes | LEFT and the CPI button have no row |
| MOUSE ×7 | `map` | 7 of 7 **as bytes**, 0 of 7 **as menu picks** | see below |
| CPI → CPI LOOP | `map ... cpi-loop` | yes | |
| CPI → FIXED CPI | `map ... fixed-cpi:N` | **only 1600/1600** | **capture B** |
| MEDIA ×8 | `map` | **1 of 8** (VOLUME UP, `20 e9`) | **capture C** |
| DISABLE | `map ... disable` | yes | |
| KEYBOARD KEY + 4 modifiers | `map ... key:` | **1 key of ~106** (`a`) | **capture D** — and see §7.32 |

**The MOUSE row is a distinction worth keeping, found 2026-09-06 by decoding
every `A0 11` write in all eleven captures.** All seven MOUSE pairs (`00 01`,
`00 02`, `00 04`, `00 08`, `00 10`, `01 01`, `01 ff`) do appear in writes, so the
*encodings* are `[O]`. But they appear only as the eight default assignments
carried along by read-modify-write, first at `02-basic.pcapng` seq 4 buttons 0-7,
in a capture about the Basic tab. **No MOUSE menu item has ever been selected in
any capture**: `05-buttonmapping.pcapng` only ever rewrote button 3, through
`ff 00`, `20 e9`, `0c 00`, `02 00`, `02 01`, `02 03`.

So the menu-item -> byte link for MOUSE is `[D]` only (§7.17, `0x40774e` through
`0x407b33`). It is low risk and needs no capture of its own -- **§7's restore step
covers it for free**, since Middle Button -> MOUSE -> MIDDLE CLICK is a genuine
menu pick and must produce `00 04`.

---

## 2. Nothing is blocked by the loss of `log.txt`

The owner asked for the category "captured, but unimplementable because there is no
log to say which click caused it". **For config, that category is EMPTY.**

Every one of the 38 record bytes that ever moved is named, and its encoding is
derived from cfg107 independently of any label
(`Tests/test_defaults.py::OnlyNamedBytesEverMove` is the standing gate). The
labels only ever said *which click*; the binary says *what the byte means*, and
that is what implementing needs. Two findings that had leaned on labels were
re-derived and both survived (§7.16).

---

## 3. What to capture, in value order

**One edit per APPLY.** Then every write's diff names itself and no log is
needed — that is exactly why `06-cpi-stage.pcapng` survived the quarantine and
the rest did not. Note only the ORDER of sections.

### Sequencing note that changes the predictions

`CAPTURE-STEPS.md` is the operational file the owner follows; these predictions are
keyed to its section numbers and its exact values. Two things in it move the
predictions off the first draft:

1. **A factory reset comes FIRST** (`12-reset-1.10.pcapng`), so every prediction
   below starts from known defaults: CPI3 `1600/1600`, CPI4 `3200/3200`. It also
   captures firmware 1.10's factory record, which has never been observed.
2. **`X/Y Settings` is ticked before any CPI edit.** the owner, 2026-09-05: *"the
   slider snaps the other axis if X/Y settings is not ticked"* (`gui-surface.md`
   §6). Unticked, the axes mirror and the section is worthless. Ticked, setting
   X alone leaves Y where it was — so the flag moves one step EARLIER than the
   first draft of this file predicted.

### §3 `13-cpi-stage34.pcapng` — CPI stages 3 and 4

Records `0x2d`–`0x31` (stage 3) and `0x32`–`0x36` (stage 4) have **never been
written by the vendor**. Each stage is `flag, Xlo, Xhi, Ylo, Yhi`.

| step | do | predicted diff, and nothing else |
| --- | --- | --- |
| 2 | CPI3 X = 1200 | `0x2e`/`0x2f` → `b0 04`, **and `0x2d` → `01`** (1200 ≠ Y's 1600) |
| 3 | CPI3 Y = 2400 | `0x30`/`0x31` → `60 09`; `0x2d` stays `01` |
| 4 | CPI4 X = 3000 | `0x33`/`0x34` → `b8 0b`. **Not discriminating** — stage 4 is now 3000/3200 and stage 3 is 1200/2400, so both readings give `0x32` = `01` |
| 5 | CPI4 Y = 3000 | `0x35`/`0x36` → `b8 0b`. **THE TEST** |

> **THE PREDICTION, and it says the vendor is WRONG.** After step 5 stage 4 is
> `3000/3000`, equal, so a correct flag byte `0x32` reads `00`. §7.8 derives that
> cfg107 computes stage 4's flag from **stage 3's** boxes — `0x40edde` loads
> `0x4f0` and `0x40ede4` compares `0xe84`, both stage 3's members, where the
> pattern demands `0x4f4`/`0xe88`. Stage 3 is `1200/2400`, unequal.
>
> **Predicted: `0x32` = `01` after step 5.** If it reads `00`, §7.8's
> copy-paste-bug reading is REFUTED and §7.8 and §7.31 both need correcting.

This is the only prediction in the project that asserts the vendor's own tool
computes something incorrectly.

### §4 `14-fixed-cpi.pcapng` — FIXED CPI with X ≠ Y

Never observed; every captured FIXED CPI is `0c 00 40 06 40 06`. Middle Button
is entry 2, record `0x45`-`0x4b`.

**The popup carries its own `X/Y Settings` tick.** `[D]` already, and I missed
it when drafting the steps: `gui-surface.md` line 58 has DIALOG 150 as *"a value
EDIT + trackbar, an `X/Y Settings` checkbox with its own X and Y pair, and
`OK`"*, read from the dialog resource. the owner restated it from the running tool on
2026-09-06 — corroboration of a `[D]`, not new evidence.

It revises the steps: the tick goes on FIRST, every time the popup opens, and
**both boxes are typed explicitly** (the popup does not retain either). My
original step "change Y to 800, leave X at 2500" would have snapped X to 800 as
well, and the section would have proved nothing. This is also the likely reason
every existing capture is X=Y.

| step | do | predicted entry bytes |
| --- | --- | --- |
| 1 | X = 2500, Y = 400 | `0c 00 c4 09 90 01 <+6 kept>` |
| 2 | **swapped:** X = 400, Y = 2500 | `0c 00 90 01 c4 09 <+6 kept>` |
| 3 | X = 2505, Y = 2500 | X normalises to **2510** -> `0c 00 ce 09 c4 09`, never `c9 09` (`0x401e70` clamps `[10,30000]` then rounds to 10, ties up) |

400 = `0x0190` = `90 01`; 2500 = `0x09c4` = `c4 09`; 2510 = `0x09ce` = `ce 09`.

**Steps 1 and 2 are deliberate mirror images.** Be precise about what that buys:
the order is **already `[D]`** and is not in doubt -- §7.31 traces
`0x00407c7e movl 0x57f328,%edx` -> entry `+2` from member `0x300` (the X box's
`EN_CHANGE` at `0x004016e0`), and `0x00407ca0 movw 0x57f32c,%cx` -> entry `+4`
from member `0x304` (`0x00401813`). `encodeButtonEntry` matches, X at
`out[2]`/`out[3]` and Y at `out[4]`/`out[5]`.

What step 2 adds is the **only `[O]` confirmation of that order obtainable at
all**, and it is free. Every FIXED CPI ever captured is X=Y
(`0c 00 40 06 40 06`) -- precisely the input under which a swapped mapping is
invisible -- so no existing capture can distinguish the two. A swap would return
identical bytes for steps 1 and 2. The uncited `26000` bound survived a 14/14
replay test for exactly this reason, so the cost of leaving a `[D]`-only chain
untested where testing is free is a cost this project has already paid once.

**Step 3's UI half is already confirmed, `[O]`.** the owner, 2026-09-06, before
capturing: *"2505 doesnt work it automatically corrects me to 2510"*. That is
`0x401e70` behaving exactly as derived, and the first `[O]` on the FIXED CPI
normaliser -- the domain whose uncited `26000` bound was the shipping bug the
audit found. The capture is still wanted for the **wire** bytes: the UI showing
2510 does not prove the record receives `ce 09`.

Also `[O]` from the owner, 2026-09-06, and why the steps set both boxes every time:
**the popup does not retain values between openings, it reopens at 400.** So an
"unchanged" box is not a held value, and a step that typed only X twice would
have emitted two identical applies -- possibly zero writes, since
`gui-surface.md` §5's dirty gate greys APPLY when nothing changed.

All three steps have X != Y, so any one of them is first evidence that the two
boxes reach the record independently (§7.31, `0x401eb0` reads members
`0x300`/`0x304`; `0x407c7e`/`0x407ca0` store them to `+2`/`+4`). Step 2
additionally fixes the **order**, which independence alone does not.

**Three ways this can come back, and all are answers:**
- as predicted -> §7.31 confirmed, `encodeButtonEntry`'s separate `out[4]`/`out[5]`
  stores are `[O]`-backed and the `1600x800` syntax stands.
- **the tick exists but Y still drags X** -> the mirror is below the checkbox,
  X=Y is structural, and `1600x800` must be withdrawn even though the record has
  room for it.
- **the popup has only ONE box** -> this contradicts a `[D]` reading of DIALOG
  150's own resource (`gui-surface.md` line 58), not merely a guess, so it would
  mean `dlgdump.py` is mis-parsing the dialog and every control inventory built
  on it needs rechecking. Least likely, most expensive if true.

Also asked: does the tick persist between popup openings? Unknown, and it decides
whether a user of our tool can be told the vendor tool keeps the mode.

### §5 `15-media.pcapng` — seven never-captured MEDIA actions, plus a control

Measured 2026-09-06, not assumed: decoding the eight 7-byte button entries out of
every `A0 11` write in all eleven `windows-run/*.pcapng` gives exactly **one**
MEDIA pair ever written, `20 e9` (VOLUME UP), first at `05-buttonmapping.pcapng`
seq 12 button 3. So 7 of 8 are unobserved. `[O]`

**All eight are now in the steps, VOLUME UP first as an in-file control.** It is
the only step whose answer is known, so if it does not return `20 e9` the parse of
that capture is wrong and nothing else in the file can be trusted. One click,
spent before the seven unknowns rather than after.

`[D]` at §7.17 (cfg107 `0x407d76` through `0x40820a`), one `movb` immediate each.
Predicted `+0 +1`, with `+2`-`+5` zero:

| step | action | predicted |
| --- | --- | --- |
| 1 | VOLUME UP (**control**) | `20 e9` -- already `[O]`, must reproduce |
| 2 | **BROWSER** | **`18 96`** |
| 3 | **EXPLORER** | **`18 94`** |
| 4 | PLAY/PAUSE | `20 cd` |
| 5 | NEXT | `20 b5` |
| 6 | PREVIOUS | `20 b6` |
| 7 | MUTE | `20 e2` |
| 8 | VOLUME DOWN | `20 ea` |

**Steps 2 and 3 are the live question, and the rest is corroboration.** §7.15
records the standing bar: *"No MEDIA value other than `0xe9` may be written until
this is settled."* BROWSER and EXPLORER are the only two whose `+0` is `18` rather
than `20`, and their HID Consumer usages (`0x0196`, `0x0194`) do not fit the
single byte `+1` that VOLUME UP occupies. Either they spill into `+2`-`+3`, or
`+1` is an index into a vendor table rather than a raw usage -- in which case
VOLUME UP matching HID exactly was a coincidence. `18 96`/`18 94` is what the
`movb` immediates say; the capture is what makes it `[O]` and lifts the bar on all
seven.

### §6 `16-keys.pcapng` — KEYBOARD KEY beyond `a`

Only `a` has ever been captured. §7.32 rebuilt cfg107's whole VK->HID table from
the .exe and `Tests/test_keymap.py` gates it, so this corroborates 105 untested
values.

| step | key pressed | predicted `+1 +2` |
| --- | --- | --- |
| 1 | Left Shift | `00 e1` — the screenshot's own key, and the one that proved our table was short (§7.32) |
| 2 | Keypad + (SHIFT auto-ticked) | **see below — two live outcomes** |
| 3 | F5 with WIN ticked | `08 3e` |
| 4 | Print Screen | `00 46` |
| 5 | Num Lock (**optional**, skip if absent) | `00 53` |

Step 1 doubles as the check that a modifier pressed AS a key is distinct from a
modifier ticked as a checkbox: `+1` must be `00` here, not `02`.

**Step 2 is no longer a simple corroboration.** the owner, 2026-09-06: *"I used the +
on the keypad and it entered + accurately, but it automatically ticked SHIFT
alongside it."* `[O]`, and it was not predicted. The dialog sets the modifier
boxes from the keypress, so they are not purely manual input.

Two outcomes, and they are distinguishable:

- **`02 57`** -- VK_ADD went through the jump table to HID keypad-plus `0x57` as
  §7.32 says, and SHIFT was added by some separate path. Our `kp-plus` entry
  stands and the auto-tick is a UI quirk to document.
- **`02 2e`** -- the dialog resolved the keypress to the *character* `+` and
  re-expressed it as Shift + `=` (HID `0x2e`). Then the dialog is character-driven
  rather than VK-driven for at least some keys, and **the whole keypad half of
  §7.32's 21 new names may be unreachable through the vendor UI** -- `kp-slash`,
  `kp-star`, `kp-minus`, `kp-plus`, `kp-dot` all produce characters that the main
  row can also produce. It would not make our table wrong -- it is `[D]` from the
  jump table itself (cfg107 index table `0x0040a370`, jump table `0x0040a2b0`,
  §7.32) and the device may well accept `0x57` -- but it would mean the vendor
  tool cannot emit those five, which is worth knowing before we claim parity.

A third possibility to watch for: the owner is on a laptop, and if its numpad is an
`Fn` overlay the hardware itself may have sent Shift+`=`. That is indistinguishable
from the second outcome in the bytes alone, so **`02 2e` is not by itself proof
the dialog is character-driven.**

**Step 4 breaks that tie and was already in the list.** Print Screen emits no
character in any layout, so `00 46` shows the dialog can read a key that types
nothing -- which means a `+` re-expressed as Shift+`=` was re-expressed *because a
`+` can be typed another way*, not because the dialog cannot read keys at all.
I added a Scroll Lock step for this and withdrew it: step 4 already covers it and
The owner's laptop has no Scroll Lock key.

**Step 5 (Num Lock) is optional and nothing rests on it.** It is the one key on
the numeric keypad that types no character, so it would separate "the keypad is
unreadable" from "character-producing keys get re-expressed". Worth a click if
the key exists, but even a `02 2e` plus a missing step 5 leaves nothing we would
do differently: our `kp-*` names stay `[D]`-correct from the jump table either
way, and the only casualty is a parity claim about the vendor UI.

### §7 `17-restore.pcapng`

Middle Button → MIDDLE CLICK predicts `00 04 00 00 00 00 <+6 kept>`, then the
reset returns the whole record to §2's values.

## 4. What NO capture can ever reach — do not spend time

| bytes | why |
| --- | --- |
| `0x0f`–`0x22` (20) | the LED page, dialog 137, is created by no tool. Its `Apply led settings` handler exists (§7.24a) but can never fire. The CPI colour swatches on the Basic page are display-only, and an absolute-literal scan finds **zero** reads of these object bytes |
| `0x00`, `0x07` | serialised, always zero, no control traced (§7.26) |
| `0x02`–`0x04` | not written by the serialiser at all (§7.4) |
| `0x6f` | Sensor Glass Mode — absent from this model's page |
| `0x06` bit 4 | Motion Jitter Filter — absent from this model's page |

## 5. What NOT to recapture

`00`, `01`, `02`, `03`, `04`, `07`, `08`, `09`, `10` — held, and checked by
`test_config_replay.py`, `test_button_map.py`, `test_golden_vendor.py`,
`test_defaults.py`. Re-taking them buys nothing.

### 5c. Does an X≠Y record survive being re-loaded?  [G]

Raised 2026-09-06, when the owner restated that `X/Y Settings` is a view mode: *"Apply
only works on the condition that a setting changes. Clicking X/Y Settings
maintains Apply greyed out because you are not actually changing a setting."*

Nothing new in that — it is the same `[O]` as `gui-surface.md` §6, which quotes
him saying it on 2026-09-05. It is recorded here only because restating it made
me notice the consequence below, which nobody had written down.

The hazard it exposes: **`A0 11` writes all 115 bytes from the tool's in-memory
model**, so the model's state at APPLY time matters even for bytes the user never
touched. Sections 4-6 each open fresh on the record section 3 leaves behind,
where CPI3 is 1200/2400 and `0x2d` should be `01`.

- **Predicted: the mirror is an edit-time control behaviour, not a load-time
  one**, so the record survives and `0x2d`-`0x36` are byte-identical across the
  opening `A1 12` of sections 4, 5 and 6 and every `A0 11` in them.
- **If instead** the first `A0 11` of section 4 carries `0x30`/`0x31` = `b0 04`
  (Y snapped to X's 1200), the mirror runs at load and **sections 4-6 cannot be
  read as starting from section 3's state.** They are still valid for the button
  block, which is what they are for; the CPI block in them is then tool-authored
  and must not be cited.

Checked by: `diff <(recdump 13-cpi-stage34 last) <(recdump 14-fixed-cpi first)`
restricted to `0x23`-`0x36`. the owner is also asked to read the CPI 3 box aloud at the
top of section 4, which answers it without needing the capture parsed first.


## 6. Scoring

    python3 Tools/capture/whatmoved.py         # REACHABLE must FALL
    python3 -m unittest Tests.test_defaults    # a NEW unnamed byte moving = a new field
    python3 -m unittest Tests.test_button_map  # must still reproduce every entry
    python3 -m unittest Tests.test_keymap      # our key table vs cfg107's jump table
