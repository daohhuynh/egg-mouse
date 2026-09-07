# PRE-REGISTERED: `egg-flash read-firmware backup.bin` — §4.4 stage 3

Committed BEFORE the command runs, 2026-09-05. Same discipline as
`prediction-factory-reset.md` (21/21), `prediction-restore.md` (21/21),
`prediction-bootloader-entry.md` (9/11) and `prediction-postwindows.md` (7/7).

---

## 0. WHAT THIS IS, AND HOW IT DIFFERS FROM WHAT WE ALREADY RAN

The owner asked, reasonably: *"isnt stage 3 what we did earlier?"* No, and the naming
in this project is genuinely misleading. Three different commands this session
have all been described as "read and save":

| command | reads | via | bytes | run? |
| --- | --- | --- | --- | --- |
| `egg-config read --save f.bin` | **settings** | `A0 11` | 1041 | **yes, done** |
| `egg-flash enter/leave-bootloader` | nothing | `A1 3A` / `A1 09` | — | **yes, done** |
| `egg-flash read-firmware f.bin` | **firmware** | `A0 07` × 65 | 66560 | **NO. this file.** |

`egg-config read` reads the **settings record** — DPI, polling rate, LED,
debounce. One command, one 1024-byte payload, and it has been run against the
mouse repeatedly today. `read-firmware` reads the **application flash region**
— the program the mouse actually executes — block by block, 65 commands. They
share the word "read" and share nothing else: different report, different
command byte, different region, 65× the data.

`A0 07` has **never been sent to this device.** That is what makes this a stage.

---

## 1. THE NECESSITY GATE (engineering-rules.md §4.2a), answered before proposing the send

> **ENTRY CHANGED BEFORE THE RUN, 2026-09-05.** An earlier draft of this file
> pre-registered a BUTTON entry. the owner overruled it before anything was sent —
> see gate 3 below. Recorded as an amendment rather than a silent edit, because
> a pre-registration that can be quietly revised is not one.

**1. What question does this answer?**

**NOT "where does the backup come from".** An earlier draft of this file said
so, and it was wrong: `fw140.bin`, extracted from the `.exe`, is a valid backup
and passes the gate without the device being touched at all. The Windows
updater wrote that exact image today and verified it per block, so a copy of
what is on the mouse already exists on disk. Correcting this matters, because
it was the strongest-sounding justification for the command and it does not
hold.

What the command actually answers, and nothing else does:

- **Can we read a block back and understand the reply?** This is the real one.
  During the flash, every block is written and then read back and compared at
  `resp[16..1039]` against the source. If that offset, the index mapping, or
  the reply length is wrong, block `0x34` fails verification and
  `driveToVerifiedImage` — which by design never gives up — rewrites it
  forever, on a device whose application region has already been erased.
  **This command tests that half of the flash while the firmware is still
  intact and nothing is at stake.**
- **Does the bootloader serve `A0 07` outside a flash session started by
  `A0 03`?** `[G]`, and the last open protocol question in the flash path.
  Note what this is NOT: the flash only ever reads *inside* a session, which
  is `[O]` 65 times in each capture. So a refusal here does not predict a
  failed flash — it is inconclusive, and cheap.
- **Is our block arithmetic right?** 65 blocks at `0x34..0x74` is `[D]` and
  never confirmed against the device. A read-back is the only way to test it
  that writes nothing.

**2. Can it be answered without touching the device?**
No. Static analysis gives the frame `[D]` and it is already built and tested
against the mock under *both* answers. Whether the device honours the command
outside a session is a property of firmware we do not have and cannot read.
The captures show `A0 07` only ever inside a session, which is exactly why the
question is open rather than settled.

**3. Is there a hardware-forced alternative?**
For the *entry*, yes — the buttons — and **it is deliberately NOT used.**

This is a change made by the owner *before the run*, overruling a split I had written
into §4.2b on my own initiative. His argument: **a test whose result does not
transfer is not worth its risk.** The flash reads blocks back inside an
`A1 3A`-entered bootloader; a read-back performed in a button-entered one
proves nothing about the flash unless the two bootloaders are identical, and
that is `[G]`. The fields we can compare — PID `0x1977`, bcdDevice `0x0006`,
product `Bootloader` — are `[O]`-identical, but a flag set by `A1 3A` would
not appear in any of them.

I had already conceded the load-bearing half of this and missed what it meant:
I wrote that a button-mode refusal would be *"inconclusive about the flash"*.
That makes half the outcomes worthless, which is the argument against paying
anything at all for that entry mode.

So gate 3 is answered **no, and on purpose**: the hardware alternative exists,
reaches the same mode, and is rejected because what it would prove is not the
thing we need proved.

**4. If the effect is unpredictable, what is the recovery, and does the command
consume it?**
`A0 07` carries a block index and nothing else: no payload, no length, no
count. The frame cannot express a write. **That is a statement about the frame,
not a safety argument** — §4.2a bans treating those as the same thing.

