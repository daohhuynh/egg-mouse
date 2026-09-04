# Config tool — protocol notes

Derived independently from the vendor `.exe` files, statically. Nothing here has
touched hardware: there is **no `[O]` in this file**. Provenance tags per
`CLAUDE.md` §1.2.

Scope: **all four configuration tools** — cfg107, cfg104, cfg101, cfg100. Claims
state which binaries they were checked against. cfg100 is a **separate code
base** and is the cross-check on the other three, the way fw104 is for the
updaters. Function counts, from `.analysis/export/<tag>.jsonl` — regenerate with
`wc -l`, do not quote: cfg100 **11,071**, cfg101 **9,516**, cfg104 **9,495**,
cfg107 **9,528**. The gap is the point: cfg100 is not a rebuild of the others.

**Corrections applied 2026-09-04 are marked inline.** Two mattered: §2 undercounted
the device-facing surface by missing dynamically resolved HID entry points, and a
retracted section was left standing as live prose. Both are fixed below.

## 1. The headline: config and flasher share one transport [D]

`CLAUDE.md` §4.4 says to *establish* whether the two tools share a transport
rather than assume it, and `notes/updater-protocol.md` §6.1 left it open. It is
now settled for cfg107, and the answer is yes — the framing is shared, the
command numbers are not.

| | updater 1.10 | config 1.07 |
| --- | --- | --- |
| feature report IDs | `0xA0`, `0xA1` | **same** |
| buffer length for `0xA0` | `0x411` = 1041 | **same** (`movl $0x411,%esi`, `0x404238`) |
| buffer length for `0xA1` | `0x40` = 64 | **same** (`leal 0x40(%ebx),%esi` with `ebx=0`) |
| byte 0 | report ID | **same** |
| byte 1 | command out, status in | **same** |
| bulk payload offset in `0xA0` | `+0x10`, 1024 bytes | **same** (`rep movsl` 0x100 dwords to `-0x45c(%ebp)` = base `-0x46c` + `0x10`, `0x404222`–`0x404233`) |
| `HidD_SetFeature` retry error set | `0x15,0x17,0x1D,0x57,0x65B` | **same** (`0x40389b`–`0x4038af`) |
| status byte read from response | `resp[1]` | **same** (`movb -0x53(%ebp),%bl` with base `-0x54`, `0x404292`) |
| return on send failure | literal `2` | **same** (`movb $0x2,%al`, `0x404264`) |

That is the same house code, not a coincidence of conventions. The practical
consequence for us is direct: **one `EGGCore` transport serves both executables**,
as `CLAUDE.md` §3 hoped but did not assume, and proving the transport once in
§4.4 stage 1 proves it for both.

**What is *not* shared is the command space.** Updater commands are `0x03`,
`0x06`, `0x07`, `0x08`, `0x09`, `0x13`, `0x3A`; cfg107's four send sites use
`0x02`, `0x11`, `0x12`, `0x13`. Do not read across.

**Nor is the busy convention shared, and this one is a trap** — the receive
wrappers are structurally near-identical, so the difference is easy to miss by
reading one and assuming the other:

| | updater `0x401330` | config `0x403920` |
| --- | --- | --- |
| "ready" status in `resp[1]` | `0x01` | `0x01` — same |
| **"busy" status in `resp[1]`** | **`0x04`** | **`0x03`** |
| backoff step | +100 ms per pass | +100 ms per pass — same |
| **total budget** | **`0x7d0` = 2000 ms** | **`0x3e8` = 1000 ms** |
| initial sleep before first re-read | none | fixed `Sleep(0x64)` = 100 ms |

Verified in raw disassembly on both sides: config `0x40396e cmpl $0x3` and
`0x4039d7 cmpl $0x3e8`; updater `0x40135d cmpb $0x4,0x1(%edi)` and
`0x40139d cmpl $0x7d0`. fw104's receiver `0x401ed0` agrees with fw110 —
`0x401f08 cmpb $0x4`, `0x401f4d cmpl $0x7d0` — so the updater value is confirmed
across two code bases, not read once.

**A transport that hardcodes one busy byte is wrong for the other tool.** It is
a parameter, not a constant.

## 2. Device-facing surface — 17 functions, in three routes [D]

> **CORRECTED TWICE, and the shape of both errors is the same.**
>
> *First*, 2026-09-04: the section said "nine references" and listed only
> IAT-slot users. Undercount, because the config tools resolve eleven HID entry
> points through `GetProcAddress` and call them via `.data` slots, which no IAT
> scan can see. That took the seed from 9 to 13. Account: `updater-protocol.md`
> §9.3.
>
> *Second*, 2026-09-04, later the same day: 13 was **still** an undercount,
> because the HID imports are not the only route to the device. The config
> tools' hidapi is vendor-patched and its read/write/feature paths go through
> **kernel32 on the device handle**, referencing no HID import at all. That is
> route three, it takes the seed to **17** and the closure from 32 to 38, and it
> was hiding a live device-read channel nobody had noticed — §10.
>
> Both errors were "exhaustive scan of the wrong search space", which is the
> failure `CLAUDE.md` §1.2a names. The scan was never wrong; the set it ran over
> was. Regenerate with `Tools/ghidra-export/closure.py`; do not quote these
> numbers from prose.

**Two ways in, and both must be scanned.**

*Statically imported*, 15 `HidD_*`/`HidP_*`/`SetupDi*` entries. The
protocol-relevant slots in cfg107:

| import | IAT slot |
| --- | --- |
| `HidD_SetFeature` | `0x0052d1dc` |
| `HidD_GetFeature` | `0x0052d1d8` |
| `HidP_GetCaps` | `0x0052d1d0` |
| `HidD_GetAttributes` | `0x0052d1e0` |

*Dynamically resolved*, 11 more. cfg107's resolver at **`0x004027d0`** calls
`LoadLibraryA("hid.dll")` (string `0x5568dc`, import `*0x52d258`), stores the
module handle to `0x57f330`, then `GetProcAddress` (`*0x52d250`) eleven times
into `0x57f160`–`0x57f188`:

```
HidD_GetPreparsedData      -> 0x57f160     HidD_GetIndexedString  -> 0x57f164
HidD_SetNumInputBuffers    -> 0x57f168     HidD_GetAttributes     -> 0x57f16c
HidD_GetManufacturerString -> 0x57f170     HidD_SetFeature        -> 0x57f174
HidD_GetSerialNumberString -> 0x57f178     HidD_FreePreparsedData -> 0x57f17c
HidD_GetFeature            -> 0x57f180     HidP_GetCaps           -> 0x57f184
HidD_GetProductString      -> 0x57f188
```

cfg100 does the same from `0x004038a0`, handle `0x5dcb70`, slots
`0x5dcb74`–`0x5dcb9c` — same eleven names, different encoding (`pushl <mem>`
rather than `pushl %eax` for the handle).

**The proof that this matters:** hidapi's `hid_send_feature_report` at
`0x00403320` executes `ff 15 74 f1 57 00` = `calll *0x57f174` at `0x403333`.
That is a `HidD_SetFeature` call which references **no IAT slot at all**.

### 2.1 Route three — kernel32 on the device handle [D]

The vendor's hidapi does not use `HidD_*` for the bulk paths. Four functions
per config tool do device I/O through kernel32 and reference no HID import:

| hidapi function | cfg107 / 104 / 101 | cfg100 | kernel32 route |
| --- | --- | --- | --- |
| `hid_write` | `0x403100` | `0x404290` | `WriteFile` + `GetOverlappedResult` |
| `hid_read_timeout` | `0x4031c0` | `0x4043e0` | `ReadFile` + `CancelIo` |
| `hid_get_feature_report` | `0x403350` | `0x404610` | `DeviceIoControl`, code `0xB0192` = `IOCTL_HID_GET_FEATURE` (`0x40337f`) |
| `hid_close` | `0x4033d0` | `0x404720` | `CancelIo` |

Only the four config tools import `DeviceIoControl`; the four updaters and the
XM1r do not. In the updaters every `ReadFile`/`WriteFile` reference belongs to
the CRT or MFC file classes, so **route three does not exist in the flasher's
binaries and the updater closures are unchanged at 14 (1.10) and 10 (1.04)**.

**The seed is 17 functions** for every config version — static-IAT users,
dynamic-slot users, and route three:

```
cfg107  0x4027d0 0x402980 0x402f60 0x403100 0x4031c0 0x403320 0x403350
        0x4033d0 0x403420 0x403460 0x4034a0 0x4034e0 0x403550 0x4035f0
        0x403850 0x403920 0x404180
cfg100  0x4038a0 0x403a30 0x403fe0 0x404290 0x404590 0x404610 0x4043e0
        0x404720 0x404770 0x404800 0x404890 0x404920 0x4049c0 0x404a50
        0x404ce0 0x404db0 0x405660
```

