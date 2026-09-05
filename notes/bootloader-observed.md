# The bootloader, observed [O]

Everything here was read off the physical device on 2026-09-04 with
`Tools/device/pidwatch.py` and `ioreg -c IOHIDDevice -r -l -a`. **No HID device
was opened and no byte was sent.** These are `[O]` under CLAUDE.md §1.2 — the
first `[O]` facts this project has about the bootloader.

## 1. There is a hardware-forced bootloader entry, and it works

Holding a button while plugging the mouse in re-enumerates it as **PID
`0x1977`** — the bootloader product id the updater searches for (§1.1 of
`updater-protocol.md`, `FUN_00401000(0x1977)`).

```
[23:18:00]  PID 0x1978 APPLICATION  ver=0x0107 (fw 1.07)
[23:18:06]  (no VID 0x3367 device present)          <- unplugged
[23:18:19]  PID 0x1977 BOOTLOADER   ver=0x0006
```

### The procedure [O]

> **Hold LEFT and RIGHT mouse buttons together. Keep holding. Plug the cable in.
> Keep holding for a few more seconds, then release.**

Reported by the owner, 2026-09-05, as performed in the run logged above. This is the
recovery procedure for the entire project and it belongs in the README, in
`egg-flash --help`, and printed by the flasher before it sends anything.

**Not yet established, and each is worth a line when someone next has the device
in hand:** the minimum hold time; whether left alone or right alone also works;
whether the buttons must be held *before* the cable goes in or can be pressed
after; and whether releasing early aborts the entry. Nothing here depends on
those answers, but a recovery procedure with unknown tolerances is worse than
one with known ones.

### Why this is the most important observation in the project

CLAUDE.md §2's threat model is bugs in our own code, and the reason that mattered
is that the consequence was unbounded: one mouse, no spare, no way back. The
recovery argument in `updater-protocol.md` §10.2 rested on a `[G]` — that the
bootloader lives below block `0x34`, survives a failed update, and leaves the
device enumerable. It could not be tested, because testing it meant breaking the
mouse with no way back.

It no longer rests on that guess. A bootloader reachable **without a working
application image** means:

- Any failed or partial flash is recoverable. The updater flashes a device it
  finds already in the bootloader with no application handshake at all
  (`updater-protocol.md` §5.1 case 6, `mode == 2`) — `[D]`.
- The only unrecoverable failure left is **corrupting the bootloader itself**,
  and the only route to that is emitting a block index below `0x34`. That is a
  number our own code computes (`0x34 + i`, §10.1), so it is prevented by
  assertion, not by hope. §10.3 invariant 3 is now the one invariant that
  carries real weight; every other failure mode is an inconvenience.

## 2. The bootloader identifies itself [O]

| field | value |
| --- | --- |
| `Manufacturer` | `EGG` |
| `Product` | **`Bootloader`** |
| `VendorID` | `0x3367` |
| `ProductID` | `0x1977` |
| `VersionNumber` | `0x0006` |

CLAUDE.md §4.2 requires: *"if the bootloader reports a version or an identity of
any kind, refuse anything unrecognised."* We now have that identity and it is
unambiguous — a product string that literally reads `Bootloader`, and a version
field distinct from the application's `0x0107`. Preflight must check all of it.

`VersionNumber` `0x0006` decoded by the updater's own BCD round trip
(`updater-protocol.md` §1) displays as `0.06`. Whether the bootloader version is
meant to be read that way is **not** established — the updater only ever formats
the *application's* version — so treat `0x0006` as an opaque identity token to
match exactly, not as a number to compare against.

## 3. Both feature report lengths are confirmed, in one collection [O]

This closes `updater-protocol.md` §6.1, which was explicitly waiting on the
device and named this as the specific thing to look for.

The bootloader's second interface (`PrimaryUsagePage` `0x01`, `PrimaryUsage`
`0x06`) declares a vendor collection **usage page `0xFF01`, usage `0x02`**
containing **both** report ids:

| report | Report Size | Report Count | payload | + report id | updater sends |
| --- | --- | --- | --- | --- | --- |
| `0xA1` | 8 | 63 | 63 | **64** | `0x40` = 64 ✓ |
| `0xA0` | 128 | 65 | 1040 | **1041** | `0x411` = 1041 ✓ |

`MaxFeatureReportSize` = **1041**, the maximum over the two, exactly as §6.1
predicted Windows would compute it — which is why the updater can hardcode both
lengths against one handle and still work.

**Both hardcoded lengths are now `[O]`-confirmed against the device.** macOS
does no padding, so we send exactly these and treat any mismatch as a preflight
failure.

## 4. The bootloader presents a full application-shaped descriptor [O]

