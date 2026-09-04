# Firmware updater — derived protocol

Scope: `Endgame Gear OP1 8k v2 Firmware Updater` 1.04 / 1.06 / 1.07 / 1.10.
Every claim carries a `CLAUDE.md` §1.2 tag. `[D]` cites a VA in a named binary.

**Nothing here has touched hardware.** There is no `[O]` in this file yet, and
until there is, §1.3 forbids acting on anything below that is `[G]`.

Addresses are VAs in **updater 1.10** unless stated. Image base `0x400000`.

## 0. Which binaries these addresses belong to

`.text` is **byte-identical** across updaters 1.06, 1.07 and 1.10 (sha256
`4a2b50d5…`, 1,154,048 bytes, verified by direct section compare) [D]. So there
are only **two** distinct updater code bases:

| Code base | Builds | Notes |
| --- | --- | --- |
| A | 1.06, 1.07, 1.10 | identical `.text`; differ only in `.rdata` (build stamp, PDB name) and `.rsrc` (firmware images) |
| B | 1.04 | different toolchain and layout |

1.07 vs 1.06 differ **only** in the debug timestamp, PDB GUID and the PDB
filename character `6`→`7`; the functional change is entirely in the `FWFILE`
resources [D]. Cross-version diffing therefore yields exactly one comparison for
the updaters: **B vs A**. Stated plainly because §6 forbids silent sampling.

## 0.1 Exhaustive non-code diff of all four updaters [D]

Done 2026-09-03 because "the code is identical" is not the same claim as "the
files are identical", and 1.06/1.07 had been getting the former as a substitute
for the latter.

| section | 1.04 | 1.06 | 1.07 | 1.10 |
| --- | --- | --- | --- | --- |
| `.text` | `9e29da4b…` | `4a2b50d5…` | `4a2b50d5…` | `4a2b50d5…` |
| `.rdata` | `1cd566e8…` | `7726c0ce…` | `99295bed…` | `45bfd758…` |
| `.data` | `c7e96ccd…` | `cb326ae0…` | `cb326ae0…` | `cb326ae0…` |
| `.rsrc` | `534ab47b…` | `cac3359c…` | `d83eb8b7…` | `b0e0eff9…` |
| `.reloc` | `4a341677…` | `118af942…` | `118af942…` | `82642f1b…` |

**`.data` is byte-identical across 1.06/1.07/1.10 as well as `.text`** — newly
verified, and not previously claimed. 1.04 has a sixth section, **`.fptable`**
(VA `0x5c7000`, `0x80` bytes, zero-filled in the file), which the others lack.

`.rdata`, **1.06 vs 1.07: exactly 20 bytes differ, in 3 runs** — `0x51bcf4` (3 B,
debug signature), `0x544ccc` (16 B, PDB GUID), and `0x544d4d` (1 B, `'6'`→`'7'`).
That is the whole difference between those two builds. The earlier informal claim
is now exact.

`.rdata`, 1.06 vs 1.10: 16,998 bytes in 6,273 runs — but almost all are single
bytes shifting by −4, the low bytes of ~1,600 pointers into the string pool,
because one string changed length by 4 bytes. `.reloc` differs for 1.10 for the
same reason.

**The format string at `0x005443b0` (`L"%x"`), on which the whole version decode
in §1 rests, is byte-identical across 1.06/1.07/1.10** — checked explicitly,
because it is the one `.rdata` address the protocol depends on.

### 0.1.1 The 1.10 build's project identity is a *different mouse* [D]
The string that changed length is the CodeView PDB path:

```
1.04  G:\project\K26\K26 firmware update V0.11_package_k26_1.23\Release\OP1 8k firmware updater.pdb
1.06  G:\project\K26\K26 firmware update V0.12_package_k40_1.04\Release\Endgame Gear OP1 8k v2 Firmware Updater 1.06.pdb
1.07  G:\project\K26\K26 firmware update V0.12_package_k40_1.04\Release\Endgame Gear OP1 8k v2 Firmware Updater 1.07.pdb
1.10  G:\project\K26\K26 firmware update V0.12_package_k40_1.04\Release\Endgame Gear EL1 8k Firmware Updater 0.01.pdb
```

**The binary Endgame distributes as "OP1 8k v2 Firmware Updater 1.10" was linked
from a project named "Endgame Gear EL1 8k Firmware Updater 0.01".** And 1.10 is
the same release that gained a sixth `FWFILE` resource, 143, which 1.04/1.06/1.07
do not have (§4).

**Do not over-read this.** Every other identifier in 1.10 says OP1:
`FileDescription`, `InternalName`, `OriginalFilename` and `ProductName` are all
`OP1 8k v2 Firmware Updater`, `FileVersion` is `1.1.0.0`, and the dialog caption
is `Endgame Gear OP1 8k v2 Firmware Updater`. The literal `EL1` occurs **exactly
once in the entire binary**, in that PDB path. The overwhelmingly likely reading
is a copied project directory, not a mis-shipped image. Recorded as `[D]` for
what the bytes say and `[G]` for why.

**What it does establish, and this is the part that matters:** Endgame's build
process permits one product's updater to be linked from another product's
project. That is `CLAUDE.md` §1.4's hazard demonstrated on our own target rather
than argued from principle, and it means **"which `FWFILE` holds the OP1 8k v2
image" is an open question, not a settled one.** The tool hardcodes 140 and says
nothing about what 140 contains.

**It also refines the code-identity gate.** 1.10's `.text` matches 1.06/1.07
byte for byte, so a gate that checks only `.text` would accept 1.10, extract 140,
and never notice that the build provenance changed and a new blob appeared.
**Code identity is necessary but not sufficient.** A resource-set fingerprint —
the exact list of `FWFILE` names and their sizes — has to be part of the gate too.

## 1. Device identity

`FUN_00401000` @ **`0x00401000`** — the only enumeration routine in the tool.

- `HidD_GetHidGuid` → `SetupDiGetClassDevsW(guid, NULL, NULL, 0x12)` [D]
  (`0x12` = `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE`).
- `CreateFileW(path, 0xC0000000, 3, NULL, 3, 0, NULL)` [D]
  (`GENERIC_READ|GENERIC_WRITE`, `FILE_SHARE_READ|WRITE`, `OPEN_EXISTING`).
- `HidD_GetAttributes` into a `HIDD_ATTRIBUTES` (`Size=0xC`), then:
  - **VendorID must equal `0x3367`** [D] — literal compare at `0x00401000`.
  - **ProductID must equal the function's argument** [D] — the PID is a
    parameter, not a constant, which is how the tool searches for two identities.
- `HidD_GetPreparsedData` + `HidP_GetCaps`, then two more compares [D]:
  - `caps.Usage == 0x0002` and `caps.UsagePage == 0xFF01`
    (the compare is against `-0xff` on a signed short = `0xFF01`).
- On match it stores `caps`-adjacent `VersionNumber` to `DAT_0056a0f4`, sets the
  found flag `DAT_0056a0f8 = 1`, keeps the handle in `DAT_0056a174`, and saves
  the device path.

### Product IDs [D]
| PID | Meaning | Evidence |
| --- | --- | --- |
| `0x1978` | application / normal mode | `FUN_00401000(0x1978)` at `0x00403600`, `0x00403750` |
| `0x1977` | bootloader mode | `FUN_00401000(0x1977)` at `0x00403600`, `0x00403750`, and the failure strings around it say `bldr` |

**The device does re-enumerate under a different USB identity when it enters the
bootloader**, and the tool handles it by re-running the whole SetupDi
enumeration against the other PID [D].

### Firmware version display — it is BCD, and the decode is not division [D]
`FUN_004011f0` @ `0x004011f0`, instruction by instruction (`0x401245`–`0x401263`):

```
movl 0x56a0f4, %ecx        ; the HID VersionNumber captured by FUN_00401000
pushl %ecx
pushl $0x5443b0            ; the format string, which is  L"%x"
call  0x4014a0             ; CStringT::Format
call  0x4f7965             ; -> __wtol  (decimal parse)
movzwl %ax, %edi           ; truncate to 16 bits
```
and the caller divides by `100.0` for `L"Mouse firmware current version %.2f"`.

So the decode is **format `bcdDevice` in hexadecimal, read those digits back as
a decimal number, divide by 100**. That is a BCD round trip, not arithmetic:

| `bcdDevice` | `"%x"` | `__wtol` | displayed |
| --- | --- | --- | --- |
| `0x0140` | `"140"` | 140 | 1.40 |
| `0x0143` | `"143"` | 143 | 1.43 |

Getting this wrong is easy and consequential. Treating the field as a plain
integer and dividing by 100 turns `0x0143` into 3.23, because `0x143` is 323.
**`bcdDevice` is packed BCD; decode it digit-wise.**

The format string at `0x005443b0` really is `L"%x"` — verified in the file at
offset `0x1433b0` [D].

The tool never gates on the version; it only displays it [D].

### 1.1 Exactly two product IDs are ever searched for [D]
An exhaustive `E8`-rel32 scan of `.text` finds **exactly 9 direct callers** of
the enumerator `FUN_00401000` — `0x403608`, `0x403649`, `0x403696`, `0x4036c9`,
`0x403805`, `0x403836`, `0x4038d6`, `0x403c81`, `0x403c97` — and at all nine the
pushed argument is a literal, either `0x1978` or `0x1977`. There is no path that
computes a PID, reads one from a resource, or takes one from a list.

That matters for §1.4 of `CLAUDE.md` in the same way the `FWFILE` constant does:
the device-identity search space is closed at two values, both compile-time
constants, and ours should be too.

The `VendorID` test is a genuine 16-bit compare (`0x4010fa movl $0x3367,%eax`,
`0x4010ff cmpw %ax,-0xc(%ebp)`). The `ProductID` test is **32-bit**:
`0x401105 movzwl -0xa(%ebp),%ecx` then `0x401109 cmpl 0x8(%ebp),%ecx` — the
zero-extended 16-bit PID against the full 32-bit stack argument, so any argument
with a bit set above bit 15 could never match. Harmless with the two literals it
actually uses; recorded because a reimplementation that widened the parameter
would inherit a silent never-match.

### 1.2 Two resource leaks in the enumerator — do not copy [D]
`FUN_00401000` stores every `CreateFileW` result to the handle global
`0x0056a174` (`0x4010e8`) as it walks the interface list, and calls no
`CloseHandle` anywhere in its body — so every non-matching candidate's handle is
leaked to the caller's bookkeeping. Separately, the **success** path
(`0x40118a` → `0x4011cc` → `0x4011e9 retl`) returns without calling
`SetupDiDestroyDeviceInfoList`; only the not-found exit at `0x40116b` destroys
the device-info set. The same shape exists in code base B at `0x00401bc0` [D],
so it is the vendor's habit, not a one-off.

Neither leak affects the protocol. Both are noted because §4.2 has us
re-enumerating repeatedly across a bootloader transition, which is exactly the
loop where leaking a handle per pass would matter for us and evidently did not
for a tool that exits soon afterwards.

## 2. Transport

Two HID **feature report** IDs, distinguished by length [D]:

| Report ID | Total buffer passed to HID | Payload |
| --- | --- | --- |
| `0xA0` | **`0x411` = 1041 bytes** | 1040 after the ID |
| `0xA1` | **`0x40` = 64 bytes** | 63 after the ID |

Byte 0 of every buffer is the report ID. Byte 1 is the **command** on the way
out and a **status** on the way back [D].

