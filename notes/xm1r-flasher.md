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

> **CORRECTION 2026-09-04.** There are **eleven** `TEXTFILE` resources, not one:
> names **4000–4010**. 4000 is 4,844 bytes; 4001–4010 are 4,720 bytes each.
> Found by the exhaustive resource walk in `Tools/ghidra-export/filemap.py`,
> whose whole-file partition closes to zero residue, so the count is complete
> rather than "what was noticed". Only 4000 has been read. The other ten are
> **unread** — near-identical sizes suggest translations of the same text, but
> that is `[G]` and they have not been opened. Recorded as an open item.

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
inner sleep : Sleep(2)                                (0x644a2f)
backoff     : failure paths sleep i*5+1 / j*15+1 ms
```
Wholly unlike the OP1's 4-attempt / 5-error-code ladder.

**Correction to an earlier reading in this file.** An earlier draft described the
outer sleep as `Sleep([ebp+0x18])`, "initialised to 2 ms". That was wrong in a way
worth naming: `0x64497e` is `movl $0x2, 0x18(%ebp)` — an **unconditional store into
the caller's fifth argument slot**, executed before any read of it and before the
outer loop test at `0x644985`. **Arg 5 is dead.** Whatever delay a caller passes is
discarded and 2 ms is always used. "Initialised" implies the caller's value was
absent; in fact it was overwritten. [D-X]

### Function signature and guard  [D-X]
`ret $0x14` at `0x64504a` ⇒ 5 stack arguments; `this` arrives in ECX (thiscall).

- `this` is stored to `-0x60(%ebp)` at `0x644737` and **never read again** —
  dead. (Verified: that store is the only `-0x60(%ebp)` reference in
  `0x644730`–`0x645050`.)
- Return slot `-0x2c(%ebp)` is initialised to **1** at `0x64473a` and set to
  **0 only on success** (`0x644b64`, `0x644dcb`, `0x64500f`). Non-zero = failure;
  it fails closed.
- Entry guard `0x644741`–`0x644756`: if `arg1 == 0` **or** `*(arg1+0xa8) == 0`,
  return 1 immediately with **no I/O**.

### 3.1 The receive path — reply tags and the two-reply regime  [D-X]

The send-side layout above was derived first; this receive-side structure was
missed on that pass and is the substantive addition.

Replies are read back into the receive buffer at `obj+0x116` and validated on
**two** fields:

```
recv[2] = reply tag        ; obj+0x118
recv[3] = sequence         ; obj+0x119
```

**There are two distinct replies, with different tags.**

| | tag check | site | sequence expected |
|---|---|---|---|
| first reply | `recv[2] == 0x14` | `cmpl $0x14` @ `0x644a71` | echoes the sent sequence |
| second reply | `recv[2] == 0x15` | `cmpl $0x15` @ `0x644cfe` | `(sent + 1) % 0xff` |

The second receive loop is **not** run for every command — it is armed by a
dispatch class field, so the 45 commands of §4 split into "immediate reply only"
and "immediate reply plus a deferred completion reply". Which commands fall in
which class is **not** yet enumerated here.

The expected second sequence is computed at `0x644d8e`–`0x644da0`:
```
addb  $0x1, %cl          ; sent_seq + 1
movl  $0xff, %ecx
idivl %ecx               ; remainder, i.e. % 255
```
then compared against `recv[3]` at `0x644dba`.

**Note the modulus is `0xff` = 255, not 256** — matching the send-side `% 0xff`
at `0x64481a`. Sequence numbers cycle **0..254**; the value 255 is never
generated and never expected. A reimplementation that used `& 0xff` would
desynchronise after 255 frames. [D-X]

**An ACK frame is sent back.** At `0x644f72`, `movb $0x14, 0xd5(%eax,%edx)` writes
tag `0x14` into the *send* buffer, and `0x644f90`/`0x644f97` copy a byte out of the
receive buffer into it. The exchange is not request/response; it is
request → reply → ack (→ deferred reply).

**Independent corroboration of the 65-byte report size.** The two buffer bases
differ by `0x116 - 0xd5 = 0x41 = 65`, exactly one report. The size in §3 was taken
from the `HidD_SetFeature` length argument; the object layout gives it again from
a different direction.

### 3.2 The host can be told to ignore a checksum mismatch  [D-X]

This is the finding in this file with the clearest bearing on the OP1 work, and
it is about the **host tool**, not the device.

`0x00706a80` is a one-byte flag in `.data` (file offset `0x304e80`; `.data` VA
`0x705000`, raw `0x303400`, raw size `0x20800`, so the byte is initialised in the
image, not in the uninitialised tail).

**Its initial value in the shipped file is `0x01`.**

An exhaustive scan of `.text` finds exactly **four** references, and no others:

| site | instruction | role |
|---|---|---|
| `0x644ade` | `movzbl 0x706a80, %eax` | read (first reply) |
| `0x644d6b` | `movzbl 0x706a80, %edx` | read (second reply) |
| `0x645637` | `movb $0x0, 0x706a80` | write — store **zero** |
| `0x645c9e` | `movb $0x0, 0x706a80` | write — store **zero** |

The read is reached only when the checksum comparison **failed**:
```
644acd: cmpl  %edx, %ecx          ; computed vs received
644acf: jne   0x644ad8            ; differ -> leave match flag clear
644ad1: movl  $0x1, -0x44(%ebp)   ; equal  -> match flag = 1
644ad8: cmpl  $0x0, -0x44(%ebp)
644adc: jne   0x644aed            ; matched -> continue
644ade: movzbl 0x706a80, %eax     ; NOT matched:
644ae5: testl %eax, %eax
644ae7: je    0x644bae            ;   flag == 0 -> failure path
644aed: ...                       ;   flag != 0 -> continue as if it matched
```

So **non-zero means a checksum mismatch is ignored**, and non-zero is the default.
Both writes clear it — i.e. they *enable* enforcement — and both are gated
identically:
```
movzbl 0x492(%reg), %r
cmpl   $0x7, %r
jg     <skip the store>
movb   $0x0, 0x706a80
```
Enforcement is switched on only when the byte at `obj+0x492` is **≤ 7** (signed
compare). What `obj+0x492` means is **[G]** — plausibly a protocol or firmware
version — and is not derived here.

**What this establishes, and what it does not.** It does not show the XM1r
updater is unsafe in practice; the gate may well be satisfied on every real
device. What it shows is that **a shipping Endgame-branded updater contains a live
code path that accepts a frame whose checksum did not match**, selected by a field
unrelated to the integrity of the data. Verification present in the binary was not
verification always applied.

This is direct support for the OP1 posture recorded in `CLAUDE.md` §2 ("assume the
device does not validate the image it is given"), and it extends it: **do not
assume a checksum in the vendor's protocol is enforced merely because the code to
enforce it exists.** For our own flasher the rule is the stricter one — verification
is unconditional, with no flag, no version gate, and no path that continues after a
mismatch. [D-X]

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

## 4.1 Command `0x13` is an unlock handshake carrying a 32-byte key  [D-X]

Derived from `0x006450c0`, the device-matching function (1,620 bytes,
`0x6450c0`–`0x645714`). It walks the enumerated device list — stride `0xa8`,
index `-0x18(%ebp)`, bound `-0x14(%ebp)` (`0x645279`–`0x645293`) — and for each
device calls the path matcher `0x00649730(device->path, <hardware-id string>)`.

On a match it calls the transport `0x00644730` with **command `0x13`** and a
**33-byte (`0x21`) payload that is a 32-character ASCII key**:

```
0x645409  pushl $0x0            ; arg5 sleep
0x64540b  pushl $0x21           ; arg4 payload length = 33
0x64540d  pushl $0x6b9e2c       ; arg3 payload  -> "MCIQFIFEDLH9F4AECX916PBD5P3A3078"
0x645412  pushl $0x13           ; arg2 command
0x64541d  pushl obj+0x2dc       ; arg1 device object
0x645421  calll 0x644730
```

Three keys, selected by which hardware ID matched:

| key (32 ASCII chars) | at | matched hardware IDs | flag set |
| --- | --- | --- | --- |
| `MCIQFIFEDLH9F4AECX916PBD5P3A3078` | `0x6b9e2c` | `vid_3367&pid_2003` | `obj+0x38c = 1` (`0x6453ff`) |
| `GEGJGYIKDCGBE9DWDKB27EAA7K9Z5J1P` | `0x6b9eb0` | `vid_24f0&pid_2020`, `vid_22d4&pid_1804`, `vid_3367&pid_1903`, `vid_3367&pid_1905` | `obj+0x388 = 1` (`0x64548d`) |
| `IOUIOGFTRBVGFRIOWEFHZXKLKLERSDFP` | `0x6b9ed4` | fallback, no ID matched | `obj+0x390 = 1` (`0x6454bc`) |

The three send sites are `0x645421`, `0x6454af`, `0x6454de`; the key strings are
each referenced exactly once in the whole `.text`, all inside this function
(verified by 4-byte literal scan).

**This joins up with §4's dispatch table.** Command `0x13` is the *only* command
in dispatch case 2, whose handler at `0x6448b1` zeroes the sequence counter
`obj+0xd4`. So `0x13` opens a session: it resets the sequence and presents a
per-product-family key.

`[G]` — and it stays `[G]` — whether the device *validates* the key, and what it
does if the key is wrong. Nothing in the tool reveals that.

### Why this matters beyond the XM1r
Two transferable points, neither of which licenses an OP1 byte:

1. **A vendor flasher can require an unlock token before it will do anything.**
   This is not derivable by guessing; it needed the binary. The OP1 updater's
   `A1 3A` command carries a 3-byte `5A A5 32` (`notes/updater-protocol.md` §3),
   which is the same *idea* at a much smaller scale. That is a reason to be
   confident those three bytes are a deliberate magic rather than incidental —
   but the OP1 finding stands on its own `[D]` evidence and gains nothing from
   here.
2. **This binary matches the connected device against a runtime list of five
   hardware IDs from three different USB vendors** (`0x6452bf`–`0x645353`, each
   gated on `obj+0x384`). It is precisely the pattern `CLAUDE.md` §1.4 forbids
   in our code: identity and image selection must be compile-time constants,
   never a search over a list. Here is a shipping vendor flasher that does the
   forbidden thing, and it flashes firmware.

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
- The 45 commands' meanings beyond `0x13` (§4.1), and which the flash sequence uses.
- The firmware image's exact location, size, and whether it is encrypted.
- The three-component version decode.
- The runtime CRC-16 table generator and its polynomial.
- Device identity: VID/PID have not been located yet.
- Everything upstream: enumeration, the upgrade button handler, the sequence.
- **Which of the 45 commands arm the deferred `0x15` reply** (§3.1). The dispatch
  class field selects it; the per-command values are not enumerated.
- **The meaning of `obj+0x492`**, the byte that gates checksum enforcement (§3.2).
  Currently `[G]`. This is the most load-bearing unknown left in this file.

*Resolved since the first draft:* the second `HidD_SetFeature` site at `0x00644ff7`
is the **ACK frame** (§3.1), not a separate command path.
