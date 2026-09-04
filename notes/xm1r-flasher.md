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

> **CORRECTION 2026-09-04, and now RESOLVED.** There are **eleven** `TEXTFILE`
> resources, not one: names **4000–4010**, found by the exhaustive resource walk
> in `Tools/ghidra-export/filemap.py`, whose whole-file partition closes to zero
> residue — so the count is complete, not "what was noticed".
>
> All eleven have now been read, and there are only **two distinct contents**:
> **4001–4010 are byte-identical to one another** (SHA-256 `521de15e…`, 4,720 B,
> 32 lines each), and 4000 (4,844 B) differs from them in **exactly one line** —
> its `<END_OF_FILE>` marker carries the copyright suffix quoted in §1. Ten
> identical copies of a UI string table is what an unfilled template slot looks
> like in a toolkit that expects one per supported product.
>
> Mechanical corroboration of §1's third-party claim while checking this:
> `Ruling` appears in `XM1r_Flash_Upgrade_1.9.46.exe` in both ASCII and UTF-16,
> and in **none of the eight Endgame binaries** in either encoding. So the two
> code bases share no toolkit lineage, and the practical consequence is worth
> stating plainly: **everything in this file is analogy, never evidence, about
> the OP1 protocol.** A second vendor's flasher shows what a flasher *can* look
> like; it says nothing about what Endgame's does.

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
**That quotation is the last third of the gate and was incomplete; corrected
2026-09-04.** Each store is guarded by **three** comparisons in sequence, on
three consecutive bytes, and any one of them failing skips the store
(`0x64560a`–`0x645637`, and the twin at `0x645c74`–`0x645c9e`):

```
64560a: movzbl 0x490(%eax), %ecx
645614: cmpl   $0x1, %ecx
645617: jg     0x64563e          ; skip
645619: movzbl 0x491(%edx), %eax
645623: cmpl   $0x3, %eax
645626: jg     0x64563e          ; skip
645628: movzbl 0x492(%ecx), %edx
645632: cmpl   $0x7, %edx
645635: jg     0x64563e          ; skip
645637: movb   $0x0, 0x706a80    ; enforce
```

So enforcement is switched on **only when `0x490 ≤ 1` and `0x491 ≤ 3` and
`0x492 ≤ 7`**, each component bounded independently — which is not a version
comparison, just three range checks written in a row.

**And `obj+0x490`–`0x492` is now derived: it is the firmware version the device
itself reports.** §3.2a below. The consequence is sharper than the original
finding: **both firmware versions this tool ships are 1.9.46 and 1.8.155**
(§6.9), whose middle component is 9 and 8, both `> 3`. On any device reporting
either of them the second comparison fails, the store never runs, and
`0x00706a80` keeps its shipped value `0x01` — **checksum mismatches ignored.**
The enforcing path is reachable only for a device reporting something at or below
`1.3.7` componentwise, which is not a version this tool can install.

### 3.2a `obj+0x490`–`0x493` is the device-reported firmware version  [D-X]

This resolves what §7 called the most load-bearing unknown in this file.

The four bytes are filled by four one-byte `memcpy` calls at `0x64557c`–`0x645607`
(twin at `0x645be0`–`0x645c6b`), immediately after the exchange at `0x64552c`
returns 0:

```
64551d: pushl $0x11                 ; command 0x11
645522: addl  $0x2dc, %ecx          ; the port sub-object
64552c: calll 0x644730              ; returns 0 on success
645531: testl %eax, %eax
645533: jne   0x64569f              ; failed -> abandon
...
64557c: leal  0x3f2(%eax,%edx), %ecx ; edx = 4    -> src
645587: addl  $0x490, %edx           ->            dst
64558e: calll 0x5ee040               ; memcpy(dst, src, 1)
       ... repeated with 5, 6, 7 -> 0x491, 0x492, 0x493
```

**The source is the receive buffer.** The port sub-object begins at `obj+0x2dc`
and §3.1 puts its receive buffer at `+0x116`; `0x2dc + 0x116 = 0x3f2`, exactly
the base used here. So the four bytes are `recv[4]`, `recv[5]`, `recv[6]`,
`recv[7]` — the first four payload bytes after §3.1's tag and sequence — of the
reply to **command `0x11`**, which §4's dispatch table places in case 1, the
class that arms the deferred `0x15` reply. A version query answered by a deferred
reply is exactly the expected shape.

**Then they are stored as a version triple.** `0x64563e`–`0x645677` passes
`0x490`, `0x491`, `0x492` to three one-line thiscall setters:

| setter | body | writes |
|---|---|---|
| `0x402120` | `movb %cl, 0x2d8(%eax)` | `obj+0x2d8` |
| `0x402140` | `movb %cl, 0x2d9(%eax)` | `obj+0x2d9` |
| `0x402160` | `movb %cl, 0x2da(%eax)` | `obj+0x2da` |

