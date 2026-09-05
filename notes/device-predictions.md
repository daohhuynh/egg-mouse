# Predictions, written BEFORE the device was ever observed

**Committed before a single `[O]` was taken.** `CLAUDE.md` §7 makes the device
the tiebreaker; a tiebreaker is worthless if the prediction is written after the
answer. So this file is the register: every claim below is `[D]`, derived from
the vendor's `.exe` files with a citation, and each has a stated way to be
**wrong**. When the mouse is plugged in, the result goes in the right-hand
column and disagreements are treated as findings, not as noise to be explained
away.

Nothing here needs the device to be opened. It is all answerable from the HID
**report descriptor** and the USB device descriptor, which macOS already has
before any of our code runs.

## What the report descriptor can and cannot settle

This is the distinction that decides what is worth predicting.

**The descriptor is the envelope.** A USB HID device self-describes at
enumeration: which report IDs exist, whether each is Input, Output or Feature,
and the exact bit length of each. That is mandatory, static, and handed over
without anyone asking. It settles **shape**.

**The descriptor says nothing about meaning.** It will not say that byte 1 of
report `0xA0` is a command, that `0x13` is factory reset, that block indices
start at `0x34`, or what the checksum covers. **Those are payload semantics, and
a report descriptor never contains them** — a vendor-defined report is declared
as an opaque run of bytes with a vendor usage page and nothing more.

Payload semantics are exactly what a Windows capture rig would have given us:
run the vendor tool, tick every setting, diff the traffic. We cannot do that
(`CLAUDE.md` §2 — no Windows, no VM, no capture), which is *why* the semantics
were derived statically from the `.exe` instead. The descriptor does not replace
that work and cannot check it.

**What it does check is the one thing static analysis is weakest on.** Every
length in these notes is an immediate someone pushed in the vendor's code. The
vendor never validates report lengths (`updater-protocol.md` §6.3), so on
Windows a length that disagrees with the device would be silently absorbed. Here
it would not be. Lengths, report IDs, collection count and usage pairs are
precisely the claims the descriptor can falsify, so they are what is predicted.

## The register

| # | prediction `[D]` | derived from | falsified if | observed `[O]` |
|---|---|---|---|---|
| 1 | `VendorID == 0x3367` | `fw110 0x4010fa`, `cmpw $0x3367` against `HidD_GetAttributes`' `VendorID` | any other VID | *pending* |
| 2 | In normal operation the device presents **`ProductID 0x1978`** | all five call sites of cfg107's matcher `0x4035f0` push `$0x1978` (`0x4130ac 0x413844 0x413edb 0x413f9d 0x414046`); `0x1977` appears nowhere in cfg107 | it enumerates as `0x1977`, or as neither | *pending* |
| 3 | It presents **two vendor collections**, not one | `config-protocol.md` §10: the vendor patched `hid_open` to filter on usage pair, and the tool opens two | one vendor collection, or three or more | *pending* |
| 4 | Collection A is **`UsagePage 0xFF01`, `Usage 0x02`** | `cfg107 0x403777`–`0x40377c`, `cmpw $0xff01`; `fw110 0x40113c`/`0x401145`, same pair | either value differs | *pending* |
| 5 | Collection B is **`UsagePage 0xFF02`, `Usage 0x01`** | `config-protocol.md` §10.1, the second `hid_open` filter | either value differs | *pending* |
| 6 | Collection A declares a **Feature report `0xA0` of 1041 bytes total** (1 id + 1040) | `updater-protocol.md` §2; `movl $0x411,%esi` at cfg107 `0x403bc9` and `0x404238` | no 1041-byte feature report; or the id is not `0xA0`; or macOS reports a different max | *pending* |
| 7 | Collection A declares a **Feature report `0xA1` of 64 bytes total** (1 id + 63) | `updater-protocol.md` §2 | no 64-byte feature report, or a different id | *pending* |
| 8 | Collection B declares an **Input report `0x03` of 8 bytes** | `config-protocol.md` §10.3, the poll thread's `buf[0]==3` guard and 8-byte read | different length or id | *pending* |
| 9 | `bcdDevice` is the firmware version, and decodes as **BCD, not division** | `updater-protocol.md` §1, "Firmware version display — it is BCD, and the decode is not division" | the BCD reading gives a nonsense version and `raw/100` gives a sensible one | *pending* |
| 10 | Report IDs are **numbered** — the descriptor declares explicit IDs | byte 0 of every vendor buffer is the report ID (§2) | the descriptor declares no `Report ID` item, making reports unnumbered | *pending* |

**Convention check, so a mismatch is not misread.** macOS's
`MaxFeatureReportSize` **includes** the report-ID byte — verified against an
attached Apple HID device whose descriptor parses to an 8-byte feature payload
and which reports `MaxFeatureReportSize = 9`. Windows' `HIDP_CAPS
.FeatureReportByteLength` uses the same convention, and it is the number the
vendor's code compares against. So predictions 6 and 7 are directly comparable
to what `hidobserve.py` prints, with no adjustment.

## Predictions this cannot test, and must not be claimed to

- **Anything about the bootloader.** PID `0x1977` is a *different device mode*
  with, presumably, its own descriptor. Seeing it requires entering the
  bootloader, which is stage 2 of `CLAUDE.md` §4.4 and is not free.
- **Every command byte map** (§3), the block arithmetic (§10.1), the checksum
  (§3.5), the busy convention (`0x04`/2000 ms updater, `0x03`/1000 ms config),
  and which `FWFILE` is ours (§8.5a). None of these is in a descriptor.
- **Whether the device validates the image it is given.** `CLAUDE.md` §2 assumes
  not, deliberately, and nothing observable here changes that.

## Result

*Not yet run. `Tools/device/hidobserve.py` produces it, reads only `ioreg`, and
opens no device.*
