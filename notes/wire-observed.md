# Wire observations — what the captures actually show

Everything here is **[O]**, read off a USB capture of the vendor software talking
to the one physical mouse. This file holds only what the bytes show. Predictions
live in `wire-predictions.md`; they were written and committed *before* any of
this data existed (commit `7ef14f8`), so scoring them is honest.

Tag discipline still applies inside this file. A byte pattern that *looks* like a
structure is not an observation of that structure — it is an observation of the
bytes, plus a **[G]** about what they mean. Those are marked. **No [G] in here is
a basis for a write (§1.3).**

---

## 1. `01-baseline.pcapng` — provenance

| | |
| --- | --- |
| captured by | the owner, 2026-09-05, borrowed Windows 11 25H2 laptop |
| tool | Wireshark + USBPcap, interface `USBPcap1`, link type 249 |
| size | 6,976 bytes |
| contents | vendor config tool 1.07 launched, settings page reached, nothing changed |
| device address | 4 (device descriptor confirms `idVendor` 0x3367, `idProduct` 0x1978) |
| reader | `Tools/capture/usbpcap.py` |

The capture is the **baseline**: the config tool's own start-up conversation with
the mouse, with no setting touched. Everything below is from that single file.

## 2. The whole conversation is four transfers

```
SET_REPORT  wValue=0x03a1  wLength=64    payload a1 02 00 00 ...
GET_REPORT  wValue=0x03a1  wLength=64    payload a1 01 00 00 ...
SET_REPORT  wValue=0x03a1  wLength=64    payload a1 12 00 00 ...
GET_REPORT  wValue=0x03a0  wLength=1041  payload a1 01 00 00 ... (settings)
```

`0x03a1` = report type 3 (Feature) | report id `0xA1`. `0x03a0` likewise for `0xA0`.

**Confirmed [O], previously [D]:**

1. **`wLength` is 1041 (`0x411`) for report `0xA0` and 64 (`0x40`) for `0xA1`.**
   These are the two lengths hardcoded in the vendor binaries and the two
   feature-report sizes the bootloader's own report descriptor declares
   (`bootloader-observed.md` §3). Three independent sources now agree.
2. **The command byte is payload byte 1, not byte 0.** Byte 0 is the report id
   echoed into the buffer. `a1 02` and `a1 12` are `<report id> <command>`.
3. **`0x02` is a small query and `0x12` requests the settings record** — the two
   opcodes `Protocol.h` names `cfg::kSmallQuery` and `cfg::kReadRequest`.
4. **Response byte 1 is a status byte and `0x01` means ready.** Both responses
   begin `a1 01`. This is the `resp[1] == 0x01` check the vendor code performs.
5. **A settings read is one round trip, not a chunked sequence.** The whole
   record arrives in a single 1041-byte feature report.

**Note the asymmetry:** the request for the *large* record is sent on the *small*
report (`0xA1`, 64 bytes) and answered on the large one (`0xA0`, 1041). The
report id is not a channel; it is a size selector chosen per direction.

### 2.1 RESOLVED — the wire index IS the buffer index  [O]+[D], 2026-09-05

This was the top open gap: the device returned **1040** bytes against
`wLength=1041` and **63** against 64, N−1 both times, and whether the recorded
bytes were the whole application buffer or `buffer[1:]` shifted **every field
offset in this file by one**. It is settled, and it is (a): *the bytes USBPcap
recorded are the application buffer, index for index.*

**The argument, and it is a refutation rather than a preference.** cfg107's
small-report exchange is at `0x004047cc`, in raw bytes:

```
4047cc  8d bd 70 ff ff ff        leal  -0x90(%ebp), %edi     ; edi = buf
4047d2  c6 85 70 ff ff ff a1     movb  $0xa1, -0x90(%ebp)    ; buf[0] = 0xA1
4047d9  e8 42 f1 ff ff           calll 0x403920              ; the exchange
4047de  85 c0 / 74 18            testl %eax,%eax ; je fail
4047e2  80 bd 71 ff ff ff 01     cmpb  $0x1, -0x8f(%ebp)     ; buf[1] == 1 ?
4047e9  75 0f                    jne   fail
```