It is not a minimal DFU-style stub. Its 156-byte descriptor carries the same
keyboard collection, the same consumer-control collection, and the same three
vendor collections (`0xFF01/0x02`, `0xFF02/0x01`, `0xFF02/0x02`) as the running
application, and its first interface still presents a working mouse.

Consequence for enumeration: **usage page and usage do not distinguish the two
modes.** Only `ProductID` and the `Product` string do. Any code that finds the
device by usage alone will happily talk to the wrong mode.

## 5. Open gap — a one-byte descriptor difference, source unverified

Diffing the bootloader's `usage 0x06` descriptor against the application-mode
descriptor **as transcribed into `notes/device-predictions.md`** gives one
difference, at byte 35:

```
app  ... 95 05 75 08 15 01 25 65 05 07 19 01 29 65 81 00 ...
boot ... 95 05 75 08 15 00 25 65 05 07 19 01 29 65 81 00 ...
                     ^^
```

That is `LogicalMinimum` of the keyboard key array: `1` in the note, `0` in the
bootloader. `0` is the conventional value for that field.

**This is not yet a finding either way.** The application-mode bytes were
transcribed by hand into a note; the bootloader's were read programmatically
from a plist. CLAUDE.md §1.2b says a derived view is never ground truth, and a
hand transcription is a derived view. **Re-read the application-mode descriptor
from the device programmatically and re-run the diff** before recording any
conclusion. Recorded now, unresolved, per §1.7.

Also outstanding: we have never captured the **mouse interface** (`usage 0x02`)
descriptor in application mode at all — only the bootloader's 69-byte one. Grab
it on the next replug.

## 5a. Bootloader mode is NOT latched — a power cycle exits it [O]

Observed 2026-09-05. The device sat in the bootloader for **about four hours**,
continuously powered, and stayed there — so the mode does not time out. Unplug
and replug with no button held, and it returns to `PID 0x1978`, ver `0x0107`,
application mode, first try.

```
[03:21:28]  PID 0x1977 BOOTLOADER   ver=0x0006      <- 4 hours after entry
   ... unplug, replug, no buttons ...
[03:23:37]  PID 0x1978 APPLICATION  ver=0x0107
```

Two consequences, and they pull in opposite directions:

- **Good:** a stray bootloader entry is self-correcting. A user who fumbles the
  button combo is one replug from normal, with nothing to explain.
- **Watch:** it means the bootloader hands off to the application whenever one
  looks valid. So a device with a *partially written* application will attempt
  to run it rather than sitting safely in the bootloader — which is exactly why
  the button entry matters, and why it is the recovery path rather than a power
  cycle.

Note also that the entry survived four hours of being powered without reverting,
so nothing in our flasher needs to race a timeout after `A1 3A`.

**Still `[G]`:** whether the bootloader validates the application before handing
off. If it does, a corrupt application would leave the device in the bootloader
by itself, which would be a second recovery path. Nothing observed either way,
and the button makes it unnecessary to know.

## 6. What this does NOT establish

- **That the bootloader survives a corrupted application.** What was shown is
  that a *button* reaches the bootloader from a *healthy* device. Whether the
  same mechanism works when the application region is blank or partial is still
  `[G]`. It is now strongly plausible — a force-entry path whose whole purpose is
  recovery would be strange to gate on a valid application — but plausible is
  not observed.
- **That the bootloader accepts a write.** Nothing has been sent to it.
- **Where the bootloader lives in flash.** Still `[G]`, still below `0x34` by
  inference only.
- **Whether the device auto-enters the bootloader on an invalid application.**
  Untested, and the cheap test is described in §10.2's successor work.

---

## 4. The kernel log settles the button question as far as it can be settled  [O]

2026-09-05. Three button-plug cycles run with `Tools/device/bootwatch.py` at
100 Hz, then cross-checked against macOS's unified log, which records every USB
enumeration with a kernel timestamp and is therefore blind to nothing a poll
could miss.

```
/usr/bin/log show --last 20m --style compact --predicate 'eventMessage CONTAINS "0x3367"'
```

Six attach events, perfectly alternating, three of each:

| time | PID | |
| --- | --- | --- |
| 13:54:31.155 | `0x1977` | button plug 1 |
| 13:54:48.187 | `0x1978` | normal plug |
| 13:54:57.513 | `0x1977` | button plug 2 |
| 13:55:06.194 | `0x1978` | normal plug |
| 13:55:23.449 | `0x1977` | button plug 3 |
| 13:55:29.637 | `0x1978` | normal plug |

**Every button plug produced exactly ONE enumeration, and it was the
bootloader.** No `0x1978` attach precedes any `0x1977` attach, in any trial.

**What this rules out, and it is the stronger half.** The mechanism "the
application boots, brings up USB, reads the buttons, then resets into the
bootloader" is dead. That path requires an application-mode enumeration, the
kernel logs every enumeration however brief, and there is none. The 100 Hz poll
could have missed a short transient; the kernel log cannot.

