# The config protocol, on the wire [O]

From `windows-run/01`–`07` and `10`: Endgame's config tool 1.07 driving the owner's
mouse, one setting change per APPLY, captured 2026-09-05. All `[O]`.

The chronological order of the captures is **not** their numbering. By file
mtime: 00, 01, 02, 03, 04, 05, **07, 06**, 08, 09, 10. Section 07 ran before
section 06. Several findings below only make sense in that order.

## 1. Command set

Same two feature reports as the flasher (`notes/flash-wire-observed.md` §1),
same direction asymmetry: outbound byte 0 is the report id, inbound byte 0 is
already payload.

| sent | report | meaning | answer |
| --- | --- | --- | --- |
| `a1 02` | 64 | identify | `a1 01 …` with version and ids |
| `a1 12` | 64 | read settings | 1040-byte record on report `0xa0` |
| `a0 11` + record | 1041 | write settings | `a1 01 …` |
| `a1 13` | 64 | **factory reset** | `a1 01 …` after ~1.1 s |
| `a1 3a 00 00 00 5a a5 32` | 64 | enter bootloader | see the flash notes |

The identify response carries the firmware version at payload `0x11`–`0x12` and
the USB ids right after it:

```
01-baseline   a1 01 … 00 07 01 00 67 33 78 19     fw 1.07, VID 0x3367, PID 0x1978
10-postflash  a1 01 … 00 10 01 00 67 33 78 19     fw 1.10
```

Version is minor-then-major, one byte each. That is the whole of §4.2's
"round-trip a benign query and confirm a structurally plausible response", and
it is the cheapest §4.4 stage-1 command there is.

## 2. Framing of the record

Offsets are on the bytes **as transferred**, one origin for both directions:

```
write buffer, 1041 bytes   [0]=0xA0 report id, [1]=0x11 command, record at [16]
read buffer,  1040 bytes   [0]=0xA1 status,    [1]=0x01,         record at [16]
```

Record `N` is buffer byte `N + 16`, in **both** directions. `cfg107` agrees
independently: its write builder targets frame+0x10 and its read parser reads
frame+0x10 — the same constant, both ways.

**Corrected 2026-09-05**, after an independent check refuted an earlier wording
that put the write's record at 15 and the read's at 16. That asymmetry was an
artefact of measuring the two directions from different origins, not a property
of the protocol. The real asymmetry is that the device omits the report-id byte
from responses and returns one byte fewer than asked for.

**That alignment is not fitted — it was derived first.** `config-protocol.md`
§7.2a read the builder out of `cfg107 FUN_00404180`: `memset` the 1041-byte
buffer, `movl $0x11a0` putting `a0 11 00 00` at `buf[0..3]`, then `rep movsl` of
0x100 dwords into `leal -0x45c(%ebp)` = **`buf[16]`**. Buffer byte 16 is payload
byte 15, because the report id occupies `buf[0]`. So the `.exe` and the wire
agree on the offset without either being adjusted to the other.

That matters for how much the sixteen confirmations are worth. Had the shift
been fitted, one free parameter would be placing all sixteen predictions and the
count would partly be measuring the fit. It was not, so it isn't: polling at
0x05, the LED at 0x08, angle snapping at 0x0a, downshift and smoothing sharing
0x0b, motion sync at 0x0c, stage count at 0x0e, the CPI flag at 0x23, multiclick
at 0x3d and 0x44, sensor angle at 0x70, fps at 0x71 and the acknowledgement at
0x72 each land on an independently derived offset.

The record proper is the 1024 bytes copied by that `rep movsl`, of which only
`0x00`–`0x72` — 115 bytes — carry anything. Payload byte 1039 is a pad and is
always zero.

## 3. What each APPLY moved

Every one of the 73 writes across five captures is accounted for, and each maps
to a numbered line of the repo-root `log.txt`. The maps are in
`Tools/capture/maps/`, written by hand with the residue enumerated, because
pairing write N with line N is wrong wherever a line produced no write.

| record | field | encoding |
| --- | --- | --- |
| `0x01` | unknown | device reports `0x80`; the vendor writes `0x00`. §5 |
| `0x05` | polling rate | `8000 / rate` |
| `0x06` bit 0 | slamclick filter | rest of the byte held |
| `0x08` | LED on lift-off | **inverted** — ticking "disable" stores 0 |
| `0x09` | lift-off distance | 0–10, eleven options |
| `0x0b` bits 3:2 | CPI downshift | remapped 0→2, 1→3, 2→1, 3→0 |
| `0x0b` bits 1:0 | smoothing | remapped 0→2, 1→0, 2→1 |
| `0x0a` | angle snapping | 1 when ticked |
| `0x0c` | motion sync | 1 when ticked |
| `0x0d` | **active** CPI stage | 0-based |
| `0x0e` | CPI stage **count** | 1–4 |
| `0x23`+5n | stage n X≠Y flag | leads its group |
| `0x24`+5n | stage n X | LE16, raw CPI |
| `0x26`+5n | stage n Y | LE16, raw CPI |
| `0x37`+7n | button n type | see below |
| `0x3d`+7n | button n multiclick / SPDT | shared byte, 0–25 or `0xf0`/`0xf1` |
| `0x70` | sensor angle | raw trackbar, two's complement, max 127 |
| `0x71` | force max sensor fps | 1 when ticked |
| `0x72` | multiclick acknowledgement | 1 once ticked |