The recovery, stated honestly and it is weaker than it was: the mouse will be
in an `A1 3A`-entered bootloader, which **latches**. A power cycle does not
undo it. The only thing that clears the latch is a completed flash. So if this
read is refused, the mouse stays a bootloader until either this tool's `flash`
or Endgame's Windows updater finishes one. Both are `[O]` to accept a `0x1977`
device as a starting state, and the Windows path was exercised today.

**Does the command consume its own recovery?** No, but only just. The recovery
is a completed flash, and a flash needs a backup — which is what this command
produces. If the read fails, the backup does not exist. The escape is that
`fw140.bin`, extracted from the `.exe`, is a valid backup and passes the gate
(verified: `flash --backup fw140.bin` is accepted). **So the recovery survives
a failure of this command only because a backup can be obtained without the
device at all.** That fact is now load-bearing and is recorded here as such.

**Verdict: send it.** Gate 4 holds, on the strength of `fw140.bin` being an
acceptable backup independent of the device.

---

## 2. WHAT WILL BE SENT — exact bytes, pinned before the run

65 frames, report `0xA0`, 1041 bytes each:

```
a0 07 34 00 00 00 00 00   + 1033 zero bytes     <- first
a0 07 35 00 00 00 00 00   + 1033 zero bytes
   ...
a0 07 74 00 00 00 00 00   + 1033 zero bytes     <- last
```

Only `[2]` varies. Generated, not typed: `readBlock(idx)` in
`FlashCommands.cpp`, whose bytes are already checked frame-for-frame against
Endgame's own capture by `Tests/test_golden_vendor.py`.

**Nothing else goes out. Zero `A0 03`, zero `A0 06`, zero `A1` anything.**
Verified against the mock under both hypotheses: on the refusal path it emits
5 frames and stops, and `emitted any A0 03 or A0 06: no`.

---

## 3. PREDICT — the transport

1. **Each reply reports `got=1040`**, not 1041. The N−1 rule: `buf[0]` is the
   report-id slot the device never sends. `[O]` for `A0 11` on this mouse; this
   predicts it holds for `A0 07`, same report id, same length. **A value of
   1041 would be new information about the transport** and would mean the
   config path and the flash path disagree about the wire.
2. **`resp[1] == 0x01`** on success. `[D]` §5.5 step 4.
3. **`resp[6..7]` is the little-endian 16-bit sum of the 1024 bytes returned**,
   i.e. `blockChecksum(resp+16, 1024)`. `[D]` `0x401d90`–`0x401dab`. This is
   checkable per block from the saved file and is a real prediction: if the
   device puts its checksum somewhere else, or big-endian, this is where it
   shows.
4. **The payload is at `resp[16..1039]`**, flush to the end of the 1040 bytes.
   `[D]` §5.5 step 2 (`-0x418 + 0x10 == -0x408`).

## 4. PREDICT — the outcome, in order of how much it would teach us

5. **The read succeeds for all 65 blocks.** This is the coin-flip and I am
   calling it: **the bootloader will serve `A0 07` outside a session.** The
   reasoning, stated so it can be wrong: the vendor's read-back at `0x401ad0`
   takes a block index and nothing else — no session handle, no sequence number
   — and `A0 03` is documented as erase-and-declare, not open-a-session. A
   device that tracked sessions would need somewhere to reject from. `[G]`,
   and the honest confidence is about 70%.

6. **THE LOAD-BEARING PREDICTION. The 66560 bytes read back will be
   byte-identical to `FWFILE/140` of updater 1.10:**

   ```
   sha256  8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0
   ```

   The Windows updater wrote exactly that image to this mouse today and
   reported success, and per-block verification means the device acknowledged
   holding it. If reading it back gives the same bytes, then in one command
   **the block arithmetic, the payload offset, the index mapping and the N−1
   rule are all confirmed against hardware at once**, with nothing written.

   Confirm with:
   ```
   python3 Tools/pe/fwfile.py --extract 140 \
       "Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe" /tmp/fw140.bin
   cmp backup.bin /tmp/fw140.bin
   ```

7. **Timing: 5–15 s for all 65 blocks.** 50 ms sleep per block plus the reads.
   Materially longer means the device is going busy on `A0 07`, which the
   retry path handles but which would be worth recording.

8. **The saved file is 66560 bytes and re-reads byte-identical.** `saveAndVerify`
   writes it, reopens it, and compares. A failure here is a disk problem, not a
   device one, and it must abort rather than report a backup it does not have.

9. **Settings are untouched.** No `A0 11` write, no `A1 13`.
   **CORRECTED with the entry change:** this can no longer be checked straight
   after the run. `A1 3A` latches, so the mouse is a bootloader and
   `egg-config` — which talks to the application device — has nothing to open.
   The check moves to *after the flash completes*, at which point `A1 13` will
   have reset settings anyway, so what gets verified is the restore, not this
   command. **P9 is therefore UNTESTABLE in this run.** Recorded rather than
   quietly dropped.

10. **The flash that follows will emit 134 frames, not 135**, because it finds
   the device already in the bootloader and skips its own `A1 3A`. Falsifiable:
   count them. 135 would mean the tool sent `A1 3A` to a device already in the
   bootloader, which nothing has ever done.

---

## 5. IF PREDICTION 6 FAILS — what each shape of mismatch means