### `FUN_004012a0` @ `0x004012a0` — SetFeature with retry [D]
`HidD_SetFeature(DAT_0056a174, buf, len)`. On failure, `GetLastError()`; retries
only for `0x15`, `0x17`, `0x1D`, `0x57`, `0x65B`. Retry loop runs `i = 1`
while `i < 4`, i.e. **at most 3 retries (4 attempts total)**, `Sleep(10)` between.

### `FUN_00401330` @ `0x00401330` — GetFeature with retry and busy-poll [D]
`HidD_GetFeature(DAT_0056a174, buf, len)`.
- **On transport failure**: same five error codes, `i = 1` while `i < 4`
  (**4 attempts total**), `Sleep(50)` between.
- **On transport success**, it inspects `buf[1]`:
  - `buf[1] == 0x01` → done, success.
  - `buf[1] == 0x04` → **device busy**; re-issue GetFeature in a loop with
    `Sleep(d)` where `d` starts at **0** and increases by **100 ms** each pass
    while `d < 2000` — so 0, 100, 200 … 1900 ms, **20 passes, ~19 s total**.

**Status byte values seen: `0x01` = ready/OK, `0x04` = busy** [D]. No other
value is handled anywhere in the tool.

## 3. Commands — byte maps

**Everything in this section was read from raw disassembly, not from the
decompiler.** That distinction is not pedantry: Ghidra's output for these
functions is wrong by omission in at least three places, and each omission is a
byte that goes to the device. Specifically it drops the 32-bit register argument
`FUN_00401890` writes to `buf[17..20]`, and it drops the block index
`FUN_00401980` writes to `buf[2..3]` entirely.

In every builder the true buffer base is the pointer handed to `memset`, which
is **not** the lowest local Ghidra names. All offsets below are from that base,
with the report ID at `[0]`.

### 3.1 Enter bootloader — built in `FUN_00403750` @ `0x00403750` [D]
```
[0] = 0xA1      [1] = 0x3A      [2..4] = 0
[5] = 0x5A      [6] = 0xA5      [7] = 0x32      rest 0     len 0x40
```
Sent up to 9 times (`i = 1..9`, `Sleep(i*2)` between), each followed by a 64-byte
`0xA1` read; any successful read ends the loop [D].

### 3.2 Bootloader start — `FUN_00401890` @ `0x00401890` [D]
Base `-0x458(%ebp)` (`memset` at `0x4018bc`), length `0x411`.
```
[0] = 0xA0   [1] = 0x03   [2..3] = 0   [4..7] = 0
[16]     = block_count & 0xFF                    ; store at 0x4018c4
[17..20] = whole-image checksum, 32-bit LE       ; stores at 0x4018ce..0x40190f
rest 0
```
Arguments at the call site `0x4039a0`–`0x4039ae` [D]:
`stack = (byte)*(dword*)(dlg+0x21c)` = block count; `ECX = *(dword*)(dlg+0x25a20)`
= the whole-image checksum computed by `FUN_00403580`.

**So the start command declares up front how many blocks are coming and what
the total checksum must be.** Writing zeros there, which is what a
decompiler-only reading produces, would be wrong.

After sending: `Sleep(3000)`, then a 64-byte `0xA1` read; the function returns
that response's `[1]`.

### 3.3 Write one block — `FUN_00401980` @ `0x00401980` [D]
Base `-0x458(%ebp)` (`memset` at `0x4019b9`), length `0x411`.
```
[0] = 0xA0   [1] = 0x06                          ; movw $0x6a0 at 0x4019d5
[2] = block_index & 0xFF                         ; 0x4019be
[3] = (block_index >> 8) & 0xFF                  ; 0x4019de
[4] = payload_sum & 0xFF                         ; 0x401a54
[5] = (payload_sum >> 8) & 0xFF                  ; 0x401a5a
[6..15] = 0
[16..1039] = 1024 payload bytes                  ; rep movsl, 0x100 dwords, 0x4019ee
```
`ECX` = block index, `EDX` = source pointer, stack arg = the report ID used for
the follow-up read [D].

`payload_sum` is a **16-bit** sum of `[0x10..0x40F]` — exactly the 1024 payload
bytes — accumulated over the loop at `0x401a00`–`0x401a34` [D].

After sending: `Sleep(50)`, then a **64-byte** read whose report ID is the stack
argument; returns that response's `[1]` [D].

### 3.4 Read one block back — `FUN_00401ad0` @ `0x00401ad0` [D]
Base `-0x418(%ebp)` (`memset` at `0x401af7`), length `0x411`.
```
[0] = 0xA0   [1] = 0x07                          ; movw $0x7a0 at 0x401b0b
[2] = block_index & 0xFF                         ; 0x401b14  -- ONE byte only
[3..7] = 0
```
**Asymmetry worth carrying**: the write command puts a 16-bit index at `[2..3]`,
the read command puts an 8-bit index at `[2]` [D]. With the indices this tool
actually uses (`0x34..0x74`) the difference never shows, but it is real.

`ECX` = destination buffer. After `Sleep(50)` it reads a **1041-byte** `0xA0`
response and, if `resp[1] == 0x01`, copies all 1041 bytes to `ECX`
(`rep movsl` 0x104 + `movsb`, `0x401b96`) [D].

### 3.4a Return values are not always device status bytes [D]
Three of the send functions return a `uint8` that the caller compares against
`0x01`. On their failure paths that byte does **not** come from the device — it
comes from the request buffer or from a literal. Established by disassembly on
2026-09-03 while reconciling the adversarial verify pass.

| function | on success | `HidD_SetFeature` failed | `HidD_GetFeature` failed |
| --- | --- | --- | --- |
| `FUN_00401890` `A0 03` start | `resp[1]` (`0x40195b`) | `0x02` (`movb $0x2,%al`, `0x40192a`) | `req[1]` = **`0x03`** (`0x401960`, reads `-0x459(%ebp)` = base `-0x458` + 1) |
| `FUN_00401980` `A0 06` write | `resp[1]` (`0x401aac`) | `0x02` (`0x401949` region) | `req[1]` = **`0x06`** (`0x401ab1`, same idiom) |
| `FUN_00401ad0` `A0 07` read | `resp[1]` (`0x401b7f`) | **`0x02`** (`movb $0x2,%al`, `0x401b3e`) | `resp[1]`, buffer left zeroed ⇒ `0x00` |

Every one of these values is `!= 0x01`, so all three **fail closed** and no
caller can mistake a transport failure for a device success [D].

Two consequences for our implementation. First, a returned `0x02`, `0x03` or
`0x06` is a *transport* failure, not a device status — §2 lists only `0x01`
ready and `0x04` busy as device values, and conflating the two would make a
`0x06` look like an unknown device state. Ours should return a distinct error
type rather than reusing the status byte, precisely because the vendor's
overloading is what makes its own repair loop misbehave (§5.6). Second, `[G]`:
the choice to return `req[1]` looks like a bug rather than a design — the
command byte is a meaningless thing to hand back — but it is harmless here and
we must not "fix" it in a way that changes which values are treated as success.

### 3.5 Whole-image checksum — `FUN_00401bb0` @ `0x00401bb0` [D]
Base `-0x44(%ebp)`, length `0x40`.
```
[0] = 0xA1   [1] = 0x08                          ; movw $0x8a1 at 0x401be5
[2] = 0x34                                       ; 0x401beb -- first block
[3] = last_block                                 ; 0x401bef -- from arg 2
[4..5] = 0
```
Call site `0x403b6c`–`0x403b87`: `last_block = (byte)block_count + 0x33` [D].
With 65 blocks that is `0x74`. So the command asks the device to checksum the
**block range `0x34 … 0x74` inclusive — 65 blocks**, which is the whole image.

Response: `Sleep(100)`, 64-byte `0xA1` read, require `resp[1] == 0x01`, then the
result is assembled at `0x401c55`–`0x401c7a` as
`resp[19]<<24 | resp[18]<<16 | resp[17]<<8 | resp[16]` — a **little-endian
32-bit value at `resp[16..19]`** [D].

### 3.6 Bootloader complete — built inline in `FUN_00403960` [D]
```
[0] = 0xA1   [1] = 0x09   [2..5] = 0     len 0x40
```

### 3.7 Post-success command — built inline in `FUN_00403960` [D]
```
[0] = 0xA1   [1] = 0x13   [2..5] = 0     len 0x40
```
Sent only after the update has already succeeded, then `Sleep(900)` and one
64-byte `0xA1` read. **What it does is not derivable from the tool.** `[G]`.

### 3.7a The command table above is complete, and that is checkable

`FUN_004012a0` is the **only** function in the binary that calls
`HidD_SetFeature` (§7). Disassembling the whole `.text` — all 1,154,048 bytes,
`0x00401000`–`0x0051b000` — finds exactly **seven** call sites for it, and
exactly seven for the `HidD_GetFeature` wrapper `FUN_00401330` [D]:

| call site | command |
| --- | --- |
| `0x0040190f` | `A0 03` bootloader start (§3.2) |
| `0x00401a60` | `A0 06` write block (§3.3) |
| `0x00401b26` | `A0 07` read block (§3.4) |
| `0x00401bf6` | `A1 08 34` whole-image checksum (§3.5) |
| `0x004037a1` | `A1 3A` enter bootloader (§3.1) |
| `0x00403bf2` | `A1 09` complete (§3.6) |
| `0x00403ddd` | `A1 13` post-success (§3.7) |

Independently, scanning every immediate stored to a stack slot across the whole
vendor band yields exactly these report/command pairs and no others — `0x3a0`,
`0x6a0`, `0x7a0`, `0x3aa1` with `0x32a55a00`, `0x8a1` with `0x34`, `0x9a1`,
`0x13a1` — plus the four bare report IDs used for reads (`0xA1` three times,
`0xA0` once) and two Windows structure sizes (`0x1c` for
`SP_DEVICE_INTERFACE_DATA`, `0xC` for `HIDD_ATTRIBUTES`) [D].

**So the updater knows seven commands and this table is all of them.** Any
eighth command the device may implement is invisible here, and would have to
come from somewhere other than these binaries.

### 3.7b Responses — every byte the tool actually reads

The tool never reads a response field this table does not list. Verified by
looking at each read buffer's stack slots in the disassembly [D].

| after | read with | length | bytes the tool reads | used for |
| --- | --- | --- | --- | --- |
| `A1 3A` enter bootloader | report `0xA1` | `0x40` | none — only whether `HidD_GetFeature` succeeded | breaks the 9-attempt retry loop (`0x403793`) |
| `A0 03` start | report `0xA1` | `0x40` | `[1]` | returned; caller requires `== 0x01` (`0x40195b`) |
| `A0 06` write block | report = caller's arg | `0x40` | `[1]` | returned; caller requires `== 0x01` (`0x401aac`) |
| `A0 07` read block | report `0xA0` | `0x411` | `[1]`, `[6..7]`, `[16..1039]` | status; **LE16 per-block checksum**; the 1024 bytes to compare (`0x401b7f`, `0x401d90`, `0x401dc0`) |
| `A1 08 34 74` checksum | report `0xA1` | `0x40` | `[1]`, `[16..19]` | status; **LE32 whole-image checksum** (`0x401c49`, `0x401c55`–`0x401c7a`) |
| `A1 09` complete | report `0xA1` | `0x40` | `[1]` | requires `== 0x01` before re-enumerating (`0x403c2c` region) |
| `A1 13` post-success | report `0xA1` | `0x40` | **none** | the return value is discarded; fire and forget |

Two things follow that matter for our implementation:

1. **`[1]` is the only status the protocol has**, and the tool only ever
   distinguishes `0x01` (ready) and `0x04` (busy). Every other value is treated
   as "not ready" and simply retried or failed on. There is no error code, no
   reason byte, nothing to log. Whatever the device says when it is unhappy, the
   vendor tool cannot tell you. Ours should capture the whole response on any
   non-`0x01` status so a failure is at least diagnosable.
2. **The `A0 07` read-back is the only way to get data out of the device**, and
   it returns the payload at `[16]`, the same offset the write command puts it.
   That is what makes `CLAUDE.md` §4.4 stage 3 — flash read-back with zero
   writes — possible.

### 3.8 Block numbering — the single most important derived fact
The flash loop at `0x403a70`–`0x403b5f` computes the block index as
**`i + 0x34`** for `i = 0 … block_count-1` [D]:
- `0x403a8b`: `leal 0x34(%ebx), %ecx` → `ECX` for the write
- `0x403aff`: `leal 0x34(%ebx), %eax` → argument for the verify

With 65 blocks the indices run **`0x34` to `0x74` inclusive**. The checksum
command's range (§3.5) independently confirms the same two endpoints, derived
from a different expression at a different address.

So the application region the updater writes begins at block `0x34` = 52. If the
device's block size is the 1024 bytes the protocol uses throughout, that is byte
offset `0xD000`, running to `0x1D400`. The `0x400`-byte block size is `[D]`; the
mapping from block number to a flash address is **`[G]`** and must not be
assumed.

## 4. Firmware image — §1.4

`FUN_00403200` @ **`0x00403200`**:

```
FindResourceW(NULL, (LPCWSTR)0x8c, L"FWFILE")
```

**Resource type `FWFILE`, name `0x8c` = 140, hardcoded** [D]. Not a variable,
not selected at runtime, not read from a list.

The same constant appears in the **other** code base: updater 1.04,
`FUN_00404330` @ `0x00404330`, `FindResourceW(NULL,(LPCWSTR)0x8c,L"FWFILE")`
[D]. Two independently compiled builds agree.

Then [D]:
- `block_count = SizeofResource() >> 10` (i.e. `size / 1024`), stored at `+0x21c`.
- remainder `size - block_count*1024`; if non-zero, the tail is copied into one
  more zero-filled 1024-byte block and `block_count` is incremented.
  For the shipped image `65536 + 1024 = 66560` bytes, so `block_count = 65`
  **exactly and the remainder is 0** — the partial-block path is never taken
  with the resource this tool ships.
- The resource is copied into the dialog's buffer at `+0x220`
  **byte for byte, with no transform of any kind** — the copy loop is four
  plain byte moves per iteration. **There is no decryption or decompression in
  the updater.** The `FWFILE` blobs measure H≈7.94 bits/byte, so whatever
  encryption exists is the device's problem, not the host's [D].
- `FUN_00403580()` computes the whole-image checksum into `+0x25a20`.

Failure string: `L"Loading firmware file failed"`.

**Which `FWFILE` belongs to which product is still not derived.** The tool loads
140 and that is all the code says.

There is a tempting correlation, and it is recorded here as `[G]` precisely so
it does not quietly harden into a fact. The six resource names — 133, 135, 137,
140, 142, 143 — read as firmware versions 1.33 … 1.43 under exactly the BCD
convention §1 shows the tool using for `bcdDevice`, and the tool loads 140,
which would be v1.40.

**But that reading has a problem**: the bytes of `FWFILE` 140 are different in
1.06, 1.07 and 1.10 (§0). A resource whose name is a firmware version should not
change contents between releases. So either the names are product codes and the
resemblance to versions is coincidence, or the vendor re-cuts a version slot in
place. Nothing in either binary decides it. **Do not act on either reading.**

## 5. Sequences

### 5.1 Entry point — `FUN_004033b0` @ `0x004033b0` (the Update button) [D]
1. Return immediately if the busy flag at `+0x25a38` is set.
2. Disable the button (`+0x130`), clear the status label (`+0x1a4`), progress 0.
3. `FUN_00403200()` — load `FWFILE` 140. Fail → `L"Read firmware file failed"`.
4. `FUN_00403600(&mode)` — find the device. Fail → `L"Device not found"`.
5. `mode == 1` (found at PID `0x1978`, application): show the current version,
   then `AfxBeginThread(FUN_00402f00)` → `FUN_00403750` (enter bootloader) →
   `FUN_00403960`.
6. `mode == 2` (found at PID `0x1977`, already in the bootloader):
   `AfxBeginThread(FUN_00402f10)` → `FUN_00403960` **directly**.

Case 6 matters: **the tool will flash a device it finds already in the
bootloader, with no application-mode handshake first.**

### 5.2 Find the device — `FUN_00403600` @ `0x00403600` [D]
- Try PID `0x1978`. Found → `mode = 1`.
- Else `CloseHandle`, clear flags, retry PID `0x1978` with `Sleep(d)`,
  `d = 500, 1000, 1500` (`d < 2000`).
- Else alternate by loop parity between PID `0x1977` (→ `mode = 2`) and PID
  `0x1978` (→ `mode = 1`), starting at `Sleep(3000)` and **decreasing by 300 ms**
  per pass, 10 passes, closing the handle each time. Returns 0 / `mode = 0`
  if nothing is ever found.

### 5.3 Enter the bootloader — `FUN_00403750` @ `0x00403750` [D]
1. Up to 9 attempts (`i = 1 … 9`): send `0xA1/0x3A` + `5A A5 32`, `Sleep(i*2)`,
   then read a 64-byte `0xA1` report. Any successful read breaks out.
2. Poll `FUN_00401000(0x1977)` with `Sleep(d)`, `d = 0, 500, … < 10000`.
   **At exactly `d == 0x1194` (4500)** it gives up with
   `L"send bldr request failed"` / `L"Open bldr device request failed"`,
   re-enables the button and closes the handle.
3. On success `Sleep(1000)`, re-confirm PID `0x1977`, `Sleep(1000)`, then
   `FUN_00403960()`.

### 5.4 Flash — `FUN_00403960` @ `0x00403960` [D]
1. **Start**: `FUN_00401890()` = `0xA0/0x03`, then `Sleep(3000)`. Retried with
   `Sleep(d)`, `d = 100, 200, 300, 400, 500` (`d < 0x1f5`). All fail →
   `L"send bldr start request failed"`, close handle, **abort**.
2. Status → `L"send firmware block data..."`, `Sleep(500)`.
3. For each of the `+0x21c` blocks:
   - `FUN_00401980()` (`0xA0/0x06` write) retried while it does not return 1,
     `Sleep(200)` between, **giving up after 5 tries**.
   - `FUN_00401c90()` — read back and repair (§5.5). Not 1 →
     `L"send block data request failed"`, close handle, **abort**.
   - progress = `(100*i)/block_count + 2`, `SendMessageW(hwnd, 0x402, …)`,
     `Sleep(90)`.
4. `Sleep(200)`, then `FUN_00401bb0(&v)` = `0xA1/0x08/0x34` to read the device's
   whole-image checksum, compared against `+0x25a20`. Mismatch →
   `L"get all check sum error"`, **abort**.
5. **Complete** — loop at `0x403bc7`–`0x403c4c` [D]. Counter starts at 1.
   Each pass: build `A1 09` with `[4..7] = 0`, send 64 bytes, `Sleep(50)`; if the
   send succeeded, read a 64-byte `0xA1` report and require `resp[1] == 0x01`.
   On any failure `Sleep(counter)` — **1 ms, then 2, … up to 10** — and increment;
   the loop runs while `counter <= 10`, so **10 attempts**. These retry sleeps are
   milliseconds, not the growing hundreds used elsewhere; the decompiled C makes
   them look like the latter, which is wrong.
   All 10 fail → `L"send bldr complete request failed"`.
6. **Wait for the device to come back**, `0x403c7c`–`0x403cc3` [D]: one immediate
   `FUN_00401000(0x1978)`, then a loop with `Sleep(d)` where `d` starts at
   **800** and increases by **800** while `d <= 16000` — 800, 1600 … 16000, i.e.
   20 further attempts and about **136 s** of waiting in total. Still not found →
   `L"Update failed, try again"`.
7. On success: send `0xA1/0x13`, `Sleep(900)`, one `0xA1` read, then display
   `L"Update Succeed, current firmware version is V%.2f"`.
   **See §5.4a — "on success" is three gates, and one path skips this step
   while still reporting success.**

### 5.4a The post-flash `A1 13`, in full  [D]

`A1 13` is **factory reset** (§6, `notes/config-protocol.md` §2.4), so this step
is what makes a firmware update destroy the user's settings. Its gating therefore
matters, and "sent after a verified success" understated it.

**The frame.** Built and sent at `0x403dbc`–`0x403ddd`:
```
403dbc  pushl $0x40 ; leal -0x50(%ebp),%eax ; pushl $0 ; pushl %eax
403dc4  calll 0x4f83a0                  ; memset(buf, 0, 0x40)
403dcc  pushl $0x40                     ; length
403dce  pushl %ecx                      ; buffer
403dcf  movl  $0x13a1,-0x50(%ebp)       ; A1 13 00 00
403dd6  movl  $0x0,-0x4c(%ebp)          ; (redundant; memset already zeroed)
403ddd  calll 0x4012a0                  ; the HidD_SetFeature wrapper
```
64 bytes: `A1 13 00 00` then 60 zeros. **Byte-for-byte identical to the config
tool's Factory Reset frame** — the same 7-byte encoding `c7 45 b0 a1 13 00 00`
builds it in both binaries (fw110 `0x403dcf`, cfg107 `0x40479f`), to the same
VID/PID and the same HID collection.

**Three gates, not one.** `A1 13` fires only after all of:

| gate | what must hold | site |
|---|---|---|
| (a) | device-reported whole-image checksum == host-computed | `cmpl 0x25a20(%esi),%eax` `0x403b95` |
| (b) | `A1 09` completion returns `resp[1] == 0x01`, within 11 tries | `0x403c30`, retry `0x403c49` |
| (c) | device re-enumerates as VID `0x3367` / PID `0x1978` | loop `0x403c8d`–`0x403cb9` |

Between gate (c) and the send the path is straight-line: the only branches
(`0x403cee`, `0x403d21`, `0x403d81`) guard `E_FAIL` throws and a `CString`
refcount release, and all reconverge before `0x403dbc`.

**The corner case, and it is reachable  [D].** Gate (c) fails at
`0x403cc3 je 0x403e6f`. That path clears the busy flag, updates the UI, closes
the handle — and then:
```
403ea3  xorl %edx,%edx
403ea5  xorl %eax,%eax
403ea7  movw %ax,0x56a0fc
403ead  leal 0x1(%edx),%eax      ; eax = 1
```
**It returns 1 — success — having never sent `A1 13`.** So there is a real,
reachable outcome in which the image is written and verified, the tool reports
success, and the settings are *not* reset. "A firmware update always resets
settings" is therefore **false**; "a firmware update that completes normally
resets settings" is the accurate statement.

**Fire-and-forget  [D].** The updater sleeps 900 ms (`pushl $0x384`, `0x403de9`),
reads one 64-byte `0xA1` report (`0x403e10`), and **never inspects it** — the
next instruction consumes an unrelated float. On send failure (`0x403de7`) it
skips the read and still returns success. The config tool, by contrast, sleeps
1100 ms and *requires* `resp[1] == 0x01`. So the device does answer `A1 13` with
the usual status byte; the updater simply ignores it.

