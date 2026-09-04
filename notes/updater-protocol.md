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

## 3. Commands

Byte offsets are into the buffer including the report ID at `[0]`.

| Report | `[1]` | Other fixed bytes | Len | Derived at | What the tool does with it |
| --- | --- | --- | --- | --- | --- |
| `0xA1` | `0x3A` | `[5]=0x5A [6]=0xA5 [7]=0x32` | `0x40` | `0x00403750` | **enter bootloader** |
| `0xA0` | `0x03` | `[16]` = argument | `0x411` | `0x00401890` | **bootloader start**; followed by `Sleep(3000)` |
| `0xA0` | `0x06` | `[4..5]` = checksum, `[16..1039]` = 1024 data bytes | `0x411` | `0x00401980` | **write one block** |
| `0xA0` | `0x07` | `[2]` = argument | `0x411` | `0x00401ad0` | **read one block back** |
| `0xA1` | `0x08` | `[2]=0x34`, `[3]` = argument | `0x40` | `0x00401bb0` | **read whole-image checksum**; result is the dword at response `[16..19]` |
| `0xA1` | `0x09` | — | `0x40` | `0x00403960` | **bootloader complete** |
| `0xA1` | `0x13` | — | `0x40` | `0x00403960` | issued after a successful update, then `Sleep(900)` and one `0xA1` read |

The 16-bit constants are stored by the compiler as one `mov` of a word or dword
(e.g. `local_48 = 0x3aa1` → bytes `A1 3A`), so they must be read
little-endian; both encodings appear and both were checked.

### Per-block checksum [D]
`FUN_00401980` @ `0x00401980` sums bytes `[0x10 .. 0x40F]` — the 1024 payload
bytes — into a **16-bit** accumulator and stores it at `[4..5]`. The loop uses
four interleaved adds and terminates at `0x410`, so the range is exactly the
1024 payload bytes and nothing else.

### Whole-image checksum [D]
`FUN_00403580` @ `0x00403580` sums **every byte of every block** into a
**32-bit** accumulator (four interleaved adds, `block_count × 1024` bytes) and
the result is stored at dialog offset `+0x25a20`. It is compared against the
dword the device returns for `0xA1/0x08/0x34`.

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
1. Copy the 1024 source bytes locally and compute the 16-bit sum.
2. Zero the 1041-byte response buffer.
3. `FUN_00401ad0(block)` = `0xA0/0x07` read-back.
4. If `resp[1] == 0x01`, compare the 1024 bytes at `resp[16…]` against the
   source byte for byte.
5. If the device's checksum at `resp[6..7]` differs from the computed sum, **or**
   the byte compare failed, re-issue the write `FUN_00401980(0xA0)` in a loop
   with `Sleep(d)`, `d = 0, 100 … 1900`, breaking when the response status
   becomes `0x01`.

So the vendor protocol **does** offer read-back and both a per-block and a
whole-image checksum. Per §4.2 our flasher mirrors all three.

## 6. Not yet derived — do not guess

- The meaning of `FUN_00401890`'s argument, written to `[16]` of the `0xA0/0x03`
  start command. Its value comes from a register at the `0x00403960` call site
  and needs disassembly-level confirmation.
- Likewise the register-passed arguments of `FUN_00401980`, `FUN_00401ad0` and
  `FUN_00401c90`. Ghidra is losing a custom/`__fastcall` convention on these,
  and the block index is exactly the byte a wrong reading would corrupt.
  **These must be confirmed against raw disassembly before any write path.**
- What `0xA1/0x13` does. It is only ever sent after a verified success.
- Whether `0xA0/0x03` erases. The name "bldr start" and the 3-second sleep are
  suggestive but the tool never says so. `[G]` — and §1.3 applies.
- Whether the device's whole-image checksum covers 65 KiB or something else.
