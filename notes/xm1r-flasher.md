# XM1r Flash Upgrade 1.9.46 — a *different product* and a *different vendor's code*

## 0. Read this before using anything below

**This file is about the Endgame Gear XM1r, not the OP1 8k v2.** Nothing in it
may inform a byte written to the OP1. `CLAUDE.md` §1.4 exists for exactly this
hazard — "other resources in the binary may belong to other products" — and the
risk here is larger, because this is a whole second protocol sitting in the same
working set.

Tags in this file are deliberately **not** the §1.2 tags, so that nothing here
can be mistaken later for an OP1 finding:

- **`[D-X]`** — derived from `XM1r_Flash_Upgrade_1.9.46.exe`, address cited.
- **`[V]`** — observed in a vendor video of the tool running, relayed by the owner
  from screenshots, a transcript and a moderator's DM dated 2022-04-05.
- `[V]` is **not** `[O]`. §1.2's `[O]` means read off *our* device. This is a
  second-hand observation of someone else's device, mediated by a description.

### Provenance of the binary
`XM1r_Flash_Upgrade_1.9.46.exe`, 5,664,256 bytes,
sha256 `670230936df1d54067f4d85e5b88df2d3ad426c0efd136852b1a129e873569b1`.
Obtained from a Discord channel, not from Endgame's own download page, so its
provenance is **weaker than the eight OP1 binaries** in `notes/binaries.md`.
Never executed (§1.5); everything here is static.

### A methodological failure, recorded rather than hidden
The plan was to derive this binary **cold** and commit predictions *before*
seeing any video material, so that agreement would be evidence the method
produces true statements. That did not happen — the screenshots, transcript and
DM arrived in the same message as the file.

So **everything below that agrees with `[V]` is post-hoc explanation, not
prediction, and is weak evidence.** It is recorded as such and must not be
cited later as a successful blind test. What survives undamaged is the reverse:
a *contradiction* between `[D-X]` and `[V]` would still be strong, because
nothing about seeing the video first makes a contradiction easier to invent.

## 1. This is not Endgame's code, and not the OP1 codebase  [D-X]

The UI string table ends with:

> `<END_OF_FILE (c) 2020 by Ruling Technologies Sdn. Bhd. All rights reserved>`

and the embedded PDB path is

> `C:\Developer\Projects\Windows\Endgame Gear Flash Upgrade\Endgame Gear Flash Upgrade\Flash Upgrade\Release Firmware Update\Flash Upgrade.pdb`

— a **generic "Flash Upgrade" project**, third-party, not per-product. RTTI
names `CUpgradeDlg`, `CFlashUpgradeApp`, `CFlashUpgradeDlg`. The GUI is skinned
with a library identifying itself as `GameWizard Lite/Full Combo Version`,
hence the `gdiplus` / `UxTheme` / `MSIMG32` imports the OP1 updater does not have.

Structural differences from the OP1 updaters, all `[D-X]`:

| | OP1 updater 1.10 | XM1r flasher 1.9.46 |
| --- | --- | --- |
| size | 2,133,504 B | 5,664,256 B |
| sections | 5, incl. `.reloc` | 4, no `.reloc` |
| HID imports | 7 | 10, adding `HidP_GetButtonCaps`, `HidP_GetValueCaps`, `HidP_MaxUsageListLength` |
| firmware location | `FWFILE` resource 140 | **not in the resource tree** |
| UI strings | in `.rdata` as UTF-16 | in a `TEXTFILE` resource |
| optimisation | optimised | **unoptimised** — every local spilled, very readable |
| version gate | none; displays only | **gates**: "You already have the latest Firmware or newer" |

**The last row matters.** `notes/updater-protocol.md` §1 establishes that the OP1
updater never gates on version. This one does. Do not generalise either way.

## 2. The complete UI surface  [D-X]

`TEXTFILE` resource **4000**, lang 1033, 4,844 bytes, UTF-16LE. Resources
4001–4010 are all byte-identical to each other (`521de15e…`) and differ from
4000 — a localisation table with only English populated.

Records are separated by CRLF; `*|` is a line-break marker *inside* a record and
`|` a line break in the older variant. Strings come in **pairs** — an older
single-line form and a newer multi-line form. Full contents, in order:

```
||FIRMWARE UPGRADE
PRODUCT_CODE*|*|Not Found !
*|*|XM1R*|*|Not found !
Your PRODUCT_CODE*|is currently on*|Version
*|*|Your XM1R is currently running*|on version
With this upgrade package your product|can be updated to the latest firmware. |
  Once the upgrade is started, it cannot be cancelled or reversed.*|Use at your own Risk.
Your Product can not be Upgraded ... There is no supported product connected
Your Product can not be Upgraded. You already have the latest Firmware or newer
EXIT   /   UPGRADE NOW
Transfer in progress ...
Do not disconnect your device until the transfer is complete. ...
Do Not Disconnect your Device !|After Reprogramming your Device is complete,
  your Device will restart automatically
||Firmware upgrade was NOT successful!|Please restart the application and try again.
||Firmware upgrade was successful!
Transfer completed. Please unplug the device now.
Transfer completed. Reprogramming the device now.
<END_OF_FILE (c) 2020 by Ruling Technologies Sdn. Bhd. All rights reserved>
```

### 2.1 The `PRODUCT_CODE` bug the screenshot shows
`[V]` Screenshot 1 reads **"Your PRODUCT_CODE is currently on Version 1.8.40"**.

`[D-X]` The table holds *both* `Your PRODUCT_CODE*|is currently on*|Version` and
`*|*|Your XM1R is currently running*|on version`. `PRODUCT_CODE` is a literal
placeholder token that a per-product build step is meant to substitute. The tool
selected the un-substituted generic record.

This is a real vendor defect and it is worth one sentence of consequence for us:
**it is direct evidence that Endgame ships flashers built from a generic
template with per-product substitution, and that the substitution can silently
fail.** It is a reason to be sceptical of any assumption that strings, resource
ids or product tables in a vendor flasher are product-correct.

### 2.2 Two post-transfer regimes — the mod's step 7 explained
`[D-X]` The table contains **both** `Transfer completed. Reprogramming the device
now.` and `Transfer completed. Please unplug the device now.`

`[V]` The moderator's DM, step 7: *"In case the flash process is done (full bar),
but the XM1r does not reboot itself, re-plug the USB cable."*

So the flash is **two-phase — transfer, then a separate reprogram/commit step**,
with a fallback that asks the user to power-cycle if the device does not restart
itself. `[G]` on why: possibly the device stages the image and commits on reset.

**This is architecturally different from the OP1 updater**, which writes and
verifies block by block and then sends `A1 09` (`notes/updater-protocol.md`
§5.4–§5.5). Do not read one onto the other.

## 3. Transport — 65-byte feature reports  [D-X]

Nothing here resembles the OP1's `0xA0`/`0xA1` pair.

The transport function is **`0x00644730`**. Its device object arrives as
`[ebp+8]`; the HID handle is at `obj+0x04`.

```
send buffer    = obj+0xd5, 0x41 = 65 bytes   (memset 0, 0x644770)
receive buffer = obj+0x116, 0x41 = 65 bytes  (memset 0, 0x644786)   -- adjacent
```

`HidD_SetFeature(handle, obj+0xd5, 0x41)` at **`0x006449bc`**.

### Packet layout
```
[0]        = 0x00                       ; 0x644830   (report ID)
[1]        = 0x00                       ; 0x644843
[2]        = command                    ; 0x644858   from arg [ebp+0xc]
[3]        = sequence                   ; 0x644873   from obj+0xd4
[4 .. 0x3e]= payload                    ; memcpy(dst=buf+4, src=[ebp+0x10],
                                        ;        len=[ebp+0x14])  call 0x6448ef
[0x3f,0x40]= checksum16, little-endian  ; memcpy 2 bytes, 0x644933
```

**The sequence byte is a protocol feature the OP1 has no equivalent of** [D-X]:
`obj+0xd4` is incremented (`0x6447ff`), then reduced `% 0xff` by an `idivl`
(`0x64481a`), and the remainder stored back (`0x64481f`).

### Checksum
`0x00643760(ptr, len, mode)`; the send path calls it as
`(buf+1, 0x3e, 1)` at `0x0064490e` — i.e. over bytes `[1..0x3e]`, **excluding**
the report ID and the checksum field itself.

The routine is **dual-mode** [D-X]:
- `mode != 0` → plain 16-bit sum of bytes (`0x64378e`–`0x6437a1`). This is what
  the send path uses.
