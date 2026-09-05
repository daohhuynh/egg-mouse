# Windows session — step by step

Everything you do on the borrowed laptop. Written to be followed without me.

**The golden rule, and the whole session depends on it:**

> **Change ONE setting. Click APPLY. Write down what you changed. Repeat.**

Why that works: the config tool's entire write surface is two buttons — `APPLY`
(control 1043) and `Factory Reset` (1039), `config-protocol.md` §12.2. Pressing
APPLY sends **one `A0 11` frame containing the whole 1024-byte settings record**
(§7.2). So a capture with ten APPLYs contains ten complete records, and the
difference between record *N* and record *N+1* is exactly the setting you
changed in between. That is the entire mapping method, and it needs no
reverse engineering at all.

**Partial is fine. Stop whenever you want.** Every setting you get is mapped
forever; every one you skip stays safe because read-modify-write preserves bytes
we do not understand (`CLAUDE.md` §4.1). Nothing is wasted and nothing is
all-or-nothing.

---

## 0. Setup (about 20 minutes)

1. Install **Wireshark** from wireshark.org. **Tick USBPcap** in the installer —
   it is off by default and it is the whole point. **Reboot.**
2. **Plug a second mouse in, or use the trackpad.** You will be clicking around
   the vendor UI, and every movement of the OP1 floods the capture with input
   reports. Do not touch the OP1 once recording starts.
3. Plug the OP1 into a **direct USB port** — no hub, no dock.
4. **Keep the OP1 UNPLUGGED for all of the above.** You do not need it yet, and
   plugging it in later is worth more than plugging it in now — see the next step.
5. Open Wireshark. The interface list shows `USBPcap1`, `USBPcap2`, … one per
   root hub. Find the right one **and capture the enumeration at the same time**:
   - start a capture on `USBPcap1`
   - **now plug the OP1 in**
   - packets appear? right hub. Nothing? stop, unplug, try `USBPcap2`, repeat
   Plugging in while recording captures the **full enumeration** — device
   descriptor, configuration descriptor and the HID report descriptor as Windows
   asks for them. That independently corroborates the `[O]` descriptor data we
   read on macOS, from a different OS, for free. Save it as `00-enumerate.pcapng`.
6. **The mouse now stays plugged in for the whole session.** Stopping and
   starting Wireshark between experiments is enough; do not unplug it. It will
   re-enumerate by itself during the flash when it enters the bootloader — that
   is expected, and USBPcap follows it because it is the same root hub.
7. Make a folder `windows-run` on the desktop. Everything goes in it.
8. Launch the config tool once and confirm it **sees the mouse** and reports
   firmware **1.07** before capturing anything for real.

**Do not run the config tool and the firmware updater at the same time.** We
have never established that the device tolerates two hosts, and this is not the
moment to find out.

---

## 1. Baseline (5 min) — `01-baseline.pcapng`

1. Start capture. Launch the config tool. Let it fully populate. **Change
   nothing.** Close it. Stop capture. Save as `01-baseline.pcapng`.

This is the record in its current state, and the reference every later diff is
taken against.

---

## 2. Screenshots (10 min) — into `windows-run/screenshots/`

All of these go in `windows-run/screenshots/` — one flat folder, no
subfolders. Name them plainly (`basic.png`, `sensor-cpi-downshift-open.png`).

Take one of **every** page. These test predictions that are already committed,
so they are evidence, not decoration. **Do this section BEFORE section 3**, while
every page is still in its original state — that is what makes the starting
values recoverable without writing them down.

- The main window, showing the tab bar, the firmware/software version readouts,
  and both buttons.
- Each of the four tabs: **Basic**, **Advanced Sensor**, **Buttons**,
  **Button Mapping**.
- Every dropdown **while it is open**, so we see its actual items in order:
  CPI Levels, LOD, Polling Rate, CPI Downshift Tuning, Smoothing Tuning, and the
  two `SPDT:` combos.
- One button-assignment dropdown **with a submenu expanded** — these are nested
  and the nesting is the part we cannot read off the resource.
- The firmware updater window, before you press anything.