and `0x2d8/0x2d9/0x2da` are the device-side counterpart of the image-side triple
`0x24c/0x24d/0x24e` that §6.9 derives: the constructor `0x0064bb20` initialises
both runs adjacently (`0x64bc43`–`0x64bc93`), each has its own one-line getter
(`0x4040a0/0x4040c0/0x4040e0` against `0x404040/0x404060/0x404080`), and the
image-side one is written with literal `1, 9, 0x2e` and `1, 8, 0x9b`.

So **`obj+0x492` is the third component (patch) of the device's reported firmware
version**, `0x490` the first and `0x491` the second. `[D-X]`. What `obj+0x493`
means stays `[G]`; §6.9 shows it gating image selection on `== 1`, so a product
or variant code is the obvious reading and is not derived.

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

**Superseded by §6.9** (2026-09-04): the images are three contiguous 32,768-byte
`.data` blobs at `0x706a88`, `0x70ea88` and `0x716a88`, found from the code that
references them rather than from the entropy scan. The entropy run above brackets
them loosely and its 91.75 KiB figure is the scan's artefact, not the image size;
`3 × 0x8000 = 0x18000` = 98,304 bytes is the derived total. The `.rdata` runs are
not firmware.

## 5.1 The flash sequence, end to end  [D-X]

`FUN_006441c0` (`0x6441c0`–`0x644728`, thiscall, three stack arguments, returns
`-0x38(%ebp)`). §6.9 covers its first half — image selection. This is the second
half, the part §7 listed as "everything upstream: … the sequence".

**Two commands do the whole job**, both sent through the §3 transport
`0x00644730(port, cmd, buf, len, sleep_ms)`, which returns **0 on success**
(`-0x2c(%ebp)`, initialised to 1 at `0x64473a`, cleared only at `0x644b64` and
`0x64500f`):

| step | command | frame | length | sleep arg | site |
|---|---|---|---|---|---|
| start | `0x08` | the descriptor below | `0x3b` = 59 | `0` | `0x644598` |
| data × N | `0x09` | index + payload | `0x3b` = 59 | `0x1f4` = 500 | `0x64469c` |

Both dispatch to case 0 in §4's table, so **neither arms the deferred `0x15`
reply**; each frame is a single request/reply/ack.

### Chunk arithmetic

Fixed at `0x64442e`–`0x644458`, using `-0xc(%ebp)` = `0x39` = **57 payload bytes
per frame** and `-0x24(%ebp)` = `0x8000` = the image size:

```
remainder = 0x8000 % 57      ; -0x30(%ebp)
frames    = 0x8000 / 57      ; -0x1c(%ebp)
if remainder > 0: frames++
```

`32768 = 574 × 57 + 50`, so **575 frames**, the last carrying 50 bytes. The
progress bar range is `frames × obj+0xb0` (`0x644460`).

57 is consistent with the report size derived twice in §3: 65 − 8 header bytes.

### The start frame (command `0x08`), 59 bytes at `obj+0x254`

Zeroed first (`0x644483`), then filled field by field:

| offset | width | value | site |
|---|---|---|---|
| 0 | 1 | `0x01` | `0x644496` |
| 1 | 4 | total payload size (`0x8000`) | `0x64449e` |
| 3 | 4 | frame count | `0x6444bf` |
| 7 | 1 | bytes per frame (`0x39` = 57) | `0x6444e0` |
| 8 | 2 | **the per-image 16-bit constant** | `0x6444f5` |
| 11 | 1 | `0x01`, only if an image was selected | `0x64451e` |
| 12 | 1 | image version major (`obj+0x24c`) | `0x64453f` |
| 13 | 1 | image version minor (`obj+0x24d`) | `0x64454c` |
| 14 | 1 | image version patch (`obj+0x24e`) | `0x644567` |

**Note the overlap at offsets 3 and 4**, which is in the bytes and not a
transcription slip: the 4-byte size is written at 1..4 and the 4-byte frame count
at 3..6, so the count's low half overwrites the size's high half. It is harmless
for these magnitudes — `0x8000` and 575 both fit in 16 bits, so the frame carries
size in 1..2, count in 3..4 and zeros in 5..6 — and it would stop being harmless
for an image of 64 KiB or more. Recorded because a reimplementation that wrote
non-overlapping fields would produce a *different* frame from the vendor's.

**This is where the per-image 16-bit constant goes.** §6.9 established the three
values (`0x8298`, `0x08b4`, `0xa59c`) and ruled out three CRC-16s and an additive
sum over the raw blob. Its *role* is now derived even though its meaning is not:
it is **transmitted to the device in the start frame**, at offset 8, 16-bit
little-endian, once per flash. The host never computes it and never checks it.
So whatever it is, the device is the only thing that can validate it — and `[G]`
whether it does.

