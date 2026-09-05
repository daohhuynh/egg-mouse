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