**Two things I specifically want to know from the screenshots:**

1. On the **Basic** page, are there buttons labelled **`Apply CPI settings`** and
   **`Surface Calibration`**? They are in the resource but shipped without the
   visible flag. We predicted they are *invisible*. If you can see them, that
   prediction is wrong and it is a real finding.
2. Does the **CPI Downshift Tuning** dropdown show **three** items or **four**?
   The resource says three (`Force Off / Medium / Default`); the code fills four
   (`Force Off / Light Timer Only / Medium Timer Only / Default`). A screenshot
   settles which list the user actually sees.

---

## 3. The settings experiments (about 40 min)

One capture file per page. Inside each file: change one setting, **click APPLY**,
change the next, **click APPLY**, and so on, in the order the table gives.

### The table order IS most of the log

Do section 2's screenshots first and you have already captured every page in its
untouched state — **there is no second round of "before" screenshots.** Those
images give me every starting value, so you do not have to write any of them out.

Then: follow each table **in order, one APPLY per line**. The first record diff
in the capture is line 1, the second is line 2, and so on. That correspondence is
the log, and it costs you nothing to maintain beyond not skipping around.

So `log.txt` only needs the things neither the screenshots nor the order can
supply:

1. **Values and items you chose yourself** — the sensor angle number, which
   button you remapped and to what, what a dropdown's entries are actually
   called and how many there are.
2. **Anything that deviated.** Skipped a line, did them out of order, a value
   was refused, the app hiccupped, you clicked APPLY twice by accident. Say so
   plainly. **A noted mistake costs nothing; an unnoted one can make a whole
   file unreadable**, because it breaks the order-to-diff correspondence
   everything else depends on.

You do **not** need to write out toggle directions — the before screenshot has
them. Note one only if you toggled something twice or the checkbox did not do
what you expected.

`log.txt` at the repo root already has a blank for every one of these.

### Start `log.txt` with this block, and just fill in the blanks

Every "note this" scattered through the tables below is collected here, so you
answer them in one place instead of hunting. Anywhere I ask a question, the
answer goes here. Copy this in and fill it as you go:

```
=== ANSWERS ===
Basic page: is there a button labelled "Apply CPI settings"?      YES / NO
Basic page: is there a button labelled "Surface Calibration"?     YES / NO
CPI Downshift Tuning: how many items, and what are they called?   ___
Smoothing Tuning: what are the items called?                      ___
Polling Rate: how many options?                                   ___
LOD: how many options, and what is the highest?                   ___
Button dropdown: what are the TOP-LEVEL group names, in order?    ___
Does the mouse light up at all when you change CPI level?         YES / NO
Anything that looked odd, crashed, or refused a value:            ___
=== END ANSWERS ===
```

Those first two are the interesting ones. Both buttons exist in the resource but
are shipped without the visible flag, and we have **predicted in a committed file
that you cannot see them**. If you *can*, that prediction is wrong and it is a
real finding — say so plainly, it is worth more than a confirmation.

Same with CPI Downshift Tuning: the resource holds three items, the code fills
four. Whichever you see settles which list is dead.

**There is no LED capture and no LED questions.** There was, until 2026-09-05.
`notes/gui-surface.md` §3 has the working; the short version is that the LED
page exists in the resources of all four config-tool versions and is created as
a tab by none of them, so there is nothing to click. the owner independently reported
seeing no LED controls and no lights on the mouse. The one LED setting that *is*
reachable, `Disable LED on Lift-Off`, is on the Basic page and is already line
10 of `02-basic`.

### A complete example of what `log.txt` should look like

```
02-basic
1 polling 8000 -> 125
2 angle snapping OFF -> ON
3 ripple control ON -> OFF
4 disable LED on lift-off OFF -> ON
6 cpi1 800 -> 3200
10 clicked the 3rd radio button from the top (was on the 1st)

03-sensor
1 motion sync OFF -> ON
2 motion jitter filter ON -> OFF
5 sensor angle tuning 0 -> 37
6 cpi downshift -> "Default"  (list had FOUR items, not three)
7 smoothing -> "Ripple Control On"

04-buttons
1 left-handed OFF -> ON
2 changed FORWARD button to "Middle Click"
3 changed BACK button to keyboard key F13
   -- note: SCROLL DOWN combo would not open, skipped it
```

