# PRE-REGISTERED: what `egg-config factory-reset --yes` should do

Written and committed BEFORE the command was run, 2026-09-05, so that
scoring it afterwards is honest. CLAUDE.md §4.1 gates every config write on
factory reset being *confirmed*, and working-memory records why section 07 of
the capture could not confirm it: the record was already at defaults, so
before == after proved nothing.

## The reference

`windows-run/10-postflash-baseline.pcapng` is the settings record read
immediately after a flash whose final command was `a1 13`. It is therefore
the 1.10 post-reset state, produced by Endgame's own tool on this device.

The owner's device today differs from it in **21 bytes** (wire offsets, byte 0
excluded because §2.3 shows it is not stable on macOS):

| wire | payload | now | expected after reset |
| --- | --- | --- | --- |
| `0x0011` | `+0x001` | `00` | `80` |
| `0x0015` | `+0x005` | `08` | `01` |
| `0x0019` | `+0x009` | `00` | `03` |
| `0x001b` | `+0x00b` | `0a` | `00` |
| `0x001d` | `+0x00d` | `03` | `01` |
| `0x0034` | `+0x024` | `20` | `90` |
| `0x0035` | `+0x025` | `03` | `01` |
| `0x0036` | `+0x026` | `20` | `90` |
| `0x0037` | `+0x027` | `03` | `01` |
| `0x0039` | `+0x029` | `b0` | `20` |
| `0x003a` | `+0x02a` | `04` | `03` |
| `0x003b` | `+0x02b` | `b0` | `20` |
| `0x003c` | `+0x02c` | `04` | `03` |
| `0x003e` | `+0x02e` | `d0` | `40` |
| `0x003f` | `+0x02f` | `07` | `06` |
| `0x0040` | `+0x030` | `d0` | `40` |
| `0x0041` | `+0x031` | `07` | `06` |
| `0x004d` | `+0x03d` | `f1` | `08` |
| `0x0054` | `+0x044` | `f1` | `08` |
| `0x0081` | `+0x071` | `01` | `00` |
| `0x0082` | `+0x072` | `01` | `00` |

## PREDICT

1. The command is acknowledged (`resp[1] == 0x01`) after the 1100 ms wait.
2. The read-back differs from the pre-reset record in **exactly these 21
   bytes**, and each takes the value in the last column.
3. No byte outside this set moves.

## REFUTED IF

- Nothing changes. `egg-config` already says so loudly and refuses to call it
  confirmed; that would mean `0x13` is not what we think, not that the device
  was already at defaults, because we have measured that it is not.
- A byte outside the set above changes. The set is derived from ONE vendor
  capture on ONE device, so this is the likeliest honest failure: §1.2a, the
  set may be incomplete rather than wrong.
- `0x71` in particular. working-memory already flags it as the single byte
  separating `01-baseline` (1.07) from `10-postflash` (1.10), so a firmware-
  dependent default there would NOT refute the reset.

## Undo, if it goes wrong

`egg-config restore ~/.egg-mouse-known-good.bin --yes`, or the
`egg-before-reset.bin` the command writes first and now never overwrites.
Both are confirmed loadable offline (`Tests/test_undo_loadable.py`), and
neither has been exercised against the device -- restore is the first
1041-byte SET_REPORT this machine will ever send. The worst realistic case
is settings lost and re-entered by hand or from Windows.
