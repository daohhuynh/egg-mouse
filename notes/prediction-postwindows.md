# PRE-REGISTERED: the device state after Endgame's updater recovered it

Committed BEFORE the read-back, 2026-09-05. Updater 1.10 was run on a Windows
machine against a mouse presenting only PID `0x1977` (the latched bootloader).
The update succeeded and the settings came back at defaults.
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

---

# SCORED, 2026-09-05 — 7 of 7

Committed as `b746d0f` before the read-back was run.

| # | prediction | result |
|---|---|---|
| 1 | PID `0x1978`, bcd `0x0110`, product + manufacturer strings | **HIT**, all four fields |
| 2 | seven interfaces, the same seven | **HIT** |
| 3 | 1040 of 1041 returned (N−1) | **HIT** |
| 4 | exactly the 21 tabulated bytes differ from the vault | **HIT** |
| 5 | no byte outside that set moves | **HIT** |
| 6 | byte-identical to `10-postflash-baseline` over 1039 bytes | **HIT, by transitivity — not measured directly this time.** `prediction-factory-reset.md` established the device was byte-identical to that baseline after our own reset, and the vault differs from that state in exactly these 21 bytes, which is what was just re-measured. Recorded as inferred, not observed, so nobody later quotes it as a direct comparison. |
| 7 | firmware still 1.10 | **HIT** — `10 01` at record `+0x11`, bcdDevice `0x0110` |

Scored mechanically, not by eye: the 21 rows were re-parsed out of
`prediction-factory-reset.md` and set-compared against the actual byte deltas
between the vault and `after-windows-flash.bin`. Zero predicted-but-unmoved,
zero moved-but-unpredicted, zero wrong values.

## What this rung established beyond its own predictions

**The command census is complete and every command is an immediate  [D].**
Re-derived here rather than quoted (§6). `HidD_SetFeature`'s IAT slot is
dereferenced at exactly 2 sites, both inside the wrapper `0x4012a0`; an
`E8 rel32` sweep of all 1,154,048 bytes of `.text` gives that wrapper exactly
7 callers; each writes its command bytes as an immediate, never a computed
value:

| site | immediate | command |
| --- | --- | --- |
| `0x4018e9` | `movl $0x3a0` | `A0 03` erase / bootloader start |
| `0x4019d5` | `movw $0x6a0` | `A0 06` write block |
| `0x401b0b` | `movw $0x7a0` | `A0 07` read block |
| `0x401be5` | `movw $0x8a1` | `A1 08` whole-image checksum |
| `0x403793` | `movl $0x3aa1` | `A1 3A` enter bootloader |
| `0x403be4` | `movl $0x9a1` | `A1 09` complete |
| `0x403dcf` | `movl $0x13a1` | `A1 13` factory reset |

Method blind spots, stated per §1.2a: an `E8 rel32` sweep cannot see indirect
calls to the wrapper, nor `E9` tail-jumps into it. Within those limits the set
is exactly seven.

**Consequence, and it is the answer to a question asked directly:** the only
command that writes flash is `A0 06`, and it carries a block index. Nothing in
the vendor's tool writes a CONFIG bit, selects a boot source, or addresses
LDROM. The device's non-APROM regions are not reachable by this protocol, so
they cannot be quietly damaged by it — and their failure modes are boot-critical
rather than degrading, so a damaged one does not enumerate at all.

**The full reflash is the deepest reset that exists for this device.** Erase and
rewrite of all 65 APROM blocks with per-block read-back and a device-computed
whole-image checksum, then `A1 13`. There is no deeper vendor command to find;
the census above is why that is a claim and not a hope.