That is the whole job. Roughly one short line per change.

### RULES. Read these once; they remove every judgement call below.

1. **One change, then APPLY once, then wait two seconds.** Never batch two
   changes into a single APPLY. Never click APPLY twice for one change.
2. **Every numbered line below is exactly one APPLY.** Line 7 in the table is
   the 7th record in that capture. That correspondence is the entire labelling
   system, so it must not break.
3. **If a value is refused, clamped, greyed out, or the control will not accept
   it: write `REFUSED` in log.txt with what actually happened, and go to the next
   line.** Do **not** substitute a different value. A silent substitution makes
   that diff and every diff after it unattributable. A refusal is *data* — the
   vendor enforcing a rule is itself a finding.
4. **If you cannot do a line at all, write `SKIPPED` and keep the numbering.**
   Never renumber and never fill the gap with something else.
5. **Dropdown items are named by POSITION — 1st, 2nd, 3rd from the top** — because
   we have never seen most of these lists and their names are unknown to me.
   Write down what each position is actually called.
6. **If a number gets clamped to something else, write the value it actually
   became.** `set 400 -> became 800` is useful; `400` when it became 800 is
   poison.
7. **If APPLY does nothing, or the app hangs or crashes,** note it, restart the
   app, start a fresh capture file with a `b` suffix (`02-basic-b.pcapng`), and
   note where you resumed.

### How many values, and why it differs by control type

**We can only ship a value we have observed.** Offering an unobserved one means
writing a byte whose meaning is `[G]`, which `CLAUDE.md` §1.3 forbids. So:
checkbox → one click (both states seen); **dropdown → every option** (no formula
exists, a skipped option is a feature we cannot ship); number → **three values**
(a formula exists; two set it, the third proves it).

---

### `02-basic.pcapng` — Basic page

| # | do exactly this |
|---|---|
| 1 | Polling Rate → **125** |
| 2 | Polling Rate → **250** |
| 3 | Polling Rate → **500** |
| 4 | Polling Rate → **1000** |
| 5 | Polling Rate → **2000** |
| 6 | Polling Rate → **4000** |
| 7 | Polling Rate → **8000** |
| 8 | click **Angle Snapping** once |
| 9 | click **Ripple Control** once |
| 10 | click **Disable LED on Lift-Off** once |
| 11 | CPI Levels → **1** |
| 12 | CPI Levels → **2** |
| 13 | CPI Levels → **3** |
| 14 | CPI Levels → **4** |
| 15 | CPI level 1 → **400** |
| 16 | CPI level 1 → **800** |
| 17 | CPI level 1 → **3200** |
| 18 | CPI level 2 → **1600** |
| 19–30 | LOD → **every option in the list, top to bottom, one APPLY each.** Number them 19, 20, 21 … and write down how many there actually were |
| 31 | CPI level 1 **X** value → **1000** (if X and Y are locked together, write `LOCKED` and skip to 33) |
| 32 | CPI level 1 **Y** value → **2000** |
| 33 | radio buttons → click the **1st** from the top. *(These four have no labels in the resource. Write down what they sit next to. If you cannot find four radio buttons, write `NOT FOUND` on 33–36 and move on — that is a finding, not a failure.)* |
| 34 | radio buttons → click the **2nd** |
| 35 | radio buttons → click the **3rd** |
| 36 | radio buttons → click the **4th** |

### `03-sensor.pcapng` — Advanced Sensor page

| # | do exactly this |
|---|---|
| 1 | click **Motion Sync** once |
| 2 | click **Motion Jitter Filter** once |
| 3 | click **Force max Sensor fps** once |
| 4 | click **Sensor Glass Mode** once |
| 5 | Sensor Angle Tuning → **0** |
| 6 | Sensor Angle Tuning → **20** |
| 7 | Sensor Angle Tuning → **its maximum** (write the number) |
| 8+ | CPI Downshift Tuning → **every option, top to bottom, one APPLY each.** Number them 8, 9, 10 … and write each name down |
| next | Smoothing Tuning → **every option, top to bottom, one APPLY each.** Continue the numbering, write each name |