### The data frames (command `0x09`)

```
for i in 0 .. frames-1:
    memset(buf, 0, 59)                                 ; 0x6445d7
    buf[0..1] = i                    (16-bit)          ; 0x6445ec
    if last frame and remainder > 0:
        memcpy(buf+2, image + off, remainder)          ; 0x64461f
    else:
        memcpy(buf+2, image + off, 57)                 ; 0x64464a
    off += 57                                          ; 0x644674
    send(0x09, buf, 59, sleep=500)                     ; 0x64469c
```

`off` is `-0x14(%ebp)` and is **16-bit** (`movw` at `0x64467e`), which is fine at
32,768 but wraps at 65,536 — a second size-dependent limit, matching the one
above.

### Failure handling, and one asymmetry worth naming

- **A data-frame failure aborts.** Nonzero return → `0x6446bf` → `-0x38 = 0` →
  the function returns 0. (The intervening test `index == frames` at `0x6446c3`
  can never be true: the loop guard at `0x6445ca` exits when `index >= frames`,
  so inside the body `index ≤ frames-1`. That branch is unreachable.)
- **A start-command failure does not.** Nonzero return at `0x64459f` jumps
  straight to the epilogue at `0x6446e8` with `-0x38` still holding its initial
  `1` from `0x6441c9`, so the function **returns the same value a completed flash
  returns**, having sent no data frame at all. The caller `0x6479a0` stores that
  return into global `0x72a5a4` (`0x647a42`) and carries on.

We do not copy this. `CLAUDE.md` §4.2 already forbids treating a transport
success as a device success; this is the mirror-image defect — treating a
transport *failure* as a completed operation — and it is the same root cause,
a status flag that defaults to the good value and is only ever cleared.

### Dead code, recorded because it is easy to mistake for the live path

`-0x1(%ebp)` is written five times (`0x6441d7`, `0x644237`, `0x64423d`,
`0x6442bc`, `0x644327`, `0x64438a`) and **never read**; its address is never
taken. The first two writes are the interesting ones:

```
64420a: movzbl 0x2d8(%edx), %eax   ; device version major
644211: cmpl   $0x7, %eax
644214: jl     0x64423d
644219: movzbl 0x2d9(%ecx), %edx   ; minor
644220: cmpl   $0x3, %edx
644223: jl     0x64423d
644228: movzbl 0x2da(%eax), %ecx   ; patch
64422f: cmpl   $0xa5, %ecx
644235: jl     0x64423d
644237: movb   $0x38, -0x1(%ebp)   ; 56
64423d: movb   $0x6,  -0x1(%ebp)   ; 6
```

A payload size of 56 or 6 chosen from the device version — superseded by the
constant 57 in `-0xc(%ebp)` and left in place. Anyone reading this function
looking for "how the chunk size is decided" will find this block first and it is
the wrong answer. Note also that its thresholds (`7.3.165`) and §3.2's
(`1.3.7`) are different scales on the same three bytes, which is a reason to
distrust either as a considered version policy.

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

## 6.9 Product selection, firmware images and the version triple  [D-X]

Derived 2026-09-04 from `FUN_006441c0`, a device-closure member. It tries three
product branches in order, each self-contained:

```
644292  pushl $0x1903            ; PID
644297  pushl $0x3367            ; VID
64429c  movl  -0x18(%ebp), %ecx  ; context
64429f  addl  $0x2dc, %ecx       ; -> its HID sub-object
6442a6  calll 0x647660           ; predicate, NOT an open -- see below
...  on success:
6442b5  movl  $0x8000,   -0x24(%ebp)   ; 32,768
6442bc  movb  $0x38,     -0x1(%ebp)    ; 56
6442c0  movb  $0x1,      -0x2(%ebp)
6442c4  movb  $0x1,      -0x3(%ebp)
6442c8  movl  $0xbadf00d,-0x28(%ebp)   ; magic
6442cf  movl  $0x716a88, -0x20(%ebp)   ; -> a .data blob
6442d9  movb  $0x1,  0x24c(%eax)       ; version major
6442e3  movb  $0x9,  0x24d(%ecx)       ; version minor
6442ed  movb  $0x2e, 0x24e(%edx)       ; version patch = 46
6442f4  movl  $0x8298, %eax
6442f9  movw  %ax, -0x10(%ebp)         ; a 16-bit per-image constant
```

then the same block for **PID `0x1905`** with blob `0x706a88` and constant
`0x08b4`, then a third branch gated on `0x493(%ecx) == 1` **and**
`0x38c(%eax) != 0`, with blob `0x70ea88` and version minor `8`.

