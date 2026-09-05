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

Independently, scanning every immediate stored to memory **across all of
`.text`** — re-run 2026-09-04 with `Tools/ghidra-export/cmdscan.py`, previously
scoped only to the vendor band — yields exactly **13** instructions whose stored
immediate has a report id in its low byte, and every one is inside the six
functions above:

```
4018e9  movl $0x3a0   -> -0x458(%ebp)   in 0x401890   A0 03 start
40194d  movb $0xa1    -> -0x44(%ebp)    in 0x401890   response report id
4019d5  movw $0x6a0   -> -0x458(%ebp)   in 0x401980   A0 06 write block
401b0b  movw $0x7a0   -> -0x418(%ebp)   in 0x401ad0   A0 07 read block
401b6d  movb $0xa0    -> -0x82c(%ebp)   in 0x401ad0   response report id
401be5  movw $0x8a1   -> -0x44(%ebp)    in 0x401bb0   A1 08 checksum
401c36  movb $0xa1    -> -0x84(%ebp)    in 0x401bb0   response report id
403793  movl $0x3aa1  -> -0x44(%ebp)    in 0x403750   A1 3A enter bootloader
4037d2  movb $0xa1    -> -0x84(%ebp)    in 0x403750   response report id
403be4  movl $0x9a1   -> -0x50(%ebp)    in 0x403960   A1 09 complete
403c1d  movb $0xa1    -> -0x90(%ebp)    in 0x403960   response report id
403dcf  movl $0x13a1  -> -0x50(%ebp)    in 0x403960   A1 13 post-success
403e09  movb $0xa1    -> -0x90(%ebp)    in 0x403960   response report id
```

Three further hits are **addresses, not commands** — `movl $0x53c7a0` twice and
`movl $0x5443a0` once, pointers whose low byte happens to be `0xa0`. Note also
that objdump prints `movb $0xa1` as **`$-0x5f`**, which is exactly how a naive
text search for `$0xa1` finds nothing and looks like a clean negative.

**Two genuinely independent methods, converging on the same six functions.** One
enumerates the callers of the only two APIs that can move a byte to or from the
device; the other scans for immediates and never looks at a call graph. Unlike
the cross-check retracted in §9, these two share no blind spot. And the API bound
is the stronger of the pair, because it does not depend on the command byte being
an immediate at all: however a frame were built — byte at a time, from a table,
out of a register — it still has to be handed to `FUN_004012a0`, whose seven call
sites come from a raw `E8` scan of the whole section and whose six calling
functions have all been read in full.

**fw104 agrees, by the same whole-`.text` scan.** Its 37 hits reduce to the same
seven commands — `0x3a0`, `0x6a0`, `0x7a0`, `0x8a1`, `0x3aa1`, `0x9a1`, `0x13a1`
— inside its four command-building functions `0x402a90`, `0x402c00`, `0x404760`,
`0x404980`. So the command set is identical across the two code bases, checked
the same way in both.

**Two classes of false positive, both worth knowing before trusting such a
scan.** First, pointers whose low byte happens to be a report id
(`movl $0x5779a0`, `$0x57e1a0`, `$0x57e5a0`, `$0x53c7a0`, `$0x5443a0`). Second,
and more convincing at a glance: fw104 `0x004a99c5` and `0x004a99fe` store
`$0xa0` and `$0xa1` **as bytes**, which is precisely the shape of a report-id
store —

```
4a99c5  movb $-0x60, -0x4(%ebp)     ; 0xa0
4a99fe  movb $-0x5f, -0x4(%ebp)     ; 0xa1
```

— but the destination is `-0x4(%ebp)`, the **MSVC C++ exception-handling state
slot**, and the surrounding code is constructing and destroying `CString`
temporaries. They are EH state numbers that happen to equal `0xa0`/`0xa1`. Two
things separate them from real commands and both are mechanical: the destination
is the EH slot rather than a frame buffer that is later passed to the send
wrapper, and the containing function `0x004a704e` is **not in the device
closure** and references no HID import at all.

**So the updater knows seven commands and this table is all of them.** Any
eighth command the device may implement is invisible here, and would have to
come from somewhere other than these binaries.

### 3.7b Responses — every byte the tool actually reads

The tool never reads a response field this table does not list. Verified by
looking at each read buffer's stack slots in the disassembly [D].

| after | read with | length | bytes the tool reads | used for |
| --- | --- | --- | --- | --- |
| `A1 3A` enter bootloader | report `0xA1` | `0x40` | none — only whether `HidD_GetFeature` succeeded | `testl %eax,%eax; jne` at `0x4037e1` leaves the retry loop, whose bound is `cmpl $0xa,%ebx; jl` at `0x4037f0` — `ebx` runs 1…9, so **9 attempts** |
| `A0 03` start | report `0xA1` | `0x40` | `[1]` | returned; caller requires `== 0x01`. Read at `0x40195b` (`movb -0x43(%ebp),%al`, buffer base `-0x44`) |
| `A0 06` write block | report = caller's arg (`0x401a9f`) | `0x40` | `[1]` | returned; caller requires `== 0x01`. Read at `0x401aac` |
| `A0 07` read block | report `0xA0` | `0x411` | `[1]` in `0x401ad0`; `[6..7]` and `[16..1039]` in the **caller** | see the note below — `0x401ad0` copies the whole response out |
| `A1 08 34 74` checksum | report `0xA1` | `0x40` | `[1]`, `[16..19]` | status at `0x401c49`; **LE32 whole-image checksum** assembled at `0x401c55`–`0x401c78` as `[19]<<24 \| [18]<<16 \| [17]<<8 \| [16]` and written through the out-parameter at `0x401c7a` |
| `A1 09` complete | report `0xA1` | `0x40` | `[1]` | `cmpb $0x1, -0x8f(%ebp)` at `0x403c30`, buffer base `-0x90`; retried while `ebx <= 0xa` (`0x403c49`), so **10 attempts**, before the PID `0x1978` re-enumeration |
| `A1 13` post-success | report `0xA1` | `0x40` | **none** | a `GetFeature` **is** issued (`0x403e10`, after `Sleep(0x384)` = 900 ms) and then neither its return value nor any byte of its buffer is examined — see below |

**`A0 07` reads more than `0x401ad0` looks at.** `0x401ad0` checks the wrapper's
return (`cmpl $0x1`) and `resp[1]` (`0x401b7f`), and on success copies the
**entire 1041-byte response** to the caller's buffer — `movl $0x104,%ecx` then
`rep movsl` plus one `movsb` at `0x401b96`, i.e. `0x104*4 + 1 = 0x411`. So the
offsets `[6..7]` and `[16..1039]` are read by `0x401c90`, not here. The
distinction matters for a reimplementation: the transport function must hand the
whole frame up, not a parsed subset, or the caller's checks cannot be written.

**`A1 13`'s response is fetched and thrown away.** `0x403ddd` sends it;
`testl %eax,%eax; je 0x403e18` skips the read on a **failed send**; on success it
sleeps 900 ms and issues the `GetFeature` at `0x403e10` — and then falls into
`0x403e18`, the success-message formatting, which is the *same* continuation the
failed-send path jumps to. Nothing between the two paths differs. So the vendor
cannot distinguish "factory reset acknowledged" from "factory reset command never
reached the device", and does not try. `build-design.md` §2.4 says that if we
send `A1 13` at all we should use the config tool's checked form; this is the
evidence for that sentence — the updater already has the read and discards it.

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
   20 further attempts and **168 s** of waiting in total (`800 × 20×21/2`;
   corrected 2026-09-05 from 136 s, which was quoted rather than computed — the
   loop bound at `0x403cb3` is `cmpl $0x3e80,%ebx` tested *after* the increment,
   so the last sleep really is the full 16000. 136.8 s is the 18-term sum.
   **The pre-registration audit already caught this** — `wire-predictions.md`
   states 168 s and explicitly flags this section as wrong — and the correction
   never reached here, which is the same failure as the FWFILE-selection
   duplicate: a finding is not closed until every file that states it agrees).
   Still not found →
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
length** (`pushl $0x40` at `0x401a89`). On the normal path `FUN_00403960` passes
`0xA1` (`pushl $0xa1`, `0x403a86`), which matches a 64-byte report. On the repair
path `FUN_00401c90` passes **`0xA0`** (`pushl $0xa0`, `0x401e06`) — the 1041-byte
report ID, read into a 64-byte buffer [D]. Whatever that returns, it is not a
well-formed `0xA0` response.

Both defects sit on the repair path and nowhere else. The main write-and-verify
path is sound.

> **RE-DERIVED 2026-09-04 from raw bytes, because we deliberately deviate from
> this and a deviation built on a misreading is worse than copying the bug.**
> Every element checks out:
>
> - `0x401ca7 movl %ecx,%esi` then `0x401caa movl %esi,-0x424(%ebp)` — the slot
>   takes the incoming `ECX`.
> - **`-0x424(%ebp)` is written exactly once.** Scanning the whole body
>   `0x401c90`–`0x401e61` for the displacement finds one store (`0x401caa`) and
>   three loads (`0x401d9e`, `0x401e00`, `0x401e19`). It is never reassigned, so
>   the pointer tested at `0x401e19` is unambiguously the source buffer.
> - `0x401e19 movl -0x424(%ebp),%eax` / `0x401e1f movzbl 0x1(%eax),%ebx` /
>   `0x401e23 cmpl $0x1` / `0x401e28 cmpl $0x4` — byte 1 of the **image data**
>   compared against the protocol's ready and busy values.
> - `0x401e06 pushl $0xa0` on the repair path against `0x403a86 pushl $0xa1` on
>   the normal one, into a follow-up read whose length is the fixed `pushl $0x40`
>   at `0x401a89` (cited as `0x401a8a` before this check — corrected).
>
> So §5.6 stands exactly as written, and `notes/build-design.md` §2.3's
> deliberate deviation rests on a re-derived reading rather than a remembered
> one.

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
- ~~**Which `FWFILE` belongs to which product.**~~ **Largely closed 2026-09-04,
  §8.5a.** 1.10's 140 is the same image lineage as 1.04's — same filler
  ciphertext across all four releases, and the same lock-step change profile
  with 142 — so the EL1 project path in 1.10 did not come with a swapped image.
  What the other five *are* remains unknown and does not need to be known.
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

### 6.2.3 The starts Ghidra missed do not reach the device [D]

`gapscan.py` recovers function starts Ghidra never created, from two
loader-authoritative sources — direct call targets, and addresses taken as
pointers in `.reloc`. There are a lot of them: **2,405** in fw110 (240 by call
target, 2,165 by pointer), 2,178 in fw104, 2,990 in cfg107, 2,708 in cfg100.
This is the same hole that hid cfg107's Factory Reset handler, so "do any of
these reach the device?" is not a rhetorical question.

**It is settled mechanically, not by reading them.** `closure.py`'s seed scan
walks every byte of `.text` for a reference to any HID/SetupAPI slot — static or
`GetProcAddress`-filled — and attributes each hit to its owning function *or*,
where no function owns those bytes, to the orphan unit that does. Orphan-unit
hits are reported separately. The result:

| binary | slot references outside every Ghidra function |
|---|---|
| fw110, fw104, cfg107, cfg104, cfg101, cfg100 | **none** |
| xm1r | `0x411025` — the IAT thunk block, and nothing calls it (§9) |

So in all six PE binaries, **every** HID/SetupAPI reference in `.text` lies
inside a function Ghidra already knows about. Not one of the ~2,400 recovered
starts per binary touches the device, and the seeds are unchanged by them: 3, 3,
13, 13, 13, 13, 7. This is an exhaustive byte scan, not a sample — the same
method that produced the seeds themselves.