### `04-buttons.pcapng` — the **Buttons** tab

*(This is the tab with the Multiclick Filter sliders. Corrected 2026-09-05: an
earlier version of this file had 04 and 05 the wrong way round.)*

| # | do exactly this |
|---|---|
| 1 | click **Slamclick Filter** once |
| 2 | Left Button Multiclick Filter → **its minimum** (write the number) |
| 3 | Left Button Multiclick Filter → **its maximum** (write the number) |
| 4 | Left Button Multiclick Filter → **roughly halfway** (write the number) |
| 5 | Right Button Multiclick Filter → **its maximum** |
| 6 | Middle Button Multiclick Filter → **its maximum** |
| 7 | Forward Button Multiclick Filter → **its maximum** |
| 8 | Back Button Multiclick Filter → **its maximum** |
| 9 | the **first** `SPDT:` combo (next to *Left* Button) → **1st item** |
| 10 | first `SPDT:` combo → **2nd item** |
| 11 | first `SPDT:` combo → **3rd item** |
| 12 | the **second** `SPDT:` combo (next to *Right* Button) → **1st item** |
| 13 | second `SPDT:` combo → **2nd item** |
| 14 | second `SPDT:` combo → **3rd item** |
| 15 | click the **"I understand…"** checkbox once |

Lines 6–8 exist because there are **five** Multiclick sliders, one per button,
and each needs to be moved at least once for its byte to be located. Lines 2–4
move one slider three times to prove the byte holds a *number* rather than a
flag; the rest only need to move once each.

Both `SPDT:` combos should hold three items. If either holds more, keep going
and renumber the rest.

### `05-buttonmapping.pcapng` — the **Button Mapping** tab

Each mouse button has a dropdown, and the dropdowns are **nested**: the top
level groups functions, and you open a group to reach the actual function.
The owner confirmed the shape on 2026-09-05 — e.g. Right Button → **MOUSE** → **LEFT
CLICK**.

There is no entry for the left mouse button itself. That is expected.

The groups, and what the binary says is inside each (`0x155cde`, the vendor's
own strings, in its own order):

| group | contains |
|---|---|
| **MOUSE** | LEFT CLICK, RIGHT CLICK, MIDDLE CLICK, FORWARD, BACK, SCROLL UP, SCROLL DOWN |
| **KEYBOARD KEY** | opens a small window: a key field, tick boxes SHIFT / CTRL / WIN / ALT, and **OK** |
| **CPI** | CPI LOOP, FIXED CPI — FIXED CPI opens a small window with a value and **OK** |
| **MEDIA** | PLAY/PAUSE, NEXT, PREVIOUS, MUTE, VOLUME UP, VOLUME DOWN, BROWSER, EXPLORER |
| **DISABLE** | on its own, no submenu |

That grouping is derived from the binary, not observed. **If what you see is
arranged differently, follow what you see** and note the difference — three of
those names could be group headings I have put in the wrong place, and the
screenshot below settles it.

| # | do exactly this |
|---|---|
| — | open the **Forward Button** dropdown and **screenshot it with a submenu expanded**. No APPLY, no number — screenshot only |
| 1 | click **Left-handed Mode** once |
| 2 | Forward Button → MOUSE → **BACK** |
| 3 | Forward Button → MOUSE → **MIDDLE CLICK** |
| 4 | Forward Button → **DISABLE** |
| 5 | Forward Button → MEDIA → **VOLUME UP** |
| 6 | Back Button → MOUSE → **MIDDLE CLICK** — deliberately the same function line 3 gave Forward |
| 7 | Back Button → MOUSE → **FORWARD** |
| 8 | Wheel Up → **DISABLE** |
| 9 | Forward Button → CPI → **FIXED CPI**, set it to **1600**, press **OK** |
| 10 | Forward Button → **KEYBOARD KEY**, press the **A** key, tick nothing, press **OK** |
| 11 | Forward Button → **KEYBOARD KEY**, press **A** again but tick **CTRL** first, press **OK** |
| 12 | Forward Button → **KEYBOARD KEY**, press **A**, tick **CTRL and SHIFT together**, press **OK** |