`0x0d` and `0x0e` are different fields and the difference bites: reducing the
stage count to 2 while stage 4 was active moved **both**, because the active
stage was forced down. A tool that treats them as one will silently change which
CPI the mouse is using.

### Button entries

Eight entries of seven bytes, `0x37`–`0x6e`. Observed indices: 0 left, 1 right,
2 middle, 3 forward, 4 back, 6 wheel-up. Entries 5 and 7 were never touched by
any line — **not "unused", just unobserved** (§1.2a).

Entry layout, from the five action types the capture exercised:

```
+0 type   +1 parameter   +2..+5 payload   +6 multiclick/SPDT
```

| type | +1 | +2.. |
| --- | --- | --- |
| mouse button | button mask: 01 L, 02 R, 04 M, 08 back, 10 forward | — |
| `0xff` disable | 00 | — |
| `0x20` media | `0xe9` for volume up (USB HID consumer usage) | — |
| `0x0c` fixed CPI | 00 | X then Y, LE16 each — `40 06 40 06` = 1600 |
| `0x02` keyboard | **HID modifier byte** | HID usage — `04` is A |

The modifier is a bitfield, not an enumeration: no modifier `0x00`, Ctrl `0x01`,
Ctrl+Shift `0x03`. That was the specific thing that could have failed, and it
is why every unobserved combination is writable rather than guesswork.

Left-handed mode swaps the `+1` masks of entries 0 and 1 and nothing else.

## 4. Two things the capture proved that were not predictions

**The APPLY button is not the only writer.** Section 06 clicked four radio
buttons with APPLY never pressed, and four full settings writes went out
regardless. That is the bypass path found in `cfg107` at `0x414010`, confirmed
on the wire. Anything we build has to assume a config write can be triggered by
something other than the obvious control.

**Every write sends the whole record.** All 73 are 1040-byte writes; none is a
partial update. So the vendor tool pushes its entire in-memory copy on every
change, and any byte stale in that copy is pushed with it. This is the concrete
reason §4.1 says read-modify-write: our copy must come from a fresh read, not
from anything we assembled.

## 5. Where this gets uncomfortable, and what is genuinely not known

### Factory reset was NOT confirmed to work

Section 07 pressed Factory Reset. It sent `a1 13` and nothing else — no settings
write. But **the record read back identical before and after**, and identical to
`01-baseline`. That is not a demonstration that reset works; the record was
already at defaults when the section opened, so the test was vacuous.

§4.1 requires factory reset to be implemented first and *confirmed working*
before any other config write. **This capture does not confirm it.** It has to
be redone with the device in a knowingly non-default state.

### Settings did not survive between most captures

Opening reads, in chronological order:

```
01  02  03    04            05  07  06     10
--  --  --    --            --  --  --     --
defaults ...  03's end      defaults ...   defaults except 0x71
```

Only 04 opened where the previous section left off. Every other section opened
at defaults, discarding the section before it. The one structural marker that
tracks it is record `0x01`: it reads `0x80` in every opening read except 04's,
where it reads `0x00` — and `0x00` is what the vendor always writes there.

The likeliest reading is that the record lives in RAM, reloads from
firmware-held defaults on power cycle, and that the owner replugged the mouse between
most captures because USBPcap generally needs the device reattached to see it.
That is `[G]`, and **the capture argues against the replug half of it.**

Machine-checked 2026-09-05, over `usbpcap.read()` on all eleven files: the
device's USB address is **4 in every capture from 00 through 07**, and changes
only at the flash captures (`08` sees 5 and 6, `09` sees 6, 7 and 8, `10` sees
8) — which is what a bootloader re-enumeration looks like. So no address change
is visible across the very boundaries where the settings vanished. Windows can
reuse an address on the same port, so this is evidence rather than proof; but
"The owner replugged between sections" is no longer the comfortable explanation it
looked like, and something that reverts the record **without** re-enumeration
now has to be considered.

The gaps themselves, end of one capture to start of the next, in chronological
order (note that `07` runs **before** `06` by timestamp — the file numbering is
not chronological, and any analysis that assumes it is will mis-pair a write
with the read that follows it):

```
01 -> 02   5501 s   reverted        05 -> 07    958 s   reverted
02 -> 03    648 s   reverted        07 -> 06    231 s   reverted (07 was a reset)
03 -> 04    124 s   HELD, 0/115     06 -> 08      -     no read
04 -> 05    388 s   reverted        09 -> 10    269 s   reverted (fw 1.10)
```

The survivor is the shortest gap by a factor of three, which is consistent with
a timeout and equally consistent with the tester having done something in the
longer ones. `[G]` either way.

The correlation that does hold without exception across all nine reads is
simply: `record 0x01 == 0x80` exactly when the record is at firmware defaults,
`0x00` exactly when it holds what a host wrote. n=9, one negative case, and
still a correlation — **not a basis for a write** (§1.3). What is **not** a guess is the consequence, and it was load-bearing
for §4.1: **we have no evidence that `a0 11` persists across a power cycle, and
direct evidence that written settings vanished across most capture boundaries.**
Reading back after a write therefore verifies RAM. It does not verify what
survives unplugging, and our config tool must not claim otherwise until someone
writes a setting, replugs, and reads.