The host writes the report id into `buf[0]` itself and then requires
**`buf[1] == 0x01`**. On the wire the reply is `a1 01 00 …`, so `wire[1] = 0x01`.

- Under (a), `buf[1] = wire[1] = 0x01`. The check passes.
- Under (b), `buf[1] = wire[0] = 0xA1`. The check fails **every time**, and the
  vendor tool could never read a setting. It demonstrably does.

So (b) is refuted by the vendor's own working code, not chosen against.

**Two independent corroborations**, neither used to reach the conclusion:

1. **Both replies put their content at `0x10`.** `config-protocol.md` §7.2a has
   the `A0 11` write frame placing its 1024 payload bytes at `buf+0x10`
   (`rep movsl` to `-0x45c(%ebp)`, base `-0x46c`). The 1040-byte reply's content
   begins at wire `0x10` and the 63-byte reply's at wire `0x10` as well. Read
   and write are mirrors about the same origin, which only holds under (a).
2. **The 1024-byte payload lands flush.** `0x10 + 1024 = 1040`, exactly the
   returned length. Under (b) it would end one byte short of the transfer.

**The N−1 is not a truncation, and the device has a quirk worth knowing.**
`buf[0]` is the report-id slot; the device never sends it, so a read of an
N-byte report returns N−1 bytes of content and the host's own `0xA1` stays in
`buf[0]`. The quirk: **frame 3 requested report `0xA0` and the reply's byte 0 is
still `0xA1`** — this device answers every feature read with `0xA1` there
regardless of which report was asked for. Harmless, and it is the single fact
that made the two readings look equally good for a day.

**Consequence.** Every `wire offset` in this file is now also the hidapi buffer
index, and the translation is no longer `[G]`. §4.4 stage 1 should still confirm
it on macOS — hidapi's own report-id convention is a *third* API and has not
been observed — but nothing here is waiting on it any more.

### 2.1a CONFIRMED ON macOS — hidapi is the third API and it agrees  [O], 2026-09-05

§2.1 left one thing open: "hidapi's own report-id convention is a *third* API and
has not been observed." It has now. `egg-config read` on the owner's device, macOS
15 / hidapi via Homebrew, first successful read from the physical mouse.

**The result is (a) again, and the test was not a judgement call.** The macOS
buffer was compared byte-for-byte against `01-baseline-003-in-01.bin` under all
three alignments:

| hypothesis | bytes matching |
| --- | --- |
| **aligned — `buf[i] == wire[i]`** | **1019 / 1039 (98.08%)** |
| macOS shifted −1 | 957 / 1039 (92.11%) |
| macOS shifted +1 | 960 / 1040 (92.31%) |

The two shifts are not merely worse, they are at the floor: the record is 87.5%
zeros, so ~92% is what *any* misalignment scores by matching zero against zero.
Only the aligned reading rises above the noise.

**The 20 differing bytes corroborate rather than complicate it.** They are
settings, and they are self-consistent: wire `0x34`/`0x36`, `0x39`/`0x3b`,
`0x3e`/`0x40` are the CPI stage X and Y pairs, and in each stage *both copies
moved together* — `90 01`→`20 03` (400→800), `20 03`→`b0 04` (800→1200),
`40 06`→`d0 07` (1600→2000). A shift or a misread does not produce paired,
round-numbered, independently-plausible CPI values in six places. §4's CPI table
is confirmed a second time, on a second device state, through a second API.

**The one macOS difference is byte 0, and it is the report-id slot.** Windows
shows `0xa1` there; macOS shows `0x00`. §2.1 explains why: the device never
sends that byte, so whatever the host left in the slot survives — Windows leaves
its own `0xA1`, and hidapi leaves a zero. **Byte 0 carries no device
information on either platform**, which is why `plausible()` no longer looks at
it. It used to require `0xA0`, a value neither platform produces.

