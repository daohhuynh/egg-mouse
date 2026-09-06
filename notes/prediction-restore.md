# PRE-REGISTERED: `egg-config restore ~/.egg-mouse-known-good.bin --yes`

Committed BEFORE the command runs, 2026-09-05, same discipline as
`prediction-factory-reset.md`, which scored 21/21.

## Why this rung

**It is the first 1041-byte `SET_REPORT` this machine has ever sent.** Every
write path in both tools uses that transport and none of it has touched
hardware. The 64-byte SET has now been exercised many times (`A1 12`, `A1 13`);
hidapi takes the same path for both and differs only in length, so this tests
length handling and nothing else about the convention.

It is also the safest possible first use of it: **every payload byte is a byte
this device itself produced**, read out of it this morning. §1.3 cannot be
violated by a record the hardware authored.

## State going in

The device is at factory defaults, byte-identical to `10-postflash-baseline`.
The target is the vault, the owner's settings as of 14:51 today. Under BOTH
`--unknown-bytes` policies the outgoing frame is identical to the vault
(`0 of 1024 differ`), because the vault already holds `00 00 00 00` at records
`0x01`-`0x04`. The policy is a no-op here and cannot be a confound.

## PREDICT -- stage A, the write

1. `A0 11` is acknowledged (`resp[1] == 0x01`) after the 320 ms wait.
2. The read-back differs from the pre-restore record in **exactly 21 bytes**,
   each taking the value below. This is the exact inverse of the 21 the
   factory reset produced, and it must be, because the two records are the
   same pair read in the other direction.
3. `egg-config` reports the write VERIFIED, not merely acknowledged.

| wire | payload | now (defaults) | expected after restore |
| --- | --- | --- | --- |
| `0x0011` | `+0x001` | `80` | `00` |
| `0x0015` | `+0x005` | `01` | `08` |
| `0x0019` | `+0x009` | `03` | `00` |
| `0x001b` | `+0x00b` | `00` | `0a` |
| `0x001d` | `+0x00d` | `01` | `03` |
| `0x0034` | `+0x024` | `90` | `20` |
| `0x0035` | `+0x025` | `01` | `03` |
| `0x0036` | `+0x026` | `90` | `20` |
| `0x0037` | `+0x027` | `01` | `03` |
| `0x0039` | `+0x029` | `20` | `b0` |
| `0x003a` | `+0x02a` | `03` | `04` |
| `0x003b` | `+0x02b` | `20` | `b0` |
| `0x003c` | `+0x02c` | `03` | `04` |
| `0x003e` | `+0x02e` | `40` | `d0` |
| `0x003f` | `+0x02f` | `06` | `07` |
| `0x0040` | `+0x030` | `40` | `d0` |
| `0x0041` | `+0x031` | `06` | `07` |
| `0x004d` | `+0x03d` | `08` | `f1` |
| `0x0054` | `+0x044` | `08` | `f1` |
| `0x0081` | `+0x071` | `00` | `01` |
| `0x0082` | `+0x072` | `00` | `01` |

## PREDICT -- record `0x01`, a sub-question worth its own line

Wire `0x11` (payload `+0x01`) is `0x80` now and `0x00` in the vault.
working-memory's standing reading is that `0x80` there means **"at defaults"**:
the device reports `0x80`, the vendor writes `0x00` in all 73 captured writes,
and it tracked whether a capture section opened at defaults. That reading is
`[G]` and this run tests it two ways at once:

- It is `0x80` now, immediately after a reset. **Consistent.**
- It was `0x00` before the reset, when the owner had changed settings. **Consistent.**
- After this write it should be `0x00` -- either because we wrote `0x00`, or
  because the device clears the flag on any host write. Both give `0x00`, so
  this run cannot separate them; it can only refute the reading if `0x80`
  SURVIVES a write that explicitly set it to `0x00`.

## PREDICT -- stage B, persistence. THE OPEN QUESTION.

Then unplug, wait, replug, and read again.

`config-wire-observed.md` §5: the vendor's own writes had reverted by the next
session in **five gaps out of six**. Nobody knew whether that meant `A0 11`
does not persist, or that nothing survives re-enumeration. §2.4 settled the
second half today -- with no write, the record is stable across a replug -- so
**this test is now interpretable and was not before.**

No prediction is offered, deliberately. Both outcomes are informative and
guessing would only tempt a post-hoc rationalisation:

- **Settings survive** -> `A0 11` persists; the vendor's reverts had another
  cause, and the capture's five-of-six needs re-explaining.
- **Settings revert to defaults** -> `A0 11` writes RAM, and a config tool that
  cannot make a change stick is a finding that belongs in the README before
  anyone else runs it. There may be a separate commit/save command we have
  not found (§1.2a: absence of a name is not absence of a capability).

## REFUTED IF

- The write is acknowledged but the read-back does not verify. `egg-config`
  exits non-zero and says so; that is a real failure, not a warning.
- A byte outside the 21 moves. Would mean the device edits the record on
  write, which the tool reports explicitly ("N bytes changed on the device; we
  sent M").
- `hid_send_feature_report` fails outright. Clean abort, nothing written.
  This is the failure mode the 1041-byte length would most plausibly cause,
  and it is the harmless one.

## Undo

There is nothing to undo *to* that is better than where we are: the device is
at vendor defaults, and `egg-config factory-reset --yes` is now a CONFIRMED
way back to exactly this state. That is the whole reason §4.1 puts factory
reset first, and it is why this rung is being run second rather than first.