The 18 recovered starts that fall inside fw110's vendor band were read anyway,
being few and device-adjacent: `0x40219e 0x4021a3 0x4021a8 0x4021ac 0x4021bc
0x4024b9 0x4024be 0x4024c3 0x4024c8 0x4024d8 0x4025e1 0x4025e6 0x4025eb
0x40260c 0x40261c 0x402e23 0x4031f0 0x403ee0`. They are: `jmp`/`call` stubs into
the EH helpers at `0x404e78`/`0x404eb0`; funclet epilogues (`popl %ebp; retl`);
an SEH unregistration tail at `0x402e23` (`movl -0xc(%ebp),%ecx; movl
%ecx,%fs:0x0`); one field accessor at `0x4031f0` (`movl 0xb8(%ecx),%eax; retl`,
followed by `int3` padding — a genuine standalone function Ghidra missed); and
one window helper at `0x403ee0`, which is
`SendMessageW(hwnd, WM_SYSCOMMAND 0x112, 0xF012, 0)` — `SC_MOVE|HTCAPTION`, the
standard trick for dragging a window by its client area. fw104's 7 in-band
recovered starts are the same shapes.

### 6.2.4 fw104 `[0x401000,0x401bb0)` is MFC init, not vendor code [D]

Working memory carried this as "~3 KB of real vendor code adjacent to its device
layer, touching `0x5c69b0`/`0x5c6a5c`, unanalyzed", and it sits immediately below
fw104's vendor band, so it looked like the most likely place for device code to
be hiding outside the band. **It is MFC framework initialization.**

Ghidra defines exactly one function in the whole range — `guard_check_icall` at
`0x401bb0`, three bytes, `retl` — while `gapscan.py` recovers **173** starts
there by address-taken pointer. That gap is the whole reason the region looked
unexamined.

What it actually contains, from the strings its code references: the block of
`RegisterWindowMessage` calls that create MFC's internal window messages —
`AFX_WM_ON_AFTER_SHELL_COMMAND`, `AFX_WM_DRAW2D`, `AFX_WM_RECREATED2DRESOURCES`,
`AFX_WM_PROPERTYCHANGED`, `AFX_WM_ON_CHANGE_RIBBON_CATEGORY`, the whole
`TOOLBAR_*` family, `commctrl_DragListMsg`, and 40-odd more. One small function
per message, each storing its result to a global, each referenced from an init
table rather than called — which is exactly why 173 of them are pointer-recovered
and none are in Ghidra's list.

**The one genuine connection to the device layer, and it is worth having.**
`0x5c69a8` is not a scalar. `FUN_00401000` initialises it:

```
401000  calll 0x40561d           ; get the framework's nil-string object
40100d  calll *0xc(%eax)         ; virtual call, slot 3
401010  addl  $0x10, %eax        ; skip the 16-byte CStringData header
401018  movl  %eax, 0x5c69a8     ; -> the buffer pointer of a global CString
```

and the **enumerator** `FUN_00401bc0` writes the device path into it:

```
401d20  movw  (%eax), %cx        ; wide-string length loop
401d23  addl  $0x2, %eax
401d29  jne   0x401d20
401d2b  subl  %edx, %eax
401d2d  sarl  %eax               ; (end - start) / 2 = character count
401d2f  pushl %eax               ; length
401d30  pushl %ebx               ; pointer to the path
401d31  movl  $0x5c69a8, %ecx    ; `this` -- thiscall
401d36  calll 0x402810           ; assign
```

So **`0x5c69a8` is a global `CString` holding the path of the opened device**,
and the updater keeps it for the lifetime of the process. cfg107 does the same
thing at `0x00580190` (§9.3), so this is a house pattern across both tools, not
an updater quirk. The `CString` identification rests on the MFC layout — a
buffer pointer 16 bytes past a header object, assigned by a thiscall taking
(pointer, character-count) — which is `[D]` as structure; what the framework
calls the type is convention, and nothing downstream depends on the name.

**Nothing in the range touches HID or SetupAPI**, by the exhaustive slot scan of
§6.2.3.

### 6.2.5a Wave 2 — the other 4,343 bodies, and what reading them settled [D]

The 474 of §6.2.5 were the bodies unique to fw110. The remaining **4,343**
distinct bodies each appear byte-identically in at least one other binary, which
is what `classify.py` calls library code. `CLAUDE.md` §6.1 as amended says a
match is **no longer an alternative to reading it**, on the grounds that reading
the matched set is the only empirical check on that classifier's premise. So
they were read too — 64 batches, all 64 returned.

| check | result |
|---|---|
| functions reported vs. sent | 4,343 / 4,343 |
| invented function ids | **0** |
| call targets not present in the body | **4 of 17,522** (0.023%) |
| import slots not present in the body | **1 of 3,727** |

The readers were asked a different question this time — not "does this touch the
device" (§6.2.5 showed that was unanswerable from the input) but **"does this
read as application code written for one product, or as general-purpose framework
code?"**, judged on the instructions rather than on the `also_in` list they were
shown.

**Result: 1 of 4,343 flagged as vendor-specific, at low confidence, and it is a
false positive.** `0x0049d694` is `GetMDITabsContextMenuAllowedItems` — MFC's
`CMDIFrameWndEx`, present in cfg104 and cfg107 as well, building a bit-mask of
which context-menu items are allowed on MDI tabs. It reads as application policy
because it *is* policy; it is just MFC's, not Endgame's.

**And the category distribution contains no `device-io` at all**: mfc-atl 2,043,
windows-ui 1,466, string-or-container 236, c-runtime 219, cpp-runtime-eh 88,
math 85, file-io 64, thread 42, allocator 35, registry 33, unclear 32.

So the amended §6.1's empirical check has been carried out and `classify.py`'s
premise survived it: **4,343 bodies it called library, read, and none of them is
vendor code.** Together with §6.2.6 — none of the 14 device functions matches
anything elsewhere — the classifier is now bounded from both sides.

### 6.2.5b Updater 1.10 is read — the partition [D]

`CLAUDE.md` §6.1 requires every one of 1.10's functions to end in a known state,
and §6 requires it stated as a partition computed in one place:

> **CORRECTED 2026-09-04.** The second identity below was computed by
> *subtraction* — "duplicates = 5232 − 4817" — and that is exactly the move
> `CLAUDE.md` §6 forbids, in the section whose whole purpose is to show a
> partition closing. The subtraction silently absorbed **39 functions that have
> no normalised body hash at all**: `classify.py`'s `sigs()` skips bodies under
> 16 bytes, so those 39 were never in the read queue and were never a
> "duplicate" of anything. They have now been read, by hand, and are enumerated
> below. The corrected partition:

```
total sized functions .......................... 9076
  settled mechanically (microread.py) .......... 3844
  required a reader ............................ 5232
     with a normalised body (>= 16 bytes) ...... 5193
        distinct .............................. 4780   every one in the read queue
        duplicates within fw110 ...............  413   covered by reading one instance
     TOO SMALL TO HASH (< 16 bytes) ............   39   read by hand, 2026-09-04
```

`3844 + 5193 + 39 = 9076`, and `4780 + 413 = 5193`. Both identities close, and
neither is a subtraction. The read queue holds 4,817 hashes, a **superset** of
the 4,780 actually required, so the reading was over- not under-covered.

**The 39, in full**, because a residue that is not enumerated is not accounted:

| shape | members |
| --- | --- |
| member accessor, `mov eax,[ecx+N]; test; jne; mov eax,[eax+0x20]` | `0x0041347a` `0x00453e3a` `0x0048cb8b` `0x004e8de7` `0x00508c9e` `0x0048ebb3` |
| conditional virtual call, `cmp [ecx+N],0; je; mov eax,[ecx]; push 1; call [eax+4]` | `0x00404da8` `0x0040bdca` `0x0040f3a2` `0x0041e428` `0x00432548` `0x00499111` |
| bool-normalising virtual call, `call [eax+N]; neg; sbb; inc` | `0x00461c34` `0x004dcd4b` `0x004dcd5c` `0x0048793b` (`GiveFeedback`, masks to `0x40102`) |
| predicate returning 0/1 | `0x00471014` `0x004c7233` |
| conditional tail call | `0x0045759a` `0x004a643c` `0x004b39c8` `0x004edd58` `0x004cadf2` `0x004caf83` |
| refcount / destructor | `0x00404206` (`lock xadd`) `0x0041c1d4` (`~CDialogTemplate`) |
| CRT and compiler helpers | `0x004f735c` `__security_check_cookie`, `0x004fa1c3` `_ldiv`, `0x004f7c6a` `_CallMemberFunction1` thunk, `0x004fa5f8` `0x00502c52` EH cleanup funclets, `0x005086fc` `fstp/fld` |
| Ghidra labels *inside* a larger function, not functions | `0x004c0043` (3 B `sub (%esi),%ebx`), `0x005052c8` `0x00505d58` `0x00506498` (6 B `movlpd 4(%esp),%xmm0`, no return), `0x005062bc` (`and byte [ebp-0x2c8],0xFE`), `0x00505643` (`or cl,cl; je`), `0x00508f04` (`call *%eax; ret`) |

**None of the 39 touches the device**, which is not a judgement: none appears in
§9's 14-member closure, and `closure.py`'s exhaustive scan attributes every
HID/SetupAPI/kernel32 device-I/O slot reference in `.text` to an owner, so a
device reference inside any of them would have put it in the seed.

Beyond the function list, the rest of the file is accounted for by §6.2 (whole-file
partition, zero residue), §6.2.2 (every DARK byte belongs to a known function),
and §6.2.3 (no recovered start references a HID/SetupAPI slot).

### 6.2.6 `classify.py`'s worst case is empirically excluded for the flasher [D]

`classify.py` identifies library code by cross-binary body match, and its own
docstring names the way that can go wrong:

> "This tool is sound only if the vendor shares no source between the updater
> and the config tool. That is UNPROVEN, and there is direct reason to doubt it:
> the two tools use the same transport design … If a shared source file were
> compiled into both, its functions would byte-match and be silently labelled
> 'library' — and those would be exactly the protocol functions that matter."

That is a live risk, not a hypothetical: §1 of `notes/config-protocol.md`
establishes that the two tools **do** share the transport design — same report
IDs, same lengths, same byte-1 framing, the same five `GetLastError` retry codes.

**Tested directly.** Normalised body hash of each of fw110's 14 device-closure
members, looked up in cfg100, cfg101, cfg104, cfg107, fw104 and xm1r:

**0 of 14 match anything in any other binary.**

So the vendor did not compile a shared transport source file into both programs.
The design is common; the code is not. `classify.py`'s premise holds exactly
where a failure would have been most expensive, and this is now a measured
result rather than an assumption carried in a docstring.

Two of the fourteen sit below `classify.py`'s 16-byte hashing cutoff and are
excluded from hashing by construction, so they were read instead:
`0x00402f00` (16 B) is `pushl %ebp; movl %esp,%ebp; movl 0x8(%ebp),%eax; pushl
%eax; calll 0x403750; xorl %eax,%eax; popl %ebp; retl` — a thin `cdecl`
forwarder into **enter-bootloader** that discards the result and returns 0 — and
`0x00402f10` (15 B) is the same shape forwarding into **`0x403960`, the flash
worker**. Neither adds protocol; both matter only as the entry points the UI
calls.

### 6.2.5 The 474 bodies unique to 1.10 were read, and the readers were checked [D]