Device handle globals: cfg107 `0x0057f338` (the `CreateFileW` handle from the
§3a matcher) and `0x00580188` (the `hid_device*` from §10's `hid_open`).

**How route three is separated from ordinary file I/O.** `ReadFile` and
`WriteFile` are also the CRT's and MFC's, and seeding on them wholesale would
drag in every file operation in the program. `closure.py` admits a kernel32
handle-I/O function only if it lies inside the hidapi span (lowest to highest
HID-slot user) **or** is already in the HID-only closure. The second arm exists
because the XM1r's device read `0x643b80` sits *below* its span and would
otherwise have been filed as generic file I/O. Every function the rule rejects
is printed, per binary, rather than dropped silently.

**Blind spot, stated:** a device-I/O call outside the hidapi span and outside
the HID-only closure would still be missed. `closure.py` prints that residue
for every binary (`devio_out_of_span`); in all seven it is the CRT's
`_read`/`_write` and the ATL/MFC file classes, whose handles come from
`CreateFile*` on filesystem paths.

**A hazard this method cannot fix, recorded rather than hidden.** A function
whose address is only ever *taken* — a thread procedure, a callback stored to a
`.data` slot — has no incoming call edge, so no call-graph closure in either
direction can reach it. §10's poll thread `0x412c60` is in the closure only
because it *calls* a seeded function; its sibling `0x412c40`, the event
callback stored to `*0x0057f298` and invoked indirectly, has no such edge and
is invisible to the method. `closure.py` therefore audits the case that would
matter — an address-taken function that does device I/O and is absent from the
closure — and reports it empty for all seven binaries.

**Method, and its blind spot.** Exhaustive 4-byte scan of the whole `.text` for
every static IAT slot *and* every dynamic slot address; dynamic slots are found
by locating each HID API name string whose address is pushed in `.text` and
taking the following `a3` (`mov %eax,<abs>`) store. Keying on the name string
rather than an instruction shape is load-bearing — a shape-matched regex found
only 1 of cfg100's 11. **Blind spot:** a slot loaded into a register and called
indirectly without the slot address appearing as a literal would still be
missed. No such case is known here; that is not the same as none existing.

## 3. The send wrapper — `0x00403850` [D]

`__fastcall`-ish: **`EBX` = buffer, `ESI` = length**, read off the push order at
`0x403882`–`0x403885` (`push %esi; push %ebx; push %ecx` → `HidD_SetFeature(
handle, buffer, length)`).

Guards on the handle global `0x0057f338` being non-null. On failure calls
`GetLastError` (`0x403895`) and retries only for
`0x15, 0x17, 0x1D, 0x57, 0x65B`; second attempt then `Sleep(0x32)` = 50 ms
(`0x4038ed`). Exactly **4 direct callers**, found by exhaustive `E8`-rel32 scan:
`0x403b91`, `0x404243`, `0x404676`, `0x4047a6`.

## 3a. How the device is selected — `FUN_004035f0` [D]

The whole open-and-match predicate, read from raw disassembly. This is what
`EGGCore`'s enumerator has to reproduce.

```
403709  calll *0x52d270          ; CreateFileW(path, 0xC0000000, 3, 0, 3, 0, 0)
40370f  movl  %eax, 0x57f338     ; the device handle global
403714  cmpl  $-0x1, %eax        ; INVALID_HANDLE_VALUE -> give up on this path
403722  calll *0x52d1e0          ; HidD_GetAttributes -> HIDD_ATTRIBUTES at -0x10(%ebp)
403738  movl  $0x3367, %eax      ; VID
40373d  cmpw  %ax, -0xc(%ebp)    ;   .VendorID   must equal 0x3367
403747  movzwl -0xa(%ebp), %ecx  ;   .ProductID
40374b  cmpl  0x8(%ebp), %ecx    ;   must equal the CALLER'S ARGUMENT
40375e  calll *0x52d1d4          ; HidD_GetPreparsedData
403771  calll *0x52d1d0          ; HidP_GetCaps -> HIDP_CAPS at 0x57f1d0
403777  movl  $0xff01, %edx
40377c  cmpw  %dx, 0x57f1d2      ;   .UsagePage  must equal 0xFF01
403785  cmpw  $0x2, 0x57f1d0     ;   .Usage      must equal 0x02
```

Layout note: `HIDD_ATTRIBUTES` is `{Size, VendorID, ProductID, VersionNumber}`,
so `-0x10/-0xc/-0xa/-0x8`; `HIDP_CAPS` starts `{Usage, UsagePage}`, so
`0x57f1d0` is Usage and `0x57f1d2` is UsagePage — they are the other way round
from the order the code tests them in, which is worth care when transcribing.

On a full match it stores `.VersionNumber` to `0x0057f190`, sets the
device-present word `0x0057f194` to 1, and keeps the device path.

**Four conditions, all required: VID `0x3367`, PID = argument, UsagePage
`0xFF01`, Usage `0x02`.** The usage-page pair is what picks the vendor
collection out of the several a gaming mouse presents; matching on VID/PID alone
would open the wrong interface.

### 3a.1 The bootloader PID question, answered properly [D]

An earlier open item asked whether cfg107 ever looks for the bootloader PID
`0x1977`, and reasoned from a literal scan: `0x1978` appears as a 32-bit
immediate 7 times, `0x1977` zero times. **That framing could not have settled
it**, because the PID is never a literal inside the comparison — it is the
function's argument, compared at `0x40374b` against `0x8(%ebp)`. A scan of the
matcher would find no PID at all.

The answer comes from the call sites instead, and it is stronger than the guess
was. `FUN_004035f0` has exactly five callers, and **every one pushes the
compile-time constant `$0x1978`**:

```
4130ac  pushl $0x1978    ; OnInitDialog
413844  pushl $0x1978    ; WM_DEVICECHANGE
413edb  pushl $0x1978    ; APPLY
413f9d  pushl $0x1978    ; Factory Reset
414046  pushl $0x1978    ; sub-dialog live-apply
```

Each verified by disassembling at the site. **cfg107 has no bootloader
awareness**: it cannot open PID `0x1977`, because nothing ever asks it to.

This is a `CLAUDE.md` §1.2a case worth keeping as calibration — the original
negative was *stated over the wrong search space*. It happened to reach the
right conclusion, which is the dangerous kind of near-miss.

> **And it was not finished.** Five call sites were named against a count of
> seven and the two-site residue was never enumerated. One of the two is the
> entry to a second device channel nobody had seen. The conclusion above
> survives unchanged — no `0x1977` anywhere in cfg107 — but the method did not.
> §10.6.

## 4. The four commands cfg107 sends [D]

Each read off the `movl` immediate that initialises byte 0 and byte 1 together.
Little-endian, so `$0x12a1` writes `a1 12 00 00`.

| site | immediate | report | cmd | length | follow-up |
| --- | --- | --- | --- | --- | --- |
| `0x403b8a` | `$0x12a1` | `0xA1` | `0x12` | `0x40` | `Sleep(0x50)` = 80 ms |
| `0x404218` | `$0x11a0` | `0xA0` | `0x11` | `0x411` | `Sleep(arg)`, then a 64-byte `0xA1` read |
| `0x40466c` | `$0x2a1` | `0xA1` | `0x02` | `0x40` | `Sleep(0x32)` = 50 ms |
| `0x40479f` | `$0x13a1` | `0xA1` | `0x13` | `0x40` | **success** path: `Sleep(0x44c)` = 1100 ms, then a 64-byte `0xA1` read |

> **CORRECTION 2026-09-04 — the `A1 13` row had the branch backwards.** It read
> "failure path pushes `0x44c`", and recorded no response read at all. Both
> wrong. At `0x4047ad` the `je 0x4047fa` jumps to the **failure** epilogue
> (`orb $-0x1,%al` at `0x404805`); the 1100 ms sleep at `0x4047af` is on the
> **fall-through, i.e. success**. It is followed by a real response read:
> memset 64 bytes at `-0x90(%ebp)` (`0x4047c4`), `movb $0xa1` at `0x4047d2`
> — objdump prints this as `$-0x5f`, which is exactly how a text scan for `$0xa1`
> misses it — then `calll 0x403920` at `0x4047d9`, requiring both a non-zero
> return and **`resp[1] == 1`** (`cmpb $0x1,-0x8f(%ebp)` at `0x4047e2`) before
> `movb $0x1,%al`.
>
> **This matters for us.** Factory reset is not fire-and-forget on the config
> side: the vendor waits 1.1 s and then requires an explicit ready status. Per
> `CLAUDE.md` §4.2, "mirror whatever verification the vendor protocol provides,
> exactly" — so our factory reset must do the same and must not report success
> on the send alone. Note the contrast with the *updater's* post-flash `A1 13`,
> which genuinely is fire-and-forget (`notes/updater-protocol.md` §5.4a): same
> command byte, different caller discipline.

**`0xA0 0x11` carries 1024 bytes** copied to buffer `+0x10` by `rep movsl` of
`0x100` dwords (`0x404222`–`0x404233`) — the same payload geometry as the
updater's `0xA0 0x06` block write.

**What any of these four commands mean is `[G]` and stays `[G]`.** The names are
not in the binary and nothing here has been on a wire. Per §1.3 none of them may
inform a write. The `0xA0 0x11` payload size and offset are `[D]`; calling it
"the settings blob" would be a guess, and the obvious guess is exactly the kind
this project is trying not to make.

## 5. `A1 13` is FACTORY RESET — resolved [D]

> **RESOLVED 2026-09-04.** This section previously called `A1 13` "a lead, not a
> conclusion" and said the next step was to read `0x00404770`'s caller. That was
> the right next step and it settled the question. Full derivation in §7 below.

`A1 13` is the command behind the config tool's **Factory Reset** button
(dialog 102, control 1039). It is a device-side reset: the host sends 64 bytes
with no payload and the device restores its own defaults. The updater sends the
byte-identical frame after a successful flash, which is why a firmware update
resets settings (`notes/updater-protocol.md` §5.4a).

## 6. Not yet derived
- The 1024-byte settings blob's field layout, and which UI control writes which
  offset. Lead: `0x413db0` writes the host-side defaults as inline immediates
  including `0x190/0x320/0x640/0xc80` = 400/800/1600/3200.
- What `A1 02`'s returned dwords mean (§7.3).
- The three top-level handlers `0x412fb0`, `0x413600`, `0x414010`.
- Validation and clamping applied before an `A0 11` write.

## 7. The complete command set, and what each command is  [D]

Work-plan item 7. `CLAUDE.md` §4.1 requires factory reset to be implemented and
confirmed **before any other write path**, on the grounds that it is the undo for
bad config state. That requirement rests on a premise this section tests.

### 7.1 The command set is four, and it is complete

Commands are built as a 32-bit immediate stored to a stack slot — `movl
$0x12a1, -0x50(%ebp)` — **not** as `push imm32`. (Worth stating because a scan
for the push form finds nothing and looks like a clean negative result. It is
not; it is the wrong instruction form.)

Disassembling each config tool's whole device band and taking every immediate
whose low byte is a report id gives the complete set:

| command | cfg100 | cfg101 | cfg104 | cfg107 |
|---|---|---|---|---|
| `A1 12` | `0x404ff9` | `0x403b6a` | `0x403b6a` | `0x403b8a` |
| `A0 11` | `0x4056fb` | `0x404208` | `0x404208` | `0x404218` |
| `A1 02` | `0x405b25` | `0x40465c` | `0x40465c` | `0x40466c` |
| `A1 13` | **`0x415783`** | `0x40478f` | `0x40478f` | `0x40479f` |

**All four commands are present in all four versions**, including the oldest.

> **Scope upgraded 2026-09-04.** The table above came from disassembling each
> tool's *device band*. Re-run over **all of `.text`** with
> `Tools/ghidra-export/cmdscan.py`, which reports every instruction storing an
> immediate whose low byte is a report id to memory:
>
> - **cfg107: 10 hits.** Eight are the four commands plus their four response
>   report ids, all inside `0x403b20`, `0x404180`, `0x4045e0`, `0x404720`. Two
>   are pointers whose low byte happens to be `0xa0` (`movl $0x5737a0`,
>   `$0x5379a0`).
> - **cfg100: 47 hits.** Eight are the same four commands plus responses —
>   including `A1 13` at `0x415783`, inside `FUN_00415700`. The rest are the
>   pointer `0x5814a0` stored repeatedly, and four `movb $0xa0`/`$0xa1` to
>   **`-0x4(%ebp)` inside `0x00466e71`**, which is the MSVC exception-handling
>   state slot, not a frame buffer — the same false-positive shape fw104 shows
>   (`notes/updater-protocol.md` §3.7a).
>
> So the command set is four, over the whole section rather than a band, in both
> code bases. The band scoping was the same defect §3.7a had on the updater side.

> **CORRECTION (2026-09-04).** This table previously recorded `A1 13` as absent
> from cfg100, and said "`A1 13` appears from cfg101 onward". **Both were wrong.**
> cfg100 builds it at `0x415783` (`c7 45 b0 a1 13 00 00`,
> `movl $0x13a1,-0x50(%ebp)`), followed by `movl $0x40,%edx` /
> `leal -0x50(%ebp),%ecx` and SSE zeroing of the rest of the 64-byte frame —
> structurally identical to cfg107's.
>
> **The cause was mine and it is the exact failure §1.2a exists to prevent.** My
> scan covered a hand-picked window, `0x403800`–`0x405c00`, chosen because that
> is where the *other* three commands live. cfg100's `A1 13` sits at `0x415786`,
> outside it. I then reported the absence as a finding. A negative claim whose
> search space is a guessed address range is worthless, and I made it one message
> after writing the rule that says so.

**Method, restated so the claim is checkable.** Search space: the **entire
`.text`** of each binary — cfg100 `0x401000`–`0x57f957`, cfg101
`0x401000`–`0x52c65f`, cfg104 `0x401000`–`0x52bfd1`, cfg107
`0x401000`–`0x52c591`. Every 4-byte little-endian occurrence of each command
immediate was located, then **mechanically discriminated** rather than judged:
a real build is `movl $imm32, disp(%ebp)`, encoded `c7 45 <disp8>` or
`c7 85 <disp32>`, so the bytes immediately preceding the literal decide it.

That discrimination matters. `A1 02` produces 5–7 raw hits per binary, but only
one in each is a build; the rest are preceded by `0f 84` — the literal is the
displacement of a `jz rel32`. Reproduce with a 4-byte search for `a1 02 00 00`
and a check of the two preceding bytes; no tool of mine required.

**Blind spot of this method, stated per §1.2a:** it finds commands built as a
32-bit immediate store. It would miss a frame built byte-by-byte with `movb`
(which is how the *updater* builds its frames) or assembled from a register or
table. Task E of the 2026-09-04 audit searched for those forms specifically and
found none in cfg107; that is corroboration, not proof, and is recorded as such.

> **CORRECTION (2026-09-03).** An earlier version of this paragraph said
> `A1 13`'s containing function `0x00404720` has "zero callers — dead code in the
> shipped build". **That was wrong.** It rested on the exported `callers` field,
> which is unreliable: `0x00413faf` is a direct `calll 0x404720`
> (`e8 6c 07 ff ff`, rel `-0xf894`, next instruction `0x413fb4`). The function is
> live, and it is the **Factory Reset** command — see §2.4. The updater's
> post-success `A1 13` (`notes/updater-protocol.md` §5.4 step 7) is the same
> command, which is why it is sent after a flash.

**The bound that does not depend on the instruction form at all** (added
2026-09-04, the same argument §3.7a of `notes/updater-protocol.md` uses for the
updater). Every byte cfg107 puts on the wire must pass through the send wrapper
`0x00403850`, because that function contains the only two references to the
`HidD_SetFeature` IAT slot `0x0052d1dc` in the whole of `.text` (§2's exhaustive
4-byte scan). Recovering its callers from **raw `E8` edges** rather than Ghidra's
call graph:

| wrapper | call sites | calling functions |
|---|---|---|
| `0x00403850` send | 4 | `0x403b20`, `0x404180`, `0x4045e0`, `0x404720` |
| `0x00403920` receive | 3 | `0x403b20`, `0x4045e0`, `0x404720` |

Those four calling functions are exactly the four commands of §7.1, one each.
`0x404180` does not appear in the receive row because it performs its own inline
`HidD_GetFeature` rather than going through the polling wrapper (§4's table).

**So however a frame were built — byte at a time with `movb`, from a table, out
of a register — it still has to be handed to `0x00403850`, whose four callers are
enumerated by a raw byte scan and all four read in full.** That closes the gap
the immediate-scan alone leaves open, and it corroborates §4's set as complete
rather than merely as what was found.

**Done the same way for all four** (2026-09-04). Each version's send wrapper was
located by slot reference and its callers recovered from raw `E8` edges:

| | send wrapper | its callers | receive wrapper | inline `GetFeature` |
|---|---|---|---|---|
| cfg107 | `0x403850` | `0x403b20 0x404180 0x4045e0 0x404720` | `0x403920` | `0x404180` |
| cfg104 | `0x403830` | `0x403b00 0x404170 0x4045d0 0x404710` | `0x403900` | `0x404170` |
| cfg101 | `0x403830` | `0x403b00 0x404170 0x4045d0 0x404710` | `0x403900` | `0x404170` |
| cfg100 | `0x404ce0` | `0x404fa0 0x405660 0x405ac0 **0x415700**` | `0x404db0` | `0x405660` |

**Exactly four callers in every version**, one per command — and cfg100's fourth
is `0x415700`, the function containing the `A1 13` store at `0x415783`. That is
the corrected cfg100 factory-reset finding arriving a third time, by a method
that never looks at an immediate.

### 7.1a The hidapi write path is unreachable in all four tools [D]

§2 records that these binaries statically link hidapi and resolve HID a second
time through `GetProcAddress`. hidapi's `hid_send_feature_report` calls
`HidD_SetFeature` through the dynamic slot, so it is a **second, independent way
to write to the device** and it has to be accounted for or §7.1's bound is void.

It is dead, by two checks in every one of the four binaries:

| | hidapi write fn | direct callers (raw `E8` scan) | occurrences of its address in the **whole file** |
|---|---|---|---|
| cfg107 | `0x403320` | **0** | **0** |
| cfg104 | `0x403320` | **0** | **0** |
| cfg101 | `0x403320` | **0** | **0** |
| cfg100 | `0x404590` | **0** | **0** |

The second column matters more than the first: a function with no direct callers
could still be reached through a pointer, so the address was searched for as a
4-byte value across **every byte of each file** — `.text`, `.rdata`, `.data`,
resources, everything. It appears nowhere, so no vtable, jump table, callback
registration or thread-start argument can reach it. The same search on the two
*live* wrappers returns 0 as well, which is the calibration: these programs
simply do not take the address of their transport functions.

**Blind spot:** an address computed at runtime rather than stored — base plus
offset — would not appear. Nothing in these binaries does that, and hidapi is
compiled in as ordinary static code, but the scan cannot exclude it.

### 7.2 Read is a request plus a large GetFeature  [D]

`FUN_00403b20` is the read path:
```
403b8a  movl $0x12a1,-0x50(%ebp)   ; A1 12, 64-byte request
403b91  calll 0x403850             ; send
403bc9  movl $0x411,%esi           ; 1041-byte response buffer
403bd4  movb $0xa0,-0x468(%ebp)    ; response report id = 0xA0
403bdb  calll 0x403920             ; receive
403bed  movl $0x100,%ecx           ; 0x100 dwords = 1024 bytes copied
403bf8  movl $0x57f340,%edi        ; into/out of the settings globals
403bfd  movl $0x57f210,%eax
```
So the settings round trip is **`A1 12` (64 bytes out) → `0xA0` GetFeature (1041
bytes in, 1024 bytes of payload)**, and the write is `A0 11` with 1024 bytes at
`+0x10` (§4). Read and write are exact mirrors, which is what read-modify-write
needs.

### 7.3 `A1 02` is a small query, not a blob read  [D]

`FUN_004045e0`:
```
40466c  movl $0x2a1,-0x94(%ebp)    ; A1 02, 64-byte request
404676  calll 0x403850             ; send
4046a8  movl $0x40,%esi            ; 64-byte response
4046b0  movb $0xa1,-0x54(%ebp)     ; response report id = 0xA1
4046b4  calll 0x403920             ; receive
4046c3+ unpacks dwords from -0x44, -0x40, -0x3c, ... into a caller struct
```
So the two reads differ in both report id and size: `A1 12` answers on report
`0xA0` with 1041 bytes (the settings blob), `A1 02` answers on report `0xA1`
with 64 and yields a handful of 32-bit fields. What those fields mean is **[G]**
and is not guessed here.

**Both response report ids are the *other* report from the request.** A request
on `0xA1` is answered on `0xA0` when the payload is large and on `0xA1` when it
is small — i.e. the report id tracks the transfer size, not the direction. Worth
knowing before assuming a request/response pair shares a report id.

### 7.4 `A1 13` IS the factory reset, and it is a device command  [D]

The main dialog (DIALOGEX resource **102**) has 8 controls, two of which matter:

| control id | class | caption |
|---|---|---|
| `1039` (`0x40f`) | BUTTON | **`Factory Reset`** |
| `1043` (`0x413`) | BUTTON | `APPLY` |

Their `ON_BN_CLICKED` entries are adjacent 24-byte structs in `.rdata`
(`0x5588f0` and `0x558908`), so they belong to the same message map — the main
dialog's:

| control | handler |
|---|---|
| `1043` APPLY | `0x00413ea0` → `0x00404180` → **`A0 11`** (settings write) |
| `1039` Factory Reset | `0x00413f90` → `0x00404720` → **`A1 13`** |

`FUN_00413f90`:
```
413f9d  pushl $0x1978            ; PID
413fa2  calll 0x4035f0           ; open the device
413fac  je    0x414002           ; not found -> return, no command sent
413faf  calll 0x404720           ; <-- A1 13
413fb4  movl  $0x57f210,%eax
413fb9  calll 0x413db0           ; reset local settings copy
413fbe  movl  $0x57f2a0,%eax
413fc3  calll 0x413db0           ; and the second copy
413fc8..413ffc                   ; UI refresh
```

`FUN_00404720` builds a **64-byte** frame — `memset` to `0x3f`/`0x40` at
`0x404782`/`0x40478e`, then `movl $0x13a1, -0x50(%ebp)` at `0x40479f` — and
sends it. It carries no payload beyond the report id and command byte.

**So factory reset is a real device-side command.** The device restores its own
defaults; the host sends 64 bytes and then re-syncs its local copies. No
host-composed defaults blob is involved anywhere.

### 7.5 The retracted recommendation

**RETRACTED.** An earlier §2.3 argued that no factory-reset command existed, that
any vendor reset must therefore be an `A0 11` write of a host-composed blob, and
that `CLAUDE.md` §4.1 should be amended to replace factory reset with
restore-from-blob.

**Every part of that is withdrawn.** §4.1 stands exactly as written: factory
reset exists, it is one 64-byte command, and implementing and confirming it
first is both possible and correct.

The error is worth keeping visible because of *how* it happened. The command
scan was sound and its result — four commands — was right. The mistake was an
**inference from absence**: no command was *named* reset, so I concluded none
was. Two cheap checks would have caught it immediately and neither was run:
grep the binaries for reset-related **strings** (`Factory Reset` is right there
in `.rsrc`, in all four config tools), and check the **dialog controls** to see
what the UI actually offers. A conclusion about what software cannot do should
never rest only on what its command encoder looks like.

The second, more dangerous cause was trusting the exported `callers` field, which
said `0x00404720` was uncalled. That field is now known unreliable (§7.1
correction) and an audit of every claim depending on it is under way.

## 8. Re-derivation log  [D]

The same discipline `notes/updater-protocol.md` §6.4 applies to the flasher.
A claim first written from a decompiler view, from Ghidra's names, or from a
function-list argument is not retired by being plausible.

**Re-derived by an independent route, 2026-09-04:**

| claim | § | independent route used |
|---|---|---|
| every IAT slot cited here is the API it is called | 2, 3a | resolved from the **import table**, the loader-authoritative source, never from Ghidra's slot names: `0x52d1d0` `HidP_GetCaps`, `0x52d1d4` `HidD_GetPreparsedData`, `0x52d1d8` `HidD_GetFeature`, `0x52d1dc` `HidD_SetFeature`, `0x52d1e0` `HidD_GetAttributes`, `0x52d270` `CreateFileW`, `0x52d250` `GetProcAddress`, `0x52d258` `LoadLibraryA`. All four tools import exactly **15** `HID`/`SetupAPI` entries |
| the four commands, and that four is all | 7.1 | whole-`.text` immediate scan **and** send-wrapper caller enumeration from raw `E8` edges — two methods, no shared blind spot (§7.1) |
| `hid_send_feature_report` is unreachable | 7.1a | 0 direct callers **and** 0 occurrences of its address as a 4-byte value anywhere in the file, in all four tools |
| the device-select predicate's five call sites | 3a.1 | raw `E8` scan for callers of `0x4035f0`: exactly **5** (`0x4130b8 0x413849 0x413ee0 0x413fa2 0x41404b`), each disassembled at the site and each pushing `$0x1978`. Four have the push immediately before the call; `0x4130b8`'s is at `0x4130ac` with an unrelated `movl` between, which is why an adjacency test would have found only four |
| the shared-transport table | 1 | re-read at the cited addresses: `0x404238 movl $0x411,%esi`; `rep movsl` of `0x100` dwords `0x404222`–`0x404233` into `-0x45c` = base `-0x46c` + `0x10`; `0x404292 movb -0x53(%ebp),%bl` against base `-0x54`; `0x404264 movb $0x2,%al` |
| the config busy convention | 1 | re-read `0x403920`–`0x4039dc`: `cmpl $0x1,%eax` on the **return**, then `movzbl 0x1(%edi)`; `cmpl $0x3` at `0x40396e` enters the busy loop, `Sleep(0x64)` at `0x403980`, accumulator `+0x64` at `0x4039d1`, budget `cmpl $0x3e8` at `0x4039d7` — so at most **10 passes**, and the 100 ms sleep happens only on the busy path |
| the device-facing seed | 2 | Independent 4-byte scan for the five kernel32 handle-I/O slots, attributing every hit to an owning function: `DeviceIoControl` is referenced **once** per config tool and the owner is hidapi's `hid_get_feature_report`. The updaters do not import it at all. Seed 13 → 17, closure 32 → 38; **fw110 unchanged at 14** |
| the `0x1978` literal count | 3a.1, 10.6 | Re-scanned and this time *partitioned*: 7 = 5 matcher call sites + 1 event-channel `hid_open` + 1 library coincidence. The unenumerated residue was the finding |

| §2's *dynamic* slot table | 2 | Re-derived 2026-09-04 by the opposite route, which never looks for a `HidD_` string: start at the import table's `GetProcAddress` slot, find **every** occurrence of that slot address in `.text` in any encoding, attribute each to an owner, and inside each owner pair every `ff d?` indirect call with the preceding `push imm32` name and the following `a3` store. cfg107 yields exactly the same **11** names and slots `0x57f160`–`0x57f188`; cfg104 `0x57e160`–`0x57e188`; cfg101 `0x57f200`–`0x57f228`; cfg100 `0x5dcb74`–`0x5dcb9c` |
| no updater resolves HID dynamically | 2 | Same route on fw110: 49 owners reference the `GetProcAddress` slot, 45 name/store pairs recovered, **none** a HID entry point. Corroborated independently: the HidD_/HidP_ **name strings present anywhere in the file** are 7 in each of fw110/107/106/104 and match the static import set exactly, so there is no name for a dynamic resolution to use |

**Not re-derived, and still resting on their original derivation:**

- Nothing. Everything in §6's "not yet derived" list is not a claim at all.

> Two method notes worth keeping. First, an `ff 15 <slot>` scan for
> `GetProcAddress` call sites finds **none** of the eleven: cfg107's resolver
> loads the slot once (`0x4027e9 movl 0x52d250,%esi`) and calls `*%esi`
> eleven times. A scan keyed on the call encoding rather than on the slot
> address would have reported zero and looked thorough doing it. Second, the
> `push imm32` name-recovery step is a heuristic; it recovered 45 pairs in
> fw110 but **zero** in fw104 and the XM1r, which means it says nothing about
> those two. Their negative rests on the name-string check, not on this.

### 8.1 Device access is serialised by a critical section  [D]

Found while re-deriving §1 and not previously recorded. Every send and every read
in cfg107 is bracketed by a critical section on the object at **`0x00578c08`**:
`0x4042a7` and `0x40425e` both call `LeaveCriticalSection` (`*0x0052d27c`) on it,
on the success and the send-failure path respectively.

Two consequences, and the second is the one that bears on our design:

1. A command and its response read are **one atomic unit** to the vendor tool.
   That is a fact about the host, not about the device, so it does not tell us
   the device requires it.
2. `EGGCore` serving two executables (§1) does not inherit this for free —
   separate processes do not share a critical section. Whatever the device's
   real constraint is, **our flasher and our config tool can be running at the
   same time**, which the vendor's two tools also can. `[G]` whether that is
   safe; it is a question for the device, and it belongs on the device-gated
   list in `CLAUDE.md` §5 rather than being designed around by guesswork.

## 9. What the unique-body read of cfg107 / cfg104 / cfg101 produced

**Scope.** 839 function bodies — the ones whose normalised body matches nothing
in updater 1.10 (already read) and nothing in the other config tools, so reading
this set reads a *different* portion of each of the three binaries. Cut into 50
batches by `Tools/ghidra-export/mkbatches.py`, one reader per batch, each shown
only raw objdump text.

**Provenance, and it is not `[D]`.** What follows is what readers reported. It
is a **lead inventory**, not a derivation. `CLAUDE.md` §1.2 applies without
exception: nothing here may inform a write until it is re-derived from the bytes,
and §1.3 forbids a `[G]` byte reaching the device regardless of how confident a
summary sounds.

**Reader error rate, measured** (`Tools/ghidra-export/verify_read.py`, final —
all 50 batches returned, 0 failed):

| check | result |
|---|---|
| invented function ids | **0** |
| functions never reported | **1** — `0x004b8da1` (cfg104). Read by hand instead: it is `AttachToTabWnd`, MFC tabbed-MDI framework code, 705 bytes, whose only indirect calls are the user32 window-lookup slots `0x52c5b0/0x52c5f4/0x52c814/0x52c868`. No HID slot, no settings global, nothing device-facing. **So the 839 are covered: 838 by readers, 1 by hand.** |
| direct call targets | 6,840 in truth; **15 functions disagreed**, 4 extra, 12 missed |
| indirect call slots | 1,013 in truth; **12 functions disagreed**, 11 extra, 1 missed |

Spot-checking the disagreements changed how they read, which is the point of
doing it: `0x00495e1e` really did drop two calls; `0x0043dd05`'s "extra" is a
vtable displacement (`calll *0x1c(%eax)`) that the field's wording invited; and
`0x004b4a89`'s extra `0x52d810` is `movl 0x52d810,%edi` followed by a call
through the register — **the reader was right and the checker was blind**, the
same blind spot §2 already declares. `summary`, `category`, `vendor_specific`
and `settings_relevant` are judgements and **nothing checks them**.

### 9.1 Two claims from the read, re-derived from the bytes  [D]

Picked because both bear on `CLAUDE.md` §1.3 and on §6's open items.

**`0x0040e410` (cfg107, 91 bytes) is a DPI clamp and quantiser.** Read in full;
the whole body is nine basic blocks and no call:

```
40e410  cmpl $0xa, %ecx     ; below 10      -> return 10
40e41b  cmpl $0x7530, %ecx  ; above 30000   -> return 30000
40e429  cmpl $0x2710, %ecx  ; above 10000   -> round to a multiple of 50
40e44e                      ; otherwise     -> round to a multiple of 10
```

The two magic multiplies are the usual reciprocal division: `0x51EB851F` with a
`shrl $0x4` is `/50` (then `imull $0x32`, and `cmpl $0x19` = remainder ≥ 25
rounds up); `0xCCCCCCCD` with `shrl $0x3` is `/10` (then `leal (%edx,%edx,4)` +
`addl %eax,%eax` = ×10, and `cmpl $0x5` = remainder ≥ 5 rounds up). Half-up in
both branches.

So the host's DPI domain is **[10, 30000], quantised to 10 up to 10000 and to 50
above it.** `0x0040d880` is the caller that applies a delta and re-quantises
through the same `/10` sequence inline.

**`0x004138f0` (cfg104, 240 bytes) writes a default settings record.** A leaf
with no calls that fills a caller-supplied buffer in `%eax` and touches offsets
`0x00` through `0x6c`, so the record is at least **0x6d bytes**. The four
16-bit values the work plan already flagged are there and paired:

```
4138f9  movw $0x190, 0x10(%eax) ; and 0x12   400
413906  movw $0x320, 0x16(%eax) ; and 0x18   800
413915  movw $0x640, 0x1c(%eax) ; and 0x1e  1600
413923  movw $0xc80, 0x22(%eax) ; and 0x24  3200
```

Each written **twice, to adjacent 16-bit slots six bytes apart**, with a zero
byte at `0x14`/`0x1a`/`0x20` between the pairs — a four-stage table of
`{flag, valueA, valueB}` records at stride 6, starting at `0x0e`. That is a
structural observation from the stores, not a claim about what the fields mean.

The tail is a second table: `movb $0x8` then a dword at stride 8 from `0x34`
(`0x200`, `0x400`, `0x1000`, `0x800`, `0xf109`, `0x101`, `0xff01`) through
`0x6c` — eight records of 8 bytes. `0xf109` is `SC_CLOSE` and `0xff01` is the
vendor usage page from §3a; both are almost certainly coincidence at this level
of evidence and neither is being read as meaningful.

### 9.2 Lead inventory — 69 bodies the readers flagged as settings-shaped

Recorded so the next session does not have to re-run 50 agents to find them
again. **Every one is an agent summary. None is derived.** Grouped by what they
appear to touch; addresses are per the binary named.

| area | bodies |
|---|---|
| serialise globals → record | cfg101 `0x403c30` (block at `0x57f3e0`); cfg107 `0x4144c0`, cfg104 `0x413fa0`, cfg101 `0x413e90` (dirty-check, two **0x73-byte** stack buffers) |
| record ↔ record sync | cfg107 `0x414010` (`0x57f210` and `0x57f2a0`, 0x90 apart), cfg101 `0x413a30` (`0x57f2b0` ↔ `0x57f340`), cfg104 `0x413b40` |
| dialog → globals (harvest) | cfg107 `0x406180`, `0x40ec20`, `0x411ab0`, `0x412030`; cfg101 `0x410750`; cfg104 `0x4107b0` |
| globals → dialog (restore) | cfg107 `0x4062b0`, `0x40ee80`, `0x411bd0`; cfg101 `0x410970`; cfg104 `0x4109f0` |
| defaults | cfg104 `0x4138f0` (§9.1); cfg101 `0x40fdb0`-region resets |
| clamping / validation | cfg107 `0x40e410`, `0x40d880` (§9.1); cfg104 `0x411bf0`; cfg101 `0x411b30` |
| apply / commit | cfg107 `0x413ea0`, cfg101 `0x4138d0`, cfg104+cfg107 `0x4139e0` |
| device arrival/removal | cfg107 `0x413600`, cfg101 `0x4130f0`, cfg104 `0x4131f0` |
| button assignment | cfg107 `0x4076c0` (command ids `0x1f41`–`0x1f7c`, jump table `0x4083a4`), `0x408e40` (**six-byte record**: type `+0`, code `+1`, words `+2`, `+4`), `0x408650` (modifier mask → `0x57f335`) |
| slider / scroll dispatch | cfg107 `0x4069f0` (control ids `0x409`–`0x42b`, jump table `0x406be4`), `0x411dd0`; cfg101 `0x40ffb0`; cfg104 `0x410010` |
| mode selector | cfg107 `0x40f750` (0–3 into `0x57f21a` **and** `0x57f2aa`, 0x90 apart — consistent with the two-record layout above) |

The categories the readers assigned across all 805 bodies: `windows-ui` 366,
`mfc-atl-framework` 259, `string-or-container` 53, `cpp-runtime-eh` 32,
`registry` 21, `c-runtime` 20, `device-io` 17, `unclear` 13, `file-io` 12,
`math-or-float` 8, `allocator` 4. **83 were flagged `vendor_specific`** — an
order of magnitude more than the updater's unique set, which is expected: the
config tool *is* mostly application code, whereas the updater is mostly MFC.

**What this does NOT settle**, and the §6 list stands unchanged: the record's
field meanings, which control writes which offset, and what the 1024-byte `A0 11`
payload contains beyond the ~0x73 bytes the host serialises. Those are LIST 3.

## 10. The second HID collection — an input-report event channel [D]

**Found 2026-09-04.** The device presents (at least) **two vendor HID
collections on the same PID**, and every published note until now described
only one of them.

| collection | usage page | usage | how it is used |
| --- | --- | --- | --- |
| command | `0xFF01` | `0x02` | feature reports `0x411` / `0x40`, §1, §3a, §4 |
| **event** | **`0xFF02`** | **`0x01`** | **8-byte input reports, report id `0x03`, polled** |

Both are VID `0x3367`, PID `0x1978`. Nothing here touches the bootloader PID
`0x1977`; §3a.1's conclusion is unaffected.

### 10.1 The open — vendor-patched `hid_open` [D]

`hid_open` is **not stock hidapi.** cfg107 `0x00402e70`, after matching VID at
`+0x4` and PID at `+0x6` on each `hid_enumerate` entry, adds two comparisons
that upstream hidapi does not have:

```
402eb5  movl  $0xff02, %ecx
402eba  cmpw  %cx, 0x18(%esi)     ; usage_page must equal 0xFF02
402ec0  cmpw  $0x1, 0x1a(%esi)    ; usage      must equal 0x01
```

cfg104 and cfg101 at `0x00402e80`; cfg100 at `0x00403ee0` (`0x403f20`,
`0x403f26`), where the constant is reloaded into `%ecx` inside the loop, which
is why cfg100 shows three `0xFF02` literals and the others one.

`hid_open` has exactly **one caller in each binary** — the starter below — so
this is the only thing hidapi's own open path is used for.

### 10.2 The starter — `0x00412d00` (cfg107) [D]

```
412d19  movl  $0x412c40, 0x57f298     ; event callback pointer
412d0d  pushl $0                      ; serial = NULL
412d0f  pushl $0x1978                 ; PID
412d14  pushl $0x3367                 ; VID
412d23  calll 0x402e70                ; hid_open
412d2b  movl  %eax, 0x580188          ; hid_device* global; NULL -> return 0xFF
412d4e  movb  $0x1, 0x57f196          ; "channel running" flag
412d45  pushl $0x412c60               ; thread proc
412d55  calll 0x508697                ; _beginthreadex(NULL,0,proc,1,0,&tid)
412d5d  movl  %eax, 0x58018c          ; thread handle; 0 -> clear flag, return 0x14
412d75  calll *0x52d268               ; CloseHandle(thread)  -> return 5
```

Two callers: `0x413191` inside `0x412fb0`, and `0x413955` inside `0x413600` —
the same two dialog functions that call the §3a matcher (`OnInitDialog` and the
`WM_DEVICECHANGE` handler). cfg100's starter is `0x00414700`, callers
`0x414c18` in `0x414a70` and `0x415360` in `0x414fe0`.

### 10.3 The poll thread — `0x00412c60` (cfg107) [D]

```
412c7e  movb  $0x3, -0xc(%ebp)        ; buf[0] = report id 3, before the read
loop:
412c90  movl  0x580188, %eax          ; device; NULL -> exit thread
412c99  movl  0x4(%eax), %ecx
412c9c  negl %ecx ; sbbl %ecx,%ecx    ; -> 0 or -1, hidapi's blocking flag
412ca8  calll 0x4031c0                ; hid_read_timeout(dev, buf, 8, ms)
412cb3  cmpb  $0x3, -0xc(%ebp)        ; require buf[0] == 3
412cbc  <buf[1] == 0x02 || 0x06 || 0xB4>
412cde  calll *0x57f298               ; callback(&buf[1], &buf[2])
412ce4  pushl $0x50 ; call *0x52d22c  ; Sleep(80 ms), repeat
```

`hid_read_timeout` (`0x004031c0`) is stock hidapi Windows: `ResetEvent`,
`ReadFile` on the device handle with `OVERLAPPED`, `GetLastError` vs
`ERROR_IO_PENDING` (`0x3E5`), `CancelIo` on failure. **Eight bytes requested,
report id `0x03`, 80 ms between polls.**

Same thread in cfg104 `0x004128f0`, cfg101 `0x004127e0`, cfg100 `0x00414660`.
**One version-to-version difference:** cfg100 has **no `buf[0] == 3` check** —
it dispatches on `buf[1]` whatever report arrived. cfg101 onward added the
guard. Treat cfg100 as the buggy one.

The thread procedure's address is taken exactly once in the whole file and
never called, which is precisely why the HID-only closure did not contain it.

### 10.4 What the events mean [D]

The callback `0x00412c40` forwards to `0x004139e0`, which switches on `buf[1]`:

**`buf[1] == 0x06` — the device reports its POLLING RATE.** `buf[2]` is a
**one-hot mask**; the switch at `0x413a8d` (jump table `0x413be4`, index byte
table `0x413c04`, input `buf[2]-1` bounded to `0..0x3f`) maps it to a combo-box
index, sends **`CB_SETCURSEL` (`0x14E`)** to the window handle at `obj+0x13b8`,
and stores the mask byte to globals `0x0057f216` and `0x0057f2a6`:

| `buf[2]` | `0x40` | `0x20` | `0x10` | `0x08` | `0x04` | `0x02` | `0x01` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| index | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
| **rate** | **125 Hz** | **250 Hz** | **500 Hz** | **1000 Hz** | **2000 Hz** | **4000 Hz** | **8000 Hz** |

Seven values, one-hot, no others: every other index in `1..0x40` maps to the
default arm and is ignored.

**CORRECTED 2026-09-04.** This paragraph previously said `TCM_SETCURSEL` to a
*tab control* and called the event "profile / bank selection". Both were wrong,
and the error was caught by §12.2 — the mechanical count of tab items — rather
than by re-reading the handler. `TCM_SETCURSEL` is `0x130C`; **`0x14E` is
`CB_SETCURSEL`.** The chain, every step an instruction:

1. `0x413404` is `leal 0xc8(%esi),%ecx` immediately before the `call *%eax` that
   creates dialog `0x87` (135), so **the page-135 object is embedded in the main
   dialog at `+0xc8`**, and the handler's `obj+0x13b8` — `obj` being the main
   dialog, from the global `0x0057f754` written at `0x412ede` inside the main
   dialog's constructor `0x00412d90` — is `page135 + 0x12f0`.
2. `page135 + 0x12f0` is exactly the handle used at `0x40bbfe`, `0x40bc13` and
   five more sites in `0x0040bbd0`, each doing
   `SendMessageW(hwnd, 0x143 /* CB_ADDSTRING */, 0, <string>)` with, in order,
   `'125Hz' '250Hz' '500Hz' '1000Hz' '2000Hz' '4000Hz' '8000Hz'`
   (`0x557ea8`…`0x557efc`, contiguous, each referenced exactly once in the whole
   `.text`). **So the target is a combo box holding seven polling rates, and the
   seven one-hot values are its seven indices.**
3. Cross-check on the base arithmetic: `DDX_Control` at `0x40bb65` binds control
   **1085** — the `'Polling Rate'` combo of §12.2 — to `page135 + 0x12d0`, and
   `0x12f0 − 0x12d0 = 0x20`. That `+0x20` is `m_hWnd`'s offset in this build's
   `CWnd`, and it is *measured*, not remembered: the four radio buttons are
   bound by `DDX_Control` to `+0x13d4 / +0x1448 / +0x14bc / +0x1530` (ids
   **1062 / 1067 / 1068 / 1069**) and the `BM_SETCHECK` sites in `0x0040f750`
   use `+0x13f4 / +0x1468 / +0x14dc / +0x1550` — the same `+0x20`, four
   independent times.

**What this is worth to us.** It is the first *decoded* field of the event
channel: the device volunteers its polling rate, unsolicited, as a one-hot byte
in which **bit 0 is 8000 Hz and bit 6 is 125 Hz**. It is `[D]` and cited, and it
is a read, not a write, so §1.3 is not engaged. It also retires the reading of
this event as "profile / bank selection", which was a `[G]` that had begun to
read like a finding — exactly the drift `CLAUDE.md` §7.1 warns about.

**`buf[1] == 0x02` — a four-way setting.** `0x0040f750` stores `buf[2]` to
globals `0x0057f21a` / `0x0057f2aa`, bounds it to `0..3`, and drives four radio
buttons (`BM_SETCHECK`, `0xF1`) at `obj+0xc8+0x13f4 / +0x1468 / +0x14dc /
+0x1550`, checking exactly the one selected.

**`buf[1] == 0xB4` — formats `buf[2]` with `L"%d"`** (`0x5567b4`) into a local
`CStringT` which is then released without being read. No other use found in the
function. Recorded as observed; do not build on it.

### 10.5 Why this matters to us

1. **`EGGCore`'s enumerator must not stop at the first vendor collection.**
   §3a's four-part predicate selects `0xFF01`/`0x02`. A device that also
   presents `0xFF02`/`0x01` means matching on VID/PID plus *any* usage pair can
   open the wrong one, in either direction.
2. **The mouse pushes state at the host unprompted.** Any read-modify-write in
   `EGGConfigCore` races a device-side change the user just made on the mouse
   itself. The vendor's answer is to poll and update the UI; ours has to be to
   re-read before writing, which §4.1 of `CLAUDE.md` already requires.
3. **The flasher is unaffected.** No updater imports `DeviceIoControl`, none
   contains a `0xFF02` literal, and neither updater closure changed.
4. **`0x0057f2a0` is the base of a byte-for-byte shadow of the settings
   record.** `0x00404830` copies `src[k]` to `0x57f2a0+k` for scattered `k`, so
   `0x57f2a6` and `0x57f2aa` above are settings-record bytes **`0x06`** and
   **`0x0a`**. That identifies two fields of the config blob for free and is the
   thread to pull for the rest of it — LIST 3, not here.

### 10.5a Every usage-page literal in the corpus, enumerated [D]

The claim "there are exactly two vendor collections" is a negative, so it gets
§1.2a treatment. Exhaustive 4-byte scan of `.text` for `0xFF01` and `0xFF02` in
all eight Endgame binaries, every hit disassembled at the site:

| binary | site | what it actually is |
| --- | --- | --- |
| fw110 / fw107 / fw106 | `0x401138` | `cmpw` against `HIDP_CAPS.UsagePage` — the matcher |
| fw110 / fw107 / fw106 | `0x4dc5bc` | `movl $0xff01,-0x34(%ebp)` in library code, byte-identical to cfg107's `0x4ec849` |
| fw104 | `0x401cde` | the matcher |
| fw104 | `0x4ce581` | the same library body |
| cfg107 | `0x403778` | the matcher (§3a) |
| cfg107 | `0x402eb6` | **`hid_open`'s `0xFF02` — the second collection** |
| cfg107 | `0x413e90` | `movl $0xff01, 0x66(%eax)` — a **settings-record default**, offset `0x66`, in the default writer `0x413db0` (cfg104's is §9.1's `0x4138f0`). Not a usage page |
| cfg107 | `0x4ec849` | library |
| cfg104 / cfg101 | `0x402ec6`, `0x4035d0`+`0x758`, `0x4138f0`/`0x4137e0` band, library | same four roles |
| cfg100 | `0x404be9` matcher; `0x403f0a`+`0x403f70` `hid_open` (constant reloaded in the loop); `0x415611` settings default; `0x52af45` library | same four roles |
| cfg100 | `0x417543` | **not a literal at all** — the bytes `02 ff 00 00` are the displacement of `e8` `calll 0x427449` at `0x417542`. A 4-byte literal scan cannot tell an immediate from a displacement, and this is what that looks like |

**Two usage pairs, and no third.** No updater contains `0xFF02` in any form.
The scan's blind spot is the usual one: a usage page built arithmetically, or
compared as two 8-bit halves, would not appear as a 4-byte literal. Nothing in
the corpus does that — every comparison found is a single `cmpw` against a
16-bit field — but the method could not have seen it if one did.

### 10.6 The residue that hid it

§3a.1 counted seven `0x1978` immediates in cfg107 and named five. It never
enumerated the other two. They are:

| site | owner | what |
| --- | --- | --- |
| `0x4130ad` | `0x412fb0` | §3a matcher — OnInitDialog |
| `0x413845` | `0x413600` | §3a matcher — `WM_DEVICECHANGE` |
| `0x413edc` | `0x413ea0` | §3a matcher — APPLY |
| `0x413f9e` | `0x413f90` (orphan) | §3a matcher — Factory Reset |
| `0x414047` | `0x414010` | §3a matcher — sub-dialog live-apply |
| **`0x412d10`** | **`0x412d00`** | **§10.2, the event channel — unaccounted for until now** |
| `0x507853` | `0x507847` | library code, coincidental constant |

`CLAUDE.md` §6 says to state coverage as a partition with the residue
enumerated. Five of seven was stated as a fact and the two-function residue was
not written down, so a whole device channel sat in plain sight for a day. This
is the second time in this project that a correct count with an unenumerated
remainder concealed the finding.


## 11. What the cfg100 read produced [D] for the numbers, lead-grade for the flags

`read5`, the 2,489 normalised bodies unique to configuration tool **v1.00**.
116 batches, **116 returned, 0 failed**.

**Mechanical check** (`verify_read.py`; it scores only the two fields a script
can recompute):

| | count |
| --- | --- |
| functions in the batches | 2,489 |
| reported | 2,489 |
| **invented ids** | **0** |
| **missing** | **0** |
| call targets in the truth set | 12,750; **20 functions disagree** (5 extra, 16 missed) |
| indirect-call slots | 5,803; **9 functions disagree** (11 extra, 0 missed) |

Cleaner than the fw104 run (`updater-protocol.md` §6.2.7), which had 1 missing
and 21/12 disagreements over a larger set.

### 11.1 Where the 84 `vendor_specific` flags actually landed

Partitioned rather than counted, and every member of the third row read by hand:

| | n | what they are |
| --- | --- | --- |
| in cfg100's device closure | 15 | `0x403ee0 0x4049c0 0x404a50 0x404ce0 0x404db0 0x404fa0 0x405660 0x405ac0 0x414660 0x414700 0x414a70 0x414fe0 0x415630 0x415700 0x415860`. The readers re-identified the device layer with no access to these notes — the same independent corroboration the fw104 read gave for the updater |
| below `0x416000`, outside the closure | 66 | the vendor band: dialog, settings and UI logic |
| **above `0x416000`** | 3 | `0x00466e71` — the 29 KB MFC body settled in `updater-protocol.md` §6.2.7a, a false positive. `0x004bc841` (426 B) and `0x004bc9ec` (132 B) — custom UI painting: `0x4bc9ec` calls `USER32!GetWindowRect` through `*0x5806c0`, stores the rect to `0x5d28c0`/`0x5d28c4`, then passes an ARGB palette at `0x005da248` whose bytes are `1a 1a 1a ff` repeated. Application code, so the flag is arguably right; it is simply not protocol |

**The lesson is about the flag, not the readers:** `vendor_specific` answers
"was this written for one product", and that is **not** the same question as "is
this protocol-relevant". cfg100 ships a dark-themed custom UI, so it has vendor
code that has nothing to do with the device. Do not use this flag as a protocol
filter; `closure.py` is the protocol filter.

### 11.2 `0x004050e0` is the settings-record BUILDER, and it runs the other way [D]

cfg107's `0x00404830` copies an incoming record into a shadow at the *same*
offsets (`src[k] → 0x0057f2a0 + k`, §10.5). cfg100's `0x004050e0` is the mirror
image and its arrangement is different:

```
4050e0  movzbl 0x5dbb38 -> movb %al, 0x00(%edx)
4050e9  movzbl 0x5dbb3d -> movb %al, 0x06(%edx)
4050f3  movzbl 0x5dbb3e -> movb %al, 0x07(%edx)
4050fd  movzbl 0x5dbb3f -> movb %al, 0x0c(%edx)
405107  movzbl 0x5dbb40 -> movb %al, 0x26(%edx)
40510b… 0x5dbb41 -> 0x27, 0x5dbb42 -> 0x28, 0x5dbb43 -> 0x29, 0x5dbb44 -> 0x2a …
```

**The globals are consecutive and the record offsets are scattered**, the
opposite of cfg107's layout. So v1.00 keeps settings in a packed struct and
scatters them into the wire record; v1.07 keeps a record-shaped shadow. Both
give a byte-level map of the record, from opposite directions, and **two
independent maps of the same record is exactly what LIST 3 should start from.**

Recorded as a lead, not as a derivation: the offsets above are transcribed from
the disassembly, the *meaning* of any of them is not derived, and nothing here
is a basis for a write.

## 12. The config tools' user-visible surface  [D]

`CLAUDE.md` §1.2a's requirement, applied to the config tools the way §11 of
`notes/updater-protocol.md` applies it to the updaters. Produced with
`Tools/ghidra-export/dlgdump.py` and `dlgref.py`.

This section is here for two reasons. The near one is that §7.1's "the command
set is four" is a negative argued from code, and a negative wants its
user-surface half. The far one is that this inventory is the natural starting
point for LIST 3: it names every setting the vendor exposes, in the vendor's own
words, before any of them has been located in a wire record.

### 12.1 There is a firmware-update dialog in the config tool, and nothing can open it  [D]

`DIALOG 131`, present in **all four** config tools, byte-identical across
cfg100/101/104/107 (`sha256 9660619c44e95a52…`), captioned
`'Endgame Gear OP1 8k v2 Configuration Tool'`:

| id | class | caption |
|---|---|---|
| 1000 | `msctls_progress32` | — |
| 1001 | `BUTTON` / `PUSHBUTTON` | `'Update'` |
| −1 | `STATIC` | `'Status:'` |
| 1017 | `EDIT` | — |
| 1003 | `BUTTON` / `PUSHBUTTON` | `'Cancel'` |

**Those are the firmware updater's four control ids — 1000, 1001, 1017 and the
`'Status:'` label — plus a Cancel button.** Compare `updater-protocol.md` §11.1.
The two products share a source tree, and the config tool once had, or was meant
to have, an integrated firmware updater.

**No code in any of the four config tools instantiates it.** `dlgref.py` finds
exactly one instruction anywhere in each binary with `0x83` as an immediate, and
in all four it is the CRT, not a dialog reference: `movl $0x83,0x64(%esi)` at
cfg107 `0x50db25` / cfg104 `0x50d675` / cfg101 `0x50dd05`, and
`movl $0x83,%eax` at cfg100 `0x562b3a`. Each sits in `__XcptFilter`'s
NTSTATUS-to-signal table, in the arm for `0xC000008E`
(`STATUS_FLOAT_DIVIDE_BY_ZERO`), where `0x83` is `_FPE_ZERODIVIDE`; the
neighbouring arms produce `0x81`, `0x84`, `0x85` for the adjacent NTSTATUS
values, which is what fixes the reading.

**Method and its limits, per §1.2a.** The scan is over the whole file
disassembled by `objdump -D`, matching the printed immediate `$0x83`, so it sees
every encoding at once — `push imm8`, `push imm32`, `mov r32,imm32`,
`mov r/m32,imm32`, the sign-extended ALU forms. It was written that way because
the first attempt at this question scanned raw bytes for `68 <id32>` and
reported **zero** references for `DIALOG 102`, the main window, whose id `0x66`
is pushed as the two-byte `6a 66`. That is exactly the failure §1.2a describes,
made again, and caught by cross-checking against a dialog that obviously *is*
used. What the method still cannot see is an id assembled arithmetically.

A control for the reading: MFC's own stock `DIALOG 30721` (`'New'`) and `30734`
score **0** references by the same method in every binary, so "0" is a value
this method genuinely produces for a dead resource.

**What this does and does not mean.** It does not give the config tools a
flashing capability — they contain no `FWFILE` resource (§6.2.1 of
`updater-protocol.md`), and none of the flash opcodes appears anywhere in their
`.text`. It *does* mean anyone who opens the resource section of a config tool
will find a firmware-update dialog, and the honest description of that is
"present, unreachable", not "absent".

### 12.2 The complete setting inventory, in the vendor's words  [D]

cfg107. The four config tools' `.rsrc` differ (cfg107 adds `DIALOG 153` and
drops the — empty, 10-byte — `RT_MENU 154` the other three carry), so this is
1.07's surface specifically.

**Main window, `DIALOG 102`:** a `SysTabControl32` (1060), `'Firmware Version :'`
and `'Software Version :'` readouts (1027/1029), and two buttons —
**`1039 'Factory Reset'`** and **`1043 'APPLY'`**. Those two are the entire
write surface of the program.

**The tab control has exactly four tabs**, inserted by four calls in
`0x00413300` and by nothing else in the binary — the only four call sites of
`0x0041c350` in `.text`:

| index | label | page dialog |
|---|---|---|
| 0 | `'  Basic  '` | 135 |
| 1 | `'  Advanced Sensor  '` | 140 |
| 2 | `'  Buttons  '` | 139 |
| 3 | `'  Button Mapping  '` | 153 |

`DIALOG 137` (LED) is created separately at `0x004056bb`, and `150` (`'FIXED
CPI'`) and `152` (`'KEYBOARD KEY'`) are modal popups.

Settings by page, verbatim:

- **135 Basic** — `'CPI Levels'` combo (1019, DLGINIT items `'1' '2' '3' '4'`),
  four CPI edits + trackbars, `'X/Y Settings'`, `'LOD'` combo (1024),
  `'Angle Snapping'`, `'Ripple Control'`, four X/Y edit+trackbar pairs,
  `'Disable LED on Lift-Off'`, four unlabelled `AUTORADIOBUTTON`s
  (1062/1067/1068/1069), `'Polling Rate'` combo (1085).
- **140 Advanced Sensor** — `'Motion Sync'`, `'Motion Jitter Filter'`,
  `'Force max Sensor fps'`, `'Sensor Glass Mode'`, `'Sensor Angle Tuning'`
  edit+trackbar, `'CPI Downshift Tuning'` combo (1082, DLGINIT
  `'Force Off' / 'Medium' / 'Default'`), `'Smoothing Tuning'` combo (1084,
  DLGINIT `'Force Off' / 'Ripple Control Off' / 'Ripple Control On'`).
- **139 Buttons** — six hidden comboboxes (1072, 1020–1024) behind six push
  buttons `'RIGHT CLICK'`…`'SCROLL DOWN'`, plus `'Left-handed Mode'`.
- **153 Button Mapping** — `'Slamclick Filter'`, five per-button
  `'… Multiclick Filter'` edit+trackbar pairs, two `'SPDT:'` combos, and an
  acknowledgement checkbox (1064).
- **137 LED** — `'LED On / Off'`, `'LED effect'` combo (1038, DLGINIT one item,
  `'Singel color'` — the vendor's typo, kept verbatim), `'Scroll led'`,
  `'Logo led'`, `'DPI led'`, R/G/B edits, `'Apply led settings'`.

Two controls are shipped **hidden** (`WS_VISIBLE` clear) on page 135:
`1059 'Apply CPI settings'` and `1061 'Surface Calibration'`. A hidden button is
a capability the vendor built and then withheld from the UI; whether either has
a live handler behind it is **not** determined here, and it is a LIST 3 question.

**DLGINIT is a starting point, not the list the user sees**, and at least one
combo is overwritten at run time — recorded because the previous paragraph would
otherwise read as an inventory of the UI rather than of the resource. Combo
**1082** (`'CPI Downshift Tuning'`) carries a three-item DLGINIT
(`'Force Off' / 'Medium' / 'Default'`) and is then filled from `0x00411980`
with **four** items — `'Force Off'` `'Light Timer Only'` `'Medium Timer Only'`
`'Default'` (`0x5582a8`, `0x5582bc`, `0x5582e0`, `0x558304`, each referenced
exactly once in `.text`). The DLGINIT list is dead.

Combos with **no** DLGINIT at all are filled the same way, and the code
therefore holds their enumerations. Located so far, each string referenced
exactly once unless noted:

| combo | filled by | items |
|---|---|---|
| `'Polling Rate'` 1085 | `0x0040bbd0` | `125Hz 250Hz 500Hz 1000Hz 2000Hz 4000Hz 8000Hz` (§10.4) |
| `'LOD'` 1024 | `0x0040bbd0` and `0x0040ee80` | `0.7mm`…`1.7mm` in 0.1 steps, then `2.0mm` — twelve |
| unidentified, page 137 | `0x00405f30` | `OFF` `GX Speed Mode` `GX Safe Mode` |

The six button-assignment combos on 139 and the two `'SPDT:'` combos on 153 are
not yet traced. **This table is a LIST 3 starting point, not a derivation:**
which wire byte each index corresponds to is not established by any of it.

### 12.3 The gap this section opened, and closed the same hour

Kept as a worked example of why §1.2a exists, not as an open item.

§12.2's mechanical tab count — four — contradicted §10.4's reading of event
`0x06` as selecting among **seven** tab indices. Seven arms cannot select among
four tabs, so one of the two had to be wrong. The resolution is in §10.4's
correction block: the message is `CB_SETCURSEL`, not `TCM_SETCURSEL`, the target
is the `'Polling Rate'` combo and not the tab control, and the seven values are
**125 / 250 / 500 / 1000 / 2000 / 4000 / 8000 Hz**.

Two things about how it was caught are worth more than the finding.

**Nothing about §10.4 looked wrong from inside §10.4.** The handler really does
send `0x14E` to a window handle held in the dialog object, really does index
`0..6`, and a mouse configurator really does have a tab control. The claim only
failed against a fact from a *different kind of evidence* — a count of tab
insertions in the resource-facing code. That is what §1.2a is asking for when it
says to check the user-visible surface: not a second opinion on the same bytes,
but a constraint from somewhere the first method could not see.

**The mislabel was a plausible constant, which is the dangerous kind.** `0x14E`
was read as `TCM_SETCURSEL`; it is `CB_SETCURSEL`, and `TCM_SETCURSEL` is
`0x130C`. Nothing in the disassembly says which. Every Win32 message constant in
these notes deserves the same treatment before it is leaned on — the value
decides the *control class*, and the control class is what the wire byte means.
