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

## 1. THE NECESSITY GATE (CLAUDE.md §4.2a), answered before proposing the send

**1. What question does this answer?**
Two, and neither is answerable any other way:
- **Does the bootloader serve `A0 07` outside a flash session it started with
  `A0 03`?** Currently `[G]`. It is the last open protocol question in the
  flash path, and §4.2 forbids erasing without a backup that this command is
  the only way to produce.
- **Is our block arithmetic right?** 65 blocks at indices `0x34..0x74`
  is `[D]`, never confirmed against the device. A read-back is the only way to
  test it that writes nothing.

**2. Can it be answered without touching the device?**
No. Static analysis gives the frame `[D]` and it is already built and tested
against the mock under *both* answers. Whether the device honours the command
outside a session is a property of firmware we do not have and cannot read.
The captures show `A0 07` only ever inside a session, which is exactly why the
question is open rather than settled.

**3. Is there a hardware-forced alternative?**
For the *entry*, yes, and it is used: **button entry**, `[O]` and exercised
four times. §4.2b assigns read-only work to the button because abort is free
there and a button-entered bootloader is one unplug from normal. For the *read
itself* there is no hardware alternative — no button dumps flash.

**4. If the effect is unpredictable, what is the recovery, and does the command
consume it?**
`A0 07` carries a block index and nothing else: no payload, no length, no
count. The frame cannot express a write. **That is a statement about the frame,
not a safety argument** — §4.2a bans treating those as the same thing. The
safety argument is the recovery: the mouse is in a **button-entered**
bootloader, which `[O]` exits on a power cycle, and no state this command could
set survives an unplug that the button entry does not already survive. The
recovery is a physical act the command cannot reach. It is not consumed.

**Verdict: send it.** Gate 3 forces button entry; gate 4 leaves the recovery
intact and off-wire.

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

9. **Settings are untouched.** No `A0 11` write, no `A1 13`. After the run,
   `egg-config read --save after-stage3.bin` then
   `egg-config diff ~/.egg-mouse-known-good.bin after-stage3.bin` should show
   **zero differing bytes**. This is the check that the command did what it
   said and nothing else.

---

## 5. IF PREDICTION 6 FAILS — what each shape of mismatch means

This is the interesting branch, so the readings are written down *now*, before
the data can bias them. Save the diff either way.

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