Of fw110's 5,232 functions that `microread.py` could not settle mechanically,
**474 have a normalised body that appears in no other binary in the corpus** —
the slice where vendor code, if any is left unfound, has to be. All 474 were
read, in 22 batches, and every batch returned.

**The readers were verified, not trusted** (`CLAUDE.md` §6.2). Each was required
to return two fields checkable against the bytes, and the prompt stated no
expected answer, gave no protocol context, and told them the Ghidra names are
sometimes wrong:

| check | result |
|---|---|
| functions reported vs. functions sent | 474 / 474, none missing |
| function ids not present in any batch (invention) | **0** |
| call targets reported that do not appear in the body | **1 of 4,686** (0.02%) — `0x4fd6d`, a dropped digit |
| import slots reported that do not appear in the body | **0 of 876** |

**The planted positives produced a real failure, and it was mine.** fw110's
three seed functions were in the batches, unlabelled. Two were flagged
`touches_device: true`; `0x004012a0` — the `HidD_SetFeature` sender, the single
most important function in the flasher — was flagged **false**, with
`confidence: low`. The reader was right to. The disassembly it was given carries
**no symbol names**, so `calll *0x51b1d4` is unidentifiable as HID from the input
alone; the question was unanswerable from what I supplied. It said so instead of
guessing. False positives across all 474: **0**.

The lesson is about harness design, not about readers: **do not ask a reader a
question the input cannot answer.** "Which functions touch the device" is
already settled mechanically and exhaustively by `closure.py` (§9), and that
answer is authoritative. Nothing a reader says overrides it.

**What the pass corroborates.** The command byte maps in §3 were re-derived
independently, by readers with no access to these notes:

| §3 | independently reported |
|---|---|
| `A0 03` start (§3.2) | "dword `0x3a0` at offset 0" |
| `A0 06` write block (§3.3) | "word `0x06a0` at offset 0, 16-bit checksum at 4–5, `0x400` bytes copied to offset `0x10`" |
| `A0 07` read block (§3.4) | "two-byte header `0xA0 0x07`" |
| `A1 08 34` whole-image checksum (§3.5) | "bytes `0xa1, 0x08, 0x34` and the caller's byte at offsets 0..3, `Sleep(0x64)`, 64-byte read" |
| `A1 3A … 5A A5 32` enter bootloader (§3.1) | "first eight bytes `A1 3A 00 00 00 5A A5 32`, sent up to nine times with increasing delay, then polls `0x401000(0x1977)`" |
| busy-poll (§2) | "`buffer[1] == 4` … delay grows 0, `0x64`, `0xc8` … up to `0x7d0`" |
| retry error set (§2) | "only for codes `0x15, 0x17, 0x1d, 0x57, 0x65b`" |

Including the bootloader PID `0x1977`, which appears in that reader's summary
without ever having been mentioned to it.

**Composition of the 474**, which is itself the answer to "is there vendor code
hiding here": windows-ui 210, mfc-atl-framework 150, string-or-container 39,
c-runtime 16, cpp-runtime-eh 16, unclear 13, **device-io 10**, math-or-float 10,
thread-or-sync 4, registry 3, file-io 3. The ten device-io are exactly the
functions §3 and §5 already document. Nothing else in the set touches the device,
and the mechanical scan of §9 says the same thing independently.

One reader also flagged `0x004c0043` as "three bytes decoding as `subl
%ebx,(%esi)` followed by `hlt`, with no prologue, no control flow" — arriving at
the same conclusion as §6.2.2's separate analysis, that Ghidra defined a function
at an address that is not an instruction boundary, without being prompted to look
for one.

### 6.2.7 Updater 1.04's unique bodies are read — partition and error rate [D]

Work-list item 3, second code base. The 3,185 normalised bodies that are unique
to 1.04 after subtracting the 4,817 the 1.10 read already covered. 72 batches,
**72 returned, 0 failed**.

**Mechanical check** (`verify_read.py`, which scores only the two fields a
script can recompute — see its docstring for what it cannot):

| | count |
| --- | --- |
| functions in the batches | 3,185 |
| reported by the readers | 3,184 |
| **invented ids** | **0** |
| missing | 1 — `0x00534bf5` |
| call targets in the truth set | 13,401; **21 functions disagree** (16 extra, 9 missed) |
| indirect-call slots in the truth set | 6,683; **12 functions disagree** (8 extra, 5 missed) |

`0x00534bf5` was read by hand: 36 bytes, calls `GDI32!GetViewportOrgEx`
(`*0x56e060`) on the handle at `this+8` and copies the returned `POINT` to the
caller's out-parameter. MFC device-context accessor; no device relevance.

**Judgement-field precision, measured rather than assumed.** 24 functions came
back flagged `vendor_specific`. 13 are the already-derived device layer and its
neighbours (§5.8, §6.3). The other **11 are all false positives, and all one
idiom**: `0x0040b0d9`, `0x004efbfc`, `0x004efc28`, `0x004efc54`, `0x004efc80`,
`0x004efca7`, `0x004efcd2`, `0x004efcfe`, `0x00535812`, `0x0053583d`,
`0x0053a469` — 33 to 44 bytes each, every one an MFC `ON_UPDATE_COMMAND_UI`
handler that computes a bool from a member (`this+0x344` against 0…5,
`this+0x13c`, `this+0x38c`) and calls `CCmdUI` vtable slot `+0x0` or `+0x4`.
Each was disassembled; **none is in the device closure**. Precision of the flag
on this run: **13 of 24**. `CLAUDE.md` §6.2 asks for the reader's error rate to
be measured instead of assumed — this is it for the judgement field, and it is
the number to quote, not the 0.6% mechanical one.

**One substantive reader error, caught and corrected.** The summary for
`0x00404760` says the `A1 3A` frame is "retried up to ten times". It is
**nine**, and the bytes are unambiguous: `0x40477c movl $0x2,%ebx`, loop bottom
`0x404821 addl $0x2,%ebx` / `0x404824 cmpl $0x14,%ebx` / `jl 0x404791`, so the
entry values are 2, 4, … 18. **Nine, the same as 1.10's**, which is what §5.7's
cross-version agreement requires; the reader's number would have contradicted
it. The frame itself is confirmed: `movl $0x3aa1,-0x44(%ebp)` +
`movl $0x32a55a00,-0x40(%ebp)` = `A1 3A 00 00 00 5A A5 32`. The retry sleep is
`Sleep(2k)` on pass `k` — 2 ms to 18 ms, not the hundreds used elsewhere.

### 6.2.7a `0x004a704e`, fw104's largest function, is library — positively [D]

Flagged during the fw104 read as a summary with nothing behind it (the reader
called it "a ~29 KB settings-document loader over a path built from a global",
which is not supported by anything in the bytes). Now settled the other way, and
the argument needs no judgement at all:

| | fw104 `0x004a704e` | cfg100 `0x00466e71` | XM1r `0x004e99e5` |
| --- | --- | --- | --- |
| size | **29,309** | **29,309** | 29,177 |
| instructions | **6,940** | **6,940** | 6,922 |
| `%eax` operands | 694 | 694 | 700 |
| calls to one helper | 410 | 410 | 410 |
| `-0x10(%ecx)` operands | 387 | 387 | 387 |
| next three operand counts | 274 / 241 / 174 | 274 / 241 / 174 | 274 / — / 174 |

Two Endgame programs **from different code bases** and one program from a
**different vendor entirely** each contain a 29 KB function with the same
instruction count and the same operand profile. It is framework code. Its head
is an MFC `__EH_prolog` with a `0x404`-byte frame and a virtual call through
vtable slot `+0x328`; it is not in fw104's device closure and it references no
HID, SetupAPI or kernel32 device-I/O slot.

**And it is a measured false negative for `classify.py`.** The three normalised
body hashes all differ, so the cross-binary matcher does *not* pair them — the
differences lie in operand bytes that `norm()` does not mask. The matcher is
therefore conservative: it under-reports library code rather than over-reporting
it, which is the safe direction, but "no cross-binary match" must not be read as
"vendor". This is the first measured instance of that.

The other three `movb $0xa0` / `movb $0xa1` sites `cmdscan.py` reports outside
the command builders are inside this same body — a store of a single byte to
`-0x4(%ebp)`, not to a report buffer. Same in cfg100 (`0x00466e71`) and the XM1r
(`0x004e99e5`). They are noise in the scan, not an eighth command.

### 6.2.8 Three of the nine binaries were built with Control Flow Guard [D]

Not a protocol fact, but it removes a large source of noise from every future
read and it explains a boundary already in these notes.

In **fw104, cfg100 and the XM1r**, every indirect call is preceded by
`calll *<one .rdata slot>` whose stored value is a `.text` address holding
`c2 00 00` — a bare `retl $0` — i.e. `_guard_check_icall_nop`:

| binary | slot | target | `ff 15` sites |
| --- | --- | --- | --- |
| fw104 | `0x0056e990` | `0x00401bb0` | 7,724 |
| cfg100 | `0x005809c8` | `0x00402a10` | 7,717 |
| XM1r | `0x0066fcc4` | `0x005eb93b` | 11,493 |

fw110 / fw107 / fw106 and cfg107 / cfg104 / cfg101 have no such slot; their
most-referenced indirect targets are ordinary imports at a few hundred sites.

Two consequences worth carrying:

1. **The single most-referenced "import slot" in those three binaries is not an
   import.** Any tool or reader that treats `calll *<abs>` as an API call will
   attribute thousands of sites to one non-existent API. `verify_read.py`
   counts it correctly — it only claims "indirect call through an absolute
   address" — but a human reading its output should know what dominates it.
2. **It names the boundary in §6.2.4.** `0x00401bb0` is exactly where fw104's
   MFC-init region stops because `0x00401bb0` *is* the CFG nop, the last thing
   the linker placed before the vendor band at `0x00401bc0`.

The CFG split also lines up with the code-base split this file already uses:
1.04 and cfg100 are the second code base, and they are the CFG builds.

### 6.2.9 The cross-binary read partition — and the hole it found [D]

`Tools/ghidra-export/readpartition.py`, written 2026-09-04 because the reading
plan's coverage had never been stated as a partition, only as per-binary counts.

**The hole.** Work-list item 3 queued, per binary, the bodies *unique* to that
binary — present in it and in none of the other eight. That is a sound way to
make each binary contribute a different portion, and it has a remainder that no
per-binary count can show: **a body shared by exactly two non-fw110 binaries is
unique to neither, so it enters no queue at all.** fw104 and cfg100 are the same
code base; 1,639 bodies are shared by exactly those two. None of the individual
numbers was wrong. The remainder was simply never computed.

```
$ python3 Tools/ghidra-export/readpartition.py

binary     sized in-queue  micrord   hand    OPEN  open bytes
fw110       9076     5193     3844     39       0           0
fw107       9076     5193     3844     39       0           0
fw106       9076     5193     3844     39       0           0
fw104      10763     3244     4168      0    3351      376037
cfg107      9528     4896     4177      0     455      137844
cfg104      9495     4854     4159      0     482      158484
cfg101      9516     4947     4158      0     411      129156
cfg100     11071     2550     4348      0    4173      446699
xm1r       23001     9540    10332      0    3129      228973

