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

Take one of **every** page and name them plainly. These test predictions that
are already committed, so they are evidence, not decoration.

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
change the next, **click APPLY**, and so on. Keep a numbered list as you go —
put it in `windows-run/log.txt`. The list is as important as the capture.

Use the exact values below where given. They are chosen to be unmistakable in a
hex dump.

### `02-basic.pcapng` — Basic page
| # | change | to |
|---|---|---|
| 1 | Polling Rate | **125 Hz** (also quiets the capture — do this first) |
| 2 | Angle Snapping | toggle it |
| 3 | Ripple Control | toggle it |
| 4 | Disable LED on Lift-Off | toggle it |
| 5 | CPI Levels | **2** |
| 6 | CPI level 1 | **3200** |
| 7 | CPI level 1 | **6400** |
| 8 | LOD | **0.7mm** |
| 9 | LOD | **2.0mm** |
| 10 | the four unlabelled radio buttons | click a different one |

### `03-sensor.pcapng` — Advanced Sensor page
| # | change | to |
|---|---|---|
| 1 | Motion Sync | toggle |
| 2 | Motion Jitter Filter | toggle |
| 3 | Force max Sensor fps | toggle |
| 4 | Sensor Glass Mode | toggle |
| 5 | Sensor Angle Tuning | some distinctive number, note it |
| 6 | CPI Downshift Tuning | last item in the list |
| 7 | Smoothing Tuning | last item in the list |

### `04-buttons.pcapng` — Buttons page
| # | change | to |
|---|---|---|
| 1 | Left-handed Mode | toggle |
| 2 | one button's assignment | anything distinctive, note which and what |
| 3 | a different button | something else, note it |

### `05-buttonmapping.pcapng` — Button Mapping page
| # | change | to |
|---|---|---|
| 1 | Slamclick Filter | toggle |
| 2 | Left Button Multiclick Filter | **maximum** |
| 3 | Left Button Multiclick Filter | **minimum** |
| 4 | Right Button Multiclick Filter | maximum |
| 5 | first `SPDT:` combo | **GX Speed Mode** |
| 6 | first `SPDT:` combo | **GX Safe Mode** |
| 7 | second `SPDT:` combo | GX Speed Mode |

### `06-led.pcapng` — LED window
| # | change | to |
|---|---|---|
| 1 | R / G / B | **R=170, G=187, B=204** (0xAA 0xBB 0xCC — these will jump straight out of the dump) |
| 2 | LED On / Off | toggle |
| 3 | Scroll led | toggle |
| 4 | Logo led | toggle |
| 5 | DPI led | toggle |

Click whatever apply button that window has, and note which one you used.

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