**Outbound is unaffected.** hidapi passes a non-zero `buf[0]` as the reportID
argument *and* keeps it in the buffer, so `egg-config`'s 64-byte `a1 12` frame
reaches the wire byte-identical to `frames/01-baseline-002-out-12.bin`.
`Tests/test_golden_config_cmds.py` pins that, and the two other short frames,
against the vendor's own captures.

## 2.2 The info reply, decoded  [O]

The 63-byte reply to `A1 02` (frame 1 of `01-baseline.pcapng`):

```
000  a1 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
010  00 07 01 00 67 33 78 19 00 00 …
```

| offset | bytes | reading |
| --- | --- | --- |
| `0x00` | `a1` | report-id slot, always `0xA1` (above) |
| `0x01` | `01` | status, `0x01` = ready |
| `0x11`–`0x12` | `07 01` | **firmware 1.07** — the version the owner's device reports |
| `0x14`–`0x15` | `67 33` | **VID `0x3367`** little-endian |
| `0x16`–`0x17` | `78 19` | **PID `0x1978`**, the application-mode id |

VID and PID land on even offsets and match values already known independently —
`0x3367` from `pidwatch.py` `[O]`, and `0x1978` pushed as an immediate at
cfg107 `0x413edb` `[D]`. The firmware version matches
`notes/device-predictions.md`.

**RESOLVED 2026-09-05, and exactly as predicted, for free.** This paragraph used
to end: *"Whether `0x11` is the minor and `0x12` the major, or the pair is one
little-endian `0x0107`, is [G] — one device on 1.07 cannot separate those, and a
post-flash capture on 1.10 settles it for free."* `egg-config info` on the
device, now running 1.10, returns `00 10 01 00 67 33 78 19` at `0x10`:

| firmware | `0x11` `0x12` | little-endian pair, as BCD | as (minor, major) decimal |
| --- | --- | --- | --- |
| 1.07 (capture) | `07 01` | `0x0107` → **1.07** ✓ | **1.07** ✓ |
| 1.10 (device) | `10 01` | `0x0110` → **1.10** ✓ | 1.16 ✗ |

Both readings agree on 1.07, which is why one device could not separate them.
They disagree on 1.10 and only one survives: **`0x11`–`0x12` is a single
little-endian BCD word.** `[O]`. It independently matches `bcdDevice` `0x0110`
from the USB device descriptor, which is a second encoding of the same value
from a different part of the stack.

### 2.3 BYTE 0 IS NOT STABLE ON macOS, AND THE PAYLOAD IS  [O], 2026-09-05

Two consecutive `egg-config read` runs, same binary, same machine, minutes
apart, returned **different values in `buf[0]`** — `0xa1` on one and `0x00` on
the other — with **byte-identical payloads**. A third read this morning gave
`0x00`. Nothing in our code varies: `receive()` writes `kReportLarge` (`0xA0`)
into `buf[0]` before every call, and `0xA0` is not what came back either time.

**What this does NOT mean.** It is not a shift. The 1024 payload bytes printed
at `+0x10` were identical across both runs, and the two saved records are equal
across all 1041 bytes. Had the buffer sometimes included the report id and
sometimes not, the payload would have moved to `+0x11` in one of them.

**What it does mean.** `buf[0]` carries no information on macOS and cannot be
validated against anything — not `0xA0`, not `0xA1`, not `0x00`. Any check on it
would fail *intermittently*, which is worse than failing always: it would pass a
test suite, pass a manual trial, and refuse a user's record next Tuesday. Two
such checks existed in this repo on the morning of the same day (`plausible()`
and `loadRecord`), both demanding `0xA0`, and both were removed before this was
observed. The observation converts that fix from correct-by-derivation to
correct-by-measurement.