distinct OPEN bodies, union over all binaries: 4497
```

**The three 1.10-family binaries close to zero**, which is the row that matters
for the flasher and it is now zero without a subtraction anywhere (§6.2.5b).
1.06 and 1.07 close for the same reason 1.10 does: identical `.text`, so their
hashable bodies are the same bodies, and `microread.py` was run for them too
rather than assumed — before that it had never been run for either, and the tool
said so in its own output instead of silently scoring them as open.

**The 4,497 are the true remainder of items 3 and 4** and they are a reading
queue, not an estimate: `readpartition.py --queue` writes each one with a
representative `(tag, entry, size)`. Median 68 bytes, 90th percentile 245,
largest 7,423; about 640 KB of disassembly in total.

Stated plainly so it is not mistaken for a smaller claim: **until that queue is
read, "most of every other executable" is true of the updaters and the config
tools by bytes, and is not yet true by bodies.** The number to quote is 4,497,
regenerated, never copied from this paragraph.

### 6.2.2 The DARK residue is fully accounted for [D]

`gapscan.py` partitions `.text` into KNOWN/CALLED/PTR/PAD/DARK, where DARK means
"nothing accounts for these bytes". It is small — 1,195 bytes in 158 runs for
fw110, 0.10% — but it was the only part of any code section with no evidence
attached, so it is precisely where something unknown could sit. `darkclass.py`
settles it.

| | fw110 | fw104 | cfg107 | cfg104 | cfg101 | cfg100 |
|---|---|---|---|---|---|---|
| DARK runs | 158 | 168 | 183 | 164 | 162 | 176 |
| past a function's declared end | 122 | 143 | 148 | 131 | 129 | 147 |
| interior bytes of a real instruction | 36 | 25 | 35 | 33 | 33 | 29 |
| **unexplained** | **0** | **0** | **0** | **0** | **0** | **0** |

**Every DARK byte in every binary belongs to a function Ghidra already knows
about.** Around 80% lie past the function's declared end — the same short-extent
defect `microread.py` measures directly, 29–71 functions per binary whose final
instruction overshoots Ghidra's `size` by 1–5 bytes. The rest are interior bytes
of a real instruction. Nothing is code Ghidra missed entirely.

**Method note, because the first two attempts at this got it wrong and the way
they were wrong is the point.** Judging the runs against a single linear
disassembly of all of `.text` gave 12 "independent" runs for fw110, then 7. Both
numbers were artefacts. A linear sweep **desyncs on data islands and the desync
is silent**: fw110 `0x40b6a0` holds an eleven-entry switch jump table, and the
sweep runs straight through it and comes out of phase, so the real MSVC prologue
at `0x40b6cc` (`8b ff` `55` `8b ec` = `movl %edi,%edi; pushl %ebp; movl
%esp,%ebp`) looks like it starts mid-instruction. On that evidence I was about to
record 19–42 "Ghidra function starts that are not instruction boundaries" per
binary as a finding about Ghidra. **It is a finding about the sweep.** Ghidra is
right at `0x40b6cc`; the sweep is not.

The fix is to re-disassemble from each function's **own start**, which resyncs by
construction. That is what `darkclass.py` and `microread.py` both do, and it
takes the residue to zero. The general rule, which now has two independent
confirmations: `dis.sh` over a wide range is a finding-aid, and its output must
never decide a boundary question.

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

## 6.2.10 The reading is harvested and the partition closes — 2026-09-05  [D]

LIST 1 item 3's residue read had landed but had never been written into
`read_<tag>.json`, so `coverage.py` was still counting thousands of functions as
unaccounted that had in fact been read. Harvested from
`scratchpad/read7` paired with workflow `wf_a0798ea3-0cc`'s journal:
**4,503 function results, 4,268 distinct normalised bodies.**

### The partition, per binary

`readpartition.py`, all nine binaries:

```
binary     sized in-queue  micrord   hand    OPEN  open bytes
fw110       9076     5193     3844     39       0           0
fw107       9076     5193     3844     39       0           0
fw106       9076     5193     3844     39       0           0
fw104      10763     6560     4168     35       0           0
cfg107      9528     5313     4177     38       0           0
cfg104      9495     5298     4159     38       0           0
cfg101      9516     5320     4158     38       0           0
cfg100     11071     6689     4348     34       0           0
xm1r       23001    12619    10332     50       0           0
```

**Distinct OPEN bodies, union over all binaries: 0.** `coverage.py` agrees
independently: `UNACCOUNTED 0` for both fw110 and cfg107, with 1 fw110 function
resting on a Ghidra FunctionID name alone and **0 such functions at ≥32 bytes**.

### What that number does NOT say, stated before anyone quotes it

1. **Scope.** The 9,076 are Ghidra's *exported functions*. They do not cover
   `.text`; `gapscan.py` puts DARK at 0.1036% of fw110's `.text` bytes and
   0.1116% of cfg107's. A residue of 0 means the LIST is accounted for. It is
   not a claim about the binary (§1.2b).
2. **In-queue means QUEUED, not READ**, and `readpartition.py` says so itself.
   Counting a queue whose read never landed turns a real hole into a clean row,
   so here is the shortfall:

   | | queued | harvested | short |
   | --- | --- | --- | --- |
   | all queues | 23,764 | 18,529 | 5,235 |
   | **excluding xm1r** | 15,759 | 15,433 | **326 (2.07%)** |
   | xm1r alone | 8,005 | 3,096 | 4,909 |

   The xm1r shortfall is a decision, not a gap: it is a different product AND a
   different vendor code base, and resuming its read was deliberately declined.
   The **326** is a real, small hole in the *reading* evidence — those bodies are
   covered by being in a queue, not by a reader having returned them. It does
   not touch the device question, which §6.2 settles mechanically by
   `closure.py` and never by anything a reader said.

### The read's own failure rate, which was recorded wrong

`working-memory.md` carried "112/112 batches, 4,503 functions, **0 failures**".
The journal disagrees, and reconciling it matters because §6.2 says a failure
rate of zero is a red flag rather than a pass:

```
distinct batch keys: started 112, result 112, failed 40
failed keys that also have a result (retried, succeeded): 40
failed keys with NO result (genuinely lost):               0
attempt-level failure rate: 40/152 = 26.3%
```

So "0 failures" was true at the **batch** level and misleading unqualified.
Nothing was lost; **26.3% of individual attempts failed and were retried.** That
is the number worth having, and it is healthy — a reading harness that never
fails is one that is not being checked.

## 6.3 What was looked for and NOT found

`CLAUDE.md` §6 forbids silent sampling, and the corollary is that a search which
returns nothing is a result, not a non-event. This section collects the
confirmed-nothings, because they are most of the evidence that the search was
wide rather than lucky, and because they are the first thing that quietly
disappears from a document that only records discoveries.

Each row says what was searched, over what, and by what method. **A zero here is
only worth the scope beside it**, so the scope is stated, and where the method
has a blind spot it is named rather than left implied.

| looked for | over | found | method, and what it cannot see |
|---|---|---|---|
| bytes in a code section that no structure accounts for | `.text` of **all 7** binaries with a Ghidra export — the XM1r added 2026-09-04 | **0** | `darkclass.py`; every DARK run resolves to a known function's tail or an instruction interior. Rests on per-function re-disassembly, not a linear sweep. The XM1r is the interesting row: `gapscan.py` reports **7.20% of its `.text` DARK** (183,399 bytes in 19,045 runs) against ~0.1% for the Endgame binaries, and **all 19,045 runs still resolve** — 1,589 interior bytes, 17,456 past a short declared end, **0 unexplained**. A 70× larger residue with the same explanation is worth more than another 0.1% row. |
| bytes in the *file* that no structure accounts for | all 9 binaries, whole files | **0** | `filemap.py`; partition must sum to the file size or it exits non-zero. Says where bytes are, not what they mean. |
| HID/SetupAPI slot references outside every Ghidra function | `.text` of 6 PE binaries | **0** | `closure.py`, exhaustive 4-byte scan attributing each hit to a function or orphan unit. Blind to a slot address held only in a register. |
| `E8` calls into a HID/SetupAPI IAT thunk | all 7 binaries | **0** | Byte scan for `E8` targeting one of the 14 thunk addresses. xm1r has all 14 thunks and nothing calls them. Blind to a computed thunk target. |
| `GetProcAddress`-resolved HID entry points in the updaters | fw110, fw104, xm1r | **0** | Name-string method that finds 11 in every config tool, so it is calibrated to succeed where such slots exist. |
| fw110 device-closure functions byte-matching another binary | 14 members × 6 binaries | **0 of 14** | Normalised body hash. Excludes the one failure mode `classify.py`'s own docstring warns about (§6.2.6). |
| `FWFILE` resources, or any 66,560-byte resource, in a config tool | cfg100/101/104/107, full resource trees | **0** | `filemap.py`'s exhaustive walk, partition closing to zero — not a search that could have missed a branch. |
| an eighth updater command | all 1,154,048 bytes of fw110 `.text` | **0** | Two independent methods (§3.7a): every report-id immediate stored to memory, and every caller of the only two device-touching APIs. |
| a bootloader PID `0x1977` reference in cfg107 | all five call sites of the matcher | **0** | Call-site enumeration, after a literal scan was shown to be the *wrong question* (§3a.1 of `notes/config-protocol.md`). |
| function bodies whose instructions will not tile their extent | 7 binaries, 82,450 functions | **0** | `microread.py`; a body that does not tile exactly is refused rather than approximated. |
| rel32 call sites that could not be attributed to an owner | all 9 binaries | **0** | `calledges.py`, after orphan units were added; before that, sites in unattributed `.text` were silently dropped. |
| device-touching functions among the 474 bodies unique to fw110 | 474 read bodies | **10, all already documented** | Independent readers, verified against the bytes; plus the mechanical scan of §9 agreeing. |
| invented call targets in the reading pass | 4,686 reported targets | **1** (0.02%) | Every reported target checked against the function's own disassembly. |
| invented import references in the reading pass | 876 reported slots | **0** | Same check. |
| **any network capability at all** | all 9 binaries — import table, delay-import table, and every DLL-name string in ASCII and both UTF-16 phases | **0** | Three routes. No binary imports or delay-imports `wininet`, `winhttp`, `ws2_32`, `wsock32`, `urlmon`, `httpapi`, `iphlpapi`, `netapi32`, `mswsock`, `dnsapi` or `rasapi32`; and none of those names occurs as a *string* either, so there is no name for `LoadLibrary` to take. The only `.dll` string in any binary that is not an imported DLL is cfg107/cfg100's `hid.dll`, already documented in `config-protocol.md` §2. **Blind spot:** a DLL name assembled at runtime from pieces, and COM-brokered networking — for the latter, no `WinHttp`/`XMLHTTP`/`MSXML`/`ServerXMLHTTP` ProgID string exists in any binary either. |
| **a fourth route to the device** — any user-mode API that could reach a USB or HID endpoint other than HID.DLL, SETUPAPI.dll and KERNEL32 handle I/O | all 9 binaries: full import directory, the delay-import directory, and every module-name string ending `.dll`/`.drv`/`.sys` in ASCII and UTF-16 | **0** | The import directory lists 17–20 DLLs per binary and the **delay-import directory is empty in all nine**. Across every module-name string in every binary — 28 to 36 distinct names each — the only device-capable ones are `hid.dll` and `setupapi.dll`. No `winusb`, `cfgmgr32`, `newdev`, `libusb*`, `ftdi*`, no `.sys`. So the three routes `closure.py` seeds on are the three that exist. **Blind spot, and it is real:** fw110 contains the format string `%s%s.dll`, so a name *can* be built at runtime — the names it builds are MFC's language-satellite modules (`acomctl32.dll`, `eshell32.dll`, `wuser32.dll` are all present as literals), but the method cannot prove that is all it builds. |
| a URL of any kind | all 9 binaries, ASCII and UTF-16 | **0 outside the manifest** | The only `http://` strings are `http://schemas.microsoft.com/SMI/2005/WindowsSettings` in the embedded application manifest. The only `connect` hits are MFC's shell-restriction name table (`NoNetConnect`/`Disconnect`). |
| a menu, a context menu or an accelerator table in any updater | full resource directory of fw110/107/106/104 | **0** | `rsrc.py`; the walk is the same one whose partition closes to zero in `filemap.py`, so a missing branch is not available as an explanation. A command reachable only from a menu therefore does not exist — there is no menu. |
| vendor text in the updaters' `RT_STRING` tables | all 88 non-empty entries × 4 updaters | **0** | `dlgdump.py --strings`. Every entry is stock MFC framework text. Says nothing about `.rdata` literals, which is the next row. |
| a vendor string in the updater band beyond the 13 flow messages, `'FWFILE'` and 4 MFC artefacts | every 4-byte window of 1.10's `[0x401000,0x4040ad)` and 1.04's `[0x401bc0,0x405200)` | **0** | `bandlit.py`; scans at every byte offset rather than at instruction boundaries, so it over-reports. Blind to a string address computed at runtime and to one reached via a resource id — the latter is covered by the two rows above. |
| a firmware-update capability in the config tools' *user surface* | all 11 dialogs × cfg100/101/104/107, every control and caption | **1 dialog, and it is unreachable** | `dlgdump.py` + `dlgref.py`. `DIALOG 131` is a firmware-update dialog and no code in any of the four instantiates it; see `config-protocol.md` §12. This row is in the "not found" table on purpose — the honest statement is that the surface exists and the code to reach it does not. |