**RESOLVED 2026-09-05, first half only.** Someone did exactly that. `egg-config
restore` wrote all 1024 payload bytes, the mouse was unplugged for 8.6 s
(kernel USB log, 18:24:06.878 → 18:24:15.465), and the read afterwards was
**byte-identical to what was written, 0 of 1024 differing**. Scored in
`prediction-restore.md`. So `a0 11` writes storage that outlives loss of bus
power — `[O]`, n=1, ~9 s off.

**AND THE DURATION HYPOTHESIS IS NOW DEAD FOR FOUR OF THE FIVE REVERTS.**
Repeated the same test with a long unplug: **1706.8 s — 28.4 minutes** — off
power (kernel log 18:54:04.299 → 19:22:31.094). Result again **0 of 1024
payload bytes differ**, sha256 `23141790…` on both sides, checked directly
against the file as well as through `egg-config diff`.

Line that up against the capture gaps:

```
reverted   231 s   388 s   648 s   958 s   5501 s
held       124 s                                    <- capture 03 -> 04
held         9 s                                    <- ours, 2026-09-05
held      1707 s                                    <- ours, 2026-09-05  ** 28.4 min **
```

1707 s is longer than **four of the five** gaps across which the vendor's
settings vanished. So "the record decays with time off power" cannot explain
those four, and the only reverting gap it does not straddle is the 5501 s one.
Combined with the earlier findings — power loss does not revert, re-enumeration
did not occur, launch-time writes are ruled out with coverage — **there is no
surviving mechanism in which the device does this to itself.**

What is left is the vendor's software or the tester's own actions, and the
latter has still not been checked with the one person who knows.

