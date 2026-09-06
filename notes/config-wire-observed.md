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
