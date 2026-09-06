# PRE-REGISTERED: the device state after Endgame's updater recovered it

Committed BEFORE the read-back, 2026-09-05. the owner ran updater 1.10 on a Windows
machine against a mouse presenting only PID `0x1977` (the latched bootloader).
He reports the update succeeded and that his settings are now at defaults.
This pre-registers the BYTE-EXACT form of that, which "looks default" is not.

## What is already `[O]`, before any read-back

- **The "Mouse firmware current version" message did NOT appear**, and the only
  message shown was the one alongside the progress bar. This is the signature of
  the **mode-2 path**: `FUN_004011f0`, the version read, has exactly one `E8`
  caller in the whole file — `0x4034ab`, on the mode-1 branch. A `0x1977` device
  is never asked its version and the string is never built.
  **Predicted in commit `201cd74` at 20:01:27; the mouse was unplugged to be
  taken to the Windows machine at 20:11:08.** Written down before the run.
- **Settings are at defaults**, so `A1 13` fired. Per `updater-protocol.md`
  §5.4a it fires only after all three gates: (a) device checksum == host
  checksum `0x403b95`, (b) `A1 09` returned `resp[1]==0x01` `0x403c30`, and
  (c) the device re-enumerated as PID `0x1978` `0x403cc3`. **This excludes the
  corner case at `0x403e6f`, which reports success WITHOUT sending `A1 13`.**
  So all three gates passed, and gate (a) is the one that matters below.

## PREDICT

1. PID `0x1978`, bcdDevice `0x0110`, product
   `Endgame Gear OP1 8k v2 Gaming Mouse`, manufacturer `Endgame Gear`.
2. Seven interfaces on that PID, the same seven as before.
3. `egg-config read` returns 1040 of 1041 bytes — the N−1 rule, unchanged.
4. **The record differs from the vault (`~/.egg-mouse-known-good.bin`) in
   EXACTLY the 21 bytes tabulated in `prediction-factory-reset.md`, each taking
   that table's right-hand ("expected after reset") value.**
5. **No byte outside that set differs.** This is the sharp one: it says the
   flash disturbed nothing in the settings region beyond what `A1 13` resets.
6. The record is byte-identical to `windows-run/10-postflash-baseline` across
   all 1039 comparable bytes — the state Endgame's own tool produced on this
   device after its own flash, and the state our own `factory-reset` produced.
7. Firmware still reads 1.10.

## REFUTED IF

- Any byte outside the 21 differs from the vault. That would mean the flash
  touched the settings region in a way `A1 13` alone does not explain, and it
  is the single most informative thing that could come back.
- Fewer than 21 differ, or one takes a value other than the tabulated one.
- The identity fields differ from item 1.
- The read is not structurally plausible.

## What this CANNOT establish, stated before anyone over-reads a clean result

- **That the application firmware is byte-correct.** That is established by a
  different fact — gate (a) above, the device's own whole-image checksum over
  blocks `0x34`–`0x74` compared against the host's checksum of `FWFILE` 140,
  plus the per-block read-back-and-repair in `FUN_00401c90` (§5.5). Endgame's
  tool would have shown `get all check sum error` and aborted instead of
  succeeding. A clean settings diff says nothing about the APROM either way.
- **That nothing outside the settings region and the APROM changed.** Nothing
  addressed such a region, but "nothing addressed it" is a claim about the
  commands sent, not about the device.
