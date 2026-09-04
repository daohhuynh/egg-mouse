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

### Firmware version display [D]
`FUN_004011f0` @ `0x004011f0` formats `DAT_0056a0f4` (the HID `VersionNumber`)
and divides by `100.0` for display: `L"Mouse firmware current version %.2f"`.
So **`bcdDevice` = firmware version × 100** (e.g. `140` → `1.40`). The tool
never gates on this value; it only displays it [D].

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
140 and that is all the code says. The six blobs and their cross-version
behaviour are recorded in `working-memory.md`; do not turn that table into a
belief about product mapping without evidence.

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
5. **Complete**: `0xA1/0x09`, `Sleep(50)`, read `0xA1`, require `resp[1] == 0x01`,
   then re-enumerate. Retried up to 11 passes with a growing `Sleep`. All fail →
   `L"send bldr complete request failed"`.
6. Wait for the device to come back: `FUN_00401000()` with `Sleep(d)`,
   `d = 800, 1600 … 16000`. Never returns → `L"Update failed, try again"`.
7. On success: send `0xA1/0x13`, `Sleep(900)`, one `0xA1` read, then display
   `L"Update Succeed, current firmware version is V%.2f"`.

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

## 6. Not yet derived — do not guess

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
- **What `0xA1/0x13` does.** Only ever sent after a verified success.
- **The mapping from block number to a flash address.** Block `0x34` is `[D]`;
  `0x34 * 1024` is `[G]`.
- **Which `FWFILE` belongs to which product.** The tool hardcodes 140 and says
  nothing about the other five.
- **What the device does with a `0xA0/0x06` whose `[2..3]` is outside
  `0x34..0x74`.** The vendor tool never sends one, so its behaviour is
  unconstrained by anything in the binary. Our flasher must never emit one.
- **Everything in `CLAUDE.md` §5.** No `[O]` exists yet; the device is not in
  hand. Nothing above has been checked against hardware.

## 7. Scope of the search — what was read, and what was not

`CLAUDE.md` §6 forbids silent sampling, so here are the numbers.

### Candidate sets
| binary | functions | named by Ghidra | unnamed `FUN_` | positively library¹ | left as candidates |
| --- | --- | --- | --- | --- | --- |
| 1.10 (= 1.07 = 1.06) | 9,076 | 7,388 | 1,688 | 921 | **767** |
| 1.04 | 10,763 | 4,043 | 6,720 | 2,873 | **3,847** |

¹ by `Tools/ghidra-export/classify.py`: a normalised function body that also
occurs in a program with different vendor code is MFC/CRT. One-directional — a
match proves library, a non-match proves nothing.

Cross-matching also exposed **two toolchain families**, which is why the 1.04
number is so much worse: `{1.06, 1.07, 1.10, cfg 1.01, 1.04, 1.07}` share ~5,000
normalised bodies, and `{1.04, cfg 1.00}` share 3,686, while across the families
the overlap collapses to ~100. 1.04 therefore has only one useful reference
binary instead of four.

Of 1.10's 767 candidates, **703 are under 16 bytes** (thunks and stubs; the
classifier deliberately does not hash bodies that small because they collide)
and only **64 are substantive**. Of 1.04's 3,847, 743 are tiny and 3,104 are
substantive.

### The vendor band
1.10's vendor code occupies `[0x00401000, 0x004040ad)` — **95 functions**, the
first objects the linker emitted, ending exactly where `AfxSetNewHandler` and
the MFC/ATL/CRT bodies begin. 1.04's occupies `[0x00401bc0, 0x00405200)` —
**89 unnamed functions**, interleaved with ATL template instantiations.

The band was **not** assumed from address locality. It was fixed from confirmed
vendor anchors (the HID enumerator, the two feature-report wrappers, the
`FWFILE` loader) and then checked.

### Why the protocol cannot be hiding outside the band
Every reference, in the whole 9,076-function image, to any HID or SetupDi import
and to every one of the vendor's device globals, is inside the band [D]:

| symbol | referencing functions | outside band |
| --- | --- | --- |
| `HidD_SetFeature` | `0x004012a0` | none |
| `HidD_GetFeature` | `0x00401330` | none |
| `HidD_GetHidGuid`, `HidD_GetAttributes`, `HidP_GetCaps`, `HidD_GetPreparsedData`, `SetupDiEnumDeviceInterfaces` | `0x00401000` | none |
| device handle `DAT_0056a174` | 6 refs | none |
| found flag `DAT_0056a0f8` | 4 refs | none |
| mode flag `DAT_0056a0fc` | 3 refs | none |
| `bcdDevice` `DAT_0056a0f4` | 3 refs | none |
| HID GUID / devinfo / caps `DAT_0056a11c`, `0056a120`, `0056a130`, `0056a132`, `0056a118` | 1 ref each | none |

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