**Exhaustive transport census, by raw scan  [D].** `HidD_SetFeature`
(IAT `0x51b1d4`) is dereferenced at exactly **2** sites, both inside the wrapper
`0x4012a0`; `HidD_GetFeature` (`0x51b1d0`) at exactly **3**, all inside
`0x401330`. Recovering `E8` targets across all 1,154,048 bytes of `.text` gives
the wrapper **7 callers each**:
```
send 0x4012a0 : 0x40190f 0x401a60 0x401b26 0x401bf6 0x4037a1 0x403bf2 0x403ddd
recv 0x401330 : 0x401951 0x401aa2 0x401b74 0x401c3d 0x4037d9 0x403c24 0x403e10
```
Every command this updater can emit is one of those seven sends. `0x403ddd` is
the last. Method: 4-byte search for the IAT address, plus an `E8 rel32` sweep of
the whole section — reproducible with `objdump` and `grep`.

fw104 matches structurally: frame at `0x404f4c`, sent `0x404f88`, same 64 bytes,
same three gates, same `Sleep(900)`, same discarded read. fw106/fw107 carry the
identical build at the identical address as fw110.

### 5.5 Per-block verify and repair — `FUN_00401c90` @ `0x00401c90` [D]
`ECX` = pointer to the 1024 source bytes, stack arg = block index [D]
(call site `0x403aff`–`0x403b0c`).

1. Copy the 1024 source bytes to `-0x408(%ebp)` and compute the 16-bit sum
   over them (`0x401ce1`–`0x401d15`).
2. `memset(-0x418, 0, 0x411)`. **`-0x418 + 0x10 == -0x408`**, so this zeroes the
   copy: the copy's address is deliberately the payload region of the response
   buffer. The checksum was already taken.
3. `FUN_00401ad0(block)` reads the block back into `-0x418`.
   Returns 0 → this function returns 0 and the flash aborts.
4. `Sleep(30)`. If `resp[1] != 0x01`, return `resp[1]`.
5. Device's per-block checksum is `resp[7]<<8 | resp[6]` — **16-bit
   little-endian at `resp[6..7]`** (`0x401d90`–`0x401dab`) [D].
6. Compare the 1024 read-back bytes against the source, byte by byte
   (`0x401dc0`–`0x401dd9`). Mismatch → flag 2.
7. Checksum mismatch → flag 2.
8. If flag == 2, **repair**: loop at `0x401e00`–`0x401e3d`, re-issuing
   `FUN_00401980(ECX = block index, EDX = source, stack = 0xA0)`, with a sleep
   accumulator starting at 0 and increasing by 100, while it is `< 2000`.

### 5.6 A defect in the vendor's repair loop — read this before copying it
In step 8 the loop decides whether the repair worked by reading
**`source_pointer[1]`**, not the device's response:

```
401e19: movl  -0x424(%ebp), %eax      ; -0x424 was set to the incoming ECX
401e1f: movzbl 0x1(%eax), %ebx        ;   at 0x401caa, i.e. the SOURCE pointer
401e23: cmpl  $0x1, %ebx              ; treated as "ready"
401e28: cmpl  $0x4, %ebx              ; treated as "busy"
```

`-0x424` is written once, at `0x401caa`, from the incoming `ECX`, and is never
reassigned; at `0x401e00` the same slot is loaded into `EDX` as the source
pointer for the rewrite. So byte 1 of the **firmware image data** is being tested
against the protocol's ready/busy status values [D].

Consequence: whether the repair loop reports success depends on the contents of
the firmware block, not on the device. If a block's second byte happens to be
`0x01`, the loop exits and `FUN_00401c90` returns 1 — a **verified-success
report that verified nothing**. Every other status wrong-foots it the other way.

This is `[D]` as to what the instructions do. That it is unintended is `[G]`,
but the alternative reading — that the vendor meant to test firmware content
against protocol status codes — has nothing to recommend it.

**Our flasher must not copy this.** §4.2 requires the read-back and both
checksums; it does not require the vendor's bug. Reproduce the *commands*
exactly and the *verification* correctly. This is also a concrete reason not to
treat the vendor tool as the reference for correctness, only for protocol.

There is a second oddity in the same loop. `FUN_00401980`'s follow-up status
read uses the report ID it is handed on the stack, with a **fixed 64-byte
length** (`pushl $0x40` at `0x401a8a`). On the normal path `FUN_00403960` passes
`0xA1` (`pushl $0xa1`, `0x403a86`), which matches a 64-byte report. On the repair
path `FUN_00401c90` passes **`0xA0`** (`pushl $0xa0`, `0x401e06`) — the 1041-byte
report ID, read into a 64-byte buffer [D]. Whatever that returns, it is not a
well-formed `0xA0` response.

Both defects sit on the repair path and nowhere else. The main write-and-verify
path is sound.

## 5.7 Cross-version confirmation: code base B agrees with code base A

Updater 1.04 is a separately compiled program — different toolchain, 1,459 KiB
of `.text` against 1,127 KiB, 10,763 functions against 9,076, built 14 months
earlier — and its vendor code sits at completely different addresses. It is
therefore an independent check on every constant above, and it agrees on all of
them [D].

| Fact | code base A (1.06/1.07/1.10) | code base B (1.04) |
| --- | --- | --- |
| Vendor ID `0x3367` | `0x4010fa` | `0x401ca2` |
| Usage page `0xFF01` | `0x401137` | `0x401cdd` |
| PID `0x1978` application | `0x403603` … | `0x4045b6` … |
| PID `0x1977` bootloader | `0x4036c4` … | `0x404686` … |
| `FWFILE` name `0x8c` | `0x00403200` | `0x00404330` |
| enter bootloader `A1 3A` + `5A A5 32` | `0x00403750` | `0x00404760` |
| start `A0 03`, `Sleep(3000)` | `0x4018e9` | `0x4049xx` (inlined) |
| checksum at start cmd `buf[17..20]` | `0x4018ce`–`0x40190f` | `0x404a34`–`0x404a4e` |
| write block `A0 06` | `0x4019d5` | `movw $0x6a0` |
| read block `A0 07` | `0x401b0b` | `movw $0x7a0` |
| checksum query `A1 08`, `buf[2]=0x34` | `0x401be5`, `0x401beb` | `0x404c68`, `0x404c73` |
| complete `A1 09` | `0x00403960` | `movl $0x9a1` |
| post-success `A1 13` | `0x00403960` | `movl $0x13a1` |
| **block base `+0x34`** | `0x403a8b`, `0x403aff` | `0x404b82`, `0x404bd2` |
| **checksum end `+0x33`** | `0x403b72` | `0x404c5d` |
| write retries: 5, `Sleep(200)` | `0x403aae` | `0x404bac` |
| per-block pacing `Sleep(90)`, stride `0x400` | `0x403b43`, `0x403b47` | `0x404c11`, `0x404c25` |
| progress `(100*i)/count + 2` | `0x403b1e` | `0x404bed` |

Two independently compiled builds agreeing byte for byte on the command set,
the block base and every retry bound is the strongest confirmation static
analysis alone can produce. It does not make any of it `[O]`.

**Where B differs from A:** 1.04 inlines the "bldr start" and per-block
sequences into its flash driver (`FUN_00404980`, 1728 bytes) instead of calling
out to small builders, and its dialog member offsets differ (`+0x25c` block
count, `+0x25a60` checksum, `+0x25a7c` resource cursor, versus A's `+0x21c`,
`+0x25a20`, `+0x25a3c`). Those are layout, not protocol.

## 5.8 Function correspondence between the two code bases

For the three builds sharing a `.text` (1.06, 1.07, 1.10) every function is at
the same address and is byte-identical, so "how it differs across versions" has
one answer for all of them: **it does not** [D].

Against 1.04 the mapping is below. It was established from cited evidence — the
imported APIs each function calls, the literal strings it references, and the
call-site argument patterns — not from a similarity score.

| role | 1.06/1.07/1.10 | 1.04 | evidence |
| --- | --- | --- | --- |
| HID enumeration, identity match | `0x00401000` | `0x00401bc0` | both are the sole caller of `HidD_GetHidGuid`/`GetAttributes`/`HidP_GetCaps`/`SetupDiEnumDeviceInterfaces` in their binary |
| `HidD_SetFeature` wrapper + retry | `0x004012a0` | `0x00401e30` | sole `HidD_SetFeature` caller in each |
| `HidD_GetFeature` wrapper + retry | `0x00401330` | `0x00401ed0` | sole `HidD_GetFeature` caller in each |
| read `bcdDevice`, format version | `0x004011f0` | `0x00401d80` | both called once, from the Update handler, before `L"Mouse firmware current version %.2f"` |
| MFC resource-string helper | `0x004023f0` | `0x00401fc0` | `LoadResource`+`LockResource`+`SizeofResource`, no `FWFILE` |
| **load `FWFILE` 140, split, checksum** | `0x00403200` | `0x00404330` | `FindResourceW(NULL,0x8c,L"FWFILE")` and `L"Loading firmware file failed"` in both |
| whole-image checksum over blocks | `0x00403580` | inlined into `0x00404330` | 1.04 folds the sum loop into the loader (`0x4044xx`) |
| **Update button handler** | `0x004033b0` | `0x004044f0` | `L"Read firmware file failed"`, `L"Mouse firmware current version %.2f"`, `L"Device not found"` in both |
| find device / decide mode 1 vs 2 | `0x00403600` | **inlined** into `0x004044f0` | 1.04's PID literals `0x1978` at `0x4045b6`, `0x4045e1`, `0x40461d` and `0x1977` at `0x404686` all sit inside `0x004044f0` |
| **enter bootloader** | `0x00403750` | `0x00404760` | `L"send bldr request failed"`, `L"Open bldr device request failed"`; PID `0x1977` at `0x404830`, `0x404862`, `0x4048a3` |
| **flash driver** | `0x00403960` | `0x00404980` | `L"send bldr start request failed"`, `L"send firmware block data..."`, `L"send block data request failed"`, `L"get all check sum error"`, `L"send bldr complete request failed"`, `L"Update Succeed…"`, `L"Update failed, try again"` — all seven in both |
| bootloader-start command builder | `0x00401890` | **inlined** into `0x00404980` | 1.04 builds `A0 03` at `0x404a34`–`0x404a54` |
| **write one block** | `0x00401980` | `0x00402a90` | called from the block loop with `ECX = i+0x34`, `EDX = data`, stack `0xA1` — `0x404b82`–`0x404b98` |
| **verify and repair one block** | `0x00401c90` | `0x00402c00` | called next with `CL = i+0x34`, `EDX = data` — `0x404bc3`–`0x404bd5` |
| read one block back | `0x00401ad0` | inlined into `0x00402c00` | 1.04's `movw $0x7a0` sits inside it |
| whole-image checksum query | `0x00401bb0` | **inlined** into `0x00404980` | `movw $0x8a1` + `movb $0x34` at `0x404c68`–`0x404c73` |
| worker thread trampolines | `0x00402f00`, `0x00402f10` | `0x00404140`, `0x00404150` | both are 15-byte `AfxBeginThread` targets calling straight into the two drivers |

**The pattern of difference is inlining, not protocol.** 1.04 folds five of the
small builders into its two big drivers, which is why its flash driver is 1,728
bytes against 1,386. Every command byte, retry bound and sleep survives the
change unaltered (§5.7).

Dialog member offsets differ and are the other systematic difference:

| meaning | A (1.06/1.07/1.10) | B (1.04) |
| --- | --- | --- |
| block count | `+0x21c` | `+0x25c` |
| block buffer base | `+0x220` | `+0x260` |
| whole-image checksum | `+0x25a20` | `+0x25a60` |
| progress value | `+0x25a24` | `+0x25a64` |
| busy flag | `+0x25a38` | `+0x25a78` |
| resource read cursor | `+0x25a3c` | `+0x25a7c` |
| status label `CWnd` | `+0x1a4` | `+0x1d8` |
| action button `CWnd` | `+0x130` | `+0x158` |

## 6. Not yet derived — do not guess

**One item moved out of this section on 2026-09-03.** Whether the config tool
shares this transport is no longer open: it does. Configuration tool v1.07 uses
the same two feature report IDs at the same two lengths, the same byte-1
framing, the same 1024-byte payload at `+0x10`, the same five `GetLastError`
retry codes and the same `resp[1]` status convention. Derived in
`notes/config-protocol.md` §1. The command *numbers* are disjoint and must not
be read across.

Resolved since the first draft, by disassembly: the register-passed arguments of
`FUN_00401890`, `FUN_00401980`, `FUN_00401ad0` and `FUN_00401c90`, and the block
index. See §3. The first draft of this file, written from the decompiler alone,
was missing the block index entirely — worth remembering as a calibration point.

Still open:

- **What `0xA0/0x03` actually does to the device.** "bldr start" and the
  3-second sleep suggest an erase, and it carries the block count and total
  checksum, which fits a "prepare to receive N blocks" reading. Both are `[G]`.
  §1.3 applies: this command will be sent, but no *inference* about erasing may
  drive any other decision.
- ~~What `0xA1/0x13` does.~~ **RESOLVED 2026-09-03 — it is FACTORY RESET.**
  Derived from the config tool, not this one: cfg107's main dialog control
  `1039` is captioned `Factory Reset`, its handler `0x00413f90` opens the device
  and calls `0x00404720`, which builds a 64-byte frame `movl $0x13a1` at
  `0x40479f` and sends it (`notes/config-protocol.md` §2.4). The frame carries
  no payload, so the two tools' `A1 13` are byte-identical.

  **Consequence for the flasher, and it is user-facing:** the updater sends
  `A1 13` unconditionally after a verified flash (§5.4 step 7), so **a firmware
  update resets the mouse's settings to factory defaults.** Our flasher must
  either warn before flashing or capture the settings blob first (`A1 12` read,
  §config §2.2) so the user can restore afterwards. Currently `[D]` for the
  frame and the send; `[G]` only for what the device does on receipt.
- **The mapping from block number to a flash address.** Block `0x34` is `[D]`;
  `0x34 * 1024` is `[G]`.
- **Which `FWFILE` belongs to which product.** The tool hardcodes 140 and says
  nothing about the other five.
- **What the device does with a `0xA0/0x06` whose `[2..3]` is outside
  `0x34..0x74`.** The vendor tool never sends one, so its behaviour is
  unconstrained by anything in the binary. Our flasher must never emit one.
- **Everything in `CLAUDE.md` §5.** No `[O]` exists yet; the device is not in
  hand. Nothing above has been checked against hardware.

### 6.1 One question the binaries cannot answer, and it is a design input
The tool passes **two different buffer lengths to the same device handle**:
`0x411` for report `0xA0` and `0x40` for report `0xA1` [D]. On Windows,
`HidD_SetFeature` is normally called with the collection's
`FeatureReportByteLength`, which is the **maximum** over all feature reports in
that collection. If this device's vendor collection declares both report IDs,
that maximum would be 1041, and every 64-byte call — including the
enter-bootloader command that the whole update depends on — would have to be
tolerated by the stack rather than being exactly sized.

`FUN_00401000` calls `HidP_GetCaps` but only ever reads `Usage` and `UsagePage`
from the result; it never looks at `FeatureReportByteLength` [D]. So the tool
hardcodes both lengths and the binaries say nothing about what the device
actually declares.

This matters because macOS is not Windows: `IOHIDDeviceSetReport` takes an
explicit length and does no padding, so we must send exactly the right number of
bytes and cannot rely on a driver being lenient.

**Resolve it from the report descriptor when the device arrives** — that is
already `CLAUDE.md` §5's first item, and this is the specific thing to look for:
whether `0xA0` and `0xA1` live in one collection or two, and what feature length
each declares. Until then, the plan is to mirror the vendor exactly (`0x411` for
`0xA0`, `0x40` for `0xA1`) and to treat a length mismatch as a preflight failure
rather than something to paper over.

## 6.2 Whole-file byte partition — every byte of all nine binaries [D]

`coverage.py` partitions Ghidra's function list; `gapscan.py` partitions `.text`
bytes. Neither covers the **file** — headers, `.rdata`, `.data`, `.rsrc` and
`.reloc` were outside both, so "exhaustively accounted" had never once meant the
whole binary. `Tools/ghidra-export/filemap.py` closes that:

```
.analysis/venv/bin/python Tools/ghidra-export/filemap.py <tag> --json .analysis/filemap_<tag>.json
```

Every byte lands in exactly one class — DOS header/stub, PE header, section
headers, header pad, each data directory, each `.reloc` block, each resource
blob named by type and name, section bodies, section padding, overlay — and the
classes must sum to the file size or the tool exits non-zero.

**Result: all nine binaries partition with zero residue and zero double-claimed
bytes**, first run, no exceptions carved out:

| | fw110 | fw107 | fw106 | fw104 | cfg107 | cfg104 | cfg101 | cfg100 | xm1r |
|---|---|---|---|---|---|---|---|---|---|
| file bytes | 2,133,504 | 2,066,944 | 2,066,944 | 2,427,904 | 1,836,032 | 1,833,472 | 1,837,568 | 2,193,920 | 5,664,256 |
| GAP residue | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

For fw110 the largest classes are `.text` 1,153,969 (54.09%), `.rdata` 268,113
(12.57%), `.reloc` blocks 105,332 (4.94%), the six `FWFILE` blobs 399,360
(18.72% together), section padding 61,969, `.data` 23,169.

### 6.2.1 `FWFILE` is a STRING-typed resource, and the config tools have none [D]

The resource type is the string `L"FWFILE"`; the six names are integers. Read
directly off the loader — `FUN_00403200`:

```
403207  pushl $0x5447c0     ; L"FWFILE"  (the TYPE, a string)
40320c  pushl $0x8c         ; 140        (the NAME, an integer)
403211  pushl $0x0          ; hModule = NULL
403213  calll *0x51b230     ; FindResourceW
```

Worth stating explicitly because getting it wrong is easy and it looked like a
finding: a resource directory entry is either an integer id or, with the high
bit set, an **offset** to a length-prefixed UTF-16 string. Reporting the masked
offset as though it were an ordinal makes one string type appear as three
different numbers across builds — 3304 for 1.04, 3340 for 1.06/1.07, 3388 for
1.10 — which reads exactly like a per-version type ordinal that our code would
have to track. **It is not.** It is `"FWFILE"` in every build, stored at a
different directory offset. `filemap.py` resolves the strings; the trap is
recorded in its docstring.

**The negative that matters:** cfg100, cfg101, cfg104 and cfg107 contain **no
`FWFILE` resource of any name**, and no resource of 66,560 bytes at all. Their
only string-typed resources are MFC's `AFX_DIALOG_LAYOUT` markers, 2 bytes each.
Scope of that claim: the full resource tree of each file, walked exhaustively by
`filemap.py`, whose partition closes to zero — so this is not a search that
could have missed a branch. **The vendor's configuration tools carry no firmware
image and therefore cannot flash.**

Corroborates §8's census by a second, independent route, and confirms the blob
inventory exactly: 133/135/137 byte-identical across all four releases
(SHA-256), 140 and 142 different in every one of the four, 143 present only in
1.10.

> One caution about 140 and 142, since it invites a wrong inference: across
> releases these blobs share a **16-byte identical prefix** in some pairs while
> their full hashes differ. Comparing leading bytes therefore says "same" when
> the blobs are not the same. Compare whole-blob hashes, never prefixes.

## 7. Scope of the search — what was read, and what was not

`CLAUDE.md` §6 forbids silent sampling, so here are the numbers.

### Candidate sets

**Regenerated 2026-09-04. Do not quote these; re-run the command.**

```
.analysis/venv/bin/python Tools/ghidra-export/classify.py <tag> cfg100 cfg101 cfg104 cfg107
```

Reference set is all four config tools and nothing else. Adding `fw104` or
`xm1r` changes nothing for 1.10; adding `fw106`/`fw107` is **forbidden** — their
`.text` is byte-identical to 1.10, so they self-match and inflate the library
count from 881 to 969 while proving nothing (`coverage.py` refuses this case).

| binary | functions | named by Ghidra | unnamed `FUN_` | positively library¹ | candidates |
| --- | --- | --- | --- | --- | --- |
| 1.10 (= 1.07 = 1.06) | 9,076 | 7,388 | 1,688 | 881 | **807** |
| 1.04 | 10,763 | 4,043 | 6,720 | 2,831 | **3,889** |

¹ by `Tools/ghidra-export/classify.py`: a normalised function body that also
occurs in a program with different vendor code is MFC/CRT. One-directional — a
match proves library, a non-match proves nothing.

Cross-matching also exposed **two toolchain families**, which is why the 1.04
number is so much worse: `{1.06, 1.07, 1.10, cfg 1.01, 1.04, 1.07}` share ~5,000
normalised bodies, and `{1.04, cfg 1.00}` share 3,686, while across the families
the overlap collapses to ~100. 1.04 therefore has only one useful reference
binary instead of four.

**The candidate set as a partition** — every candidate lands in exactly one cell,
and the cells sum to the total, which is the form `CLAUDE.md` §6 requires:

| | 1.10 | 1.04 |
| --- | --- | --- |
| under 16 bytes (thunks/stubs; classifier declines to hash them) | 719 | 761 |
| substantive, **inside** the vendor band | 24 | 24 |
| substantive, **outside** the vendor band | 64 | 3,104 |
| **total candidates** | **807** | **3,889** |

