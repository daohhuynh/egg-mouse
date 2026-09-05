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

### 2.1 Open gap — buffer alignment across three APIs

The device returned **1040** bytes against `wLength=1041`, and **63** against 64:
N−1 in both cases. The returned bytes begin `a1 01`, and `0xA1` is the id of the
report *requested* in the first case but not the second (that one asked for
`0xA0`).

Two readings fit the capture equally well and they differ by one byte:

- **(a)** the wire carries the full buffer, the device puts `0xA1` at byte 0 in
  both cases, and USBPcap simply does not count the trailing byte; or
- **(b)** the wire carries buffer[1:], the host prepends the report id, and the
  device's own first byte is what we are calling byte 0.

Under (a) status is at buffer[1]; under (b) it is at buffer[2]. **Every field
offset in this file shifts by one if (b) is right.** The vendor code's `resp[1]`
check favours (a), which is why offsets below are quoted under (a) — but a
`.exe`-derived offset cannot settle what a USB stack does.

**This is decidable in one command against the device** and is exactly §4.4
stage 1's job: `egg-config read` on macOS, then compare the first bytes hidapi
hands back against the 1040 bytes here. Until then, `wire offset` in this file
means *offset within the bytes USBPcap recorded*, which is unambiguous, and the
translation to a hidapi buffer index is **[G]**. Nothing writes until it is [O].

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
