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

| # | prediction `[D]` | falsified if | observed `[O]` | verdict |
|---|---|---|---|---|
| 1 | `VendorID == 0x3367` | any other VID | `0x3367` | **CONFIRMED** |
| 2 | `ProductID 0x1978` in normal operation | `0x1977`, or neither | `0x1978` | **CONFIRMED** |
| 3 | **two** vendor collections | one, or three or more | **three** | **WRONG — see below** |
| 4 | collection A is `UsagePage 0xFF01` / `Usage 0x02` | either differs | `0xFF01` / `0x02` | **CONFIRMED** |
| 5 | collection B is `UsagePage 0xFF02` / `Usage 0x01` | either differs | `0xFF02` / `0x01` | **CONFIRMED** |
| 6 | Feature report `0xA0`, **1041 bytes** total | no such report, or a different id | Feature `0xA0`, 8320 bits = 1040 payload, **1041 on the wire**; macOS `MaxFeatureReportSize` = **1041** | **CONFIRMED, exactly** |
| 7 | Feature report `0xA1`, **64 bytes** total | no such report, or a different id | Feature `0xA1`, 504 bits = 63 payload, **64 on the wire** | **CONFIRMED, exactly** |
| 8 | Input report `0x03`, 8 bytes, on collection B | different length or id | Input `0x03`, 56 bits = 7 payload, **8 on the wire** | **CONFIRMED** |
| 9 | `bcdDevice` decodes as **BCD, not division** | BCD nonsense and `raw/100` sensible | `0x0107` → BCD **1.07**; division gives **2.63** | **CONFIRMED, and division refuted** |
| 10 | report IDs are numbered | no `Report ID` item | six numbered ids across two interfaces | **CONFIRMED** |

**Nine of ten exactly right, and the tenth is wrong in the direction that
matters.** Every length, every id and both predicted usage pairs came back
identical to constants read out of a Windows `.exe` on a machine that has never
run it. The risk this observation existed to retire — that a report length in
these notes was an immediate the device does not actually agree with — is
retired: `0x411` and `0x40` are the device's own numbers.

## The device presents a THIRD vendor collection, and nothing in Endgame's software opens it  [O]

Interface A's descriptor declares **five** top-level collections. The third
vendor one was not predicted because nothing in the binaries points at it:

| top-level collection | usage page / usage | reports | what it is |
|---|---|---|---|
| 1 | `0x01` / `0x06` — Generic Desktop, **Keyboard** | Input `0x02`, 8 bytes | boot-keyboard layout: 8 modifier bits, 1 reserved byte, 5 keycode slots |
| 2 | `0xFF01` / `0x02` — vendor | Feature `0xA1` (64), Feature `0xA0` (1041) | **the command channel** |
| 3 | `0x0C` / `0x01` — Consumer Control | Input `0x06`, 3 bytes | 16-bit consumer usage, range `0x01`–`0x23C` |
| 4 | `0xFF02` / `0x01` — vendor | Input `0x03`, 8 bytes | **the event channel** |
| 5 | **`0xFF02` / `0x02` — vendor** | **Input `0x08`, 64 bytes** | **unidentified** |

Collection 5 is 63 bytes of vendor-defined input per report — the same payload
size as Feature `0xA1` — and **no binary in the corpus opens it.** That is
mechanical, not an impression. The vendor's patched `hid_open` filters on the
usage *pair*, and every filter site in every config tool compares the usage
against `1`:

```
cfg107 0x403777  movl  $0xff01,%edx      cfg107 0x402eb5  movl   $0xff02,%ecx
       0x40377c  cmpw  %dx,0x57f1d2             0x402eba  cmpw   %cx,0x18(%esi)
       0x403785  cmpw  $0x2,0x57f1d0            0x402ec0  cmpw   $0x1,0x1a(%esi)
```
cfg104 `0x402eca`/`0x402ed0` and cfg101 `0x402eca`/`0x402ed0` are byte-identical
to cfg107's. The updaters never compare `0xFF02` at all — `litscan.py` finds
**zero** 4-byte `0xff02` immediates in fw110 and fw104.

**So the mouse has an input channel its own vendor's software never listens to.**
Recorded as `[O]` and nothing more: its content is completely unknown, no
`[D]` evidence bears on it, and per §1.3 it is not a basis for anything. It is a
LIST 3 question and a good one — an 8-byte event channel that reports polling
rate sits next to a 64-byte one that reports *something*.

## Other observations taken at the same time  [O]

- **The device is at firmware 1.07** (`bcdDevice 0x0107`). The newest updater we
  have is 1.10, so this mouse is a genuine update target and the flasher has a
  real job to do.
- **Two USB HID interfaces, not one.** Interface A is the 156-byte descriptor
  above (`MaxInput` 64, `MaxFeature` 1041). Interface B is a 69-byte descriptor,
  Generic Desktop / **Mouse**: Input `0x01`, 8 bytes — 8 button bits, X and Y as
  **16-bit** values, wheel and AC Pan as 8-bit. Nothing vendor-specific on it.
- **The keyboard and consumer collections corroborate `config-protocol.md`
  §12.2 from the hardware side.** That inventory, read out of the config tool's
  dialogs, lists a `'KEYBOARD KEY'` binding dialog with SHIFT/CTRL/WIN/ALT and
  media bindings `'PLAY/PAUSE' 'NEXT' 'PREVIOUS' 'MUTE' 'VOLUME UP' 'VOLUME
  DOWN' 'BROWSER' 'EXPLORER'`. Collections 1 and 3 are exactly the transports
  those need. Neither was predicted; both are explained.
- **macOS handed all of this over with no permission prompt at all**, because
  `ioreg` reads the I/O Registry rather than opening a device. Whether *opening*
  the vendor collection needs Input Monitoring is still untested and remains on
  `CLAUDE.md` §5's list.
- macOS runs **Keyboard Setup Assistant** on plug-in, because collection 1 is an
  unrecognised keyboard. Quit is the correct answer; there is no keyboard to
  identify. Worth knowing before our own tool is blamed for it.

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

Taken 2026-09-04 with `Tools/device/hidobserve.py --save .analysis`, which reads
`ioreg` and opens no device. Raw properties in `.analysis/hidobserve_3367.json`.
Interface A's descriptor, verbatim, so anyone can re-parse it:

```
05010906a1018502050719e029e71500250175019508810295017508810195057508150125
650507190129658100c00601ff0902a10185a17508953f150025010921b10385a075809541
150025010922b103c0050c0901a101850619012a3c021501263c02950175108100c00602ff
0901a1018503190129ff15002500950775088101c00602ff0902a1018508190129ff150025
00953f75088101c0
```