> **CORRECTION (2026-09-04).** This section previously gave 921/767 for 1.10 and
> 2,873/3,847 for 1.04, with "703 tiny, 64 substantive" and "743 tiny, 3,104
> substantive". **The totals were unreproducible.** Neither the current
> `classify.py` nor its pre-break revision produces them under any legitimate
> reference set. The reproducible values are the ones tabled above.
>
> Cause, now fully accounted for: **64 and 3,104 were right but mislabelled.**
> They are the *outside-the-band* substantive counts, not the substantive totals
> (88 and 3,128). The totals were then back-solved so that
> `tiny + substantive = total` would hold — 767 = 703 + 64 — which is how a
> 40-function discrepancy got absorbed into prose instead of being resolved.
> Every figure above is now regenerated and the partition closes exactly.
>
> **Two process failures made this survive.** First, the numbers were written
> into prose and thereafter quoted rather than recomputed — the exact hazard
> §6 names. Second, the commit that wrote them (`3602d95`, "Resolve the
> 40-function gap and validate `classify.py` before relying on it") **left
> `classify.py` with a syntax error**: a corrected docstring was pasted above
> the stale one and the two collided, so the file has not parsed since. The
> commit that claimed to validate the tool is the commit that made it
> unrunnable, and the numbers it reported could not be re-derived by anyone who
> tried. Repaired in the same change as this correction; the stale docstring,
> which still asserted `.reloc` masking that the file does not do, is deleted.

**The consequence that matters** is that 1.10 has **88** substantive candidates,
not 64, and 1.04 has **3,128**, not 3,104 — 24 more each, all inside the vendor
band and all already read and documented in this file. So no function was
actually unread because of this. The error was in the accounting, not the
coverage, but an accounting error that understates the read-set is the kind that
would hide a real gap next time, which is why it is written up rather than
quietly patched.

### The vendor band
1.10's vendor code occupies `[0x00401000, 0x004040ad)`, the first objects the
linker emitted, ending exactly where `AfxSetNewHandler` and the MFC/ATL/CRT
bodies begin. 1.04's occupies `[0x00401bc0, 0x00405200)`, interleaved with ATL
template instantiations. Stated on one basis for both, since the two were
previously quoted on different ones (1.10 as a total, 1.04 as unnamed-only):

| band contents | 1.10 | 1.04 |
| --- | --- | --- |
| functions, total | 95 | 94 |
| of those, named by Ghidra | 4 | 5 |
| of those, unnamed `FUN_` | 91 | 89 |
| of those, body ≥ 16 bytes | 79 | 76 |

The band was **not** assumed from address locality. It was fixed from confirmed
vendor anchors (the HID enumerator, the two feature-report wrappers, the
`FWFILE` loader) and then checked.

### Why the protocol cannot be hiding outside the band
Every reference, in the whole 9,076-function image, to any HID or SetupDi import
and to every one of the vendor's device globals, is inside the band [D]:

Re-done on 2026-09-03 as an **exhaustive 4-byte-literal scan of the whole
`.text` section** (VA `0x401000`–`0x51ac00`, 1,153,536 bytes), not by counting
referencing functions. The earlier version of this table undercounted every
global — it is corrected below. The conclusion did not change.

| symbol | occurrences in `.text` | outside band |
| --- | --- | --- |
| `HidD_SetFeature` | `0x004012a0` only | none |
| `HidD_GetFeature` | `0x00401330` only | none |
| `HidD_GetHidGuid`, `HidD_GetAttributes`, `HidP_GetCaps`, `HidD_GetPreparsedData`, `HidD_FreePreparsedData`, `SetupDi*` | `0x00401000` only | none |
| device handle `0x0056a174` | **14** (12 reads, 2 writes) | **0** |
| found flag `0x0056a0f8` | **7** | **0** |
| mode flag `0x0056a0fc` | **6** | **0** |
| `bcdDevice` `0x0056a0f4` | **3** | **0** |
| HID GUID `0x0056a11c` | **5** | **0** |
| devinfo `0x0056a120` | **3** | **0** |
| `caps.Usage` `0x0056a130` | **2** | **0** |
| `caps.UsagePage` `0x0056a132` | **1** | **0** |
| `0x0056a118` | **1** | **0** |

### The vendor never validates report lengths [D]
The same scan run over the rest of `HIDP_CAPS` returns **zero**:

| field | address | occurrences in all of `.text` |
| --- | --- | --- |
| `caps.InputReportByteLength` | `0x0056a134` | **0** |
| `caps.OutputReportByteLength` | `0x0056a136` | **0** |
| `caps.FeatureReportByteLength` | `0x0056a138` | **0** |

The tool calls `HidP_GetCaps`, checks `Usage` and `UsagePage`, and **never looks
at the declared report lengths**. `0x411` and `0x40` are hardcoded at every one
of the seven send sites (§3.7a). It has no idea whether the collection it opened
actually declares a 1041-byte feature report; if the device presented a
different length, `HidD_SetFeature` would simply fail and the tool would read
that as a transport error.

**We should not copy this.** §4.2 makes any preflight failure abort, and this is
free safety the vendor left on the table: on macOS the report descriptor is
available before any write, so comparing the device's declared feature-report
lengths against the constants `0x411`/`0x40` is a preflight check that costs
nothing and would catch a wrong-collection or wrong-product open before a single
byte is sent. It also resolves §6.1 from our side without needing the device:
whatever the descriptor says, we compare rather than assume.

The same holds for 1.04: all eight HID/SetupDi imports and the sole `FWFILE`
reference resolve to `0x00401bc0`, `0x00401e30`, `0x00401ed0` and `0x00404330`,
every one below `0x00405200` [D].

**Nothing in either binary can send or receive a byte to the device from outside
the band.** That is a positive, re-checkable statement, not a sampling argument.

### What that does and does not settle
It settles the protocol: no device-facing code was missed. It does **not** by
itself discharge §6 for the remaining candidates, which could in principle hold
vendor code that never touches the device. Those are being read separately; the
counts above are the honest denominator.

## 8. Structure of the `FWFILE` images

Nothing here changes what our flasher does — the updater copies the resource
byte for byte (§4) and so will we. It is recorded because it says something
about what the *device* does, and because it bounds what a corrupted image would
look like.

Every one of the six 65 KiB blobs in updater 1.10 has the **same shape** [D],
measured by hashing each 1024-byte block:

```
blocks  0 .. 28   29 distinct blocks   real content
blocks 29 .. 63   ONE block repeated 35 times, contiguous   filler
block  64         distinct                                  real content
```

`FWFILE` 133, 135, 137, 140, 142 and 143 all give `distinct = 31` with the same
35× run at the same indices [D].

### The encryption is deterministic and position-independent [D]
A single 1024-byte value occupying 35 different block positions produces
**identical ciphertext at every one of them**. So whatever transform produced
these blobs has no IV, no chaining and no dependence on block index or address:
the same plaintext block always yields the same ciphertext block. That is
ECB-shaped, at a granularity no coarser than 1024 bytes.

Confirmed independently by the version diffs of `FWFILE` 140 [D]:

| pair | differing bytes | shape |
| --- | --- | --- |
| 1.07 → 1.10 | 30,600 / 66,560 | blocks 0–28 and 64 differ ~100%; blocks 29–63 **identical** |
| 1.04 → 1.06 | 4,078 / 66,560 | exactly four 1024-byte blocks differ, ~99.6% within each |

A change confined to four blocks leaving all others bit-identical is only
possible without chaining.

### The key appears to be per-image [D]
**No 1024-byte block is shared between any two of the six `FWFILE` resources** —
not even the filler block, which all six have 35 copies of. If the filler
plaintext is the same in all six, which its identical position and run length
makes likely, then the six images are encrypted under different keys or
otherwise separated. `[G]` as to mechanism.

### What follows for us
- The bytes are opaque to the host and stay opaque. §1.3.
- The device decrypts. We never need to.
- Roughly 30 KiB of the 65 KiB is content and 35 KiB is filler, but the updater
  sends **all 65** blocks and the device's whole-image checksum covers all 65
  (§3.5). Do not "optimise" by skipping the filler.

## 8. The `FWFILE` resources — census, selection, and what is actually sent

Work-plan item 5. The question this had to settle: **which blob reaches the
mouse, is it transformed on the way, and what happens when Endgame changes the
set.** Method is `Tools/ghidra-export/rsrc.py`, which walks the PE resource
directory and reports type/name/lang/RVA/size/SHA-256/entropy for every leaf. It
ranks and filters nothing (CLAUDE.md §7.1).

### 8.1 Census across the four updaters  [D]

Every `FWFILE` in every version is **exactly 66560 bytes** = `0x10400` =
65 × 1024, at Shannon entropy 7.94–7.95.

| name | 1.04 | 1.06 | 1.07 | 1.10 |
|---|---|---|---|---|
| 133 | `09ba5297…` | `09ba5297…` | `09ba5297…` | `09ba5297…` |
| 135 | `4fb2c174…` | `4fb2c174…` | `4fb2c174…` | `4fb2c174…` |
| 137 | `9cf31ca8…` | `9cf31ca8…` | `9cf31ca8…` | `9cf31ca8…` |
| 140 | `41d5397e…` | `9f01d732…` | `3922b141…` | `8148ebe9…` |
| 142 | `b09b0ac0…` | `defb2243…` | `05c051d9…` | `42660cc0…` |
| 143 | — | — | — | `13c13a62…` |

- **133, 135, 137 are byte-identical across all four releases.** Frozen.
- **140 and 142 both change in every release.** Two blobs track the version.
- **143 exists only in 1.10** — the same build whose CodeView PDB path names an
  **EL1 8k** project (§0.1.1).

### 8.2 Only resource 140 is reachable  [D]

The `FWFILE` type string is UTF-16 in `.rdata`. An exhaustive `.text` scan finds
**exactly one reference to it in each of the four binaries** — therefore exactly
one `FindResourceW` call site, therefore exactly one resource name can ever be
requested.

| | string VA | ref site | pushed name |
|---|---|---|---|
| 1.04 | `0x5a1cd8` | `0x40433a` | `pushl $0x8c` @ `0x40433e` |
| 1.06 | `0x5447c0` | `0x403208` | `pushl $0x8c` @ `0x40320c` |
| 1.07 | `0x5447c0` | `0x403208` | `pushl $0x8c` @ `0x40320c` |
| 1.10 | `0x5447c0` | `0x403208` | `pushl $0x8c` @ `0x40320c` |

`0x8c` = **140**, a compile-time immediate in all four. In 1.10:
```
403207: pushl $0x5447c0        ; lpType  = L"FWFILE"
40320c: pushl $0x8c            ; lpName  = MAKEINTRESOURCE(140)
403211: pushl $0x0             ; hModule = NULL
403213: calll *0x51b230        ; KERNEL32!FindResourceW
```

**So 133, 135, 137, 142 and 143 are dead resources.** No code path in any of the
four binaries can load them. This satisfies CLAUDE.md §1.4 by derivation rather
than by assumption — and note it had to be *derived*: that 142 changes in
lockstep with 140 is exactly the pattern that would tempt a reader into
"the updater must use both".

Why dead blobs ship at all is **[G]** and stays [G]. What matters is the shape of
the risk: a build carrying five unreachable firmware images, one of which
(143) appeared in the same release that was linked from another product's
project, is a build where "which resource is the OP1 image" is a property of
*this* binary and not a law. Our gate must therefore check the resource set, not
just `.text` identity — 1.10's `.text` is byte-identical to 1.06/1.07, so a
code-only gate would have accepted 1.10 without ever noticing the new blob.

### 8.3 The load path does not transform the image  [D]

```
40323a  SizeofResource                 -> size
403242  shrl $0xa                      -> chunk count  = size >> 10  -> obj+0x21c
40324b  shll $0xa ; subl               -> remainder    = size & 1023 -> edi
403258  LoadResource
40327b  LockResource                   -> src pointer  -> obj+0x25a3c
403295  movl $0x1, %ebx                -> COPY STRIDE = 1
4032a6  memset(dst, 0, 0x400)          per chunk, dst base = obj+0x220
4032c0..403313  byte copy, 4x unrolled, 0x400 bytes per chunk
403323  loop while chunk < obj+0x21c
403332..403386  the `remainder` tail path
```

For a 66560-byte resource the chunk count is **65** and the remainder is **0**,
so the tail path at `0x403336`–`0x403386` never executes.

The copy is `movzbl (src)` / `movb` with `ebx = 1`. **It is a verbatim
byte-for-byte copy. There is no decryption, no unpacking, no transformation of
any kind** between the resource and the buffer at `obj+0x220`. [D]

That single fact retires the whole question of the blob's cipher: the host never
needs to understand it. **Our flasher extracts resource 140 and ships those bytes
unmodified**, exactly as the vendor's tool does.

### 8.4 The checksum is computed by the host over the image  [D]

`0x403580`, called at `0x40338e` immediately after the load, result stored to
`obj+0x25a20`:
```
4035b0..4035d6   four accumulators, bytes summed 4 at a time, 0x400 per chunk
4035d8..4035df   the four partial sums are added together
4035e2           dst += 0x400 ; decl chunk counter ; loop
4035ed           return the 32-bit sum
```
A **plain 32-bit additive sum of every byte of the image**. Not a CRC, not a hash.

This is the code behind the posture in CLAUDE.md §2. The host computes the
checksum from the same bytes it is about to send, so any device-side comparison
can only ever establish **received == sent**. A wrong-but-well-formed image
produces a perfectly matching checksum. Nothing downstream of us catches it.

### 8.5 Structure of the blob itself  [D] for the observations, [G] for the cause

65 chunks of 1024. Per-chunk SHA-256 over each version gives **31 distinct chunks
of 65** in every release, because:

- **chunks 29–63 are one chunk repeated 35 times**, contiguous, and
- **that repeated chunk is byte-identical in 1.04, 1.06, 1.07 and 1.10**
  (`cefe77fb6c23f0d4…`).

So a constant plaintext region — almost certainly erased flash — encrypts to the
same ciphertext in every release, which means **the key is unchanged across the
product's entire release history 1.04 → 1.10**. [D]

The image is therefore ~29 KiB of content (chunks 0–28) plus a distinct final
chunk 64, inside a 64 KiB + 1 KiB container.

What is ruled out about the cipher, from the data alone:
- **not a fixed-keystream XOR** — XORing any content chunk against the padding
  chunk yields ~4 zero bytes per 1024 (chance), never a run;
- **not ECB with a ≤512-byte block** — the padding chunk has no internal
  repetition at 8/16/32/64/128/256/512;
- **not chained across chunks** — 1.04→1.06 changes only chunks 8, 27, 28, 64
  and leaves 9–26 identical.

What is observed but not explained: within each changed chunk, ~99.6% of bytes
differ, i.e. a localized plaintext edit rewrites its whole 1024-byte chunk.

**The cipher is not identified and is deliberately not guessed.** Per §8.3 it
does not need to be: the host never decrypts. Recording it here as a bounded
unknown rather than an open question that looks like a task.

### 8.6 Consequences for the ingest design

1. Extract `FWFILE`/**140** only. The name stays a hardcoded constant (§1.4).
2. Ship those bytes **verbatim**. Never transform, re-checksum, or "normalise".
3. Expect exactly 66560 bytes = 65 × 1024, remainder 0. A resource whose size is
   not an exact multiple of 1024 exercises a vendor path that has never run in
   any shipped version; refuse it rather than emulate it.
4. Gate on the **resource-set fingerprint**, not only on `.text` identity, and
   refuse an updater whose set does not match one we have read.
5. Treat the additive checksum as a transport integrity check only. It is not
   evidence that the image is the right image.

## 9. Device-layer confinement, proved twice and for every binary  [D]

This is the "hay" argument. It finds no new protocol facts. Its whole purpose is
to earn the right to say the protocol surface is *completely* enumerated, and it
is stated as a partition so a hole cannot hide as a shortfall (§6 of CLAUDE.md).

**Method.** Everything below is regenerated from **raw bytes and raw call
edges**. Ghidra supplies function *boundaries* only, used as intervals, never as
edges — `CLAUDE.md` §1.2b, and see the correction at the end of this section for
why that distinction had to be forced.

1. **Seed — exhaustive 4-byte scan of every byte of `.text`** for the address of
   any HID/SetupAPI entry-point slot. Two kinds of slot, and both must be in the
   set or the count is wrong:
   - **static IAT slots**, from the import directory;
   - **`GetProcAddress`-filled `.data` slots**. Found by locating each
     `HidD_*`/`HidP_*` name string, finding the `.text` site that pushes its
     address, and taking the following `a3` (`mov %eax,<abs>`) store. Keying on
     the *name string* is load-bearing: a shape-matched regex found 1 of
     cfg100's 11.

   Any function whose body contains such a 4-byte reference is in the seed. This
   catches the resolver itself, which stores to the slots without ever calling
   through them; a call-through-only criterion misses it and gives 12, not 13.

2. **Closure — upward over call edges recovered from raw bytes** by
   `Tools/ghidra-export/calledges.py` (`E8`/`E9` rel32 accepted when the target
   is a function entry and the site is attributable). `jmp` edges are included:
   a tail call is a call, and excluding them is what understated the config-tool
   closures below.

**Regenerate, do not quote:**

```
for t in fw110 fw104 cfg107 cfg104 cfg101 cfg100 xm1r; do
  .analysis/venv/bin/python Tools/ghidra-export/calledges.py $t \
      --out .analysis/edges/${t}_edges.json
done
.analysis/venv/bin/python Tools/ghidra-export/closure.py
```

Member lists — not just counts — go to `.analysis/device_closure_rawedges.json`,
so every number in this section is auditable rather than merely asserted.

**The method carries a planted positive** (`CLAUDE.md` §6.2 — a harness that
cannot produce a bad result is not evidence). `closure.py` asserts that cfg107's
closure contains `0x413f90` and `0x404720`, and exits non-zero if it does not.
Those two are the functions behind the retracted factory-reset conclusion:
`0x413f90` is the Factory Reset button's handler, `0x404720` is the `A1 13`
sender it calls at `0x413faf`, and **neither exists in Ghidra's export at all** —
`0x413f90` is reachable only through an MFC message map, so no function node was
ever created for it and `0x404720`'s exported `callers` field is empty. A closure
built on that export misses both and concludes the config tool has no factory
reset, which is exactly the error that was made. The raw-edge method finds both,
by attributing call sites to orphan units. Verified both ways: the check passes
on the real data and fails when a member is removed.

This also bounds the blast radius of the export's message-map hole. The known
message-map-only functions in cfg107 are `0x413f90 0x4110a0 0x4111e0 0x411320
0x411460 0x411e40 0x411e60 0x412000`; the first five are in the closure and the
last three are not, because they do not reach the device — not because they were
missed.

**Results.**

| binary | functions | seed | closure | closure span |
|---|---|---|---|---|
| fw110 / fw107 / fw106 | 9,076 | **3** | **14** | `[0x401000,0x403960]` |
| fw104 | 10,763 | **3** | **10** | `[0x401bc0,0x404980]` |
| cfg107 | 9,528 | **13** | **32** | `[0x4027d0,0x414010]` |
| cfg104 | 9,495 | **13** | **32** | `[0x4027e0,0x413b40]` |
| cfg101 | 9,516 | **13** | **32** | `[0x4027e0,0x413a30]` |
| cfg100 | 11,071 | **13** | **30** | `[0x4038a0,0x415860]` |
| xm1r | 23,001 | **7** | **31** | `[0x643b80,0x64d000]` |

fw110's closure, in full — 14 members, every one inside the vendor band:
`0x401000 0x4012a0 0x401330 0x401890 0x401980 0x401ad0 0x401bb0 0x401c90
0x402f00 0x402f10 0x4033b0 0x403600 0x403750 0x403960`.

fw104's, 10 members, likewise all in band: `0x401bc0 0x401e30 0x401ed0 0x402a90
0x402c00 0x404140 0x404150 0x4044f0 0x404760 0x404980`.

> ### CORRECTION 2026-09-04 — two things were wrong here, one of them structural
>
> **(a) The cross-validation warrant was void.** This section used to say the
> XM1r's 7 and 31 were confirmed "by both methods", and that "two unrelated
> methods landing on the same numbers is the point of doing both". **Method 1
> has no call graph**, so it cannot produce a closure at all. Only the seed was
> ever cross-checked; the closure rested on one method, and that method was
> Ghidra's call-graph field — the same field that reported cfg107 `0x404720`
> uncalled when `0x413faf` is a direct `calll` to it. The claim has been removed
> rather than softened, and the closure is now computed from raw edges.
>
> The seed numbers survive this unchanged: 3, 3, 13, 13, 13, 13, 7.
>
> **(b) The config-tool closures were understated by 7–8 each.** Published as
> 25/24/24/23; they are 32/32/32/30. Two causes, both now fixed: the closure was
> taken from Ghidra's incomplete call graph, and it counted only `call` edges
> while the artifact recorded a separate, larger `call`+`jmp` figure (33 for
> cfg107) that was never published. The prior artifact
> `.analysis/device_closure_corrected.json` stored **counts with no member
> lists**, so the discrepancy could not be audited from it — which is why the
> replacement stores the addresses.
>
> **What survives, and it is the load-bearing part:** the updater closures
> reproduce *exactly* — 3/14 and 3/10, all 24 members inside the vendor band —
> and so does the XM1r's 7/31. **The flasher's confinement is unaffected.** The
> error was confined to the config tools, in the safe direction (more functions
> to read, not fewer).
>
> **(c) A route that had never been checked at all: IAT thunks.** A function can
> reach an import by `call <thunk>` where the thunk is `jmp *<slot>`, and no
> closure above would see it, because the thunk block is unattributed `.text`
> that gets lumped into a single orphan unit referencing *every* import.
> Checked exhaustively by byte scan for `E8` targeting a HID/SetupAPI thunk:
>
> | binary | HID/SetupAPI thunks | `E8` calls into one |
> |---|---|---|
> | fw110, fw104, cfg100/101/104/107 | 0 | 0 |
> | xm1r | 14 | **0** |
>
> The XM1r has all 14 thunks and **nothing calls any of them** — every HID
> access is a direct `FF 15`. So the route exists in the image and is dead, and
> the closures lose nothing by not modelling it. Scope of that negative: linear
> byte scan of all of `.text` for `E8` rel32 whose target is one of the 14 thunk
> addresses. Not covered: a call reaching a thunk through a computed or
> register-held target, which no byte scan can see.

The three direct-touch functions of each updater code base correspond
one-to-one, which is itself a structural corroboration across the two code bases:

| role | 1.06/1.07/1.10 | 1.04 |
|---|---|---|
| enumerator | `0x401000` | `0x401bc0` |
| `HidD_SetFeature` sender | `0x4012a0` | `0x401e30` |
| `HidD_GetFeature` receiver | `0x401330` | `0x401ed0` |

**The completeness statement.** For *both* updater code bases, re-checked
2026-09-04 against the regenerated raw-edge closures:

- every function in the closure lies **inside the band** — 14 of 14 for 1.10,
  10 of 10 for 1.04 — and
- **every one of them has been read**: all 24 addresses appear in this file with
  derived content against them. Closure members absent from the notes: **0** for
  1.10, **0** for 1.04.
- Of the functions resting on Ghidra's FunctionID name alone with a body ≥32
  bytes, **zero** are in either closure.

**This holds for the updaters only.** The same check against the config-tool and
XM1r closures does *not* pass and is not claimed to — see the gap note in §9.4.

Regenerate rather than quote: member lists are in
`.analysis/device_closure_rawedges.json`. The older
`.analysis/device_closure.json` and `.analysis/device_closure_corrected.json`
are **superseded** — they store counts without members and cannot be audited.

### 9.1 What this argument does and does not establish

It **does** establish that the set of functions touching a HID/SetupAPI import is
exhaustive and independent of the call graph — that set was found by scanning
*all* functions, so an indirect call cannot hide a device-facing function from
it. This is the part that matters, and it is why the confinement claim survives
even though the XM1r showed Endgame reaching its device layer by indirect call.

It does **not** establish that the *closure* is a complete list of callers. The
closure is built from static call edges, and an indirect call into the device
layer would be missing from it. That weakness is one-directional and harmless
here: a missed caller adds a caller, it does not add a function that touches the
device.

### 9.2 A false positive caught in the making, recorded per §1.7

The first run of the call-graph method reported **11** direct-touch functions for
the XM1r, contradicting the already-published exhaustive claim of 7 in
`notes/xm1r-flasher.md`. The four extras — `0x56a956`, `0x56aac7`, `0x56ad4a`,
`0x56ada9` — were all MFC **`CPageSetupDialog`**, matched because the substring
`SetupDi` occurs inside "Page**SetupDi**alog".

The published claim was right; the new filter was wrong. Recording it because the
failure mode is the interesting part: a sloppy matcher *manufactured a
contradiction* with a correct earlier result, and the tempting resolution — "the
earlier exhaustive claim must have missed four" — would have corrupted a sound
finding. Matchers get whole API names, never substrings.

## 10. The block arithmetic, and what to do about the unknown erase boundary

### 10.1 The block arithmetic is fully pinned  [D]

This is the computation CLAUDE.md §4.3 names as a top failure mode ("wrong chunk
boundary"), so it is derived end-to-end rather than inferred from the loop shape.

In `FUN_00403960`:
```
403a48  xorl %ebx,%ebx                    ; i = 0
403a4a  cmpl %ebx,0x21c(%esi) ; jle       ; loop while i < block_count
403a56  leal 0x220(%esi),%edx             ; src = image base
403a62  movl %edx,-0xa0(%ebp)             ; src pointer lives in -0xa0(%ebp)

403a86  pushl $0xa1                       ; status-read report id
403a8b  leal 0x34(%ebx),%ecx              ; WRITE  block index = 0x34 + i
403a8e  calll 0x401980                    ; A0 06 write

403aff  leal 0x34(%ebx),%eax              ; VERIFY block index = 0x34 + i
403b02  movzbl %al,%ecx                   ; truncated to a byte
403b0c  calll 0x401c90                    ; read back / repair

403b47  addl $0x400,-0xa0(%ebp)           ; src += 1024, EXACTLY once per block
403b58  incl %ebx
403b59  cmpl 0x21c(%esi),%ebx ; jl
```

Three facts that matter more than they look:

1. **Write and verify compute the index the same way**, from the same counter
   with the same `0x34` base. They cannot drift apart.
2. **The source pointer advances by `0x400` exactly once per block**, after the
   verify succeeds — not inside the write retry loop. A block retried five times
   re-sends the same 1024 bytes.
3. **The index is truncated to a byte** (`movzbl %al`). With `block_count = 65`
   the range is `0x34 … 0x74` and nothing wraps, but the truncation is real: a
   block count above 203 would wrap the index and write to the wrong place. Our
   flasher must assert the count, not rely on the image being the usual size.

With `block_count = 65` (§8.3) the loop covers indices **`0x34`–`0x74`** and
source bytes `0` – `66559`: the whole image, exactly once, no remainder.

### 10.2 The erase boundary is still unknown — and it no longer blocks the design

`A0 03` remains `[G]` (§6). There is no erase command among the seven, so
CLAUDE.md §4.2's "before erase / after erase" split cannot be implemented
literally: we cannot point at the instant the flash is destroyed.

What removes the blockage is that **the recovery path is the same on both sides
of that unknown instant**, which follows from two `[D]` facts:

- **The tool flashes a device it finds already in the bootloader**, with no
  application-mode handshake at all (§5.1 case 6: `mode == 2` at PID `0x1977`
  goes straight to `FUN_00403960`).
- **No command ever addresses a block below `0x34`** (§10.1). The vendor tool
  cannot write outside `0x34…0x74` because the index is computed, not chosen.

The reading those support — and it is `[G]`, stated as such — is that the
bootloader lives below block `0x34`, is never a target of this protocol, and
survives a failed update, leaving the device enumerable at PID `0x1977`.

**Therefore the design rule does not need the erase instant.** Ours becomes:

> The point of no return is `A0 03`. From the moment it is sent until a verified
> image is resident, no code path returns. On any failure — including a
> disconnect — re-enumerate, accept PID `0x1977`, and resume the block loop.
> There is no timeout that gives up and no cancel.

This is *more* conservative than the vendor's tool, which does give up: five
tries per block, then abort (§5.4). We keep driving; §4.2 says exiting cleanly
after erase guarantees the bad outcome.

Treating `A0 03` rather than `A1 3A` as the boundary is deliberate. Entering the
bootloader is demonstrably survivable — the vendor tool's `mode == 2` path exists
precisely to pick up a device sitting there — so `A1 3A` is not the dangerous
step. `A0 03` is the first command that could plausibly erase, so it is where the
non-abortable region starts.

### 10.3 Invariants this yields for `EGGFlashCore`

Assertable without hardware, per CLAUDE.md §4.3 ("assert invariants, not
examples"):

1. `block_count == image_size >> 10` and `image_size % 1024 == 0`.
2. `block_count == 65` for every image we have seen; refuse anything else rather
   than emulate an untested vendor path (§8.6).
3. Every emitted block index is in `[0x34, 0x74]`. No exceptions, no fallback.
4. Block index and source offset are derived from **one** counter — never
   tracked separately.
5. The source pointer advances only after a verified block, never inside a retry.
6. No write is emitted unless preflight passed.
7. No return from the post-`A0 03` phase without a verified image or an explicit,
   loud unrecoverable state.


### 9.3 CORRECTION — the config tools resolve HID dynamically, and both published methods were blind to it  [D]

**What was wrong.** §9 reported 7 device-touching functions for each config tool.
The true figure is **13**. The updaters' figure of 3 was right.

**Why both methods missed it, and why their agreement was worthless.** Every
config tool calls `LoadLibraryA("hid.dll")` and then resolves **eleven** HID
entry points with `GetProcAddress`, storing the results into `.data`:

| | resolver | module handle | slot range |
|---|---|---|---|
| cfg107 | `0x004027d0` | `0x57f330` | `0x57f160`–`0x57f188` |
| cfg100 | `0x004038a0` | `0x5dcb70` | `0x5dcb74`–`0x5dcb9c` |

The eleven are `HidD_GetPreparsedData`, `HidD_GetIndexedString`,
`HidD_SetNumInputBuffers`, `HidD_GetAttributes`, `HidD_GetManufacturerString`,
**`HidD_SetFeature`**, `HidD_GetSerialNumberString`, `HidD_FreePreparsedData`,
**`HidD_GetFeature`**, `HidP_GetCaps`, `HidD_GetProductString`.

Functions then call HID **through those `.data` slots**, not through the IAT. The
sharpest example is hidapi's `hid_send_feature_report` at `0x00403320`, which
executes `ff 15 74 f1 57 00` = `calll *0x57f174` at `0x403333` — a
`HidD_SetFeature` call that references no IAT slot at all.

- **§9 method 1** scanned `.text` for *IAT slot addresses*. A dynamic call
  references a `.data` slot, so it was invisible.
- **§9 method 2** used Ghidra's data-refs, which record a reference to a `.data`
  location, not to an import. Also invisible.

**The meta-lesson, and it is the important part.** §9 offered as its warrant:
*"two unrelated methods landing on the same numbers is the point of doing both."*
That reasoning is void when the methods share a blind spot — and these two do.
Their agreement on the config-tool rows measured nothing. **Cross-validation only
counts when the methods fail differently**; agreement between two methods with
the same blind spot is one method reported twice.

**A second wrong test, mine, from the same day.** I checked for dynamic
resolution by asking *"are there HID API name strings that are not in the import
table?"* — found five, none of them `SetFeature`/`GetFeature`, and concluded the
protocol path was statically imported and safe. That test cannot detect a
function that is **both** statically imported **and** dynamically resolved, which
is exactly what `HidD_SetFeature` is here. The right question is not which names
are missing from the imports; it is **which name strings have their address
pushed in `.text`**, i.e. used as a `GetProcAddress` argument.

**Corrected method.** The seed set is the union of
(a) functions referencing a static HID/SetupAPI **IAT** slot, and
(b) functions referencing a **dynamically resolved** slot, where those slots are
found by locating each HID API name string whose address is pushed in `.text`
and taking the following `a3` (`mov %eax, <abs>`) store. Keying on the *name
string* rather than an instruction shape matters: cfg100 pushes the module handle
with `pushl <mem>` where cfg107 uses `pushl %eax`, and a shape-matched regex
found only 1 of cfg100's 11 slots.

**Corrected results.** Closures rebuilt from raw `E8` displacements over entire
`.text` sections, with a start list including every recovered function:

| binary | static IAT users | dynamic slot users | **seed** | closure (`E8`) | closure (`E8`+`E9`) | span |
|---|---|---|---|---|---|---|
| fw110 | 11 | **0** | **3** | **14** | 14 | `[0x401000,0x403960]` |
| fw104 | 11 | **0** | **3** | **10** | 10 | `[0x401bc0,0x404980]` |
| cfg107 | 15 | 11 | **13** | 25 | 33 | `[0x4027d0,0x4142a5]` |
| cfg104 | 15 | 11 | **13** | 24 | 32 | `[0x4027e0,0x413b40]` |
| cfg101 | 15 | 11 | **13** | 24 | 32 | `[0x4027e0,0x413a30]` |
| cfg100 | 15 | 11 | **13** | 23 | 31 | `[0x4038a0,0x415a46]` |

`E9` tail-jumps are reported separately because a tail-jump is a transfer of
control, not a call; including them is the conservative choice and both numbers
are given rather than one being chosen silently.

### 9.4 What this does NOT change: the flasher  [D]

**The updaters have no dynamic HID resolution at all**, established by the
corrected test rather than the flawed one:

- `HidD_*`/`HidP_*` name strings in fw110 and fw104 whose **address is
  referenced from `.text`**: **zero**. No `GetProcAddress` of any HID function
  is possible.
- The single `HID.DLL` string in each updater sits inside the **import name
  table** (immediately after the `HidP_GetCaps` and `HidD_FreePreparsedData`
  hint/name entries) and has **zero** `.text` references — it is the import
  descriptor's DLL name, not a `LoadLibrary` argument.
- Dynamic-slot users: **0**. Seed unchanged at **3**; closure unchanged at
  **14** (fw110) and **10** (fw104), member-for-member.

Re-confirmed independently 2026-09-04 by the name-string method that found the
config tools' 11 slots: dynamic HID slots recovered from fw110 **0**, fw104
**0**, xm1r **0**, and 11 in every one of cfg100/101/104/107.

So the confinement argument the flasher rests on survives intact, and now rests
on a test that would have caught the config tools' dynamic resolution had it been
present. The error was confined to the config-tool rows.

**Open gap, recorded 2026-09-04 rather than resolved** (`CLAUDE.md` §1.7). The
"every closure member has been read" check passes for the updaters and fails
elsewhere. Against the regenerated closures, closure members with no mention
anywhere in `notes/`:

| binary | closure | unmentioned |
|---|---|---|
| fw110 | 14 | **0** |
| fw104 | 10 | **0** |
| cfg107 | 32 | 11 — `0x402900 0x402e70 0x412d00` and the evenly-spaced run `0x410be0 0x410d10 0x410e40 0x410f70 0x4110a0 0x4111e0 0x411320 0x411460` |
| cfg100 | 30 | 16 |
| xm1r | 31 | 29 |

The cfg107 run at `0x410be0`–`0x411460` is eight functions spaced ~0x130 apart,
which is the shape of a generated dispatch or handler table; it is new to the
raw-edge closure and is unread. The XM1r figure overstates the gap — those notes
cite *sites inside* bodies rather than entry addresses, so the check is a poor
proxy there — but it is not zero either, and it is not being claimed as read.
None of this touches the flasher. It is config-side and XM1r-side work.