This is the interesting branch, so the readings are written down *now*, before
the data can bias them. Save the diff either way.

> **⚠ 2026-09-06. THE SECOND ROW OF THIS TABLE FIRED, AND HALF OF ITS READING
> WAS WRONG. The table below is left exactly as it was pre-registered — see the
> correction under it, which is where the right reading lives.** A
> pre-registration that gets edited after the data arrives is worth nothing, so
> nothing here is rewritten; the point of writing a reading down in advance is
> to find out that it was wrong.

| what comes back | reading |
| --- | --- |
| all 65 blocks match | prediction 6 holds. Block arithmetic confirmed `[O]`. |
| **exactly one or two blocks differ, same blocks each run** | the firmware writes to its own flash at runtime and those blocks hold it. Interesting and benign — it localises where settings or calibration live. **Re-read to confirm it is stable; a moving difference is a different finding.** |
| **every block differs, but the length is right** | the payload offset or the index mapping is wrong. Do NOT flash. Compare `backup.bin` against `fw140.bin` shifted by 16 bytes and by one block to find the shift. |
| **the data looks like the file shifted by 16 bytes** | we are reading from the frame start rather than `+0x10`. That mutant exists and is killed in the suite, so it would mean the device's response layout differs from `[D]` §5.5. |
| **blocks come back all `0x00` or all `0xFF`** | the bootloader is serving an erased region, not the application. That would mean `A0 07` reads something other than what `A0 06` writes, which changes the meaning of every per-block verify in the flash. **Stop and rethink; do not flash.** |
| **`resp[1] == 0x02` on block 0x34** | hypothesis B: reads only work inside a session. Prediction 5 refuted. **This does NOT license sending `A0 03` to open one** — that erases, and §4.2 forbids erasing without the backup this command exists to make. It is a finding that changes the plan, recorded as such, and the owner decides. |

**A refusal is not a failure of the tool.** The command is built to abort on the
first block that will not read, after 5 attempts, having written nothing.

### 5a. WHAT ACTUALLY HAPPENED, and the correction to row 2 (2026-09-06)

Row 2 fired: **exactly one block differed, device index `0x74`, the last one,
1022 of its 1024 bytes**, identically across two full read-backs
(`backup.bin` and `backup2.bin` are byte-identical, sha256
`46e4dd15…ab9cac89`). Scored in full in `prediction-scores.md`.

Row 2's reading has two halves and they scored differently.

| half of the pre-registered reading | verdict |
| --- | --- |
| "the firmware writes to its own flash at runtime and those blocks hold it" | **survives** |
| "it localises where settings or calibration live" | **REFUTED** |

**Why the second half is wrong, and it is not a small thing** — it would have
sent a later session looking for the settings record inside a firmware image.

- The settings record is **not in block `0x74`**. No 16-byte run of
  `~/.egg-mouse-known-good.bin` occurs anywhere in it.
- **Nothing in the image is plaintext anything.** Block `0x74`'s entropy is
  7.80 bits/byte, and so is every other block's (mean 7.80, range 7.76–7.84).
  The whole FWFILE is encrypted or compressed. So "which block holds the
  settings" is not a question this image can answer, for `0x74` or for any
  other block, and the row assumed it could.

**Where the mistaken reading came from, since that is the reusable part.** The
row was written by reasoning about what a *typical* mouse firmware layout does
with its last flash page, and typical-mouse-firmware reasoning is exactly what
engineering-rules.md §1.2 names as the primary contamination risk. It carried no tag. Had
it been tagged it would have been `[G]`, and a `[G]` in a pre-registration is
fine — it is a hypothesis — but it must be *labelled*, because an untagged
plausible sentence is indistinguishable from a derived one three files later.
**Tag the readings in a pre-registration, not just the findings.**

What survives, with a dated chain rather than a guess: the vendor's updater
wrote all 65 blocks including `0x74` earlier the same day, the mouse was then
used normally, and `0x74` now stably differs. So the firmware writes it at
runtime. What it writes there is unknown and stays unknown.

**Consequence for the flash: none.** Our flash writes FWFILE 140's `0x74`,
byte-identical to what Endgame's own updater wrote to this same mouse hours
earlier in a run that completed and left it working.

**Consequence for the backup, so it is not a surprise later:** `backup.bin` is
NOT a pristine factory image. 64 of 65 blocks are exact; `0x74` is this device's
runtime state as of 23:03 on 2026-09-05. That is the right thing to be able to
put back. Do not describe it as "the same as the `.exe`".

---

## 6. WHAT WOULD MAKE ME STOP MID-RUN

- Any `A0 03` or `A0 06` appearing in the log. That is a defect, not a device
  behaviour, and it means the audit missed something.
- The device disappearing from the bus. Unplug is the recovery; nothing has
  been written.
- More than a handful of retries. The retry path exists for a flaky link, and a
  device that needs it on a *read* is telling us something before the flash.

## 7. HOW THIS GETS SCORED

Each numbered prediction resolves HIT, MISS, or UNTESTED, appended to
`notes/prediction-scores.md` with the raw output, **before** any conclusion is
drawn from the run. The tiebreaker is the device (§7).