Two of these were **not** zero when first computed, and both changed because the
method was wrong rather than because the binary was:

- "Recovered starts that reach the device" looked like 12, then 7, before
  per-function re-disassembly took it to 0 (§6.2.2).
- "Config-tool functions touching HID" was 7 for months. It is 13. The scan was
  exhaustive over the wrong set (§9.3).

Which is the argument for writing the scope next to every zero.

**The two network rows are worth stating as a positive**, because they bound
something our own tools must not quietly exceed: **Endgame's config tool and
firmware updater are entirely offline programs.** They do not check for updates,
do not fetch a firmware image, and do not report anything. Every firmware image
they can install is compiled into the executable (§8), which is why §1.4 of
`CLAUDE.md` is implementable at all — there is no download path to have to
reproduce, and a version of our tool that acquired one would be doing something
the vendor's never did.

*Thread closed, so it is not re-chased:* the lead that prompted this check was a
reader's summary of fw104 `0x0040845a` — "builds a request string tagged with a
fresh identifier, `CoCreateGuid`". It is benign and dead. `*0x56e970` is
`ole32!CoCreateGuid`, the 16 bytes at `0x59bc90` it copies in first are an
all-zero GUID template, the rest of the body formats the result field by field,
and the function has **no callers at all**. A GUID generator is not a network
client, and this one is not even reachable.

## 6.4 Re-derivation log — which claims were re-proven, and how

`CLAUDE.md` §7 requires this analysis to stand on its own, and §1.2b requires
Ghidra's derived views never to decide anything. A claim first written from a
decompiler view or from a function-list argument is not retired by being
plausible; it has to be re-derived. This records what has been, by what route,
and — as importantly — **what has not**.

**Re-derived from raw disassembly or a raw byte scan, 2026-09-04:**

| claim | §  | independent route used |
|---|---|---|
| the seven commands and their byte layouts | 3 | whole-`.text` immediate scan (`cmdscan.py`) **and** caller enumeration of the two device APIs — two methods with no shared blind spot |
| the same seven in the 1.04 code base | 5.7 | the same whole-`.text` scan on fw104 |
| block index = `i + 0x34`, 5 write attempts, `Sleep(200)` | 3.8, 5.4 | read at `0x403a48`–`0x403b5f`: `xorl %ebx,%ebx`, `leal 0x34(%ebx),%ecx` at `0x403a8b` for the write and `0x403aff` for the verify, `cmpl $0x5` at `0x403aae`, loop bound `0x21c(%esi)` |
| `A1 08 34 <last>` whole-image checksum, result at `resp[16..19]` | 3.5 | re-read, and independently reproduced by a reader with no access to these notes (§6.2.5) |
| busy-poll: `resp[1]==0x04`, +100 ms, 2000 ms budget | 2 | read at `0x40135d`–`0x40139d`; fw104 `0x401ed0` agrees (`cmpb $0x4`, `cmpl $0x7d0`) |
| the enumerator's sole ownership of every HID/SetupAPI slot | 5.8 | exhaustive 4-byte slot scan attributing each hit to a function — 1 function per slot in both updaters |
| send/recv wrapper call sites | 3.7a | raw `E8` scan: 7 sites each, resolving to the same 6 functions |
| only resource 140 is reachable | 8.2 | the type string's address occurs **once in the whole file**, plus closure of the other three resource-API routes |
| the device-capable API surface is exactly three routes | 6.3 | Import directory + empty delay-import directory + exhaustive module-name string scan, all nine binaries. This is the row that answers "how do you know the seed is not missing a fourth route", which is the fair question to ask after the seed was corrected twice in one day (§9.3, `config-protocol.md` §2.1) |
| the device closure is 14 and 10, all in band | 9 | raw call edges (`closure.py`), replacing Ghidra's call graph. Re-confirmed 2026-09-04 after the seed gained the kernel32 device-I/O route: **both updater closures are byte-for-byte the same set**, because no updater imports `DeviceIoControl` and every `ReadFile`/`WriteFile` in them belongs to the CRT or MFC |
| every DARK byte belongs to a known function | 6.2.2 | per-function re-disassembly (`darkclass.py`) |
| `0x5c69a8` is the device-path `CString` | 6.2.4 | read at `0x401000` and `0x401d20`–`0x401d36` |
| the repair-loop defect (we deviate from it) | 5.6 | re-read `0x401c90`–`0x401e61`; the `-0x424` slot proven single-assignment by scanning the body for its displacement |
| the four resource APIs are `FindResourceW` / `SizeofResource` / `LoadResource` / `LockResource` | 4, 8.3 | resolved from the **import table**, the loader-authoritative source, not from Ghidra's slot names: `0x51b230` `0x51b220` `0x51b228` `0x51b224` |
| the load path performs no transform | 8.3 | re-read `0x403200`–`0x403313`: `shrl $0xa` → `obj+0x21c`, `LockResource` → `obj+0x25a3c`, `movl $0x1,%ebx` as the copy stride, then four `movzbl`/`movb`/`addl %ebx` groups per iteration into `obj+0x220` |
| the checksum is a plain 32-bit additive sum | 8.4 | re-read `0x403580`–`0x4035f4`: four accumulators, `movzbl` at `+(-1,0,1,2)`, `addl $0x4`, `cmpl $0x400`, partials summed at `0x4035d8`, `addl $0x400` per chunk |
| §4's entropy, §8.1's census, §8.5's chunk structure | 4, 8.1, 8.5 | resource tree re-walked from the PE data directory by a second parser: all `FWFILE` are 66560 = 65×1024 remainder 0, H 7.941–7.952; 140 has 31 distinct chunks of 65 with exactly one repeat run, chunks **29–63**, `cefe77fb6c23f0d4…`, byte-identical in all four updaters; 133/135/137 frozen, 140/142 change every release, 143 only in 1.10 |
| §3.7b's response offsets, every row | 3.7b | each response buffer re-read from raw disassembly and every displacement off its base enumerated: `0x401890`, `0x401980`, `0x401ad0`, `0x401bb0`, `0x403750`, `0x403bc7`, `0x403dcf`. Three refinements resulted (the `A0 07` bulk copy, the discarded `A1 13` read, and three citation fixes) — the table's content stands |

**Not re-derived, and still resting on their original derivation:**

*(Nothing in `updater-protocol.md` is left on this list as of 2026-09-04. The
entry that stood here — §3.7b's response byte offsets — was re-derived; see the
row above and the corrections in §3.7b itself.)*
- The XM1r file — substantially re-derived 2026-09-04 (`notes/xm1r-flasher.md`
  §3.1, §3.2a, §5.1, and the §6.9 correction), but it is analogy, not evidence
  (§1 there), so it is not held to this standard and nothing in it may inform an
  OP1 byte.

**A parser trap found while doing this, recorded because it produced a
convincing wrong answer for several minutes.** Resource-directory strings are
**length-prefixed and NOT NUL-terminated** (`IMAGE_RESOURCE_DIR_STRING_U`: a
`WORD` count then that many UTF-16 code units). A throwaway re-walk that scanned
for a NUL ran off the end of `FWFILE` into the next string, produced the type
name `"FWFILE\x11AFX_DIALOG_LAYOUT"`, and therefore reported that **updater 1.04
has no `FWFILE` resources at all** — which would have contradicted §8.1 and
§8.5's cross-version claim. The checked-in tooling is correct
(`filemap.py:resname`, `rsrc.py:walk` both use the length prefix); it was the
second parser that was wrong. The general point is the one §6.3 keeps making: a
negative result is a claim about the method first and the binary second.

The distinction is the point. A note that says everything is verified is
indistinguishable from a note that has not checked, so the second list has to
exist for the first to mean anything.

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
| `HDEVINFO` `0x0056a11c` | **5** | **0** |
| HID GUID `0x0056a120` | **3** | **0** |
| `caps.Usage` `0x0056a130` | **2** | **0** |
| `caps.UsagePage` `0x0056a132` | **1** | **0** |
| `RequiredSize` out-param `0x0056a118` | **1** | **0** |
| device path `CStringW` `0x0056a1a0` | **3** | **2**, and both are the CRT's |
| `0x00569ff0` — write-only, see below | **1** | **0** |

**Two corrections to this table, 2026-09-04.** Both were found by re-reading
`FUN_00401000` line by line rather than by any new tool, which is the argument
for §6.4 continuing to exist.

1. **`0x0056a11c` and `0x0056a120` were labelled the wrong way round.** The
   order of the two SetupAPI calls settles it and nothing else is needed:
   `0x401015` pushes `0x56a120` into `HidD_GetHidGuid` (`*0x51b1dc`), so
   `0x56a120` *receives* the GUID; `0x401032` then passes that same `0x56a120`
   as `ClassGuid` to `SetupDiGetClassDevsW` (`*0x51b498`, with
   `Flags = 0x12 = DIGCF_PRESENT|DIGCF_DEVICEINTERFACE`) and stores the returned
   handle to `0x56a11c` at `0x401044`. `0x56a11c` is then passed first-argument
   to `SetupDiEnumDeviceInterfaces` at `0x401073`. So `0x56a11c` is the
   `HDEVINFO` and `0x56a120` is the GUID. The occurrence counts were right; only
   the names were wrong. Nothing downstream depended on either name.
2. **Two device globals were missing from the table entirely** — the very thing
   this table exists to make impossible. `0x56a1a0` is a global ATL `CStringW`
   assigned the winning `DevicePath` at `0x4011c1`–`0x4011c6`
   (`mov $0x56a1a0,%ecx; call 0x4016e0`, the `(PCWSTR,int)` assign, with the
   length computed by the inline `wcslen` at `0x4011ab`–`0x4011bd`).
   `0x569ff0` is a static dword set to **5** at `0x401095` and **never read
   anywhere in `.text`** — the exhaustive 4-byte scan finds exactly one
   occurrence, that store. It is the discarded `cbSize` of a static
   `SP_DEVICE_INTERFACE_DETAIL_DATA`: the value the code actually uses is
   written to the *heap* struct at `0x4010bc` (`movl $0x6,(%esi)`), which is the
   correct 32-bit `SP_DEVICE_INTERFACE_DETAIL_DATA_W` size, whereas 5 is the
   ANSI one. Dead, and harmless because dead.

