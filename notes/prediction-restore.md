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

---

# SCORED: stage A **21 / 21**, stage B **SETTINGS PERSIST** [O]

Scored 2026-09-05 ~18:25 local, against the commit that fixed this file
(`c433007`) before either command ran. Observer: the owner at the machine; the byte
comparison below is mine, mechanical, over his pasted output.

## Stage A — the first 1041-byte `SET_REPORT` this machine has ever sent

1. **Acknowledged.** `<-- A0 11 write settings len=64 got=63 id=0x00 b1=0x01`.
   `resp[1] == 0x01` after the 320 ms wait, exactly as `kDelayWriteRecord` was
   set from the capture. The length did not upset hidapi; the `REFUTED IF` that
   named `hid_send_feature_report` failing outright did not fire.
2. **Exactly 21 bytes, every value as predicted.** Scored by parsing the table
   in this file and the diff in his terminal and comparing them as maps, not by
   eye: 21 predicted rows, 21 observed rows, **0 mismatches, 0 extras**.
   No byte outside the 21 moved, so the device did not edit the record on write.
3. **The inverse check holds too.** The factory-reset table with its two value
   columns swapped is *equal as a map* to what restore observed — machine-checked,
   not asserted. The two runs are the same 21 bytes traversed in opposite
   directions, which is what it had to be if both readings were right.
4. `egg-config` printed **`verified: the device holds exactly what was sent.`**
   — verified, not merely acknowledged.

Corroborating detail nobody predicted and worth keeping: the pre-write read
reported *16 distinct byte values* across the payload and the read-back *17*.
The vault is the more varied record. Consistent, and it is an independent
one-number check that something actually changed.

## Stage B — persistence. **The record survived the power cycle, whole.**

**No prediction was offered here on purpose, so nothing is being scored as a
hit. This is an observation, and it is the one this rung existed to get.**

The post-replug payload was reconstructed from the 64 hexdump rows of his
`egg-config read` and compared against the vault byte for byte:

```
payload bytes differing (vault vs post-replug read): 0 of 1024
sha256 both                23141790a203044aea899771637b3fb29a70e3fcc66e4bc2b290c24c34ac84d9
```

**`A0 11` writes storage that outlives loss of bus power.** It is not RAM-only.
n=1, one device, ~8.6 s unpowered.

### The replug is evidenced, not assumed

The terminal output cannot show an unplug, and the entire claim rests on it, so
it was checked before scoring rather than after:

- **Kernel USB log.** `terminateDevice: destroying 0x3367/1978/0110 … hardware
  connection lost` at **18:24:06.878**, `enumerateDeviceComplete: enumerated
  0x3367/1978/0110 … at 480 Mbps` at **18:24:15.465**. 8.6 s with no power.
- **The predicate was validated before its silence or its speech was trusted**
  (§1.2a). Over the same window it also shows the four LMB+RMB bootloader cycles
  at 13:54–13:55 and the earlier application replug at 17:59:19 → 17:59:31, six
  seconds before `after-replug.bin` was written. It demonstrably sees this event
  class. An earlier query of mine returned empty only because it searched for
  `3367` where the log writes `0x3367`.
- **Ordering.** The `restore` window's zsh session history was flushed at
  18:23:55 with `egg-config restore … --yes` as its last line; a new window
  opened at 18:23:57; the unplug follows at 18:24:06; the read output was pasted
  at 18:24:49. The replug sits between the write and the read.
- **the owner, directly:** *"i actually did unplug and count to 5 and then replug
  before running the second command in a second terminal."*

### What this does NOT settle, and it is now a bigger question than before

`config-wire-observed.md` §5 ends: *"we have no evidence that `a0 11` persists
across a power cycle … our config tool must not claim otherwise until someone
writes a setting, replugs, and reads."* That has now been done and the answer is
that it persists. **The five-of-six reverts in the capture run therefore need a
different explanation, and losing the power-cycle explanation makes them worse,
not better** — the address analysis in that same section had already found no
re-enumeration across the boundaries where settings vanished, and now the one
mechanism everyone assumed was doing it demonstrably does not.

Open, and written down now rather than when it is resolved (§1.7):

- **What reverted the vendor's writes in those five gaps?** Candidates, all `[G]`:
  the vendor tool writing defaults at launch or exit; a device-side timeout; a
  distinct commit/discard command we have not found. The gaps that reverted were
  231 s–5501 s; the one that HELD was 124 s and this one held at ~9 s, so a
  duration effect is not excluded — but 9 s and 124 s are both far below 231 s
  and two points is not a curve.
- **Duration is untested above ~9 s.** Nothing here says the record survives
  overnight. Cheap to test and worth testing before the README makes any claim
  broader than what was observed.

## Record `0x01` — the sub-question, answered as far as this run can answer it

`0x80` before, `0x00` after the write, **and still `0x00` after the replug**.

The pre-registered limit stands exactly as written: this run cannot separate
"we wrote `0x00`" from "the device clears the flag on any host write", because
both predict `0x00`. It could only have *refuted* the reading by showing `0x80`
surviving, and it did not. So the `0x80` = "at firmware defaults" reading is
**still `[G]`, still unrefuted, still not a basis for a write** (§1.3).

The replug adds one thing the pre-registration did not anticipate: the flag did
not come back on power-up. Under the standing reading that is required — the
device is not at defaults — so it is consistent and not independent evidence.
