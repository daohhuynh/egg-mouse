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

### A. CPI stages 3 and 4 — highest value

Records `0x2d`–`0x31` and `0x32`–`0x36` have **never been written**.

| step | do | predicted diff, and nothing else |
| --- | --- | --- |
| A1 | stage 3 X = 1200 | `0x2e`/`0x2f` → `b0 04` |
| A2 | stage 3 Y = 2400 | `0x30`/`0x31` → `60 09`, **`0x2d` → `01`** |
| A3 | stage 4 X = 3000 | `0x33`/`0x34` → `b8 0b` |
| A4 | stage 4 Y = 3000 | `0x35`/`0x36` → `b8 0b` |

> **PREDICTION THAT THE VENDOR IS WRONG.** After A4 stage 4's X and Y are equal,
> so `0x32` should read `00`. §7.8 derives that cfg107 computes stage 4's flag
> from **stage 3's** boxes (`0x40edde` loads `0x4f0`, `0x40ede4` compares
> `0xe84` — stage 3's members, where the pattern wants `0x4f4`/`0xe88`).
> **Predicted `0x32` = `01`.** If it is `00`, §7.8 is refuted.

### B. FIXED CPI with X ≠ Y

Never observed; every captured FIXED CPI is `0c 00 40 06 40 06`. Pick a mappable
button (not LEFT, not the CPI button):

| step | do | predicted entry at `0x37 + 7n` |
| --- | --- | --- |
| B1 | FIXED CPI, X = Y = 2500 | `0c 00 c4 09 c4 09 <+6 kept>` |
| B2 | change Y to 800 only | `0c 00 c4 09 20 03 <+6 kept>` |
| B3 | type `2505` if the box allows | normalises to **2510** → `ce 09`, never `c9 09` (`0x401e70` rounds to 10, ties up) |

### C. The seven MEDIA actions never captured

`[D]` at §7.17 (cfg107 `0x407d76` through `0x40820a`), one `movb` immediate each.
**Low risk** -- all eight handlers are
the identical instruction shape and VOLUME UP is already confirmed on the wire
as `20 e9`. Corroboration, not a live question.

| action | predicted `+0 +1` | if only two are done |
| --- | --- | --- |
| PLAY/PAUSE | `20 cd` | |
| NEXT | `20 b5` | |
| PREVIOUS | `20 b6` | |
| MUTE | `20 e2` | |
| VOLUME DOWN | `20 ea` | |
| **BROWSER** | **`18 96`** | **do this one** — `+0` is `18`, not `20` |
| **EXPLORER** | **`18 94`** | **do this one** — same |

### D. KEYBOARD KEY beyond `a`

Only `a` has ever been captured, with three modifier combinations. §7.32 rebuilt
the vendor's whole VK→HID table from the .exe and `Tests/test_keymap.py` gates
it, so this is corroboration — but it is corroboration of 105 untested values.

| step | do | predicted `+1 +2` |
| --- | --- | --- |
| D1 | key = **Left Shift**, no modifier ticked | `00 e1` — the screenshot's own key, and the one that proved our table was short |
| D2 | key = **Keypad +** | `00 57` |
| D3 | key = **F5**, tick WIN | `08 3e` |
| D4 | key = **Print Screen** | `00 46` |

### E. Free, and worth one line each

- **The `A1 02` info reply on 1.10.** Open the tool on the current firmware and
  capture the first exchange. Every byte we have of that reply is from 1.07.
- **A factory reset on 1.10, LAST.** `07-factory-reset` is 1.07 only, and
  CLAUDE.md §5 says defaults are NOT stable across versions — `0x71` already
  moved once. Do it last: it costs the settings state.

---

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