**The one non-zero "outside band" cell, stated rather than rounded away.**
`0x56a1a0` is referenced twice outside the band, at `0x519245` and `0x51a3a1`.
Neither is vendor code and neither reaches the device: `0x519244` is
`movl %eax,0x56a1a0` inside the CRT static-initialiser run, immediately followed
by `push $0x51a3a0; call 0x4f755c` registering `0x51a3a0` with `atexit`; and
`0x51a3a1` is the first instruction of `0x51a3a0` itself, the matching
destructor, which does an interlocked decrement of the string's reference count.
That is the compiler constructing and destroying a global `CStringW`. It is
reported here rather than filtered out because a table of zeroes that quietly
excludes its own exceptions is worth nothing.

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

## 8.0 Structure of the `FWFILE` images

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

**This is not in conflict with §8.5's "the key is unchanged across the release
history", and the two are easy to misread as contradictory.** They are
statements on different axes: *across resource names* the filler ciphertext
always differs, *across releases of one name* it never does. §8.5a tabulates
both at once and turns the pair into the wrong-image guard.

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

> **ARGUMENT CORRECTED 2026-09-04. The conclusion survives; the reasoning as
> written did not.** This section said an exhaustive scan finds one reference to
> the `FWFILE` string "therefore exactly one `FindResourceW` call site". That
> inference is invalid, and the premise it leans on is false as stated:
> `FindResourceW`'s IAT slot is referenced at **12 sites** in fw110 and **16** in
> fw104. Most of them are MFC loading dialogs, strings and menus. What is true is
> the narrower thing — only one of those sites can pass `FWFILE` as the type —
> and it needs the argument below, not a count of string references standing in
> for a count of call sites.

**The type string's address occurs exactly ONCE in the entire file** — not once
in `.text`, once in all 2,133,504 bytes of fw110 and all 2,427,904 of fw104. So
no code pushes it anywhere else and no data structure anywhere holds a pointer to
it:

| | `L"FWFILE"` VA | occurrences of that address, whole file | at |
|---|---|---|---|
| 1.10 | `0x5447c0` | **1** | `.text` `0x403208` |
| 1.04 | `0x5a1cd8` | **1** | `.text` `0x40433a` |

and that single site is the `FindResourceW` call below, with the name immediate
`0x8c`. The other eleven `FindResourceW` sites pass some other type and cannot
reach a `FWFILE` resource.

**The other three ways in are closed too**, which is what makes this an API bound
rather than a string scan:

- **`FindResourceExW`** is imported and *is* referenced — fw110 `0x4f37fe`,
  fw104 `0x4b4a01` — so ignoring it would have been a real hole. Its single site
  passes `lpType = 5` (`RT_DIALOG`) as an integer immediate with a language id
  `0xfc11`; it is MFC's localized-dialog loader and cannot name a string type.
- **`EnumResourceNamesW`/`A` are not imported** in either binary, so the
  resource directory cannot be walked to discover names at runtime.
- **`LoadResource`, `LockResource`, `SizeofResource`, `FreeResource`** all take a
  handle produced by a `FindResource*` call. They cannot originate a lookup, so
  their many reference sites do not widen the surface.

**Blind spot, stated rather than implied:** this finds the type string's address
as a 4-byte literal. A type name assembled at runtime — built byte by byte,
decrypted, or composed from fragments — would not appear in any of these scans.
Nothing in these binaries suggests that, and the `lpName` immediate is a
compile-time constant in all four, but the scan cannot exclude it.

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

### 8.5a Which blob is ours — §6's open question, largely closed  [D]

§0.1.1 left this open and it deserved better than "the tool hardcodes 140".
The evidence is the ciphertext itself, and it needs no cipher identification.

**Fact 1 — every resource name has its own filler ciphertext, and it never
changes across releases.** Chunks 29–63 are one chunk repeated 35 times in all
21 blobs (verified: `chunks29..63 all equal` for each). Its SHA-256, by name:

| name | filler chunk SHA-256 (first 12) | 1.04 | 1.06 | 1.07 | 1.10 |
| --- | --- | --- | --- | --- | --- |
| 133 | `a6632c2e2504` | ✓ | ✓ | ✓ | ✓ |
| 135 | `fe2e7a5c0920` | ✓ | ✓ | ✓ | ✓ |
| 137 | `a744b0294fe5` | ✓ | ✓ | ✓ | ✓ |
| **140** | **`cefe77fb6c23f0d4cb19fc709232bed0035ba41cc1413d46688172d3e6c6ffda`** | ✓ | ✓ | ✓ | ✓ |
| 142 | `ec43b18e4646` | ✓ | ✓ | ✓ | ✓ |
| 143 | `e0cac456c107` | — | — | — | ✓ |

Six distinct values over 21 blobs and **zero collisions between names** — no
1024-byte chunk value whatsoever is shared by two different resource names.

**Fact 2 — 140 and 142 change in lock-step, on identical chunk indices.**

| release step | 140 changes | 142 changes |
| --- | --- | --- |
| 1.04 → 1.06 | chunks `8, 27, 28, 64` | chunks `8, 27, 28, 64` |
| 1.06 → 1.07 | chunks `0–28, 64` (30) | chunks `0–28, 64` (30) |
| 1.07 → 1.10 | chunks `0–28, 64` (30) | chunks `0–28, 64` (30) |

133/135/137 change on **no** chunk in any step. So the six split cleanly:
three frozen, two version-tracking in lock-step, one that appears in 1.10.

**Fact 3 — all four updaters identify as the same product.** Every
`VERSIONINFO` string in 1.04, 1.06, 1.07 and 1.10 says `OP1 8k v2 Firmware
Updater`, including 1.04, whose PDB path says only `OP1 8k`. The `EL1` trace is
confined to 1.10's PDB path and occurs once in the whole file (§0.1.1).

**What that settles.** 1.04 is the OP1 8k v2 updater by its own version
resource, and it predates the copied EL1 project directory. Its `FWFILE`/140
has filler ciphertext `cefe77fb…`. **1.10's 140 has the same filler ciphertext
and the same lock-step change profile with 142 that 140 had in every earlier
step.** For 1.10's 140 to be a different product's image, that image would have
to reproduce a filler ciphertext that no other resource in the corpus
reproduces, and continue 140's own three-release change pattern. **1.10's
`FWFILE`/140 is the same image lineage as 1.04's.** The sixth blob 143 arrived
alongside it with a filler ciphertext shared with nothing — an addition, not a
substitution.

**What it does not settle**, stated so it is not read as more: this is an
argument about *lineage*, not about which physical mouse 140 is for. That comes
from §8.2 — only 140 is reachable, the tool sends it to whatever answers at VID
`0x3367` / PID `0x1978`, and that is our device. And the inference "different
filler ciphertext ⇒ different key" needs the premise that the filler
*plaintext* is the same in all six, which is `[G]`. The facts above are `[D]`
and the lineage conclusion rests only on them.

**A wrong-image guard that costs nothing and runs before the first byte**
(`CLAUDE.md` §2 requires the guard to be ours):

```
size == 66560                                   exactly 65 x 1024
chunk[i] == chunk[29]  for all i in 29..63       the 35x filler run
sha256(chunk[29]) == cefe77fb6c23f0d4cb19fc709232bed0035ba41cc1413d46688172d3e6c6ffda
sha256(whole)     == 8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0
```

The last line alone pins the exact image; the three before it are what still
holds if Endgame ships a 1.11 and we deliberately move to it. **Note the third
line is the discriminator that would have caught a swapped 143** — its filler
is `e0cac456c107…`.

### 8.5b The cipher, measured — statistics, what they rule out, and what they do not

Added 2026-09-05. §8.5 recorded the observations; this section adds the numbers
behind them, corrects one claim, and states each ruling's blind spot so nobody
re-opens this as a task. **The cipher is still not identified, and §8.3 still
means we do not need it: the host never decrypts and `egg-flash` ships resource
140's bytes unmodified.**

Regenerate everything here from `.analysis/res/<tag>/tFWFILE_n<name>_l2052.bin`
(21 blobs: fw104/fw106/fw107/fw110 x names 133/135/137/140/142, plus 143 in
fw110 only). Ground truth also frozen at `scratchpad/fwfile/ground-truth.md`.

#### It is encryption, not compression  [O]

| measure | value | uniform-random expectation |
| --- | --- | --- |
| per-block Shannon entropy | 7.79-7.83 | ~7.83 ceiling for 1024 bytes |
| byte chi2, whole file | 5137.5 | 255 +- 23 |
| byte chi2, **deduplicated** (n=31744) | **243.7** | 255 +- 23 |
| byte chi2, all 314 distinct blocks pooled (n=321536) | **247.1** | 255 +- 23 |
| mean pairwise bit distance, distinct blocks | 4096.5 / 8192 | 4096 |
| internal period at unit 4/8/16/32/64 B | none; every unit distinct | none |

**The whole-file chi2 of 5137 is an artefact and must not be quoted.** 35 of the
65 blocks are the same filler block, so that one block's sampling noise is
counted 35 times. Deduplicate before testing uniformity. Having done so, the
byte distribution is indistinguishable from uniform, which **rules out
compression** — LZ/deflate output is measurably non-uniform.

#### Which blocks change between releases  [O] — and a correction

`.text`-style intuition does not transfer here; the answer depends on the pair.

| pair (name 140 and 142 alike) | content blocks differing |
| --- | --- |
| fw104 -> fw106 | **4** — blocks 8, 27, 28, 64. Blocks 9-26 byte-identical. |
| fw106 -> fw107 | all 30 |
| fw107 -> fw110 | all 30 |

**Correction:** a working draft of this analysis asserted that all 30 content
blocks differ between *any* two versions. That generalised from the 106/107 and
107/110 pairs and is false. §8.5's original statement was correct and is
restored here with the numbers attached.

#### Diffusion is bidirectional, and that is the load-bearing measurement  [O]

In **every** differing block, in **every** version pair, the first differing byte
is byte **0**, and 1015-1024 of 1024 bytes differ (99.1-100%). The
full-avalanche expectation for independent random bytes is 1024 x 255/256 =
1020, and the observations sit on it.

The `fw104 -> fw106` pair is what makes this an argument rather than a
statistic. Blocks 9-26 are byte-identical across it, so nothing in the image
shifted; the edits in blocks 8, 27, 28 and 64 are therefore localized and
in-place. Yet each of those four differs **from byte 0** across ~99.5% of the
block. An edit late in a block is changing byte 0 of the same block.

So diffusion runs in **both** directions inside the 1024-byte unit. That rules
out, together:

- **CBC** with any IV, fixed or derived — a plaintext change leaves every
  earlier ciphertext block untouched;
- **CFB**, **OFB**, **CTR**, and any stream cipher whose state is not
  plaintext-dependent — same reason, forward-only propagation.

Consistent with what remains: a wide-block 1024-byte SPRP (EME/XCB/HCTR-class),
or any two-pass construction that chains forward and then backward. Neither is
confirmed.

**Blind spot, stated because rule §1.2a requires it:** the argument assumes the
four edits really were localized. The evidence is that adjacent blocks did not
move, which is strong but is not proof. A rebuild that happened to change the
first 16 bytes of exactly blocks 8, 27, 28 and 64 and nothing else would fit the
data equally well and would leave CBC alive. Nobody has found a way to separate
those two readings from ciphertext alone.