That resolves what the paragraph asked for and **deepens the problem it
describes.** The reverts above were being explained by power cycling; power
cycling demonstrably does not revert. Combined with the USB-address finding
higher in this section — no re-enumeration visible across the boundaries where
the settings vanished — *neither* of the two comfortable explanations survives.
Something else emptied those records. Unknown, `[G]`, and open: the vendor tool
writing defaults at launch or exit, a device-side timeout, or a separate
commit/discard command not yet found (§1.2a — no command is *named* commit, and
that is not evidence there isn't one).

Durations, for whoever picks this up: reverted at 231–5501 s, held at 124 s, and
now held at ~9 s. Two holds and five reverts, with the holds both far below the
shortest revert. A duration effect is not excluded and two points is not a
curve. **Nothing here licenses a claim that the record survives overnight** —
that is one unplug and one read away and has not been done.

**And the duration framing is probably wrong, recorded 2026-09-05 so it stops
being restated.** Those numbers are gaps *between capture files*, not durations
of power loss. Nobody has established that the mouse was unpowered for any of
them, and the USB-address finding above argues it was not. Treating them as a
retention curve reads a physical mechanism into a wall-clock artefact.

The physics also does not have a knee there. Whatever holds this record survived
8.6 s with no bus power. Nothing passive in a wired mouse holds SRAM that long —
decoupling capacitance drains in milliseconds — so the record is in non-volatile
memory, and NVM retention is specified in years, not minutes. There is no memory
technology whose retention window is "longer than nine seconds but shorter than
a few minutes." Retention is effectively discrete: milliseconds, or years.
That reasoning is `[G]` — it is inference from general knowledge, not from this
device — which is exactly why the longer unplug is still worth running. It is
cheap confirmation of a near-certain thing, not an open risk, and it should not
be written up as one.

### Machine-checked 2026-09-05: the revert is invisible to all eleven captures

Over every `windows-run/*.pcapng`, ordered feature traffic with timestamps:

- **Every config capture opens `A1 02` → `A1 12`** — small query, then a READ.
  No write precedes the first read in any of them.
- **Each one has 3.6–15.2 s of captured silence before that first command**, so
  the capture was already running when the tool was launched. That window is
  covered and it contains nothing. **"The vendor tool writes defaults at launch"
  is ruled out with coverage, not by absence of evidence.**
- **Every config capture ends immediately after an `A0 11` write** — trailing
  silence is 0.0 s in all but `05`, which has 10.1 s of it and nothing in it.
  So the tool being *closed* is outside the capture window in every case.
  **"The vendor tool writes defaults on exit" is NOT ruled out** — it is simply
  never observed, and `05`'s ten quiet seconds are the only evidence against it,
  worth little because we do not know the tool was closed inside them.

So the last thing on the wire in section N is a write of non-default settings,
and the first thing in section N+1 is a read returning defaults, **and nothing
in between was ever captured.** Whatever reverts the record happens in a window
no capture covers.

**The cheapest remaining test is not a capture — it is asking the owner what he did
between sections.** He ran them. If he pressed Factory Reset between sections to
get a clean baseline per section — which is what a careful person capturing
`02-basic`, `03-sensor`, `04-buttons` separately would do, and it would explain
`03 → 04` holding as the one he skipped — then there is no device behaviour here
at all and this whole subsection collapses into a methodology note. **Ask before
deriving further.** §1.2a: the obvious human explanation was never checked.

### 0x71, and why the defaults may have changed with the firmware

`0x71` is the single byte separating `01-baseline` (fw 1.07) from
`10-postflash` (fw 1.10): `01` there, `00` here. Tracked across the run it goes
1 → 0 (set in 03) → 0 (04) → **1** (05, reverted) → 1 → 1 → **0** (10, after the
flash). A revert to 1 on 1.07 and a settle at 0 on 1.10 is consistent with the
defaults being held in firmware and differing between the two versions. `[G]`,
and it is the cleanest available explanation rather than a derived one.

Note what this does to the flash: the updater's last act is `a1 13`, the same
command the Factory Reset button sends. If that is what it means there too, then
**flashing resets the user's settings**, and our flasher must say so before it
starts and must save a copy first. Also `[G]` — `a1 13` was observed doing
nothing visible in section 07 because there was nothing to undo.

### Still open

- Entries 5 and 7 of the button block: never exercised.
- The stage-4 CPI flag at record `0x32`: lines 18d–18f were not performed, so
  only stage 1's flag has been seen move.
- Records `0x02`–`0x04`, and `0x01`'s meaning.
- Whether `a1 13` differs at all from what the flasher sends.

### 5.1 ANSWERED 2026-09-05: it was the tester, not the device

The owner, asked directly rather than derived: **he reset the settings to defaults
before the capture sections**, for a clean per-section baseline — *"i did reset
the settings before the captures... maybe i missed one or two but i remember
doing so."*

That is the whole explanation. Five sections start from defaults because he put
them there; the single gap that **held** (`03 → 04`) is exactly what "maybe I
missed one or two" predicts. No device mechanism is required, and after
eliminating duration, power loss, re-enumeration and launch-time writes, none
was ever found.

**Confidence, stated rather than implied.** This is recollection and he hedged
it. It is not a log and cannot be one — the resets happened outside every
capture. What makes it strong is not the memory, it is that it is the only
surviving candidate and it predicts the one anomaly (`03 → 04`) that every
device-side hypothesis had to explain away.

**This is the §1.2a case, and it went the way §1.2a says it goes.** Working
memory carried the instruction "ASK FIRST — he ran the captures" alongside
a growing body of mechanical elimination. The mechanical work was correct and
necessary — it is what reduced the field to one candidate — but the answer was
always going to come from asking the person who was there. **The subsection
built on the reverts collapses to a methodology note.**

Consequence for the tools: `egg-config`'s persistence warning no longer tells
the user the loss is unexplained, because it is not. It now reports both
power-cycle tests (8.6 s and 1706.8 s, both 0/1024 bytes changed) and this
answer, and still offers the unplug-replug check for anyone who wants it.

## 6. The captures reconcile line-by-line WITHOUT `windows-run/log.txt`  [O]

The owner asked on 2026-09-06 whether `windows-run/` is still usable given that his
filled-in `log.txt` was lost to a non-autosaving editor before upload. **It is.**
All **73** config writes across the five config captures map one-to-one onto a
numbered instruction line in the repo-root `./log.txt`, with every non-writing
line separately explained. Nothing is ambiguous and nothing is guessed.

Two things make this work, and it is worth being explicit about why:

1. **Root `./log.txt` is the instruction script, and it was revised during the
   session.** It is not a blank template — it already carries `NOT PRESENT`
   annotations (02 line 9, 03 lines 2 and 4, all "confirmed 2026-09-05"),
   `SKIPPED` markers (02 lines 31–37), inserted lines (`18a`–`18f`), the exact
   intended values, and the reasoning for each. The lost file was the owner's
   *confirmations*, not the plan.
2. **The record layout is already mapped**, so a diff labels itself. A write
   that moves `0x09` from `03` to `04` is a LOD change whatever any log says.

### Method
Diff each `A0 11` payload against the **`A1 12` read response at the start of
the capture**, not against the first write. The first write is the first APPLY,
not a baseline; diffing from it silently loses one action per capture and was
the reason an earlier pass of this analysis wrongly concluded that `motion sync`
and `slamclick filter` moved no byte. Both move a byte. Payload offset is `+16`.

### Reconciliation

| capture | writes | instruction lines | residue |
| --- | --- | --- | --- |
| `02-basic` | 30 | 1–30 + `18a`–`18f`, less 7 `SKIPPED` | 0 |
| `03-sensor` | 12 | 15 | 0 |
| `04-buttons` | 15 | 15 | 0 |
| `05-buttonmapping` | 12 | 12 | 0 |
| `06-cpi-stage` | 4 | 4 | 0 |

Lines that legitimately produced no write, all predicted **in the file itself**
before the run: 02 line 9 (`ripple control` — not on the Basic page; it is an
item in the Advanced Sensor *Smoothing Tuning* combo, see `basic.png` and
`smoothing-tuning.png`); 03 lines 2 and 4 (`motion jitter filter`, `sensor glass
mode` — neither control exists, `advanced-sensor.png` shows the page has exactly
five controls); 02 line `18a` (`tick X/Y Settings`, annotated `NO APPLY
(expected)` and indeed host-side only); 02 lines 19–30 (twelve LOD slots, eleven
real options); 03 line 15 (four smoothing slots, three real options).

**Three of those are cases where the instruction file asked for controls that do
not exist.** Without the log saying `NOT PRESENT` that reads as missing data;
with the screenshots it reads as an answered question.

### What this scored on the way past

- **Sensor angle is two's complement, `[O]`.** 03 line 6 asked for `-45`;
  record `0x70` went `0x14` → **`0xd3`**. Sign-magnitude would be `0xad`. Line 7
  ("maximum, WRITE THE NUMBER") gave `0x7f` = **127**. The log line said this was
  "the only way to tell two's complement from sign-magnitude, and no positive
  value can" — it worked, and the answer survives the log's loss because it is
  in the bytes.
- **The `X≠Y` flag at `0x23` behaves exactly as §7.8 predicted, `[O]`.** Line
  `18b` (CPI 1 X → 1000) moved `0x23` `00`→`01` *and* `0x24`/`0x25` → `e8 03`.
  Line `18c` (Y → 1500) moved `0x26`/`0x27` → `dc 05` and **left `0x23` alone**,
  because it was already `1`. Flag-first confirmed from the wire.
- **CPI values match the intended values exactly** — `0x0258` = 600, `0x04e2` =
  1250, `0x07d0` = 2000. The log line chose those three precisely because
  400/800/1600 all have a zero high byte; nothing was clamped.
- **Record `0x72`, the "I understand…" acknowledgement, moved `00`→`01`** on 04
  line 2, confirming §7.11 against the alternative that it was host-side.
- **Multiclick range is `0x00`–`0x19` (0–25), `[O]`** — 04 lines 3/4/5 gave
  min `00`, max `19`, and 12 → `0x0c`.
- **LOD is eleven steps, `[O]` on the wire**: `0x09` walked `00`…`0a`, and
  `LOD-0.7to1.2mm.png` + `LOD-1.2to1.7mm.png` show `0.7mm`…`1.7mm` in 0.1
  steps. Independently confirms the `wire-predictions.md` correction that
  refuted the twelve-entry reading.
- **Polling is a descending bitmask, `[O]`**: `0x40` 125 Hz, `0x20` 250,
  `0x10` 500, `0x08` 1000, `0x04` 2000, `0x02` 4000, `0x01` 8000.
- **Record `0x01` reads back `0x80` but is written as `0x00`.** Visible as the
  first diff of four separate captures. A read/write asymmetry, not a setting.

### The one thing genuinely lost, and it is a known gap rather than an ambiguity
02 lines **`18d`, `18e`, `18f`** — the CPI *stage 4* X/Y sequence — produced no
writes. They were not performed. That sequence exists to test a specific
committed prediction: that cfg107 computes stage 4's `X≠Y` flag from stage 3's
edit boxes (`0x40edde`, a suspected vendor copy-paste bug), so a stage-4 X/Y
split should move **stage 3's** flag and not its own. **That prediction is still
untested**, and no amount of re-reading the captures will test it.

**Correction, 2026-09-06: our own tool CANNOT settle it, and three files said
it could.** The claim is about what *cfg107's serializer computes*, not about
what the device stores. `egg-config` computes stage 4's flag from stage 4 (it
must -- §4.2 says mirror the vendor's verification, not the vendor's
arithmetic), so writing with our tool and reading back returns our own value and
tests nothing. Deliberately emulating the suspected bug would be writing a byte
we believe to be wrong, which is worse and still proves nothing about cfg107.

