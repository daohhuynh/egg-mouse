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
`notes/device-predictions.md`. Whether `0x11` is the minor and `0x12` the major,
or the pair is one little-endian `0x0107`, is **[G]** — one device on 1.07
cannot separate those, and a post-flash capture on 1.10 settles it for free.

## 3. The settings record is 114 bytes of content, not 1024

Of the 1040 recorded bytes, **58 are non-zero, and every one of them lies below
wire offset `0x82`.** Offsets `0x82`–`0x40f` — 910 bytes — are entirely zero.

That is a large, cheap result. The field-mapping problem is ~114 bytes wide, not
1024, and a diff between two records will have nowhere to hide.

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