**`0x647660` is a predicate on the already-open device, not an open.**
Corrected 2026-09-04; the earlier "open(ctx, vid, pid)" reading was wrong and it
mattered. The body (`0x647660`–`0x647781`) formats `vid_%04x` and `pid_%04x` from
its two arguments (`0x647720`, `0x647738`, format strings at `0x6ba1b4` /
`0x6ba1c0`), fetches the **currently open** device's path through the import at
`*0x66f534` into a `0x104`-wide buffer, and returns 1 iff the path contains both
substrings (`0x403010`, the case-insensitive finder; `0x64776e` sets `al = 1`,
`0x647772` clears it). It opens nothing and changes no state.

The consequence is the reassuring one and is worth stating because the wrong
reading suggests a hazard that is not there: since all three branches test the
*same* open device, the tool **cannot** pair one product's image with another
product's device by trying opens in order. If two branches both matched it would
be a real wrong-image path, but they cannot: a single device path cannot contain
two different `pid_` substrings.

**Device identity — resolves a §7 item.** VID **`0x3367`** (the same vendor id
the OP1 config tool matches on) with PIDs **`0x1903`** and **`0x1905`**, plus a
third gated branch. Note the identity comparison is *not* in `FUN_006473d0`,
which merely fills a struct from `HidD_GetAttributes`/`HidP_GetCaps`; matching
happens in the caller, which is why looking for it inside the HID wrapper found
nothing.

**The three-component version decode — resolves a §7 item.** It is three
consecutive bytes in the context object, `+0x24c` major, `+0x24d` minor,
`+0x24e` patch, written as plain integers. `1, 9, 0x2e` = **1.9.46**, matching
the tool's own filename; the third branch writes minor `8`.

**Firmware image location and size — resolves a §7 item.** The three pointers are
**contiguous, in `.data`, exactly `0x8000` = 32,768 bytes each**, back to back:

| blob VA | file offset | ends at | selected by |
|---|---|---|---|
| `0x706a88` | `0x304e88` | `0x70ea88` | PID `0x1905` |
| `0x70ea88` | `0x30ce88` | `0x716a88` | the `+0x493` branch |
| `0x716a88` | `0x314e88` | `0x71ea88` | PID `0x1903` |

Each blob's last byte abuts the next blob's first, verified by reading across the
boundaries, and UTF-16 `"Arial"` follows the last one — so the array is exactly
three images and the run is bounded on both sides. Entropy **7.73 bits/byte**
with 149–160 zero bytes in 32,768: **encrypted or compressed**, on the same
entropy standard §4 of `notes/updater-protocol.md` applies to the OP1's `FWFILE`
blobs (7.94). Blobs 1 and 3 share an identical 16-byte prefix while differing
overall — the same prefix-collision the OP1 blobs show, and the same warning
applies: compare whole-blob hashes, never prefixes.

**What the 16-bit constant is NOT.** `0x8298` (blob `0x716a88`) and `0x08b4`
(blob `0x706a88`) look like per-image checksums and the shoe fits — §3.2's
machinery is CRC-16. **Tested and it does not match.** Over the raw 32,768
bytes: additive sum `0xb62a`/`0xb82d`, CRC-16-CCITT `0x7b54`/`0x73af`, CRC-16-IBM
`0x0383`/`0x8ae7`. None equals the expected value for either blob. So either the
checksum is over the *decrypted* image, or it is not a checksum at all. Recorded
as `[G]` and unresolved rather than fitted to a fourth polynomial until something
matched — which is how a wrong constant gets into a flasher.

**`obj+0x493`** gates the third branch. It sits one byte after **`obj+0x492`**,
the checksum-enforcement gate §3.2 calls the most load-bearing unknown in this
file, so the two are almost certainly fields of one small per-product descriptor.
That is a lead, not a finding.

## 7. Not yet derived
- The 45 commands' meanings beyond `0x13` (§4.1), and which the flash sequence uses.
- The runtime CRC-16 table generator and its polynomial.
- Everything upstream: enumeration, the upgrade button handler, the sequence.
- **Which of the 45 commands arm the deferred `0x15` reply** (§3.1). The dispatch
  class field selects it; the per-command values are not enumerated.
- **The meaning of `obj+0x492`**, the byte that gates checksum enforcement (§3.2).
  Currently `[G]`. This is the most load-bearing unknown left in this file.

*Resolved since the first draft:* the second `HidD_SetFeature` site at `0x00644ff7`
is the **ACK frame** (§3.1), not a separate command path.

*Resolved 2026-09-04 (§6.9):* device identity (VID `0x3367`, PIDs `0x1903` /
`0x1905`), the three-component version decode (`obj+0x24c/0x24d/0x24e`), and the
firmware images' location and size (three contiguous 32,768-byte `.data` blobs at
`0x706a88`, `0x70ea88`, `0x716a88`, entropy 7.73). The per-image 16-bit constant
was tested against three CRC-16s and an additive sum and matches none.