**Line 6 is the one that may be refused**, if the app will not let two buttons
share a function. **That refusal is a real finding — write `REFUSED` and exactly
what it said, then do line 7 anyway.** Line 7 uses a function nothing else
holds, so it answers the same question either way: whether each button owns its
own byte.

Lines 9–12 are the expensive ones and the most valuable. A plain function is
probably one byte; `FIXED CPI` has to carry a *number* and `KEYBOARD KEY` a
keycode plus modifier flags, so these lines are what reveal how wide a button
record really is.

Lines 10–12 are a deliberate three-step. the owner confirmed on 2026-09-05 that the
four modifier boxes — SHIFT, CTRL, WIN, ALT — are **independent tick boxes, not
mutually exclusive**. So:

- line 10 → keycode alone, modifiers clear
- line 11 → same keycode, one modifier. The diff is the CTRL bit, isolated.
- line 12 → same keycode, two modifiers at once.

Line 12 is the one that decides the *encoding*, and it is worth an extra APPLY
on its own. If the modifier byte in line 12 is line 11's value with one more bit
set, modifiers are a **bitmask** and all sixteen combinations are reachable from
three observations. If instead it is some third unrelated number, they are an
**enumeration** and every combination has to be observed separately. Those two
readings need completely different code, and nothing short of ticking two boxes
at once tells them apart.

### `07-factory-reset.pcapng`
Start capture, press **Factory Reset**, let it settle, stop. Then **read the
settings again** by closing and relaunching the config tool inside the same
capture. This gives us the defaults for every byte in one shot.

---

## 4. The firmware update (15 min) — `08-flash.pcapng`

You are flashing anyway. Just record it.

1. **Close the config tool completely.**
2. Plug the laptop into power. Pause Windows Update if it is threatening to run.
3. **Start the capture first.** Then launch `Firmware Updater 1.10`.
4. Let it sit for ten seconds without pressing anything, so we capture the idle
   state.
5. Press **Update Firmware**. Do not touch anything until it finishes.
6. Let it fully complete. Stop the capture. Save as `08-flash.pcapng`.
7. Screenshot the final status line.

Then, still worth doing and free:

### `09-flash-again.pcapng`
Run the updater a **second time** on the now-updated mouse and press the button
again. This answers a question nothing static can: **does the device refuse a
same-version image?** The host tool does not check (proven — `fw110` `0x4033b0`
has no comparison on the version), so whatever happens is the *device's* answer.
If it refuses, nothing happened. If it flashes, we can re-capture a flash any
time we like.

**Note:** the update ends with a factory reset (`A1 13`), so your settings will
be wiped. That is expected — do not reconfigure anything first.

---

## 5. Getting it to me

Everything into one folder, uploaded to Google Drive, downloaded on the Mac, and
dropped at the repo root as `windows-run/`:

```
windows-run/
  01-baseline.pcapng
  02-basic.pcapng
  03-sensor.pcapng
  04-buttons.pcapng
  05-buttonmapping.pcapng
  07-factory-reset.pcapng
  08-flash.pcapng
  09-flash-again.pcapng
  log.txt              <- the numbered list of what you changed, in order
  screenshots/*.png
```

`log.txt` matters as much as the captures. Something like:

```
02-basic: 1 polling 8000->125 | 2 angle snapping off->on | 3 ripple on->off | ...
```

---

## If something goes wrong

- **USBPcap shows nothing.** Wrong root hub. Try each; or capture on all of them.
- **The capture is enormous.** You touched the OP1. Set polling to 125 Hz first
  and drive the UI with the other mouse.
- **You lose track of the order.** Stop, start a fresh file, note where you are.
  A short clean capture beats a long ambiguous one.
- **The mouse stops responding at any point.** Unplug it. Hold **LEFT and RIGHT**
  together, plug it back in, keep holding a few seconds. It comes back as
  `Bootloader` and the updater will re-flash it from there. This is tested.