**The only test is the vendor tool on Windows with a capture running:** set CPI
stage 3 to X!=Y, set stage 4 to X==Y, apply. The prediction is that record
`0x32` (stage 4's flag) follows STAGE 3's boxes -- so it reads `01` while stage
4's own X and Y are equal. About thirty seconds of clicking.

**And nothing of ours depends on the answer.** Read-modify-write preserves the
byte when we are not editing that stage, and recomputes it correctly when we
are, whichever way cfg107 behaves. This is a curiosity about a vendor defect,
not a risk to this project -- which is why it stayed open all night rather than
being worth waking anyone for.

## 7. The factory-default record, verified against the screenshots  [O]

Supersedes §6's method. §6 reconciled the captures against the repo-root
`./log.txt`; the owner quarantined that file on 2026-09-06 (CLAUDE.md §1.1a), so §6's
*labelling* is void. **Its byte-level observations stand** — they never depended
on the log — and this section replaces the anchor with one that cannot lie.

The owner's point, 2026-09-06: *"you have all of the defaults in the screenshots.
theyre guaranteed defaults, you can trace what changed from what starting from
the values in the screenshots."* That is checkable, and it checks out.

### The check
`01-baseline`, **both** reads in `07-factory-reset` (pre- and post-reset), and
`10-postflash-baseline` are compared against the visible state of `basic.png`,
`advanced-sensor.png`, `buttons.png` and `button-mapping.png`.

| setting (screenshot) | record | value | agrees |
| --- | --- | --- | --- |
| Polling Rate `8000Hz` | `0x05` | `01` | yes |
| LOD `1.0mm` | `0x09` | `03` | yes — 4th of `0.7`…`1.7` |
| CPI Levels `4` | `0x0e` | `04` | yes |
| CPI 2 radio selected | `0x0d` | `01` | yes — zero-based |
| Angle Snapping unticked | `0x0a` | `00` | yes |
| **Disable LED on Lift-Off unticked** | `0x08` | **`01`** | yes — **inverted**, and this is the independent confirmation of the `sete` at cfg107 `0x40ecd2` (§7.16) |
| X/Y Settings unticked | `0x23` | `00` | yes |
| Downshift `Default` + Smoothing `Ripple Control Off` | `0x0b` | `00` | yes — both nibbles zero |
| Motion Sync unticked | `0x0c` | `00` | yes |
| Force max Sensor fps **ticked** | `0x71` | `01` | yes |
| Sensor Angle `0` | `0x70` | `00` | yes |
| Slamclick Filter ticked | `0x06` bit 0 | `1` | yes |
| "I understand…" unticked | `0x72` | `00` | yes |
| CPI 1–4 = 400/800/1600/3200, X=Y | `0x23`–`0x36` | flags `00`, values match | yes |
| five multiclick sliders at `8` | `0x3d`+7n | `08` ×5 | yes |
| RIGHT/MIDDLE/FORWARD/BACK/SCROLL UP/SCROLL DOWN | `0x37`–`0x6e` | `0001 0002 0004 0010 0008 09f1 0101 01ff` | yes |

**Sixteen of sixteen.** The screenshots are the factory-default state, and the
default record is now a known 1024-byte vector anchored to something other than
a log file.

### Three whole-record diffs, and they are all zero
    01-baseline            vs 07 pre-reset    0 differing bytes
    07 pre-reset           vs 07 post-reset   0 differing bytes
    01-baseline            vs 07 post-reset   0 differing bytes

So the mouse was at factory defaults at the start of the run **and** immediately
before the factory reset in 07 — the reset changed nothing because there was
nothing to change. That independently corroborates §5.1 (the tester reset
between capture sections) without needing anyone's recollection, and it is why
sections 02–06 each start from defaults.

### The one byte firmware 1.10 changed  [O]
    07 post-reset (fw 1.07)  vs  10-postflash (fw 1.10)
      1 differing byte:  0x71  01 -> 00

`0x71` is **Force max Sensor fps**. Its factory default is `1` on firmware 1.07
and `0` on 1.10. Every other byte of the 1024 is identical, across a firmware
upgrade and two factory resets.

Two consequences:

1. **The settings record layout is stable across 1.07 → 1.10.** One default
   changed; nothing moved. That is the strongest evidence yet that the config
   work derived from a 1.07-era capture set is valid on the owner's 1.10 device — and
   it is now evidence rather than an assumption.
2. **Never hardcode a default vector.** CLAUDE.md §5's "anything a new firmware
   version changes" is no longer hypothetical: it has exactly one known
   instance, and a tool that shipped 1.07's defaults would silently re-enable a
   sensor setting on 1.10 that the vendor now ships off.

This also fixes an error in an earlier pass of this analysis, recorded so it is
not repeated: a first attempt read "baselines" by filtering transfers on
`len(data) > 100`, which matched a **727-byte USB configuration descriptor**
rather than the 1041-byte feature report, and produced nonsense values for
`0x0b` and `0x06`. Filter on `is_get_report and len > 1000`.

## 8. The firmware-1.10 capture run, 2026-09-06  [O]

Seven captures in `windows-capture/`, taken by the owner on the Windows laptop against
firmware **1.10** with config tool **1.07**. Predictions were pre-registered in
`notes/prediction-capture-1.10.md` and committed at `8a25286`, before any file was
opened. Decode with:

    python3 Tools/capture/score110.py windows-capture/

`score110.py` diffs consecutive settings records inside one capture and names an
action only from §7.17's table, printing `UNKNOWN` otherwise. It never guesses.

### 8.1 The stage-4 CPI flag bug is REAL, observed twice  [O]

The headline. §7.8 derived from cfg107 that stage 4's `X != Y` flag is computed
from **stage 3's** edit boxes -- a copy-paste bug in the vendor's collect pass.
Never testable before, because stages 3 and 4 had never been written.

`13-cpi-stage34.pcapng`, factory defaults at the open (400/800/1600/3200, every
flag `00`):

| seq | user action | flags after |
| --- | --- | --- |
| 4 | CPI3 X 1600 -> **1200** | `0x2d` -> `01` **and `0x32` -> `01`** |
| 6 | CPI3 Y 1600 -> 2400 | unchanged |
| 8 | CPI4 X 3200 -> 3000 | unchanged |
| 10 | CPI4 Y 3200 -> **3000** | `0x32` **stays `01`** |

Both ends of the bug are visible:

- **seq 4 sets stage 4's flag while stage 4 is still 3200/3200, equal.** Nothing
  about stage 4 changed. The only thing that changed is stage 3.
- **seq 10 leaves stage 4's flag at `01` after stage 4 becomes 3000/3000, equal.**
  It is not cleared, because stage 3 is 1200/2400 and still unequal.

So the flag byte at `0x32` tracks stage 3, exactly as `0x40edde` says. `[D]`
became `[O]`, and the prediction (`prediction-capture-1.10.md`, the `13-cpi-stage34` block) was
a HIT.

**Consequence for us: `0x32` is not a field we can compute from stage 4.** A
read-modify-write preserves whatever the vendor left, which is correct behaviour
and requires no change. Writing `0x32` from our own `X != Y` test would produce a
record the vendor tool never produces.

### 8.2 All eight MEDIA actions confirmed; §7.15's bar is lifted  [O]

> **CORRECTED 2026-09-07, and the error was mine, not the capture's.** This
> section read the seven writes as moving "only button 2's `+0`/`+1`, `+2`-`+5`
> staying zero", and concluded that "the wide usages do not spill into
> `+2`-`+3`" and that "neither hypothesis in §7.15 was right". **The capture it
> cites says the opposite of all three.** BROWSER and EXPLORER write `01` at
> `+2`. I compared the two bytes I was looking for and called the rest zero
> without reading it.
>
> It reached the code: `egg-config map <btn> browser` wrote `18 96 00` for a
> week, a byte pattern the vendor's tool never produces. Fixed in the same
> commit as this correction, with `Tests/test_button_map.py` now scoring against
> this capture and `Tests/test_citations.py` checking the `+2..+3` and `+4..+5`
> word stores against cfg107's bytes.
>
> The original text is kept below the corrected version, because a wrong `[O]`
> claim is the most expensive kind this project produces and deleting it would
> hide how it was made.

§7.15 barred writing any MEDIA value but `0xe9`, because BROWSER and EXPLORER have
HID Consumer usages (`0x0196`, `0x0194`) too wide for the single byte `+1` that
VOLUME UP occupies -- so either they spilled into `+2`-`+3`, or `+1` was never a
raw usage and VOLUME UP matching one was a coincidence.

`15-media.pcapng` settles it. Seven writes, each moving button 2's `+0`, `+1` and
`+2`; `+3`-`+5` stay zero throughout, and `+6` stays `08`:

| action | predicted (§7.17) | observed `+0 +1 +2` | seq |
| --- | --- | --- | --- |
| BROWSER | `18 96` | **`18 96 01`** | 4 |
| EXPLORER | `18 94` | **`18 94 01`** | 6 |
| PLAY/PAUSE | `20 cd` | `20 cd 00` | 8 |
| NEXT | `20 b5` | `20 b5 00` | 10 |
| PREVIOUS | `20 b6` | `20 b6 00` | 12 |
| MUTE | `20 e2` | `20 e2 00` | 14 |
| VOLUME DOWN | `20 ea` | `20 ea 00` | 16 |

Reproduce with:

    python3 - <<'PY'
    import sys; sys.path.insert(0, "Tools/capture")
    import records
    for f in records.frames("windows-capture/15-media.pcapng"):
        if f.payload and len(f.payload) >= 0x60:
            e = f.payload[0x37 + 14 : 0x37 + 21]
            print(f.seq, f.cmd, " ".join("%02x" % b for b in e))
    PY

Eight of eight are now `[O]` (VOLUME UP `20 e9` from `05-buttonmapping.pcapng`
seq 12 on 1.07). **§7.15's FIRST hypothesis was right: the wide usages DO spill
into `+2`.** The usage is a u16 little-endian field spanning `+1`-`+2` --
`0x0196` and `0x0194` -- and `+0` changes as well, `18` for those two against
`20` for the other six. So `+0` is not a constant "media" tag; it varies within
the group, and `+1` is the usage low byte throughout rather than a vendor index.

The wire and the binary agree byte for byte. cfg107 reaches the `+2`-`+3` word
store through `xorl %edx,%edx` for the six `0x20` actions and through
`movl $0x1,%edx` for the two `0x18` ones -- `0x40817c` and `0x408226`, stored at
`0x408181` and `0x40822b`. All eighteen button handlers were checked for this,
not just the two: `Tools/ghidra-export/dis.sh cfg107 <addr> <addr+0x50>`.

The *meaning* of `18` versus `20` stays `[G]` (most likely a page or an
"application launch" flag, but nothing says so), and §1.3 keeps us from inventing
values that are not in §7.17's table.

<details><summary>The original 8.2, as written on 2026-09-06 and wrong</summary>

> `15-media.pcapng` settles it. Seven writes, each moving only button 2's
> `+0`/`+1`, `+2`-`+5` staying zero. [...] **Neither hypothesis in §7.15 was
> right in the form it was posed:** the wide usages do not spill into
> `+2`-`+3`, which stay zero. `+0` changes instead, `18` for those two against
> `20` for the other six. [...] `+1` is a byte-sized selector whose relationship
> to HID usage is not established by this capture. Our table is correct either
> way, being `[D]` from the eight `movb` immediates [...]

The last sentence is the tell. "Our table is correct either way" was true of the
two columns the table had and said nothing about the column it did not have. A
claim that survives both branches of a question is usually answering a different
question.

</details>

### 8.3 FIXED CPI: X and Y are independent, and the order is ours  [O]

`14-fixed-cpi.pcapng`. The first `[O]` FIXED CPI payload where X != Y:

    seq 4   0c 00 c4 09 c4 09    X = 2500, Y = 2500
    seq 6   0c 00 ce 09 90 01    X = 2510, Y = 400

`+2`/`+3` carries **X** and `+4`/`+5` carries **Y**, confirming §7.31's read of
`0x00407c7e` (member `0x300` -> entry `+2`) and `0x00407ca0` (member `0x304` ->
entry `+4`). `encodeButtonEntry`'s separate stores are right, and the `1600x800`
argument syntax stands.

**The normaliser is confirmed too.** the owner typed `2505`; the box showed `2510` and
the wire carries `ce 09` = 2510, never `c9 09` = 2505. That is `0x00401e70`
clamping to `[10, 30000]` then rounding to 10 with ties up, and it is the first
`[O]` on the domain whose uncited `26000` bound was the shipping bug found on
2026-09-06.

The owner performed two applies rather than the three the steps asked for. It does not
matter: one apply with X != Y and two distinct values determines the order by
itself.

### 8.4 The KEYBOARD KEY dialog is VK-driven, and it sets modifiers itself  [O]

`16-keys.pcapng`, all on button 2:

| seq | key pressed | observed | predicted | |
| --- | --- | --- | --- | --- |
| 4 | Left Shift | `02 **02** e1` | `02 **00** e1` | usage HIT, **modifier MISS** |
| 6 | Keypad `+` | `02 02 **57**` | `02 02 57` or `02 02 2e` | **HIT, and the good branch** |
| 8 | `Fn`+F5, WIN ticked | `02 08 3e` | `02 08 3e` | HIT |
| 10 | Print Screen | `02 00 46` | `02 00 46` | HIT |

**Keypad `+` reached the record as HID usage `0x57`, keypad-plus.** Not `0x2e`
(`=`). So the dialog resolves the VIRTUAL KEY through §7.32's jump table and does
**not** re-express the keypress as a character. The five keypad names §7.32 added
(`kp-slash`, `kp-star`, `kp-minus`, `kp-plus`, `kp-dot`) are reachable through the
vendor UI, and the worry that they might not be is closed.

**The modifier prediction was wrong, and the two misses share one cause.** I
predicted Left Shift would give `+1 = 00`, on the reasoning that a modifier
pressed AS a key is distinct from one ticked as a checkbox. It gave `+1 = 02`.
The owner had already reported the same thing on the keypad: *"it automatically ticked
SHIFT alongside it"*. So **the dialog sets the modifier bits from the keyboard
state at the moment of the press**, independently of which key it records. Left
Shift ticks SHIFT because shift is down; the keypad `+` ticked SHIFT on his laptop
for the same reason.

Both bytes are still fully determined and our encoder is unaffected -- it takes an
explicit modifier argument rather than inferring one. What is now `[O]` is that
**the vendor tool cannot record Left Shift with no modifier**, so a record we
produce with `02 00 e1` is one the vendor UI cannot make. That is allowed (§7.17
gives `+1` as a plain HID modifier byte) but worth knowing before comparing our
output to a vendor capture.

The confounder raised before the run -- that an `Fn`-overlay numpad might have
sent Shift+`=` in hardware -- is closed by the usage byte itself: hardware sending
`=` could not produce `0x57`.

### 8.5 1.10's factory record differs from 1.07 in exactly one byte  [O]

`13-cpi-stage34.pcapng` opens immediately after a factory reset, so its first
`A1 12` response is firmware 1.10's factory-default record. Against
`01-baseline.pcapng` (1.07), over all 115 bytes:

    0x71   1.07 = 01   1.10 = 00        (the ONLY difference)

This is an independent confirmation of `CLAUDE.md` §5, which recorded the same
single byte from the mid-run flash on 2026-09-05. The record LAYOUT is stable
across 1.07 -> 1.10 by measurement, twice, and `0x71` (Force max Sensor fps, §7.8)
is the one default that moved. **`CLAUDE.md` §5's "never hardcode a default
vector" still stands** -- two versions differing by one byte is exactly the shape
that tempts hardcoding.

### 8.6 Prediction 5c answered as a side effect: the mirror is edit-time  [O]

Written off before the run as untestable, on the grounds that the owner factory-resets
between captures so every section would open on X == Y. He reset between most, but
**not between `13-cpi-stage34` and `14-fixed-cpi`** -- the latter opens on
CPI3 = 1200/2400 and CPI4 = 3000/3000, carried straight over.

That is exactly the input the question needed. `14-fixed-cpi`'s two `A0 11` writes
move only bytes `0x45`-`0x4a`, button 2's own entry. **The CPI block is untouched,
and CPI3 is still 1200/2400 after both writes.**

So the tool does **not** mirror Y onto X when it loads a record where they differ;
the mirroring is an edit-time behaviour of the control. `A0 11` writing all 115
bytes from the in-memory model is therefore safe for bytes the user did not touch,
which was the hazard. Predicted outcome, confirmed.

### 8.7 Two smaller observations

**Opening the tool writes nothing, on 1.10 as on 1.07.** `11-open-1.10.pcapng` is
four frames: `A1 02`, `A1 12`, and their responses. No `A0 11`. This is the first
1.10 capture of the opening exchange and of a vendor settings read.

**`12-reset-1.10.pcapng` shows no re-read after the reset**, unlike 1.07's
`07-factory-reset.pcapng` which is `A1 02`, `A1 12`, `A1 13`, `A1 02`, `A1 12`.
Here it stops at `A1 13`. Recorded as observed, **not** interpreted: the capture
may simply have been stopped before the tool settled, and nothing distinguishes
that from a version difference. Do not cite this as a 1.10 behaviour change.

`11-open` and `12-reset` both open on **800/1200/2000/3200**, which is the owner's own
CPI configuration rather than factory defaults -- the reset in `12-reset` is what
returns the mouse to 400/800/1600/3200 for everything after.
