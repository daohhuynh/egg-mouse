# PRE-REGISTERED: `egg-flash enter-bootloader --yes` — §4.4 stage 2

Committed BEFORE the command runs, 2026-09-05, same discipline as
`prediction-factory-reset.md` (21/21) and `prediction-restore.md` (21/21).

## What this rung is

**One 64-byte report to the application device, then nothing but watching.**
No erase, no write, and *nothing at all sent to the bootloader* — the code
constructs no `BootloaderLink`, so there is no object in the process capable of
writing to a bootloader even by accident.

Exit is by **power cycle**, not by software. `A1 09` is now known to be the
vendor's software exit (see below) and is deliberately **not** used here: the
vendor only ever sends it to a bootloader it has just finished flashing, so its
behaviour on an unflashed one is `[G]`, and §1.3 keeps `[G]` off the wire.

## What is new since the last rung, and how it was established

Derived today from raw disassembly of updater 1.10 and from both flash captures.
Everything below is `[D]` with an address or `[O]` with a capture reference.

- **The frame** `a1 3a 00 00 00 5a a5 32` + 56 zeros, length 0x40. `[D]` two
  `movl $imm32` stores at `0x403793` and `0x40379a` over a `memset(buf,0,0x40)`;
  `[O]` byte-identical on the wire in `08-flash.pcapng` frame 0 and
  `09-flash-again.pcapng` frame 0.
- **The vendor never inspects the reply's bytes.** `0x004037e1` tests only that
  a read happened. Contrast `A1 09` at `0x00403c30`, which tests `resp[1] == 1`.
  So *the re-enumeration is the evidence, not a status byte* — and our code
  follows that, with two tests pinning it so a later tidy-up cannot add a gate.
- **The device re-enumerates in ~450–500 ms**, not the ~2.6 s the notes used to
  imply. Measured from `GET_DESCRIPTOR` on the newly-addressed device: 489 ms
  in `08`, 448 ms in `09`. The 2.6 s is the vendor's two hardcoded `Sleep(1000)`.
- **The bootloader reached by `A1 3A` is the same one the button reaches.**
  Both present PID `0x1977`, bcdDevice `0x0006`, product `Bootloader`. `[O]`
  from the captures and `[O]` from this machine's own kernel log. **This is the
  load-bearing safety fact** — see *Risk*.

## PREDICT — stage A, the send

1. Exactly **64 bytes** go out: `a1 3a 00 00 00 5a a5 32` then 56 zeros.
   Already pinned by a test against the capture, so this is really a prediction
   that macOS transmits what we hand it.
2. The reply is read and reports **`got=63`** — the N−1 rule, 64 allocated,
   63 returned. A different number would be new information about the transport.
3. **`resp[1] == 0x01`.** Both captures show it. **Not gated on**; if it comes
   back something else and the device still re-enumerates, the run still
   succeeds and the surprise is recorded.
4. **Byte 0 of the reply will be `0x00` or `0xa1`, and will NOT be `0xa0`.**
   This is a real falsifiable claim: the same command returned `a1 01 …` in
   capture 08 and `00 01 …` in capture 09, on Windows, and macOS has produced
   both values for other commands. `0xa0` would refute the whole byte-0 story.

## PREDICT — stage B, the mode switch

5. The application device disappears and **PID `0x1977` appears within 10 s**.
   Point estimate **300–1500 ms** measured from the reply. The Windows numbers
   are 448 and 489 ms; macOS enumeration is a different stack, so the band is
   wider than the observation. Anything above 1500 ms is worth writing down even
   though it is not a refutation.
6. Identity, all three fields: PID `0x1977`, bcdDevice `0x0006`, product string
   exactly `Bootloader`. **The tool refuses anything else** rather than
   proceeding, per §4.2.
7. Manufacturer string prints as **`EGG`** — not `Endgame Gear`
   (`bootloader-observed.md` §2). Reported, **not** gated on, because it has
   been read on macOS but never on the wire and a mismatch would more likely be
   a platform artefact than a wrong device.
8. `egg-config devices` in another window shows the interface as `(BOOTLOADER)`.

## PREDICT — stage C, the exit and the damage check

Unplug, wait, replug. Then:

9. The mouse returns as PID `0x1978`, bcdDevice `0x0110`, product
   `Endgame Gear OP1 8k v2 Gaming Mouse`. This is `[O]` already for the *button*
   entry (`bootloader-observed.md` §5a — four hours in the bootloader, then one
   replug, first try); the prediction is that software entry behaves the same.
10. **`egg-config read` matches the vault: 0 of 1024 payload bytes differ.**
    This is the real damage check. Entering the bootloader must not disturb the
    settings record, and we now have a byte-exact reference to prove it against.
11. `egg-config info` still reports firmware **1.10** (`10 01` at `0x11`–`0x12`).

## REFUTED IF

- Any byte of the outgoing frame differs from the eight above plus zeros.
- No `0x1977` appears within 10 s **and** the mouse is still working normally
  afterwards → the command did not take; our transport or timing is wrong.
- `0x1977` appears with any of PID / bcdDevice / product different. The tool
  refuses and prints what it saw; that is a refutation of item 6, not a bug.
- Reply byte 0 comes back `0xa0`.
- After the replug the settings record differs from the vault in any byte.
- After the replug the version is not 1.10.
- **The mouse does not return to application mode on a power cycle.** This is
  the serious one; see below.

## Risk, stated plainly rather than reassuringly

**This is the first rung with a tail risk that is not merely inconvenient.**
The previous three could at worst lose settings, which `restore` undoes.

The risk is that `A1 3A` does something the button does not — for instance sets
a flag that stops the bootloader handing back to the application. Then the mouse
would sit in bootloader mode, and **we currently have no `flash` verb**, so we
could not write an image to get it out.

What bounds it:

- **The destination is identical on every field USB exposes.** PID, bcdDevice
  and product string all match the bootloader the button reaches, and that one
  is `[O]` to sit for four hours and then hand back to a working 1.07 on a
  single replug. Same identity is not proof of same code — a firmware chooses
  what it reports — so this stays the strongest available evidence rather than
  a guarantee.
- **The frame has nothing to erase with.** It carries no address, no length, no
  block count; just a fixed magic. Every command that touches flash (`a0 03`,
  `a0 06`) is a 1041-byte report and carries parameters.
- **The ack is 6–14 ms** in both captures. A flash erase is not that fast. Weak
  evidence — it could be asynchronous — but it points the same way.
- **ENDGAME'S OWN UPDATER RECOVERS A STUCK BOOTLOADER, ON A DEDICATED PATH.**
  Added after the owner asked whether a Windows laptop is a way out. It is.
  `FUN_00403600` returns a *mode code* — 1 for PID `0x1978`, **2 for PID
  `0x1977`** (`0x00403717`) — and actively alternates its search between the two
  PIDs ten times (`0x403680`). On mode 2 the caller starts a different thread,
  `0x402f10`, which is three instructions: `call 0x403960`, the flash function,
  **skipping `A1 3A` entirely**. A mouse presenting only `0x1977` is a supported
  starting state for their tool, not an error. `updater-protocol.md` §5.1a.
  It is `[D]` — §1.5 forbids running the binary — and it needs a Windows machine
  to hand, so between the failure and the fix the mouse is unusable. But it does
  not need our flasher finished, and it is a path Endgame wrote deliberately.
- **Finishing our own flasher is the second fallback.** The protocol is fully
  derived and the byte stream already diffs clean against Endgame's capture;
  what is missing is a device-backed `BootloaderLink` and the `flash` verb.

**Revised honest summary:** small probability, and the bad outcome is now "the
mouse is unusable until the owner reaches a Windows machine" rather than "until we
finish the flasher under pressure". That is a materially smaller tail, and the
question that produced it was the owner's, not mine.

## What this rung CANNOT establish

Stated in advance so nothing gets over-claimed afterwards:

- Whether `A1 09` works as a software exit. Not sent.
- Whether the bootloader accepts a write, a read-back, or any command at all.
  Nothing is sent to it. `a0 07` read-back is stage 3's job.
- Whether the bootloader can be reached when the application region is blank —
  the case that actually matters for recovery. Untestable without breaking it.
- Whether the nine-attempt retry loop works. We send once, deliberately: that
  path has never been exercised on the wire in either capture, and running
  untested retry code against the one mouse buys nothing.