**What it does NOT rule out, per §1.2a.** Firmware that reads the buttons
*before* bringing up USB would never enumerate as `0x1978` and would produce a
log identical to the above. So the honest claim is: **the application does not
participate in bootloader entry via any path that enumerates** `[O]`. That the
bootloader owns the check outright remains `[G]`.

**Why the residue is smaller than it looks.** For the surviving case to bite,
the application must be present-and-broken rather than absent: a blank or
invalid application region cannot be jumped to at all, so the bootloader keeps
control and recovery works for a different reason. The one genuinely dangerous
shape is a *partial* image whose first block is valid — block `0x34` holds the
vector table and is written first — with garbage after it, which the bootloader
would accept and jump to, and which could then hang before any button is read.
**That is precisely the state CLAUDE.md §4.2's never-return rule exists to
prevent**, and it is why invariant 7 (no return from the post-`A0 03` phase
without a verified image or a loud unrecoverable state) is load-bearing rather
than decorative. Our flasher retries forever; the vendor's gives up after five
attempts per block.

## 5. Both report descriptors, recovered from the kernel log  [O]

The same log carries the full `ReportDescriptor` for every collection, base64 in
the `IOHIDFamily` dictionaries. This closes two open items at zero cost and with
no device opened.

**Bootloader and application differ by exactly ONE byte.**

| descriptor | bootloader `0x1977` | application `0x1978` |
| --- | --- | --- |
| 156-byte (keyboard + vendor + consumer) | offset 35 = `0x00` | offset 35 = `0x01` |
| 69-byte (mouse) | **byte-identical** | **byte-identical** |

Offset 35 is the keyboard array's `Logical Minimum`. **This is exactly what
`notes/device-predictions.md` recorded by hand.** §1.2b required treating a hand
transcription as a derived view rather than evidence; it has now been confirmed
against the bytes and is `[O]`. The mouse-interface descriptor in application
mode had **never been captured before** — working memory listed it as open. It
is identical to the bootloader's.

**156-byte descriptor (application mode):**
```
05010906a1018502050719e029e71500250175019508810295017508810195057508150125650507
190129658100c00601ff0902a10185a17508953f150025010921b10385a07580954115002501
0922b103c0050c0901a101850619012a3c021501263c02950175108100c00602ff0901a1018503
190129ff15002500950775088101c00602ff0902a1018508190129ff15002500953f75088101c0
```
**69-byte descriptor (both modes):**
```
05010902a10185010901a1000509190129081500250195087501810205010930093116008026ff7f
75109502810609381581257f950175088106050c0a380295018106c0c0
```

**The transport collection, decoded, and it matches our constants exactly:**
```
06 01 ff     Usage Page 0xFF01
09 02        Usage 0x02
a1 01        Collection (Application)
85 a1          Report ID 0xA1
75 08          Report Size 8 bits
95 3f          Report Count 63        ->   63 + 1 id  =   64 bytes
09 21 b1 03    Feature
85 a0          Report ID 0xA0
75 80          Report Size 128 bits = 16 bytes
95 41          Report Count 65        -> 65*16 + 1 id = 1041 bytes
09 22 b1 03    Feature
```
`Protocol.h` hardcodes `kSmallLen = 0x40` (64) and `kLargeLen = 0x411` (1041).
Both were already `[O]` for the bootloader; they are now `[O]` for the
**application** collection too, which is the one `egg-config` talks to.

Six top-level collections in total: Generic Desktop mouse (`0x02`) on its own
interface; and on the other, Generic Desktop keyboard (`0x06`), Vendor
`0xFF01`/`0x02` (the transport), Consumer (`0x01`), Vendor `0xFF02`/`0x01`, and
Vendor `0xFF02`/`0x02`. The two `0xFF02` collections are input-only and no
Endgame binary opens either.

## 6. Something else already holds the device open, exclusively  [O]

```
AppleUSBHostUserClient::openGated: failed to open Bootloader@00100000:
provider is already opened for exclusive access by pid 613, Google Chrome
```

Logged three times across the run, against **both** `Bootloader` and the
application-mode device. Another process (Spotify) tried to open the mouse and
the kernel refused it.

**Operational consequence, and it must be handled before §4.4 stage 1:** if a
browser holds the device when `egg-config` runs, our `hid_open_path` fails, and
that failure looks exactly like a permissions or protocol fault. Close it first,
and make the tool say "another process holds this device" rather than reporting
a generic open failure.

**One free inference the other way:** a userspace process is successfully
opening this device on macOS right now. That is weak positive evidence that the
vendor collection is reachable without special entitlement — weak, because that
process may hold permissions ours will not.