**Still open, and now diagnosable.** Whether the *returned length* also varies is
untested: the log printed `buf.size()`, which is the length we allocated, so it
could never have shown a difference. `Log::frame` now takes the transport's
actual `rc` and prints `got=N` whenever it differs from the buffer. The next
read that shows `id=0xa1` will say whether `rc` moved with it.

**First data from that instrument, same day.** Three exchanges during the
factory reset, all with `id=0x00`:

```
<-- A1 12 read settings   len=1041  got=1040   id=0x00 b1=0x01
<-- A1 13 factory reset   len=64    got=63     id=0x00 b1=0x01
<-- A1 12 read settings   len=1041  got=1040   id=0x00 b1=0x01
```

**N−1 is now `[O]` on macOS for BOTH report sizes**, which it had never been —
1040 against a 1041-byte report and 63 against 64, matching the two Windows
capture files exactly. §2.1 derived this from cfg107's own code; the device has
now said it directly, through a third API.

The `0xa1` case did not recur in this run, so the question of whether `rc` moves
with `buf[0]` is still open. It is no longer *unanswerable*, which was the
point.

### 2.4 THE RECORD SURVIVES A POWER CYCLE UNTOUCHED  [O], 2026-09-05

Read, unplug, wait, replug, read again: **all 1041 bytes identical**, vault
versus `after-replug.bin`. No write of any kind was involved.

This is the baseline the persistence question was missing. `config-wire-observed.md`
§5 records that the vendor's own writes had reverted by the next session in five
gaps out of six, and nothing distinguished "`a0 11` does not persist" from
"nothing persists across a replug". Now something does: **with no write, the
record is stable across re-enumeration.** So a value that disappears after one
of our writes is a fact about the write.

It is only a baseline. It says nothing yet about whether a *written* record
survives — that needs a write, and it is the reason to do this test before one
rather than after, since afterwards the question is contaminated.

## 3. The settings record is 115 bytes of content, not 1024

Of the 1040 recorded bytes, **58 are non-zero, and every one of them lies at or
below wire offset `0x82`.** Offsets `0x83`–`0x40f` — 909 bytes — are zero in
every record observed so far.

That is a large, cheap result. The field-mapping problem is ~115 bytes wide, not
1024, and a diff between two records will have nowhere to hide.

**CORRECTED 2026-09-05 — the original claim was an absence claim from one
capture, and the second observation broke it.** It read "every one of them lies
*below* `0x82`. Offsets `0x82`–`0x40f` are entirely zero." Wire `0x82` is `0x00`
in `01-baseline` and in `10-postflash-baseline`, and it is **`0x01`** in the
first record read off the device from macOS (`~/.egg-mouse-known-good.bin`,
stage 1). So `0x82` is a live byte that merely happened to be zero in both
Windows captures, and the record is 115 bytes (`0x10`–`0x82`), not 114.

This is §1.2a exactly: "the set is exactly N" asserts something about everywhere
you did not look, and one capture of one device in one state cannot support it.
`config-protocol.md` §7.3 had it right — it maps the record as `0x10`–`0x82` and
lists `0x72` (payload) as the last, unattributed byte. This file disagreed with
it for a day and this file was the one that was wrong.

Note also what the correction did *not* do: `0x83`–`0x40f` is still zero in all
three records, and that is still an absence claim from three observations. It is
`[G]` that it is unused, exactly as the paragraph below already says.

It is also a **[G]** that the tail is unused: zero in the default configuration is
not the same as never written. Some of it plausibly holds per-profile or macro
data that a default record leaves empty. §1.3 covers this correctly already —
read-modify-write preserves the tail whatever it is, so nothing depends on
resolving it.

## 4. The CPI table — [O], and it matches the derivation exactly

Wire `0x34`–`0x47`, four five-byte records:

| wire | bytes | X | Y | 5th byte |
| --- | --- | --- | --- | --- |
| `0x34` | `90 01 90 01 00` | 400 | 400 | `0x00` |
| `0x39` | `20 03 20 03 00` | 800 | 800 | `0x00` |
| `0x3e` | `40 06 40 06 00` | 1600 | 1600 | `0x00` |
| `0x43` | `80 0c 80 0c 00` | 3200 | 3200 | `0x00` |

