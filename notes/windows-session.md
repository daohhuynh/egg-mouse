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
4. Open Wireshark. In the interface list you will see `USBPcap1`, `USBPcap2`, …
   one per root hub. To find the right one: start a capture on one, wiggle the
   OP1, and see if packets appear. Note which one works. If unsure, capture on
   all of them — extra data costs us nothing.
5. Make a folder `windows-run` on the desktop. Everything goes in it.

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
- The **LED** window (it opens separately).
- Every dropdown **while it is open**, so we see its actual items in order:
  CPI Levels, LOD, Polling Rate, CPI Downshift Tuning, Smoothing Tuning, the two
  `SPDT:` combos, LED effect, and one of the six button-assignment combos.
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
LED effect: how many items does the dropdown show?                ___
Polling Rate: how many options?                                   ___
LOD: how many options, and what is the highest?                   ___
Button assignment dropdown: roughly how many options?             ___
LED window: what is its apply button called?                      ___
Anything that looked odd, crashed, or refused a value:            ___
=== END ANSWERS ===
```

Those first two are the interesting ones. Both buttons exist in the resource but
are shipped without the visible flag, and we have **predicted in a committed file
that you cannot see them**. If you *can*, that prediction is wrong and it is a
real finding — say so plainly, it is worth more than a confirmation.

Same with CPI Downshift Tuning: the resource holds three items, the code fills
four. Whichever you see settles which list is dead.

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

### How many values to capture, and why it differs by control type

**We can only ship a setting value we have actually seen on the wire.** Guessing
an encoding means writing a byte whose meaning is `[G]`, which `CLAUDE.md` §1.3
forbids. That gives one rule per control type:

| control | capture | why |
|---|---|---|
| **checkbox** | one toggle | two states, and one toggle shows you both |
| **dropdown** | **every single option** | there is no formula. Nothing says index 3 means `0x08` rather than `0x03`. An option you skip is an option the app cannot offer |
| **number / slider** | **three values** | there IS a formula. Two points establish it, the third proves it. Three covers a slider with 26,000 positions |

Dropdowns are the ones people cut short, and they are exactly the ones where
cutting short costs you a feature. Each extra option is about eight seconds:
pick it, click APPLY.

**The six button-assignment dropdowns are the exception.** Their lists are long
and we have not traced them, so do not try to exhaust them — see `04-buttons`.

### `02-basic.pcapng` — Basic page
| # | change | to |
|---|---|---|
| 1 | Polling Rate | **every option, one at a time, APPLY between each** — 125, 250, 500, 1000, 2000, 4000, 8000. Do this first: it also quiets the capture. **Note how many options the list has** |
| 2 | Angle Snapping | toggle |
| 3 | Ripple Control | toggle |
| 4 | Disable LED on Lift-Off | toggle |
| 5 | CPI Levels | **every option** — 1, 2, 3, 4 |
| 6 | CPI level 1 | **three values: 400, then 800, then 3200** |
| 7 | CPI level 2 | **1600** (proves level 2 is a different byte from level 1) |
| 8 | LOD | **every option** — 0.7 through 1.7 and 2.0. **Note how many there are** |
| 9 | X/Y: set X to a different value from Y | note both numbers |
| 10 | the four unlabelled radio buttons | **click each of the four in turn** — note which position is which |

### `03-sensor.pcapng` — Advanced Sensor page
| # | change | to |
|---|---|---|
| 1 | Motion Sync | toggle |
| 2 | Motion Jitter Filter | toggle |
| 3 | Force max Sensor fps | toggle |
| 4 | Sensor Glass Mode | toggle |
| 5 | Sensor Angle Tuning | **three values** — try `0`, `20`, then the maximum. Note all three |
| 6 | CPI Downshift Tuning | **every option**. **Note each name and how many there are** — three vs four settles a prediction |
| 7 | Smoothing Tuning | **every option**. Note each name |

### `04-buttons.pcapng` — Buttons page
| # | change | to |
|---|---|---|
| 1 | Left-handed Mode | toggle |
| 2 | **Screenshot one button dropdown fully open**, and scroll it if it is long. We have never seen this list | |
| 3 | pick ONE button (say FORWARD) and set it to **four different options**, APPLY between each | note each one |
| 4 | set a **different** button to one of those same options | this proves whether each button has its own byte |

Do not exhaust these lists. Four values from one button plus one from another
tells us the byte layout and the encoding shape; the rest follows and is
checkable later.

### `05-buttonmapping.pcapng` — Button Mapping page
| # | change | to |
|---|---|---|
| 1 | Slamclick Filter | toggle |
| 2 | Left Button Multiclick Filter | **three values** — minimum, middle, maximum. Note all three |
| 3 | Right Button Multiclick Filter | one distinctive value, note it |
| 4 | first `SPDT:` combo | **every option** — OFF, GX Speed Mode, GX Safe Mode |
| 5 | second `SPDT:` combo | **every option** |
| 6 | the acknowledgement checkbox (1064) | toggle — it may or may not be stored at all, which is itself worth knowing |

### `06-led.pcapng` — LED window
| # | change | to |
|---|---|---|
| 1 | R / G / B | **R=170, G=187, B=204** (`AA BB CC` — will leap out of the dump) |
| 2 | R / G / B | **R=1, G=2, B=3** (proves the order and that they are three separate bytes) |
| 3 | LED effect | **every option**. The resource holds only one item — **note how many the app actually shows**, since more than one means the list is built in code |
| 4 | LED On / Off | toggle |
| 5 | Scroll led | toggle |
| 6 | Logo led | toggle |
| 7 | DPI led | toggle |

Note which apply button you used — this window has its own.

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
  06-led.pcapng
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