- `mode == 0` → **CRC-16** using a 256-entry `u16` table at `0x0072a390` and a
  bit-reverse helper at `0x00643930` (`0x6437ca`).

The CRC table **cannot be read from the file**: `.data` has raw size `0x20800`
covering VA `0x705000`–`0x725800`, and `0x72a390` lies past that, in the
uninitialised tail. It is generated at runtime, so the polynomial is not
statically readable and is **not** derived here. Anyone continuing this must find
the generator rather than assume CCITT.

### Retry structure
```
outer loop  : counter -0x30(%ebp), bound 0x64 = 100   (0x644985)
inner read  : counter -0x28(%ebp), bound 0x14 =  20   (0x6449e9)
sleep       : Sleep([ebp+0x18]), initialised to 2 ms  (0x64497e)
inner sleep : Sleep(2)                                (0x644a2f)
```
Wholly unlike the OP1's 4-attempt / 5-error-code ladder.

## 4. The command set — 45 commands  [D-X]

`0x644889` bounds-checks `command - 1` against `0x2c` unsigned and dispatches
through a jump table at `0x00645050` via a byte index table at `0x00645060`
(45 entries). So the valid command range is **`0x01`–`0x2D`**.

| case | handler | effect | commands |
| --- | --- | --- | --- |
| 0 | `0x6448af` | none (fall through) | `01 03 05 07 08 09 0a 0b 0c 0e 10 12 16 17 18 19 1d 23 24 25 26 28 2b 2c 2d` |
| 1 | `0x6448a0` | sets `obj+0xc8 = 1` | `02 04 06 0d 0f 11 1a 1b 1c 1e 1f 20 27 29 2a` |
| 2 | `0x6448b1` | **zeroes the sequence counter** `obj+0xd4` | `13` |
| 3 | `0x6448bd` | clears the send flag → **skips the send** | `14 15 21 22` |

`[G]` — and it stays `[G]` — what any of these 45 commands *means*. Command `0x13`
resetting the sequence looks like a session/handshake open and the four in case 3
look like locally-handled pseudo-commands, but neither is derived, and this is a
different product's device besides.

## 5. Firmware image location  [D-X]

**Not in the resource tree.** The resource directory holds only bitmaps, icons,
cursors, the `TEXTFILE` table, a manifest and version info — there is no `FWFILE`
type and no `RCDATA`. There is **no PE overlay**: the last section's raw data
ends at file offset `0x566e00` = 5,664,256 = exactly the file size.

Entropy scan of the initialised sections finds one firmware-sized candidate:
a run at **VA `0x706b00`–`0x71da00`** (file `0x304f00`–`0x31be00`), ~91.75 KiB,
H ≈ 7.78, with small internal gaps — inside **writable `.data`**, which is
consistent with in-place decryption but is not yet established. Three further
8 KiB runs sit in `.rdata` at `0x6a8000`, `0x6af000`, `0x6c6000`.

**Which of these is the image, and its exact extent, is not yet derived.** The
correct next step is the code that references the region, not a guess from size.

## 6. What the video corroborates — post-hoc, weak evidence

Flagged per §0: this is explanation after the fact, not prediction.

| `[V]` observation | `[D-X]` status |
| --- | --- |
| "Your PRODUCT_CODE is currently on Version 1.8.40" | explained exactly — §2.1 |
| Version rendered as three components `1.8.40` | **unexplained**; the OP1 tool renders `%.2f`. Not yet derived here. |
| Total 26 s, bar steady, longer at empty and full | not yet checked against any derived timing |
| Device rebooted itself; no disconnect during transfer | consistent with §2.2's two regimes |
| Mod: replug if it does not reboot | matches the second regime string exactly |
| No button held, no bootloader gesture | consistent — no such gesture seen in `0x644730` |
| Never failed, no retry | untestable from the binary |

## 7. Not yet derived
- The 45 commands' meanings, and which are used by the flash sequence.
- The firmware image's exact location, size, and whether it is encrypted.
- The three-component version decode.
- The runtime CRC-16 table generator and its polynomial.
- The second `HidD_SetFeature` site at `0x00644ff7` (the `0x14` command path,
  `movb $0x14` at `0x644f72`).
- Device identity: VID/PID have not been located yet.
- Everything upstream: enumeration, the upgrade button handler, the sequence.