Two 16-bit little-endian values per record, then one byte.

**This is the strongest confirmation in the capture.** `working-memory.md`
LIST 3 item 1 recorded, from static analysis, that `0x413db0` in the config tool
writes the immediates `0x190`, `0x320`, `0x640`, `0xc80`. Those are 400, 800,
1600, 3200 — the four values here, in order, at a single contiguous table. A [D]
lead from a decompiled function and an [O] byte pattern from the wire met without
either being fitted to the other.

- **X and Y are stored separately.** Independent axes are addressable even if the
  vendor GUI ties them together.
- The 5th byte is `0x00` in all four records. Its meaning is **[G]** — enable
  flag, stage colour index, and padding all fit. `06-led` and the CPI lines of
  `02-basic` will settle it by diff.
- Whether the record count is fixed at four is **[G]**. The GUI exposes four
  stages; that the wire format allows only four does not follow (§1.2a).

## 5. Structures visible but not yet resolved

Both of these are **[G]**. They are written down because they are what the
capture diffs should be aimed at first, not because they are established.

### 5.1 Wire `0x1d`–`0x30` — four records that read as RGB

```
0x1d  01 04  ff ff 00     (255,255,  0)  yellow
0x22  01 01  00 00 ff     (  0,  0,255)  blue
0x27  01 02  ff 00 00     (255,  0,  0)  red
0x2c  01 03  00 ff 00     (  0,255,  0)  green
```

Four primaries at a regular five-byte stride is not a coincidence, and the mouse
has a CPI-stage LED. The `01 0N` prefix cycles 4,1,2,3.

**What does not fit:** a fifth record at the same stride would start at `0x31`,
and `0x31`–`0x33` is `01 04 00` — three bytes, then the CPI table. So either the
stride is not five, the run starts somewhere other than `0x1d`, or `0x31`–`0x33`
is a different field that happens to begin `01 04`. Do not resolve this by
choosing the reading that looks tidiest. `06-led` decides it in one diff.

### 5.2 Wire `0x48`–`0x81` — eight seven-byte records

```
0x48  01 00 00 00 00 08 00
0x4f  02 00 00 00 00 08 00
0x56  04 00 00 00 00 08 00
0x5d  10 00 00 00 00 08 00
0x64  08 00 00 00 00 08 09
0x6b  f1 00 00 00 00 08 01
0x72  01 00 00 00 00 08 01
0x79  ff 00 00 00 00 08 00
0x80  00 01 ...            (tail; may not be a ninth record)
```

First bytes `01 02 04 10 08` are the HID mouse button usage bitmasks — left,
right, middle, **forward (0x10) before back (0x08)** — and the mouse has eight
addressable buttons. Byte 5 is `0x08` in every record.

**Note the order.** If these are button assignments, forward precedes back in the
record layout while the HID bitmask values run 0x08, 0x10. Getting that backwards
would swap two buttons silently, which is exactly the class of bug §4.3 says
compiles clean. `04-buttons` and `05-buttonmapping` were written to pin each
button individually; that is what settles it, not this table.

`f1` and `ff` in the last three records are not mouse-button masks, so at least
one field here encodes something other than a button bitmask, or the records are
not uniform.

## 6. What this capture does *not* contain

Stated explicitly because absence is a claim (§1.2a) and this one is easy to get
wrong by omission:

- **No write.** Nothing was changed, so no `A0 11` frame appears, and the header
  bytes `[2..15]` of a write frame remain unknown. LIST 4 group G flags this as
  blocking the write path. `02-basic` onward supply it.
- **No bootloader traffic, no erase, no flash.** `08-flash` supplies those.
- **No factory reset.** `07-factory-reset` supplies that.
- The tool was already running when capture started for some of the file, so this
  is not proof that four transfers are the *complete* start-up sequence — only
  that these four occurred. `00-enumerate` covers the plug-in path.
