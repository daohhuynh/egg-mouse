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
| MOUSE ×7 | `map` | **7 of 7** | complete |
| CPI → CPI LOOP | `map ... cpi-loop` | yes | |
| CPI → FIXED CPI | `map ... fixed-cpi:N` | **only 1600/1600** | **capture B** |
| MEDIA ×8 | `map` | **1 of 8** (VOLUME UP) | **capture C** |
| DISABLE | `map ... disable` | yes | |
| KEYBOARD KEY + 4 modifiers | `map ... key:` | **1 key of ~106** (`a`) | **capture D** — and see §7.32 |

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
is entry 2, record `0x45`–`0x4b`.

| step | do | predicted entry bytes |
| --- | --- | --- |
| 2 | X = Y = 2500 | `0c 00 c4 09 c4 09 <+6 kept>` |
| 3 | Y → 800, X stays | `0c 00 c4 09 20 03 <+6 kept>` |
| 4 | type X = 2505 | normalises to **2510** → `ce 09`, never `c9 09` (`0x401e70` clamps `[10,30000]` then rounds to 10, ties up) |

Step 3 is the one that matters: first evidence either way that the two boxes
reach the record independently (§7.31, `0x401eb0` reads members `0x300`/`0x304`;
`0x407c7e`/`0x407ca0` store them to `+2`/`+4`).

**If the popup has only ONE box**, §7.31's independence reading is wrong and the
`1600x800` syntax must be withdrawn.

### §5 `15-media.pcapng` — the seven MEDIA actions never captured

`[D]` at §7.17 (cfg107 `0x407d76` through `0x40820a`), one `movb` immediate each.
**Low risk** — all eight handlers are the identical instruction shape and VOLUME
UP is already confirmed on the wire as `20 e9`. Corroboration, not a live
question. Predicted `+0 +1`, with `+2`–`+5` zero:

| order in the steps file | action | predicted |
| --- | --- | --- |
| 1 | **BROWSER** | **`18 96`** |
| 2 | **EXPLORER** | **`18 94`** |
| 3 | PLAY/PAUSE | `20 cd` |
| 4 | NEXT | `20 b5` |
| 5 | PREVIOUS | `20 b6` |
| 6 | MUTE | `20 e2` |
| 7 | VOLUME DOWN | `20 ea` |

BROWSER and EXPLORER are first because they are the only two whose `+0` is `18`
rather than `20` — where a misreading would most likely hide.

### §6 `16-keys.pcapng` — KEYBOARD KEY beyond `a`

Only `a` has ever been captured. §7.32 rebuilt cfg107's whole VK→HID table from
the .exe and `Tests/test_keymap.py` gates it, so this corroborates 105 untested
values.

| step | key pressed | predicted `+1 +2` |
| --- | --- | --- |
| 1 | Left Shift | `00 e1` — the screenshot's own key, and the one that proved our table was short (§7.32) |
| 2 | Keypad + | `00 57` |
| 3 | F5 with WIN ticked | `08 3e` |
| 4 | Print Screen | `00 46` |

Step 1 doubles as the check that a modifier pressed AS a key is distinct from a
modifier ticked as a checkbox: `+1` must be `00` here, not `02`.

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

## 6. Scoring

    python3 Tools/capture/whatmoved.py         # REACHABLE must FALL
    python3 -m unittest Tests.test_defaults    # a NEW unnamed byte moving = a new field
    python3 -m unittest Tests.test_button_map  # must still reproduce every entry
    python3 -m unittest Tests.test_keymap      # our key table vs cfg107's jump table