#### The key is indexed by resource name and has never been rotated  [O]

Each resource name has its own filler ciphertext, constant across all four
releases:

| name | filler ciphertext (sha256[:12]) | constant across |
| --- | --- | --- |
| 133 | `a6632c2e2504` | fw104, fw106, fw107, fw110 |
| 135 | `fe2e7a5c0920` | fw104, fw106, fw107, fw110 |
| 137 | `a744b0294fe5` | fw104, fw106, fw107, fw110 |
| 140 | `cefe77fb6c23` | fw104, fw106, fw107, fw110 |
| 142 | `ec43b18e4646` | fw104, fw106, fw107, fw110 |
| 143 | `e0cac456c107` | fw110 only |

The six are pairwise ~50% bit-different (4055-4164 of 8192), so no relation
between names is visible. Under the `[G]` that the filler plaintext is the same
constant in all six — almost certainly erased flash — this says the key or tweak
is a function of the resource name and is **unchanged across the product's
entire release history**.

#### Layout, identical in all 21 blobs  [O]

Blocks 0-28 content, blocks 29-63 the filler repeated 35 times, block 64
content. 30 content blocks, so ~30 KiB of firmware in a 65 KiB container. The
filler repeating byte-identically at 35 different offsets is what establishes
that the transform is **position-independent** at 1024-byte granularity: same
plaintext block, same ciphertext block, wherever it sits.

#### What decrypting this would and would not buy

**Would:** the `A0 11` settings handler, which is the one thing that could
upgrade the record `0x01`-`0x04` policy (`ConfigRecord.h`, `kDefaultUnknownBytes`)
from a defensible default to a derivation. That policy is already decided,
already matches the vendor's 73 captured writes, and is one line to reverse.

**Would not:** anything about whether the bootloader validates the image it is
given. Blob block *i* is device block `0x34 + i` (§10.1), so the 65 blocks span
`0x34`-`0x74` and **the bootloader, which lives below `0x34`, is in none of the
21 blobs.** CLAUDE.md §2's load-bearing assumption stays untestable short of a
flash, and every guard against a wrong image therefore stays ours. A previous
note claimed the opposite; it was wrong.

**Nothing in `egg-flash` is gated on any of this.**


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
6. Assert §8.5a's four-line guard on the extracted bytes before anything else
   happens. It is four comparisons and it is the only thing between us and
   flashing another product's firmware.

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
> **Superseded 2026-09-04, later the same day.** Those seed numbers were
> themselves an undercount for the config tools and the XM1r: the seed was
> built from HID/SetupAPI slots only, and the vendor's hidapi reaches the
> device through kernel32 on the device handle instead. Seeds are now
> 3, 3, 17, 17, 17, 17, 8 and closures 14, 10, 38, 38, 38, 36, 31.
> **The two updater rows are unchanged in both columns.** Full account and the
> live read channel it was hiding: `config-protocol.md` §2.1 and §10.
>
> **(b) The config-tool closures were understated by 7–8 each.** Published as
> 25/24/24/23; then 32/32/32/30; now 38/38/38/36 after the kernel32 route above. Two causes, both now fixed: the closure was
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
## 11. The user-visible surface — the other half of every negative in §6.3  [D]

`CLAUDE.md` §1.2a: *"Check the user-visible surface before concluding a
capability is missing: strings in both encodings, `.rsrc` dialogs and their
control captions, menus, message maps. A feature the vendor ships has a button
somewhere."*

Every negative in §6.3 was argued from code — a scan found no eighth command, no
network import, no fourth device route. Those arguments are only as good as the
set they ran over, and §1.2a exists because this project has twice had a scan
that was exhaustive over the wrong set. This section is the independent half:
**what the program offers its user, enumerated from the resources and from the
literals, with no reference to any of the code arguments.** It was produced
after those arguments, and it agrees with all of them.

Tools: `Tools/ghidra-export/dlgdump.py` (dialog templates, DLGINIT, string
table), `Tools/ghidra-export/dlgref.py` (is a dialog reachable at all),
`Tools/ghidra-export/bandlit.py` (every constant in a VA range, resolved).

### 11.1 The updater's entire user interface is one button  [D]

`dlgdump.py fw110` and `dlgdump.py fw104` produce **identical** output.
One vendor dialog, four controls:

| id | class | caption |
|---|---|---|
| 1000 | `msctls_progress32` | — |
| 1001 | `BUTTON` / `PUSHBUTTON` | `'Update Firmware'` |
| −1 | `STATIC` | `'Status:'` |
| 1017 | `EDIT` | — |

The dialog's own caption is `'Endgame Gear OP1 8k v2 Firmware Updater'`. The
only other two dialogs in the file are MFC's stock `30721` (`'New'`) and the
empty `30734`, and `dlgref.py` shows **neither is referenced by any code** — a
useful control, because it means "0 immediate sites" is a signal this method can
actually produce, not an artefact of looking in the wrong place.

The rest of the surface, from the resource directory (`rsrc.py`):

| resource type | fw110 | fw107 | fw106 | fw104 |
|---|---|---|---|---|
| `RT_MENU` | **0** | **0** | **0** | **0** |
| `RT_ACCELERATOR` | **0** | **0** | **0** | **0** |
| `RT_DIALOG` | 3 | 3 | 3 | 3 |
| `RT_STRING` blocks | 13 | 13 | 13 | 13 |
| `FWFILE` | 6 | 5 | 5 | 5 |

**No menu bar, no context menu, no accelerator table, in any of the four.** All
88 non-empty `RT_STRING` entries are stock MFC framework text (`'Open'`,
`'Print'`, the `AFX_IDP_*` diagnostics); **not one is vendor text.** The vendor
put its own strings in `.rdata`, which is where §11.2 finds them.

So the updater has exactly one control the user can press, one progress bar, and
one read-only status line. There is no second flow to have missed.

### 11.2 The thirteen messages, and the flow told in the vendor's own words  [D]

Every string the vendor band can reference, exhaustively: `bandlit.py` reads
every 4-byte window of the band as a little-endian dword, keeps the ones landing
inside the image, and resolves each. Over 1.10's band `[0x401000,0x4040ad)` and
1.04's `[0x401bc0,0x405200)` the result is **18 distinct strings each, and the
same 18** — a fact worth its own line, since the two are different code bases
compiled fifteen years apart:

- 4 are MFC/ATL build artefacts. Three are shared verbatim
  (`'Local AppWizard-Generated Applications'`, `'Exception thrown in
  destructor'`, and the `ASSERT` format — `'%s (%s:%d)'` in 1.10 against
  `'%Ts (%Ts:%d)'` in 1.04). The fourth is the compiler's own path to
  `afxwin1.inl`, and it is `…Visual Studio 10.0\VC\atlmfc…` in 1.10 against
  `…Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\atlmfc…` in 1.04 —
  independent corroboration of §0.1's two-code-base finding, from a string
  neither analysis went looking for.
- 1 is `'FWFILE'`, the resource type name (§8).
- **13 are the flow.** They sit in one contiguous `.rdata` run in both binaries,
  with nothing else inside it, and each is referenced from exactly one place
  (the first from two):

| # | message | referenced from | function's role |
|---|---|---|---|
| 1 | `'Loading firmware file failed'` | `0x40321f`, `0x403262` | `0x403200` — `FWFILE` load (§8.3) |
| 2 | `'Read firmware file failed'` | `0x40345f` | `0x4033b0` — the Update button (§5.1) |
| 3 | `'Mouse firmware current version %.2f'` | `0x4034ce` | " |
| 4 | `'Device not found'` | `0x40352e` | " |
| 5 | `'send bldr request failed'` | `0x40388e` | `0x403750` — enter bootloader (§5.3) |
| 6 | `'Open bldr device request failed'` | `0x403949` | " |
| 7 | `'send bldr start request failed'` | `0x4039df` | `0x403960` — flash (§5.4) |
| 8 | `'send firmware block data...'` | `0x403a31` | " |
| 9 | `'send block data request failed'` | `0x403ad0` | " |
| 10 | `'get all check sum error'` | `0x403bb6` | " |
| 11 | `'send bldr complete request failed'` | `0x403c6b` | " |
| 12 | `'Update Succeed, current firmware version is V%.2f'` | `0x403e2a` | " |
| 13 | `'Update failed, try again'` | `0x403e8c` | " |

**Read the middle column as a derivation in its own right.** The messages are
laid out in `.rdata` in flow order, they land in exactly four functions, and
those are the same four functions and the same order that §5.1–§5.4 derived from
the code. Nothing in this section used the code path; it used the string table
and a literal scan. Two independent routes to the same sequence.

It also puts a ceiling on the protocol from a direction the command scans
cannot: **there are messages for entering the bootloader, starting, sending
blocks, checksumming, completing, succeeding and failing, and for nothing
else.** No message mentions erase as a separate step, a mode the user can
select, a device to choose between, a file to open, or a recovery path. That is
consistent with seven commands and one flow, and it is what a *user* would have
had to be told about had there been more.

Three absences worth naming, because they are design inputs for us, not just
corroboration:

- **No cancel, and no warning.** There is no `'Cancel'`, no `'Do not
  unplug'`, no `'Please wait'` — anywhere in either band. The vendor's tool
  neither offers an abort nor warns the user off one. Our §4.2 rule that the
  post-erase phase is non-abortable is therefore *ours*; it is not a behaviour
  we are copying, and the vendor's UI does not enforce it.
- **`'Update failed, try again'` is the vendor's entire recovery story**, and it
  is emitted from inside `0x403960` at `0x403e8c` — that is, after the point of
  no return. "Try again" is in fact the correct advice, but the tool does not
  say so and does not distinguish a pre-erase failure from a post-erase one.
- **`'get all check sum error'`** is the only message for a verification
  failure, which matches §3.5's single whole-image checksum command; there is no
  per-block verification message even though §5.5 shows a per-block repair loop
  exists. The vendor repairs silently.

### 11.3 The rest of the band's constants, so the string list is a partition  [D]

`bandlit.py`'s other buckets over 1.10's band, stated so that "18 strings" is a
result of an exhaustive pass rather than the output of a filter:

| bucket | 1.10 | what they are |
|---|---|---|
| `STR-U` | 18 | §11.2 |
| `STR-A` | **0** | the vendor band contains no ASCII string reference at all |
| `FUNC` | 2 | `0x402f00`, `0x402f10` |
| `DATA` | 64 | dominated by `0x51b1c4`–`0x51b4a4`, the IAT slots, and `0x5623c0`, the `/GS` cookie (651 sites image-wide — every function prologue) |
| `CODE` | 45 | jump-table entries and byte-aligned false positives |

The method scans at **every** byte offset, not at instruction boundaries, so it
over-reports rather than under-reports; that is the safe direction and it is why
`CODE` has 45 entries. What it cannot see is stated in the tool's docstring: an
address computed at runtime, and a string reached only through a resource id.
Both are covered from the other side — resource ids by §11.1, and runtime
computation by the fact that §11.1 finds no resource for one to reach.

### 6.2.10 The reader harness failed for the first time, and the number is 17%  [D]

`CLAUDE.md` §6.2: *"A harness that cannot produce a bad result is not evidence.
If the planted positives never fail and no verdict is ever rejected, the harness
is measuring nothing. Report its failure rate; a rate of zero is a red flag, not
a pass."*

The residue read (4,497 bodies, 112 batches) carried **six planted positives**,
unlabelled, appended to batches 000/020/040/060/080/100 — four from fw104 and
two from cfg107, every one of them device-facing *visibly in its own
disassembly*, so the question was answerable from the input given. Scored by
`verify_read.py --plants`:

| plant | binary | verdict | reader's category | reader's confidence |
|---|---|---|---|---|
| `0x00402a90` | fw104 | HIT | `device-io` | high |
| `0x00402c00` | fw104 | HIT | `device-io` | high |
| `0x00404760` | fw104 | HIT | `device-io` | high |
| `0x00404980` | fw104 | HIT | `device-io` | high |
| `0x00403850` | cfg107 | HIT | `device-io` | high |
| **`0x00403920`** | cfg107 | **MISS** | `string-or-container` | **high** |

**5 of 6. A measured false-negative rate of 17%, on a question the input could
answer, reported with high confidence.**

What `0x00403920` actually is, from its own bytes: a device read with a busy
poll. `0x403955` is `calll *0x52d1d8`, one of the eleven dynamically-resolved
HID slots (§9.3); `0x403961` tests the result against `1`; `0x40396a` reads
`resp[1]` and compares it to `3`; `0x403980` pushes `0x64` into
`calll *0x52d22c` — `Sleep(100)`. That is the config tool's busy convention,
already documented as `0x03` with a 1,000 ms budget (`build-design.md` §0). The
function is in cfg107's mechanically-computed device **seed** — it is one of the
seventeen functions that reference a device slot directly. A reader called it
`string-or-container`.

**This is the single most useful result the reading programme has produced**,
and it is worth more than any of the verdicts it was collected alongside:

1. **The design was right and is now measured, not assumed.** §6.2's rule that
   the device question is settled by `closure.py` and never by a reader is not
   caution, it is calibration: the readers miss roughly one device-facing
   function in six, *while saying they are sure*. Every device claim in these
   notes is mechanical, and this is why.
2. **The confidence field carries no information about correctness.** All six
   plants came back `high`. Do not weight by it, here or anywhere.
3. **The machine-checkable fields are a different animal entirely.** In the same
   run: **0 invented function ids** in 3,063 reported; **10 wrong call targets
   in 7,905** (0.13%), all of them omissions, none invented; **1 wrong import
   slot in 1,720** (0.06%), also an omission. So readers transcribe reliably and
   *judge* unreliably — which is exactly the split §6.2 assumed and had never
   put a number to.
4. **A 0% rate would have been the bad outcome.** The previous plant attempt
   scored 0/1 for a reason that invalidated it (fw110 `0x004012a0`, where the
   HID call is an unlabelled IAT slot and the input genuinely could not answer
   the question). This set was built so the input *could* answer it, and it
   still produced a failure. The harness works.

### 6.2.11 Final read partition — and the column that matters is zero  [D]

Regenerated 2026-09-04 after harvesting every workflow journal
(`harvest_reads.py --write`, then `readpartition.py`). "Unread" here is the
strict sense: **not** covered by a read body hash, **not** settled by
`microread.py` in that binary, **not** in `handread.json`.

**Regenerated 2026-09-05** (the figures below had gone stale as later read waves
landed; regenerate, never quote — §6). A function counts as read if its address
is in `read_<tag>.json` or `handread.json`, or `microread_<tag>.json` settled it
in that binary, or its normalised body was read in any binary.

| binary | sized functions | unread | unread bytes | **unread AND device-facing** |
|---|---|---|---|---|
| fw110 | 9,076 | **0** | 0 | **0** |
| fw107 | 9,076 | **0** | 0 | **0** |
| fw106 | 9,076 | **0** | 0 | **0** |
| fw104 | 10,763 | **0** | 0 | **0** |
| cfg107 | 9,528 | **0** | 0 | **0** |
| cfg104 | 9,495 | **0** | 0 | **0** |
| cfg101 | 9,516 | **0** | 0 | **0** |
| cfg100 | 11,071 | **0** | 0 | **0** |
| xm1r | 23,001 | 5,511 | 1,210,973 | **0** |

**Every Endgame binary is now fully read**, fw110 included, which is the §6.1
standard for the one whose bytes reach the mouse. The last of it was cleared on
2026-09-05: fw110's 27 (§6.2.12), fw104's 22 and cfg104's 1 (§6.2.13). xm1r is a
different product and a different code base, analogy-only (`notes/xm1r-flasher.md`
§1); its device-facing functions are all read by hand and its residue is now
zero in the column that matters.

### 6.2.12 The last 27 of fw110, read 2026-09-05 — and one of them was not a function

**`0x004f0054`, 2,617 bytes and the largest of the 27, is not a function.**
`FUN_004efed8` is recorded with size 381, which ends at `0x4f0055` — *one byte
past* where Ghidra puts the next entry, so the two overlap. Disassembling from
`0x4efed8` syncs cleanly and runs straight through: `0x4f004a` is a `jmp
0x4f080f` forward into the supposed second function, and `0x4f004f`'s `je
0x403032`-style branch goes back into the first. The split point itself,
`0x4f0054`, is the operand byte of the two-byte `je` at `0x4f0053` (`74 dd`).
It has **zero** call edges in an exhaustive `E8`/`E9` scan of `.text`. One
function, mis-split; `0x004efed8` was already read, so nothing was missing.

That is §1.2b with a number on it: **the coverage accounting inherits Ghidra's
boundaries, so a bad boundary manufactures a phantom unread function** — here
the single largest one in the binary, which is exactly the one a reader would
prioritise.

**The other 26 (4,136 bytes) are MFC framework code.** Read individually; the
mechanical facts, none of which rest on a reader's judgement:

- **25 of 26 also appear in cfg101/cfg104/cfg107** — a different application,
  same vendor. `0x00435914` is the exception and is `CMap`-style `RemoveAll`:
  walks the block list at `this+4`, releases each element through `0x401820`,
  zeroes `+4/+8/+0xc/+0x10`, frees the pool via `0x435c2e`. 11 callers.
- **0 of 26 lie inside 1.10's vendor band** `[0x401000,0x4040ad)`.
- **0 of 26 are in `closure.py`'s device seed or closure.**
- **0 of 26 reference** `L"FWFILE"`, the image-buffer fields (`+0x21c`,
  `+0x220`, `+0x25a20`, `+0x25a3c`) or any protocol byte.
- Every one opens with the MSVC hotpatch prologue `8b ff` (`mov %edi,%edi`) or
  the `push $N / mov $addr,%eax / call __EH_prolog3` SEH prologue, and
  dispatches through vtable offsets of `0x174`, `0x180`, `0x188`, `0x1a0`,
  `0x1c0`, `0x1c8`, `0x210`, `0x250`, `0x26c`, `0x338` — far deeper than any
  vendor class in this binary, and characteristic of MFC's `CWnd` hierarchy.
- The imports they reach are GDI/USER32 only: `BitBlt`,
  `CreateCompatibleDC/Bitmap`, `SelectObject`, `DeleteObject`, `InflateRect`,
  `OffsetRect`, `IsRectEmpty`, `SetRectEmpty`, `UnionRect`, `EqualRect`,
  `PtInRect`, `GetClientRect`, `GetParent`, `MapWindowPoints`, `ScreenToClient`,
  `GetSysColor`, `BringWindowToTop`, `RedrawWindow`, `TlsGetValue`,
  `Enter/LeaveCriticalSection`, `RaiseException`.

**This is also the test §6.1 asks for.** The amendment says a cross-binary body
match is a *hypothesis* — "a body appearing in an unrelated product is library"
— and that reading the matched set is how it gets tested, because one vendor
function that byte-matches elsewhere would invalidate the inference everywhere.
Twenty-five matched bodies were read and none is vendor code. The premise
survives this test; it is not proven by it.

### 6.2.13 fw104's last 22 and cfg104's last 1, read 2026-09-05 [D]

5,638 bytes. Every one is MFC window-layout and painting, and every one is
**outside fw104's vendor band** `[0x401bc0,0x405200)` — the lowest is
`0x00411d16`. None is in `closure.py`'s device seed or closure.

The imports they reach, which is the whole of their contact with the system:
`BeginDeferWindowPos`/`EndDeferWindowPos` (MFC's docking layout pass),
`GetWindowRect`, `GetParent`, `SetParent`, `SetWindowRgn`, `CreatePen`,
`LoadCursorW`, `CreateAcceleratorTableW`/`DestroyAcceleratorTable`,
`GetWindowTextW`/`GetWindowTextLengthW`/`GetWindowLongW`, `SendMessageW`,
`InvalidateRect`, `UpdateWindow`, `IsWindow`, `GetCursorPos`, `ScreenToClient`,
`MapWindowPoints`, `PtInRect`, `ReleaseCapture`, `MessageBeep`,
`GetViewportOrgEx`, `InflateRect`/`OffsetRect`/`IsRectEmpty`/`SetRectEmpty`,
`BitBlt`, `CreateCompatibleDC`/`CreateCompatibleBitmap`, `SelectObject`,
`DeleteObject`, `EqualRect`, `UnionRect`. **No HID, no SetupAPI, no file I/O.**

`fw104 0x00497aa4` (766 B) is the same double-buffered paint helper as fw110's
`0x00492c71`, recompiled.

**One thing worth naming, because it looks like an import and is not.** These
functions share an indirect call `call *0x56e990`, 41 sites. `0x56e990` is
**one slot past the end of fw104's IAT** (directory RVA `0x16e000`, size 2,448,
so the IAT ends at exactly `0x56e990`), it is in read-only `.rdata`, no
instruction anywhere stores to it, and the pointer it holds is `0x00401bb0` —
which is `ret 0` followed by `int3` padding up to the vendor band at
`0x00401bc0`. A const pointer to a do-nothing stub. Any tool that resolves
indirect calls by "nearest IAT entry below" will label these `CoTaskMemAlloc`,
which is the entry immediately before.

The last column is `readpartition.py`'s unread set intersected with
`closure.py`'s device closure **and** seed for that binary — the mechanical
filter, not a reader's opinion. **For every Endgame binary it is zero.** The
xm1r's 23 are its own flash path, written up in `notes/xm1r-flasher.md` §3–§5
by hand from the disassembly; they are unread by an *agent batch*, which is a
bookkeeping fact, not a knowledge gap. xm1r is analogy-only either way (§1 of
that file).

**The four fw110 vendor-band functions that were still unread are now read**, by
hand, and none is protocol:

| address | size | what it is |
|---|---|---|
| `0x00402170` | 58 | formatting call at `0x4f75e8` followed by a 0x51-arm `switch` (jump table `0x4021ac`, index byte table `0x4021bc`) mapping the result to an error path — the ATL string-format error dispatch |
| `0x00402970` | 112 | ATL `CStringT` assign-with-refcount: `lock xadd` on `[esi+0xc]`, virtual `Free` at refcount 0, otherwise the `(PCWSTR,int)` assign at `0x4016e0` |
| `0x004029e0` | 70 | `CStringT::ReleaseBufferSetLength`: bounds-checks the index, writes the NUL at `(ecx + eax*2)`, else `AtlThrow(0x80070057)` |
| `0x00403030` | 34 | MSVC scalar deleting destructor — `call dtor; test byte [ebp+8],1; call operator delete` |

With those, **all 95 functions of 1.10's vendor band `[0x401000,0x4040ad)` are
read**, and the two that carry the flasher — `0x00403200` (the `FWFILE` load,
§8.3) and `0x00403960` (the flash sequence, §5.4) — are the most heavily
re-derived functions in this file.
