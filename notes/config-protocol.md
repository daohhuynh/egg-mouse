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

### 7.2a The `A0 11` write frame's header is ALL ZEROS  [D]

Derived 2026-09-05 from raw disassembly of `FUN_00404180` @ `0x00404180`, the
APPLY handler. This had been recorded as an open gap blocking the config write
path — "we have the command byte and the 1024-byte payload at `+0x10` and
**nothing** about the fifteen header bytes of a write" — on the reasoning that
the read gives a *response* frame and those bytes need not mean the same thing
outbound. That reasoning was right; the conclusion that only a capture could
answer it was not. The builder is in the binary.

Buffer base is `-0x46c(%ebp)`:

```
4041f9  pushl $0x411 ; leal -0x46c(%ebp),%ecx ; pushl $0 ; pushl %ecx
404206  calll 0x508df0              ; memset(buf, 0, 0x411)   -- all 1041 bytes
40420b  movw  %bx,  -0x468(%ebp)    ; buf[4..5] = 0   (bx is 0; xorl at 0x4041cf)
404212  movb  %bl,  -0x466(%ebp)    ; buf[6]    = 0
404218  movl  $0x11a0, -0x46c(%ebp) ; buf[0..3] = a0 11 00 00
404222  movl  $0x100, %ecx          ; 0x100 dwords = 1024 bytes
404227  leal  -0x86c(%ebp), %esi    ; source: the composed settings blob
40422d  leal  -0x45c(%ebp), %edi    ; dest:   base + 0x10 = buf[16]
404233  rep   movsl
404238  movl  $0x411, %esi          ; length 1041
404243  calll 0x403850              ; send
```

So the frame is:

```
[0]        = 0xA0
[1]        = 0x11
[2..15]    = 0          -- from the memset; nothing writes them
[16..1039] = the 1024 settings bytes
```

**Every byte from `[2]` to `[15]` is zero**, and the frame is a mirror of the
read response's layout, which is what read-modify-write needs.

Two details worth keeping. First, `[4..5]` and `[6]` are explicitly re-zeroed
*after* a `memset` that already zeroed them. That is dead code here; whether it
means those fields carry something in a sibling builder is **[G]** and does not
matter, because in this builder they are zero. Second, the payload source at
`-0x86c(%ebp)` is itself `memset` to `0x400` bytes of zero at `0x4041d1` and
then filled by `FUN_004042d0` — so the tool composes the blob from its own UI
state rather than modifying what it read. **We do not copy that.** §4.1 requires
read-modify-write; composing from scratch is exactly what it forbids.

The Windows capture still corroborates this, and `records.py` prints every write
header and shouts if it varies between frames. But the write path is no longer
gated on it.

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
| `'SPDT:'` 1062 and 1063 | `0x00405f30` | `OFF` `GX Speed Mode` `GX Safe Mode` |

The six button-assignment combos on 139 are not yet traced. **This table is a
LIST 3 starting point, not a derivation:** which wire byte each index
corresponds to is not established by any of it.

### 12.2a The SPDT combos ARE the GX combos, and the older builds prove it

The row above said "unidentified, page 137" until 2026-09-04. It was two
mistakes in one cell: the page was wrong, and the control was sitting in the
same section two paragraphs earlier under a different name.

`cfg107` fills combos **1062** and **1063** from `0x00405f30` with
`OFF` / `GX Speed Mode` / `GX Safe Mode`, and 1062/1063 are the two combos
captioned `'SPDT:'` on DIALOG 153. So `SPDT` is the *label* and the GX triple is
its *value list*. They are one setting, not two.

The three older builds settle it without needing the filler traced at all,
because there the same two combos carry a DLGINIT and it names them outright.
`cfg104`, DIALOG **140** — not 137 — where two `'SPDT:'` STATICs sit
immediately before 1062 and 1063:

```
  DLGINIT for dialog 140
       1062  CB_ADDSTRING    'OFF'
       1062  CB_ADDSTRING    'GX Speed Mode'
       1062  CB_ADDSTRING    'GX Safe Mode'
       1063  CB_ADDSTRING    'OFF'
       1063  CB_ADDSTRING    'GX Speed Mode'
       1063  CB_ADDSTRING    'GX Safe Mode'
```

Identical in `cfg100` and `cfg101`. Between 1.04 and 1.07 the vendor split the
one long DIALOG 140 into 140 (Advanced Sensor) and 153 (Button Mapping), moved
1062/1063 to 153, and **dropped the DLGINIT in favour of a code fill** — which
is why the strings are ASCII in `.rsrc` in the three old builds
(`cfg104` `0x0059fa74`, `cfg101` `0x005a0a20`, `cfg100` `0x005f90b8`) and
UTF-16 in `.rdata` in 1.07 (`0x00556f78`, `0x00556f94`).

**Method note, because this is the §1.2a pattern again.** The encoding change is
what found it. A UTF-16-only scan of 1.07 sees the GX strings; the same scan of
1.04 sees nothing, and would have concluded 1.04 lacks the feature. It does not
— the strings are there in ASCII, in a DLGINIT, where `rc.exe` puts them.
Scanning both encodings in all four builds is what turned an "unidentified"
row into a derivation.

**Two [D] facts fall out that are not about GX at all:**

1. **Polling rate gained three steps between 1.04 and 1.07.** `cfg104`'s combo
   **1061** on DIALOG 140 carries a four-item DLGINIT — `1000Hz` `2000Hz`
   `4000Hz` `8000Hz`. `cfg107`'s combo **1085** is filled from code with seven —
   `125Hz` … `8000Hz` (§10.4). The low three rates are new in 1.07, and §10.4's
   one-hot event byte has a bit for each of the seven.
2. **`Slamclick Filter` and `Multiclick Filter` are different controls.**
   `1029 'Slamclick Filter'` is a single `AUTOCHECKBOX` — one bit, on or off.
   Multiclick Filter is **five** `EDIT` + `msctls_trackbar32` pairs, one per
   button (Left/Right/Middle/Forward/Back), each a numeric value. Both are
   present together on the same page in all four builds. Neither is a rename of
   the other, and the vendor's own acknowledgement checkbox `1064` distinguishes
   the Multiclick Filter from a debounce slider in as many words.

**`Spamclick` appears zero times in any vendor binary.** Search space, stated
per §1.2a: all nine `.exe` files (four config builds, four updaters, XM1r),
**whole file** rather than any single section, for `Spamclick` / `spamclick` /
`SPAMCLICK` / `Spam Click` / `Spam-click` and the bare substring `Spam`, in
ASCII, UTF-16LE and UTF-16BE. Zero hits anywhere. Positive control on the same
method in the same run: `Slamclick` → `0x1931b8`, `Multiclick` → `0x193270`,
`GX Speed Mode` → `0x155978` (file offsets, cfg107), so the method does find
strings that are present.

What this negative does and does not cover: it is a literal-string scan, so it
cannot see a name assembled at run time or held only in a resource this repo
does not parse. It is strong enough for the only use it is put to — that
`Spamclick` is not Endgame's word for anything — and it is not evidence about
any *capability*.

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

## 7.2b The host default record at `0x413db0`, and why it is NOT the wire record

`0x413db0` writes the whole factory-default settings object as inline
immediates, one field at a time, into the object in `%eax`. Every byte below is
`[D]` and read from raw disassembly, not from Ghidra's decompiler:

| struct offset | value | instruction |
| --- | --- | --- |
| `0x02`–`0x03` | `0x0080` | `413db5 movw %dx,0x2(%eax)` |
| `0x04`–`0x07` | `0x01010000` | `413e2a movl $0x1010000,0x4(%eax)` |
| `0x0a`–`0x0b` | `0x0401` | `413e31 movw $0x401,0xa(%eax)` |
| `0x0e`, `0x10`, `0x12` | `0`, `400`, `400` | `413e02`, `413dbe`, `413dc2` |
| `0x14`, `0x16`, `0x18` | `0`, `800`, `800` | `413e05`, `413dcb`, `413dcf` |
| `0x1a`, `0x1c`, `0x1e` | `0`, `1600`, `1600` | `413e08`, `413ddb`, `413ddf` |
| `0x20`, `0x22`, `0x24` | `0`, `3200`, `3200` | `413e0b`, `413deb`, `413def` |
| `0x26`–`0x29` | `0x00000301` | `413e37` |
| `0x2b`–`0x2e`, `0x2f` | `0`, `1` | `413e3e`, `413e27` |
| `0x34`+`8k`, k=0..6 | `0x08` | `413e45`/`4f`/`59`/`63`/`71`/`7f`/`8d` |
| `0x36`,`0x3e`,`0x46`,`0x4e` | `0x0200`,`0x0400`,`0x1000`,`0x0800` | `413e48`/`52`/`5c`/`66` |
| `0x56`,`0x5e`,`0x66` | `0xf109`,`0x0101`,`0xff01` | `413e74`/`82`/`90` |
| `0x6c` | `0x08` | `413e9b` |

`%cl`, `%si` and `%cx` are all zeroed before use (`413dd3`, `413de9`, `413e0e`)
and `%bl`=1, `%dl`=8, so every value above is a literal.

**The important negative: this layout is not what goes on the wire.** Sliding
these 42 non-zero default bytes across the 1040-byte reply in
`01-baseline.pcapng` gives a best agreement of **10 of 42, at no offset better
than noise**. The structures genuinely differ:

| | host struct (`0x413db0`) | wire record (`[O]`) |
| --- | --- | --- |
| CPI stage stride | **6** — flag at `0x0e`, pad `0x0f`, X `0x10`, Y `0x12` | **5** — `90 01 90 01 00` at wire `0x34` |
| button record stride | **8** from `0x2c`, code at `+8`, mask at `+3` | **7** from wire `0x48`, code first, `0x08` at `+5` |

That is consistent with what §7.2a already found — the vendor **composes** the
outgoing blob from UI state in `FUN_004042d0` rather than copying the object it
read. So `0x413db0`'s offsets are host-side C++ member offsets and **must not be
used as wire offsets**. They are still worth having: the *values* are the
factory defaults, and a factory-reset capture makes them directly checkable.

**This is now a pre-registered prediction.** It is tested by
`07-factory-reset.pcapng`, which resets and then **relaunches the config tool
inside the same capture** — so that file carries the factory-default record as a
fresh baseline read. (A separate `01b-reset` was planned and dropped on
2026-09-05: 07 does the same job and the relaunch makes it strictly better.)

- **Expect:** its CPI table at wire `0x34` reads 400/400, 800/800, 1600/1600,
  3200/3200, and its button table at wire `0x48` reads codes
  `01 02 04 10 08 f1 01 ff` at stride 7 — the same multiset of values as the
  table above, at the wire's own offsets rather than the struct's.
- **REFUTED IF:** any default *value* present in `0x413db0` is absent from the
  post-reset wire record, or the reset record differs from
  `01-baseline.pcapng`'s in the CPI or button regions (the owner had not changed
  those before the baseline).
- **Matters:** it converts the whole default record from `[D]` to `[O]`, and it
  is the reference blob §4.1 wants saved before any write.

## 7.3 The settings record is 115 bytes, and its whole byte map is derived  [D]+[O]

`FUN_004042d0` (cfg107, `0x004042d0`–`0x004045dc`) is the **serializer**: it
copies the settings object byte by byte into the outgoing record. Nothing else
happens in it — it is 111 load/store pairs and a `retl`.

Extracted mechanically, not by eye, by parsing the raw disassembly for the
strict alternation `mov{zbl,b} disp(%ecx), %reg` / `movb %reg, disp(%eax)` and
**stopping at the first instruction that is neither** (§1.2b — a parser that
skips what it does not recognise silently drops fields and yields a map that
looks complete). It stopped exactly on the `retl` at `0x4045dc`, and the load
and store registers are checked to match on every pair.

**Result: the record is `0x00`–`0x72`, 115 bytes.** Record bytes `0x01`–`0x04`
are the only ones this function does not write; everything else has a traced
origin. Since the payload sits at buffer `+0x10` (§7.2a) and the wire index is
the buffer index (`wire-observed.md` §2.1), **record `r` is wire `0x10 + r`**,
so the record ends at wire `0x82` — and the last non-zero byte in
`01-baseline.pcapng` is at wire `0x81`. The 115 was arrived at from the binary
and the 0x81 from the capture, independently.

### The check that matters

Pushing `0x413db0`'s inline factory defaults (§7.2b) through this map predicts
91 of the 115 record bytes — the other 24 come from object bytes the default
writer never sets. Against `01-baseline.pcapng`, which is **the owner's own settings
and not defaults**:

> **90 of 91 predicted bytes match the observed device record.**

The single disagreement is record `0x71` (wire `0x81`): predicted `0x00`,
observed `0x01`. It is the last non-zero byte in the whole record, it comes from
object `0x2d`, and the honest reading is that **the owner had changed exactly one
setting from factory default** — which `01b-reset.pcapng` will confirm or refute
outright, since after a reset that byte must read `0x00`.

Two things this is *not*. It is not a fit: the map was extracted before the
comparison and no parameter was tuned. And it is not proof the *meanings* are
right — it shows the layout and the values, and says nothing about which control
sets which byte.

### Structure visible in the record  [O]

| record | wire | what | confidence |
| --- | --- | --- | --- |
| `0x0f`–`0x22` | `0x1f`–`0x32` | **four 5-byte RGB records**: `ff ff 00 01 01`, `00 00 ff 01 02`, `ff 00 00 01 03`, `00 ff 00 01 04` — yellow/blue/red/green, each followed by `0x01` and a **stage index 1..4** | structure `[O]`, meaning `[G]` |
| `0x23`–`0x36` | `0x33`–`0x46` | **four 5-byte CPI records**, `flag, X lo, X hi, Y lo, Y hi`: 400/400, 800/800, 1600/1600, 3200/3200, each led by an `X≠Y` flag byte | `[D]` — **corrected §7.8** |
| `0x37`–`0x6e` | `0x47`–`0x7e` | **eight 7-byte button records** (§7.11). Masks `01 02 04 10 08 f1 01 ff` at `+1`; the `0x08` at `+6` is the **multiclick filter**, default 8. Was written `0x37`–`0x6f` here, which is 57 bytes for 8×7=56 — corrected | structure `[D]`, roles of `+0` and `+2`..`+5` still `[G]` |
| `0x70`–`0x72` | `0x80`–`0x82` | three trailing bytes. `0x70` is **Sensor Angle Tuning** (§7.9, `TBM_GETPOS`), `0x71` is **Force max Sensor fps** (§7.8) and is the byte the owner changed. Only `0x72` is still unattributed | `0x70`/`0x71` `[D]`, `0x72` `[G]` |

**The RGB block resolves `wire-observed.md` §5.1**, which recorded four records
"reading as RGB" but with a stride that would not close. The stride is 5 and the
block runs record `0x0f`–`0x22`, contiguous from object `0x6e`–`0x81`; the
trailing `01 02 03 04` are stage indices, which is what makes the grouping
unambiguous. Four colours indexed by CPI stage is a CPI-stage indicator.

**Corrected 2026-09-05. The paragraph that stood here was wrong, and wrong in a
way worth keeping visible.** It said these bytes were "permanently `[G]` as to
meaning — no capture can attribute them, because no control moves them". The
first clause is still true: the LED *page* is unreachable in all four config
tools (`gui-surface.md` §3), so no UI control writes these bytes. The conclusion
does not follow. Attribution does not require a *control*; it requires an
*observation*, and the device displays these bytes on its own.

The owner, 2026-09-05: the OP1 8k v2 has **a small circular LED on the underside that
shows the current DPI level as a colour**, lit continuously. That is precisely
what four colour entries tagged with CPI stages 1–4 describe. So the block is
not decorative slack and not unknowable — it is the DPI-stage indicator, and it
is readable by eye with no capture, no write, and no risk.

**Committed prediction, registered before the owner looked** — and **REFUTED the same
day.** Predicted: stage 1 yellow, 2 blue, 3 red, 4 green, from reading the
trailing `01 0N` as "this colour belongs to CPI stage N".

The owner, 2026-09-05, giving Endgame's own numbering: **stage 1 blue, stage 2 green,
stage 3 yellow, stage 4 red.**

| block pos | bytes | colour (RGB) | its tag | stage the owner sees it at |
| --- | --- | --- | --- | --- |
| 0 @`0x0f` | `ff ff 00` | yellow | 1 | **3** |
| 1 @`0x14` | `00 00 ff` | blue | 2 | **1** |
| 2 @`0x19` | `ff 00 00` | red | 3 | **4** |
| 3 @`0x1e` | `00 ff 00` | green | 4 | **2** |

The colour **set** matches exactly — all four, none extra. The **order** does
not: tag→stage is the permutation `1→3, 2→1, 3→4, 4→2`, which is not the
identity, a reversal, or a rotation.

What survives and what does not:

- **Survives:** the block's shape. Four records of `[3 colour bytes][0x01][index]`
  at stride 5, records `0x0f`–`0x22`. The re-phasing that would put the index
  *before* its triple is ruled out by the boundary: record `0x23` is `00` and
  `0x24`–`0x25` is `90 01` = 400, which is the first CPI stage (§7.8), so the
  block cannot extend past `0x22`.
- **Dead:** "the trailing byte is the CPI stage this colour is used for". The
  index is just the record's own position, 1-based, and carries no stage
  meaning. Any stage→colour mapping lives somewhere this block does not show.
- **Still open, and it moved twice in an hour:** whether this block is the DPI
  indicator's palette. The first version of this bullet said it probably was
  not, resting on the owner's observation that "Disable LED on Lift-Off" did not
  affect the indicator. **That observation was superseded within the hour by
  the owner himself**: with the box ticked *and applied*, lifting the mouse puts the
  indicator out (2026-09-05). He had not applied it the first time. So the
  config tool **does** control this LED, through record `0x08`, and there is
  exactly one LED rather than an indicator plus an absent RGB system.

  That makes "these four colours are the indicator's palette" plausible again.
  It does **not** rescue the tag: the order refutation is an independent
  observation and stands. If the block is the palette, the slot→stage mapping
  lives somewhere else. Meaning stays `[G]`; §1.3 still forbids writing it.

So these bytes go back to `[G]` as to meaning — but for a **reason that was
tested**, not by assumption, and with the shape now `[O]`. §1.3 forbids writing
them either way, and this is what §1.3 is for: had we "known" the tail was a
stage index, we would have written it wrong.

Two lessons, and the second is the more expensive one.

§1.2a, applied to ourselves: "no control moves them" is a statement about the
*config tool*, and it was silently widened into a statement about the *world*.
The device is a second observer, and it had not been consulted before the word
"permanently" was written down.

§1.2, applied to a **[G] that had started reading as a finding.** The words
"stage index 1..4" and "Four colours indexed by CPI stage is a CPI-stage
indicator" were written in §7.3 tagged `structure [O], meaning [G]` — correctly
tagged, and then reasoned from anyway, twice, until the tag stopped being read.
The refutation cost one sentence from the owner. It would have cost a wrong write if
this block had ever been reachable. §7.1's last line names this exact failure:
"a hedge that has become an assertion by its third restatement".

### The map

`rec` is the record offset, `wire` the offset in a captured reply, `obj` the
settings-object offset it is copied from, `at` the instruction.

| `0x00` | `0x010` | `0x00` | `4042d0` |
| `0x05` | `0x015` | `0x06` | `4042d5` |
| `0x06` | `0x016` | `0x07` | `4042dc` |
| `0x07` | `0x017` | `0x0c` | `4042e3` |
| `0x08` | `0x018` | `0x26` | `4042ea` |
| `0x09` | `0x019` | `0x27` | `4042f1` |
| `0x0a` | `0x01a` | `0x28` | `4042f8` |
| `0x0b` | `0x01b` | `0x29` | `4042ff` |
| `0x0c` | `0x01c` | `0x2a` | `404306` |
| `0x0d` | `0x01d` | `0x0a` | `40430d` |
| `0x0e` | `0x01e` | `0x0b` | `404314` |
| `0x0f` | `0x01f` | `0x6e` | `40431b` |
| `0x10` | `0x020` | `0x6f` | `404322` |
| `0x11` | `0x021` | `0x70` | `404329` |
| `0x12` | `0x022` | `0x71` | `404330` |
| `0x13` | `0x023` | `0x72` | `404337` |
| `0x14` | `0x024` | `0x73` | `40433e` |
| `0x15` | `0x025` | `0x74` | `404345` |
| `0x16` | `0x026` | `0x75` | `40434c` |
| `0x17` | `0x027` | `0x76` | `404353` |
| `0x18` | `0x028` | `0x77` | `40435a` |
| `0x19` | `0x029` | `0x78` | `404361` |
| `0x1a` | `0x02a` | `0x79` | `404368` |
| `0x1b` | `0x02b` | `0x7a` | `40436f` |
| `0x1c` | `0x02c` | `0x7b` | `404376` |
| `0x1d` | `0x02d` | `0x7c` | `40437d` |
| `0x1e` | `0x02e` | `0x7d` | `404384` |
| `0x1f` | `0x02f` | `0x7e` | `40438b` |
| `0x20` | `0x030` | `0x7f` | `404392` |
| `0x21` | `0x031` | `0x80` | `404399` |
| `0x22` | `0x032` | `0x81` | `4043a3` |
| `0x23` | `0x033` | `0x0e` | `4043ad` |
| `0x24` | `0x034` | `0x10` | `4043b4` |
| `0x25` | `0x035` | `0x11` | `4043bb` |
| `0x26` | `0x036` | `0x12` | `4043c2` |
| `0x27` | `0x037` | `0x13` | `4043c9` |
| `0x28` | `0x038` | `0x14` | `4043d0` |
| `0x29` | `0x039` | `0x16` | `4043d7` |
| `0x2a` | `0x03a` | `0x17` | `4043de` |
| `0x2b` | `0x03b` | `0x18` | `4043e5` |
| `0x2c` | `0x03c` | `0x19` | `4043ec` |
| `0x2d` | `0x03d` | `0x1a` | `4043f3` |
| `0x2e` | `0x03e` | `0x1c` | `4043fa` |
| `0x2f` | `0x03f` | `0x1d` | `404401` |
| `0x30` | `0x040` | `0x1e` | `404408` |
| `0x31` | `0x041` | `0x1f` | `40440f` |
| `0x32` | `0x042` | `0x20` | `404416` |
| `0x33` | `0x043` | `0x22` | `40441d` |
| `0x34` | `0x044` | `0x23` | `404424` |
| `0x35` | `0x045` | `0x24` | `40442b` |
| `0x36` | `0x046` | `0x25` | `404432` |
| `0x37` | `0x047` | `0x2e` | `404439` |
| `0x38` | `0x048` | `0x2f` | `404440` |
| `0x39` | `0x049` | `0x30` | `404447` |
| `0x3a` | `0x04a` | `0x31` | `40444e` |
| `0x3b` | `0x04b` | `0x32` | `404455` |
| `0x3c` | `0x04c` | `0x33` | `40445c` |
| `0x3d` | `0x04d` | `0x34` | `404463` |
| `0x3e` | `0x04e` | `0x36` | `40446a` |
| `0x3f` | `0x04f` | `0x37` | `404471` |
| `0x40` | `0x050` | `0x38` | `404478` |
| `0x41` | `0x051` | `0x39` | `40447f` |
| `0x42` | `0x052` | `0x3a` | `404486` |
| `0x43` | `0x053` | `0x3b` | `40448d` |
| `0x44` | `0x054` | `0x3c` | `404494` |
| `0x45` | `0x055` | `0x3e` | `40449b` |
| `0x46` | `0x056` | `0x3f` | `4044a2` |
| `0x47` | `0x057` | `0x40` | `4044a9` |
| `0x48` | `0x058` | `0x41` | `4044b0` |
| `0x49` | `0x059` | `0x42` | `4044b7` |
| `0x4a` | `0x05a` | `0x43` | `4044be` |
| `0x4b` | `0x05b` | `0x44` | `4044c5` |
| `0x4c` | `0x05c` | `0x46` | `4044cc` |
| `0x4d` | `0x05d` | `0x47` | `4044d3` |
| `0x4e` | `0x05e` | `0x48` | `4044da` |
| `0x4f` | `0x05f` | `0x49` | `4044e1` |
| `0x50` | `0x060` | `0x4a` | `4044e8` |
| `0x51` | `0x061` | `0x4b` | `4044ef` |
| `0x52` | `0x062` | `0x4c` | `4044f6` |
| `0x53` | `0x063` | `0x4e` | `4044fd` |
| `0x54` | `0x064` | `0x4f` | `404504` |
| `0x55` | `0x065` | `0x50` | `40450b` |
| `0x56` | `0x066` | `0x51` | `404512` |
| `0x57` | `0x067` | `0x52` | `404519` |
| `0x58` | `0x068` | `0x53` | `404520` |
| `0x59` | `0x069` | `0x54` | `404527` |
| `0x5a` | `0x06a` | `0x56` | `40452e` |
| `0x5b` | `0x06b` | `0x57` | `404535` |
| `0x5c` | `0x06c` | `0x58` | `40453c` |
| `0x5d` | `0x06d` | `0x59` | `404543` |
| `0x5e` | `0x06e` | `0x5a` | `40454a` |
| `0x5f` | `0x06f` | `0x5b` | `404551` |
| `0x60` | `0x070` | `0x5c` | `404558` |
| `0x61` | `0x071` | `0x5e` | `40455f` |
| `0x62` | `0x072` | `0x5f` | `404566` |
| `0x63` | `0x073` | `0x60` | `40456d` |
| `0x64` | `0x074` | `0x61` | `404574` |
| `0x65` | `0x075` | `0x62` | `40457b` |
| `0x66` | `0x076` | `0x63` | `404582` |
| `0x67` | `0x077` | `0x64` | `404589` |
| `0x68` | `0x078` | `0x66` | `404590` |
| `0x69` | `0x079` | `0x67` | `404597` |
| `0x6a` | `0x07a` | `0x68` | `40459e` |
| `0x6b` | `0x07b` | `0x69` | `4045a5` |
| `0x6c` | `0x07c` | `0x6a` | `4045ac` |
| `0x6d` | `0x07d` | `0x6b` | `4045b3` |
| `0x6e` | `0x07e` | `0x6c` | `4045ba` |
| `0x6f` | `0x07f` | `0x2b` | `4045c1` |
| `0x70` | `0x080` | `0x2c` | `4045c8` |
| `0x71` | `0x081` | `0x2d` | `4045cf` |
| `0x72` | `0x082` | `0x08` | `4045d6` |
**Record `0x01`–`0x04` are written by something else.** They are not in this
function, and in the baseline they read `80 00 00 00` (wire `0x11`–`0x14`).
`0x80` also appears as object `0x02`–`0x03` = `0x0080` in the default writer,
which the serializer never copies — suggestive, not established. **Open, and
listed in `working-memory.md`.** Do not write those four bytes on any inference
from this paragraph; §1.3 applies and read-modify-write preserves them for free.

## 7.4 The vendor ZEROES record `0x01`–`0x04` on write, and the device reports `0x80` there  [D]+[O]

`FUN_00404180` is the only meaningful caller of the serializer, and the full
sequence is now accounted for byte by byte:

```
4041d1  pushl $0x400 ; leal -0x86c(%ebp),%eax ; pushl %ebx(=0) ; pushl %eax
4041e7  calll 0x508df0                ; memset(payload, 0, 1024)
4041ec  leal  -0x86c(%ebp), %eax
4041f2  movl  %esi, %ecx              ; ecx = the settings object
4041f4  calll 0x4042d0                ; the serializer, 7.3
4041f9  pushl $0x411 ; leal -0x46c(%ebp),%ecx ; …
404206  calll 0x508df0                ; memset(frame, 0, 1041)
404218  movl  $0x11a0, -0x46c(%ebp)   ; a0 11 00 00
404233  rep   movsl                   ; payload -> frame+0x10, 1024 bytes
```

**Nothing writes record `0x01`–`0x04`.** The payload is zeroed at `0x4041e7`,
the serializer skips those four, and no instruction between the call and the
`rep movsl` touches them. So the vendor transmits `00 00 00 00` there.

The device does not. `01-baseline.pcapng` returns **`80 00 00 00`** at wire
`0x11`–`0x14` = record `0x01`–`0x04`.

### This is a decision, not a finding, and it is the owner's

Those four bytes are the only place where §4.1's read-modify-write and "do what
the vendor does" give **different bytes on the wire**:

- **§1.3 as written** says preserve what we do not understand, so `egg-config
  restore` sends the device's own `0x80` back.
- **Matching the vendor** means zeroing them, because that is what the only
  known-working writer does.

Neither is obviously safe. Preserving means sending the device a value its own
tool has never sent it; matching means deliberately discarding a byte the device
chose to report, which is precisely what §1.3 exists to prevent. A plausible
reading — `0x80` is a direction or status marker meaningful only device→host —
is **[G]** and cannot justify either choice.

**Recommendation: follow the vendor and zero them**, on the narrow grounds that
the vendor's write path is the only write path observed to work, and these four
bytes are the only ones where we would otherwise diverge from it. But this is
not mine to settle and nothing is changed until the owner rules.

**Until then `egg-config restore` preserves them** (current behaviour, §1.3's
default) and must say so when it runs, so the divergence is never silent. The
same four bytes are also the reason a future frame-by-frame diff of our output
against a vendor capture will show a difference that is **expected, not a bug**.

## 7.5 Record byte `0x05` is the polling rate, encoded as a divisor  [D]

The first record byte whose **meaning and encoding** are derived rather than
guessed, and it is testable by `02-basic` lines 1–7.

`0x00413a79` dispatches on the stored byte and `0x00413a94`–`0x00413b8f` is a
seven-way switch. Every arm does exactly two things: `CB_SETCURSEL` (`0x14E`) on
the combo at dialog `+0x13b8`, and a write of one constant to **both** mirrors,
`0x0057f2a6` and `0x0057f216`:

| arm | combo index | byte written |
| --- | --- | --- |
| `0x413a94` | 0 | `0x40` = 64 |
| `0x413abb` | 1 | `0x20` = 32 |
| `0x413ae2` | 2 | `0x10` = 16 |
| `0x413b09` | 3 | `0x08` = 8 |
| `0x413b2d` | 4 | `0x04` = 4 |
| `0x413b53` | 5 | `0x02` = 2 |
| `0x413b79` | 6 | `0x01` = 1 |

`0x0057f2a6` is the settings-object mirror at object offset `0x06`
(`0x0057f2a0 + 0x06`, §9's `FUN_00404830`), and the serializer maps object
`0x06` to **record `0x05`** (§7.3, instruction `0x4042d5`). `0x0057f216` is the
same offset in the second parallel block at `0x0057f210`.

**Both directions were checked, which is what makes this more than a pattern.**
The read path at `0x413a79` does `movzbl (%ebx),%eax; decl %eax; cmpl $0x3f; ja
default`, then indexes a 64-entry byte table at `0x00413c04` to pick a case from
the address table at `0x00413be4`. Decoding both tables from raw bytes:

```
value  1 -> case 0 -> 0x413b79 -> writes 0x01, selects index 6
value  2 -> case 1 -> 0x413b53 -> writes 0x02, selects index 5
value  4 -> case 2 -> 0x413b2d -> writes 0x04, selects index 4
value  8 -> case 3 -> 0x413b09 -> writes 0x08, selects index 3
value 16 -> case 4 -> 0x413ae2 -> writes 0x10, selects index 2
value 32 -> case 5 -> 0x413abb -> writes 0x20, selects index 1
value 64 -> case 6 -> 0x413a94 -> writes 0x40, selects index 0
everything else (1..64) -> case 7 -> 0x413bab, which does nothing
```

Every value writes itself back and selects the arm that produced it, so the byte
round-trips. Only the seven powers of two from 1 to 64 are accepted; the other
57 values in 1..64 fall to a no-op, and 0 and >64 never reach the table at all.

### The prediction, registered before any polling capture was read

`02-basic` lines 1–7 step polling 125 → 250 → 500 → 1000 → 2000 → 4000 → 8000,
one APPLY each. **I have not seen that file.**

- **Expect:** record `0x05` (wire `0x15`) is the only byte those seven APPLYs
  move as a settings change, taking the values `40 20 10 08 04 02 01` in that
  order — and in general **`polling_rate × record[0x05] == 8000`**.
- **REFUTED IF:** record `0x05` does not change across those lines; or it takes
  a value outside {1,2,4,8,16,32,64}; or the product is not 8000 for any line.
- **Weaker on order, deliberately.** That the combo lists 125 first is `[G]` —
  the switch fixes index↔value but nothing here says which *rate* an index
  displays. If the list is descending the sequence reverses and the product law
  still holds, so **the product is the real claim** and the byte order is a
  secondary one that a refutation should not be allowed to hide behind.
- **Matters:** it is the first record byte with a derived meaning, it gives
  `egg-config set polling` a value mapping that needs no capture, and the
  round-trip means a wrong value cannot be silently accepted — the vendor's own
  reader drops anything that is not a power of two to a no-op.

**Correction to a lead in `working-memory.md`:** it recorded dialog `+0x13b8` as
"a 7-tab control". It is a **combo box** — every access here is `CB_SETCURSEL`,
which a tab control does not take (`TCM_SETCURSEL` is `0x130C`).

## 7.6 Record `0x0e` is the CPI stage count, `0x0d` the selected stage  [D]

Four near-identical handlers at `0x00410c47`, `0x00410d77`, `0x00410ea7` and
`0x00410fd7` (and a second, matching set at `0x00411119`, `0x00411259`,
`0x00411399`, `0x004114d9`). Each one:

1. writes a fixed value **0, 1, 2 or 3** to `0x0057f21a` = object `0x0a`;
2. sends `CB_GETCURSEL` (`0x147`) to the combo at `0xf0(%esi)` and writes
   **index + 1** to `0x0057f21b` = object `0x0b`, via an explicit
   `cmpl $0/$1/$2` ladder falling through to 4:

   ```
   410c59  CB_GETCURSEL -> eax
   410c5e  jne  ...          ; eax == 0
   410c60  movb $0x1, 0x57f21b
   410c7b  cmpl $0x1, %eax ; 410c80  movb $0x2, 0x57f21b
   410c9b  cmpl $0x2, %eax ; 410ca0  movb $0x3, 0x57f21b
   410cc0  movb $0x4, 0x57f21b        ; the fall-through
   ```
3. then writes that stage's CPI pair — `0x10`/`0x12`, `0x16`/`0x18`,
   `0x1c`/`0x1e`, `0x22`/`0x24` for stages 0–3 respectively.

The serializer (§7.3) maps object `0x0a` → **record `0x0d`** and object `0x0b` →
**record `0x0e`**.

| | value | tag |
| --- | --- | --- |
| record `0x0e` | number of CPI stages, `1`–`4`, = combo index + 1 | `[D]` |
| record `0x0d` | which CPI stage, `0`–`3` | **both `[D]`** — §7.18.4, cfg107 `0x40edf0`–`0x40ee4c` |

`0x0d`'s **range** is derived; its **meaning** is not. "Selected/active stage"
is the natural reading and the handlers are consistent with it, but they are
equally consistent with "the stage last edited", and nothing here separates
those. It is `[G]` and stays `[G]` until something moves it independently.

Both agree with what is already on the wire without being fitted to it. The
default writer sets object `0x0a`=`0x01`, `0x0b`=`0x04` (`0x413e31`,
`movw $0x401,0xa(%eax)`), and `01-baseline.pcapng` reads record `0x0d`=`0x01`,
`0x0e`=`0x04` — four stages, stage 1.

### Prediction, registered before the polling/CPI capture was read

`02-basic` lines 11–14 set CPI levels to 1, 2, 3, 4, one APPLY each.

- **Expect:** record `0x0e` (wire `0x1e`) takes `01 02 03 04` across those four
  lines and is the only settings byte they move.
- **REFUTED IF:** `0x0e` does not change across lines 11–14, or takes any value
  outside 1–4, or the sequence is not monotonic with the level count.
- **Also expect:** record `0x0d` (wire `0x1d`) stays in 0–3 throughout the whole
  capture and never exceeds `0x0e − 1`. A violation of that would mean `0x0d` is
  not a stage index at all.

## 7.7 The button block is eight entries of stride 8 in the object  [D]

`0x00407763` onward, the button-mapping page's write path:

```
407763  movl  0x380(%esi), %ecx           ; ecx = button index
40776b  movw  %dx, 0x57f240(,%ecx,8)      ; obj[0x30 + 8*idx] = 0
40777b  movw  %cx, 0x57f242(,%eax,8)      ; obj[0x32 + 8*idx] = 0
407789  cmpl  $0x7, %eax
40778c  ja    default
407792  jmpl  *0x408430(,%eax,4)          ; 8-entry jump table
```

**Eight buttons**, index `0`–`7`, bounded explicitly by `cmpl $0x7` and by an
8-entry jump table — which is why the wire shows exactly eight button records
(`wire-observed.md` §5.2, `config-protocol.md` §7.3). Two independent counts
agreeing, one from a bound in the code and one from the bytes.

**Object stride is 8. The base is `0x2e`, not `0x2c`** — corrected 2026-09-05,
see §7.11. The stride was right and the base was two bytes low, which put object
`0x2c` and `0x2d` inside "entry 0" when those are Sensor Angle Tuning and Force
max Sensor fps (§7.9) and belong to a different page entirely. The serializer
settles it without ambiguity: objects `0x2e`–`0x34` map to records `0x37`–`0x3d`
contiguously, then `0x36`–`0x3c` to `0x3e`–`0x44`, and so on for eight entries,
with one object byte (`0x35`, `0x3d`, `0x45`, …) dropped between each.

The entry layout below is superseded by §7.11, which derives it from the wire
bytes rather than from the default writer. It is kept only because the
`0x00407763` evidence above is still good: `0x57f240(,%ecx,8)` is `obj[0x30 +
8k]`, which under the corrected base is entry `+2`, not entry `+4`.

```
+0  u8   action type   00 for the five physical buttons; 09, 01, 01 for entries 5-7
+1  u8   mask          01 02 04 10 08 f1 01 ff
+2  u16                0, and zeroed on this path  (the 0x57f240 write)
+4  u16                0, and zeroed on this path  (the 0x57f242 write)
+6  u8   multiclick    8 in every default entry -- NOT a "type" byte
+7  u8   pad           never written, never serialized
```

**What is NOT derived here, and must not be inferred from the table above.**
The role of `type`, and of `a`'s low byte, and of `b` and `c`. The scan of
writes into the mirrors shows the low byte of `a` taking `0x0c 0x09 0x20 0x18
0x01` on other handlers around `0x00407c6f`–`0x00408240`, which is consistent
with an action code — and consistent with several other things. **`[G]`.**
`05-buttonmapping` is the capture that settles it, and the derivation above
tells the differ where to look rather than what it will find.

## 7.8 Checkboxes: every one, and where each lands in the record  [D]

`FUN_0040ec20` (`0x40ec20`–`0x40ee50`) is dialog 135's **collect-UI-into-the-
settings-object** pass: `%esi` = the page object, `%edi` = the settings object,
and every `movb`/`movw N(%edi)` in it is a settings-object offset. Its **only**
caller is `0x413f08`, inside the APPLY handler at `0x413ea0` — which takes a
reentrancy flag at `+0x2fa0`, looks the device up by PID `0x1978` (`0x413edb`),
then collects each page and serialises. So everything below is on the live write
path, not in dead code.

Control identities come from `DDX_Control` (`0x4207ed`), which binds a dialog
member to a control ID; the ID's caption is read straight out of `RT_DIALOG`
(`Tools/ghidra-export/dlgdump.py`). Object→record is §7.3's serializer table.

| checkbox | dlg | IDC | member | sense | object | **record** | at |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Slamclick Filter | 153 | 1029 | `0x628` | set bit | `0x07` bit 0 | **`0x06` bit 0** | `40677a`/`406786` |
| Motion Jitter Filter | 140 | 1030 | `0x1a4` | set bit | `0x07` bit 4 | **`0x06` bit 4** | `411e7a`/`411e86` |
| Angle Snapping | 135 | 1064 | `0xf24` | `setne` | `0x28` | **`0x0a`** | `40ecb8`/`40ecbd` |
| Disable LED on Lift-Off | 135 | 1066 | `0x127c` | **`sete`** | `0x26` | **`0x08`** | `40ecd2`/`40ecd7` |
| Force max Sensor fps | 140 | 1032 | `0x320` | `setne` | `0x2d` | **`0x71`** | `412018`/`41201b` |

### Record `0x06` is the record's only bitfield
**Corrected 2026-09-05, and the correction is the point.** The first version of
this section scanned only *absolute-addressed* operands, found four instructions
on `0x57f217`, and asserted the search space was "covered" on the grounds that
the seven sites loading a mirror base into a register all pass it straight to an
already-transcribed callee. That was false. `0x413efb` passes `%ebx =
0x57f210` to `FUN_00411ab0`, which does its **own** bit ops at `0x411aeb` /
`0x411af1` — `orb $0x10, 0x7(%ebx)`. An absolute-address scan cannot see those.
This is §1.2a's exact failure: a scan for one encoding form said nothing about
the others, and the conclusion was stated as coverage rather than as a method
with a blind spot.

The redone scan is every `or`/`and`/`xor` with an **immediate source and any
memory destination**, register-relative included, across the vendor range
`0x401000`–`0x415000`. Thirty-one instructions. Discarding `-0x4(%ebp)` frame
temporaries in `0x414a`–`0x414e` and two pointer inits at `0x414688`/`0x414716`,
the settings-object hits are exactly:

| at | op | byte | which |
| --- | --- | --- | --- |
| `40619d` / `4061a3` | `$0x1` / `$-0x2` | `0x7(%ebx)` | Slamclick, APPLY collect (dlg 153) |
| `40677a` / `406786` | `$0x1` / `$-0x2` | `0x57f217` | Slamclick, click handler |
| `411aeb` / `411af1` | `$0x10` / `$-0x11` | `0x7(%ebx)` | Motion Jitter, APPLY collect (dlg 140) |
| `411e7a` / `411e86` | `$0x10` / `$-0x11` | `0x57f217` | Motion Jitter, click handler |

So **each checkbox writes the bit twice**: once when clicked, straight into the
mirror by absolute address, and again when APPLY collects the page, through the
object pointer. Both agree on which bit, which is why the assignment above
survives the correction unchanged — but the *coverage* claim did not, and only
the wider scan established it.

Object `0x07` is still the only byte manipulated bit-wise; every other settings
byte is written whole. cfg107 writes **only bits 0 and 4**. Bits 1,2,3,5,6,7 are
never set or cleared by the config tool, so they are §1.3 read-modify-write
territory: preserve them.

One byte outside the record also takes bit ops — `0x57f335`, at `0x4051e7`,
`0x40524e`, `0x4052af`, `0x405310`, `0x40866f`, `0x408680`, `0x408689`, setting
bits `0x1 0x2 0x4 0x8`. It is **not** a settings byte: the two mirrors are 0x90
apart (§7) and `0x57f335` is `0x95` past the block-1 base, beyond the object.
**Identified 2026-09-05 — it is the KEYBOARD KEY dialog's modifier byte, in
standard USB HID encoding. §7.12.**

cfg107 writes **only bits 0 and 4**. Bits 1,2,3,5,6,7 are never set or cleared
by the config tool, so they are §1.3 read-modify-write territory: preserve them.
Bit 4's control is `Motion Jitter Filter`, which **the owner confirmed is not visible
on the Advanced Sensor page** — *"motion jitter filter and sensor glass mode are
not seen anywhere on advanced sensor"*, 2026-09-05, scored as
`cfg-advanced-sensor-hidden-controls` in `prediction-scores.md`. (**Re-anchored
2026-09-06.** This cited `./log.txt` 03 line 2, which is quarantined by
CLAUDE.md §1.1a. The observation itself is the owner's quoted words and stands; only
the pointer needed replacing.) So bit 4 is
unreachable from the shipped UI and no capture can attribute it. Note this does
not make bit 4 *unwritten*: `FUN_00411ab0` runs on every APPLY and clears the
bit from a hidden, unchecked box. Whatever the device reports in bit 4, the
vendor tool overwrites it with 0 on the next APPLY from that page.

### Disable LED on Lift-Off is INVERTED — `[D]`, and now confirmed `[O]`
`0x40ecd2` is `sete`, not `setne`: the box **ticked** stores **0**. Every other
checkbox here stores 1 when ticked. The record byte therefore means *"LED is
enabled on lift-off"*, and the caption negates it. This is precisely the shape
of error that §4.3 says compiles clean in any language, and it is the reason
`egg-config` must never infer a checkbox's polarity from its name.

**Confirmed on the device, the owner, 2026-09-05:** ticking the box, pressing APPLY
and lifting the mouse puts the underside DPI indicator out; untouched, it stays
lit at any height. So the caption is accurate about the *behaviour*, the `sete`
is accurate about the *byte*, and the two together fix the polarity without
needing the capture:

    record 0x08 == 1   indicator stays lit when lifted     (vendor default)
    record 0x08 == 0   indicator goes out when lifted      (vendor box TICKED)

`egg-config` exposes this as **`led-on-liftoff`**, named after the byte rather
than after the checkbox, precisely so that `1` never has to mean "disabled".
This also identifies the LED the vendor's caption refers to: it is the DPI
indicator on the underside, not a second light.

### Record `0x71` is Force max Sensor fps — which closes the 90/91 gap
§7.3 recorded that 90 of 91 predicted default bytes matched `01-baseline`, with
the single diff at record `0x71`, and §7.3's structure table could say no more
than "one of which (`0x71`) is the byte the owner changed". Object `0x2d` is written
only by `0x41201b`, from dialog 140's IDC 1032. So the byte that differed is
**Force max Sensor fps**, and the residual is explained rather than outstanding.

### Record `0x0d` (CPI stage index) is now [D], not [G]
§7.6 derived the range 1..4 but left the *meaning* `[G]`. `0x40edf0`–`0x40ee4c`
is four `BM_GETCHECK`s on dialog-135 members `0x13f4`, `0x1468`, `0x14dc`,
`0x1550`, each storing a literal **0, 1, 2, 3** to object `0x0a` → record
`0x0d`. Four mutually-exclusive checkboxes storing an index is a radio group.

~~these are the four unlabelled radio buttons in `log.txt` 02 lines 34–37~~ —
struck 2026-09-06 under §1.1a. The derivation never needed it: the ordering is
the vendor's own control ids, and §7.18.4 restates it with the addresses.

### The CPI block is record `0x23`–`0x36`, and §7.3 had the phase wrong
§7.3's structure table reads the CPI block as `0x24`–`0x37`, "two 16-bit LE
each … then a 5th byte `0x00`". That was `[O]`, read off the baseline bytes, and
**with all four flag bytes zero the flag-first and flag-last groupings produce a
byte-for-byte identical stream** — the capture alone cannot separate them. The
serializer can. Objects `0x0e/0x10-0x11/0x12-0x13` map to records
`0x23/0x24-0x25/0x26-0x27`, and `0x40ed47`–`0x40ed6e` shows what each is:

    obj 0x0e  flag   <- setne(dialog 0x4e8 != dialog 0xe7c)   "X differs from Y"
    obj 0x0f  PAD    <- never written, and NOT SERIALIZED
    obj 0x10  X lo }  <- movzwl 0x4e8(%esi)   (u16 LE)
    obj 0x11  X hi }
    obj 0x12  Y lo }  <- movzwl 0xe7c(%esi)   (u16 LE)
    obj 0x13  Y hi }

Object stride is 6 with a hole; wire stride is 5 because the pad is dropped.
The block is bounded on both sides by the map: record `0x22` comes from object
`0x81` (end of the RGB block) and record `0x37` from object `0x2e` (start of the
button block). So the CPI block is **record `0x23`–`0x36`, flag first**, and
§7.3's table is off by one. Corrected there.

The X/Y flag is computed by **comparing X against Y**, never from the "X/Y
Settings" checkbox — which is consistent with `gui-surface.md` §6 finding that
the checkbox writes nothing. It only ungreys the Y box.

### A vendor bug: CPI stage 4's X/Y flag reads stage 3's controls
The four stages use control stride 4 on both arrays. Stages 1–3 are regular:

    stage 1  X<-0x4e8  Y<-0xe7c   flag <- (0x4e8 != 0xe7c)   0x40ed5d
    stage 2  X<-0x4ec  Y<-0xe80   flag <- (0x4ec != 0xe80)   0x40ed87
    stage 3  X<-0x4f0  Y<-0xe84   flag <- (0x4f0 != 0xe84)   0x40edb1

Stage 4 loads its values from the right place and its flag from the wrong one:

    40edc8  0f b7 86 f4 04 00 00   movzwl 0x4f4(%esi), %eax   ; X  <- 0x4f4  OK
    40edcf  66 89 47 22            movw   %ax, 0x22(%edi)
    40edd3  0f b7 8e 88 0e 00 00   movzwl 0xe88(%esi), %ecx   ; Y  <- 0xe88  OK
    40edda  66 89 4f 24            movw   %cx, 0x24(%edi)
    40edde  8b 96 f0 04 00 00      movl   0x4f0(%esi), %edx   ; <-- 0x4f0, stage 3
    40ede4  3b 96 84 0e 00 00      cmpl   0xe84(%esi), %edx   ; <-- 0xe84, stage 3
    40edea  0f 95 c0               setne  %al
    40eded  88 47 20               movb   %al, 0x20(%edi)     ; stage 4 flag

The displacements would have to be `f4 04` and `88 0e` to match the pattern.
**Record `0x32` therefore tracks CPI stage 3's X≠Y, not stage 4's.**

Two consequences. Ours: §4.2 says mirror the vendor's *verification*; it does
not say mirror the vendor's arithmetic, and `egg-config` must compute stage 4's
flag from stage 4. Theirs: this is testable from a capture.

> ~~and `log.txt` 02 lines 18d–18f now do exactly that.~~ **Struck 2026-09-06,
> twice over.** That file is quarantined (§1.1a), and the owner has separately
> confirmed those three lines were **never performed**. So this prediction is
> UNTESTED, not tested-and-passed, and re-reading the existing captures cannot
> settle it — none of them contains the edit. It needs a new capture, or a run
> against the device with our own tool (config writes are reversible and factory
> reset scores 21/21). It is the only thing lost with the log that an action can
> recover.

## 7.9 The Advanced Sensor page, end to end  [D]

`FUN_00411ab0` (`0x411ab0`–`0x411bbf`) is dialog 140's APPLY collect, called
from `0x413efb` with `%ebx` = the settings object and `%esi` = the page. It is
short enough to read whole, and it reads exactly **six** dialog members:

| member | m_hWnd | IDC | caption | how read | object | record |
| --- | --- | --- | --- | --- | --- | --- |
| `0xb8` | `0xd8` | 1028 | Motion Sync | `BM_GETCHECK`, `setne` | `0x2a` | **`0x0c`** |
| `0x1a4` | `0x1c4` | 1030 | Motion Jitter Filter | `BM_GETCHECK`, bit | `0x07` bit 4 | **`0x06` bit 4** |
| `0x300` | `0x320` | 1032 | Force max Sensor fps | `BM_GETCHECK`, `setne` | `0x2d` | **`0x71`** |
| `0x218` | `0x238` | 1038 | Sensor Angle Tuning | `TBM_GETPOS` (`0x400`) | `0x2c` | **`0x70`** |
| `0x378` | `0x398` | 1082 | CPI Downshift Tuning | `CB_GETCURSEL` | `0x29` bits 3:2 | **`0x0b`** |
| `0x3ec` | `0x40c` | 1084 | Smoothing Tuning | `CB_GETCURSEL` | `0x29` bits 1:0 | **`0x0b`** |

Record numbers via §7.3's serializer table; verify them there rather than
trusting this column.

### Sensor Angle Tuning is a trackbar, read with TBM_GETPOS
`0x411b19` pushes `$0x400` = `TBM_GETPOS`, and `0x411b25` stores the **raw
return byte** into object `0x2c` with no clamping, no offsetting and no
`setne` — `movb %al, 0x2c(%ebx)`. So whatever the control's range is, the record
byte is that number as a byte.

**Two's complement is now `[O]`, from the bytes alone** (re-derived 2026-09-06,
§1.1a). `03-sensor.pcapng` writes 3, 4 and 5 each move record `0x70` and NOTHING
else, to `0x14`, `0xd3` and `0x7f` — 20, **−45**, and 127. `0xd3` is −45 in
two's complement and is not a plausible unsigned angle, so the encoding is
settled without needing to know which UI action produced it.

**The RANGE is `[D]` too, and it is in §7.16, not here.** `0x411f62`–`0x411f8a`
is a signed clamp to −127…+127 followed by a byte truncation into object `0x2c`.

Recording the near-miss, because it is the §7 failure mode in miniature: the
first version of this paragraph said the range was *not* derived, on the grounds
that the vendor "reads `TBM_GETPOS` and never clamps" — written on 2026-09-06,
hours after §7.16 had derived the clamp, and contradicting it. Two lessons, both
already rules. §1.2a: "never clamps" is an ABSENCE claim, and the search behind
it was a scan for `pushl $0x406` that finds only MFC control-binding ids at
`0x4118e7` — a search that cannot see a clamp done with `cmpl`/`jge` at all.
§1.7: trust the written record over recollection when they disagree.

The consequence reached the code. `encodeSensorAngle` accepted −128, which the
vendor clamps to −127, so `egg-config` could emit a byte the vendor's UI cannot
produce — `[G]` under §1.3, on the one mouse there is. Narrowed to −127…+127 on
2026-09-06 and asserted in `Tests/test_citations.py`.

### Object `0x29` packs two combos, and preserves its high nibble
Both tuning combos land in one byte. `CB_GETCURSEL` on the **Smoothing** combo
is remapped by a three-arm ladder at `0x411b36`–`0x411b4d` — index 0→`2`,
1→`0`, 2→`1` — into bits 1:0. `CB_GETCURSEL` on **CPI Downshift** selects one of
four arms through the jump table at `0x411bc0`
(`0x411b70`, `0x411b84`, `0x411b98`, `0x411bac`), which OR in `0x8`, `0xc`,
`0x4`, `0x0` — bits 3:2, in the order 0→`0b10`, 1→`0b11`, 2→`0b01`, 3→`0b00`.

Every arm begins `movb 0x29(%ebx),%dl` / `andb $-0x10,%dl` and ends
`movb %dl,0x29(%ebx)`. **The high nibble is read back and preserved**: bits 4–7
of object `0x29` are never written by this page. That is the vendor doing §1.3's
read-modify-write on a byte it does not fully own, and it is a direct instance
of why `egg-config` must do the same.

Neither remap is the identity, and neither is derivable from the combo order —
which is exactly why `log.txt` 03 lines 8–15 ask for every item by position.

### Sensor Glass Mode is bound but never read  [D]
IDC 1031 is `DDX_Control`-bound at `0x4118db` to member `0x130` (m_hWnd
`0x150`), and **nothing reads it**. Search space, stated per §1.2a: every
instruction with displacement `0x150` off any register in `0x411000`–`0x413000`,
which is the range containing dialog 140's whole class — its `DoDataExchange`
(`0x4118xx`), its collect (`0x411ab0`), and both its click handlers
(`0x411e6a`, `0x41200a`). Zero hits, and the collect reads six members of which
this is not one. What this method **cannot** see is a two-step address
computation (`lea 0x100(%esi),%eax` then `0x50(%eax)`) or an access from outside
that range, so the claim is "not read by dialog 140's class by any single-step
displacement", not "does not exist".

That matches the other half of the evidence: the owner confirmed on 2026-09-05 that
Sensor Glass Mode is **not visible** on the page — same sentence as above,
scored as `cfg-advanced-sensor-hidden-controls` in `prediction-scores.md`
(**re-anchored 2026-09-06 off `./log.txt` 03 line 4**, §1.1a). A
control that is bound, hidden, and never read is the same shape as the X/Y
Settings checkbox (`gui-surface.md` §6) — host-side only, reaching no byte.

**Motion Jitter Filter is different and must not be lumped in with it.** It is
also hidden, but it *is* read, every APPLY, and it clears record `0x06` bit 4
when unchecked. Hidden does not imply inert.

## 7.10 APPLY is not the only thing that writes to the device  [D]

Everything derived from captures so far rests on an assumption nobody wrote
down: that the config tool talks to the mouse **only** when APPLY is pressed.
The capture procedure's core rule — "every numbered line = exactly one APPLY" —
is that assumption in procedural form. It is false.

*(That rule lived in `./log.txt`, quarantined 2026-09-06 under CLAUDE.md §1.1a.
Nothing in this section rests on it: the finding below is `[D]` from cfg107 and
`[O]` from the capture bytes. The rule is named here only as the thing being
refuted.)*

The owner, 2026-09-05: *"when you change a CPI level on the software it isnt a real
change so you cant apply anything, but it changes the CPI level currently on the
mouse."* The path is in the binary.

Each of the four CPI-stage radio buttons on dialog 135 has its own click handler
(`0x4110a0`, `0x4111e0`, `0x411320`, `0x411460`, one per member `0x13f4`,
`0x1468`, `0x14dc`, `0x1550`). Taking the first:

    4110ae  movl  0x13f4(%esi), %eax     ; already checked?
    4110b4  pushl $0xf0                  ;   BM_GETCHECK
    4110be  jne   0x4111cf               ; yes -> return, do nothing
    4110c8  calll 0x417247               ; CWnd::UpdateData(TRUE)
    ...                                  ; BM_SETCHECK this one on, other three off
    411119  movb  $0x0, 0x57f21a         ; object 0x0a = stage index 0
    411120  <CB_GETCURSEL on member 0xf0 ladder> -> object 0x0b = 1..4
    411199  movw  0x4e8(%esi), %ax  -> 0x57f220     ; stage 1 X
    4111a6  movw  0xe7c(%esi), %cx -> 0x57f222      ; stage 1 Y
    4111b4  cmpl / setne           -> 0x57f21e      ; stage 1 X!=Y flag
    4111ca  jmp   0x414010                          ; TAIL CALL, and it sends

`0x417247` is MFC's `CWnd::UpdateData` — it saves and restores `0x13c` in the
module thread state around a virtual call at vtable `+0x100`, which is
`DoDataExchange`. It moves no bytes to the device and is not the write.

`0x414010` is. It carries the **same prologue as the APPLY handler** at
`0x413ea0`: the reentrancy flag at `+0x2fa0` off the window object in
`0x57f754`, the `cmpw $0x0, 0x57f194` guard, and `pushl $0x1978` /
`calll 0x4035f0` — opening the mouse by product id. It then syncs bytes between
the two mirrors and calls **`0x404180`**.

### The command set survives, and that is the point of checking
`0x404180` is one of the four command functions enumerated in §7.1 and confirmed
by the raw-byte caller scan in §4 — the full-record `A0 11` write, the one that
does its own inline `HidD_GetFeature` instead of using the receive wrapper. Its
only calls are `0x4035f0` (open), `0x404180` (the write) and `0x41b600`.

So this is a second **trigger**, not a fifth **command**. Had it been a fifth,
every "the command set is exactly four" statement in these notes would have been
wrong, and §1.2a's warning about "the set is exactly N" would have been earned
the expensive way. It was not — but the claim was only ever checked against
functions reachable from APPLY, and this path is not one of them.

### What it changes
1. ~~**`log.txt` gains section 06**, four radio-button clicks with no APPLY, so
   the bypass gets an isolated capture instead of arriving as noise at the end
   of a long section.~~ **Moot as of 2026-09-06:** the log is quarantined
   (§1.1a) and no section 06 capture was ever taken. The bypass is still real
   and still `[D]`; what is missing is an isolated capture of it, and that needs
   a new capture run.
2. **`fieldmap.py` must not assume a diff implies an APPLY.** It attributes by
   log line, not by APPLY count, so it is already correct — but the reasoning
   was accidental and is now deliberate.
3. **For `egg-config`, nothing changes yet**: we send the same command either
   way. What would change is a future "set the live CPI stage without writing
   the rest of the record" feature — and this path shows the vendor does not do
   that. It rewrites the whole record too.

### Closed the same day: how many doors the write has

The four radio handlers were found by following one observation of the owner's, which
is not a search. The search is: every `call` **and every `jmp`** targeting
`0x414010` and `0x404180` — jumps included, because the handler above reaches
`0x414010` by tail call and a call-only scan would have missed all of them.

    -> 0x414010   EIGHT edges, all jmp, all in 0x410b-0x4115:
                  0x410cf9  0x410e29  0x410f59  0x411089
                  0x4111ca  0x41130a  0x41144a  0x41158a
    -> 0x404180   TWO edges:
                  0x413f2f  the APPLY handler (0x413ea0)
                  0x414356  inside 0x414010

**Eight, not four.** Each radio button has *two* handlers, distinguished only by
their guard: the set at `0x4110a0`+ tests `testl %eax,%eax` / `jne return` and
so runs when the button was **not** already checked; the set at `0x410be0`+
tests `cmpl $0x1,%eax` / `jne return` and runs when it **was**. Both then write.
Same four dialog-135 members (`0x13f4`, `0x1468`, `0x14dc`, `0x1550`) either way.

So the record write has **exactly two doors**, APPLY and the live-CPI path, and
both are now read end to end. No other control in cfg107 reaches the device.
That is a positive accounting over raw call/jump edges rather than a reading
impression, which is the standard §1.2b asks for when the claim is an absence.

## 7.11 The Buttons page, and the byte the multiclick filter shares  [D]

`FUN_00406180` (`0x406180`–`0x4062a3`) is dialog 153's APPLY collect, called
from `0x413f17` with `%ebx` = the settings object and `%esi` = the page. It is
36 instructions and writes **seven** things:

| what | dlg 153 IDC | member | how read | object | **record** |
| --- | --- | --- | --- | --- | --- |
| Slamclick Filter | 1029 | `0x648` | `BM_GETCHECK`, bit | `0x07` bit 0 | `0x06` bit 0 |
| "I understand…" ack | 1064 | `0x6bc` | `BM_GETCHECK`, `setne` | `0x08` | **`0x72`** |
| LEFT multiclick / SPDT-1 | — | `0x560`,`0xd8` | see below | `0x34` | **`0x3d`** |
| RIGHT multiclick / SPDT-2 | — | `0x5d4`,`0x14c` | see below | `0x3c` | **`0x44`** |
| MIDDLE multiclick | — | `0x1c0` | `TBM_GETPOS` | `0x44` | **`0x4b`** |
| FORWARD multiclick | — | `0x234` | `TBM_GETPOS` | `0x4c` | **`0x52`** |
| BACK multiclick | — | `0x2a8` | `TBM_GETPOS` | `0x54` | **`0x59`** |

Five multiclick filters, not eight, and only the first two have an SPDT combo —
which is exactly what the owner reported from the page on 2026-09-05.

### Record `0x72` is the acknowledgement, and it DOES reach the mouse
Object `0x08` ← `setne(BM_GETCHECK(IDC 1064))` at `0x4061bd`/`0x4061c2`,
serialised at `0x4045d6` to **record `0x72`** — the last byte of the record and,
until now, the only one with no attribution at all. A line in the (now
quarantined, §1.1a) `./log.txt` told the owner that if no record byte moved, the
acknowledgement lived host-side in the registry. **That guess was wrong**, and
the correction rests on the two addresses above, not on the log: object `0x08`
is written at `0x4061bd`/`0x4061c2` and serialised at `0x4045d6`. It has a
byte.

### The multiclick filter and the SPDT mode are ONE byte
The two coupled fields do not merely interact in the UI; they are literally the
same byte. For the LEFT button:

    4061cb  pushl $0x147                  ; CB_GETCURSEL on the SPDT combo (member 0x560)
    4061d3  cmpl  $0x1, %eax              ; combo index 1
    4061d8  movl  $0xf1, %eax             ;   -> 0xf1
    4061f1  cmpl  $0x2, %eax              ; combo index 2
    4061f6  movl  $0xf0, %eax             ;   -> 0xf0
    406207  pushl $0x400                  ; otherwise TBM_GETPOS on the slider (member 0xd8)
    406213  movb  %al, 0x34(%ebx)         ; ONE destination for all three

The owner's SPDT items top to bottom are Off / GX Speed / GX Safe, so index 0 = Off,
1 = GX Speed, 2 = GX Safe. Therefore:

    record 0x3d == 0xf1   GX Speed
    record 0x3d == 0xf0   GX Safe
    record 0x3d == 0..25  Off, and the value is the multiclick filter

**This explains the coupling the owner observed** — that picking a GX mode locks the
multiclick filter at 8 and greys it out. The byte cannot hold both, so the UI
disables the slider rather than let it write. The displayed 8 is the default;
the byte actually carries `0xf0`/`0xf1`. And "going back to Off ungreys it but
it still stays 8" is the same fact from the other side: Off restores the
slider's value, which never left 8.

It also identifies a byte §7.3 could see but not explain: "a `0x08` five bytes
after each mask". Mask is entry `+1`, so `+1 + 5` = entry `+6` = the multiclick
filter, default 8 in all eight entries.

### The block boundary, from the wire rather than the code
`01-baseline` records `0x37`–`0x6e`, laid out as eight 7-byte entries:

    LEFT     0x37   00 01 00 00 00 00 08
    RIGHT    0x3e   00 02 00 00 00 00 08
    MIDDLE   0x45   00 04 00 00 00 00 08
    FORWARD  0x4c   00 10 00 00 00 00 08
    BACK     0x53   00 08 00 00 00 00 08
    ?5       0x5a   09 f1 00 00 00 00 08
    ?6       0x61   01 01 00 00 00 00 08
    ?7       0x68   01 ff 00 00 00 00 08

Column `+1` is `01 02 04 10 08 f1 01 ff`, the mask set §7.3 already had. Column
`+6` is `08` throughout, the multiclick default. Entries 5–7 differ from the
first five in `+0` as well as in mask, and have no slider on the Buttons page;
`05-buttonmapping` covers wheel up and wheel down, which is the obvious
candidate for two of the three and is **`[G]` until that capture lands**.

The block ends at `0x6e`, not `0x6f`: eight entries of seven is 56 bytes from
`0x37`. Record `0x6f` comes from object `0x2b`, is `0x00` in the baseline, and
is **unattributed**.

### An independent confirmation of §7.8's CPI phase correction
Record `0x35`/`0x36` read `80 0c` in the baseline, and `0x0c80` is 3200 — CPI
stage 4's Y value, sitting exactly where the flag-first grouping puts it. Under
the old flag-last reading those two bytes would have been stage 4's X. Nothing
was fitted to make that come out: the phase was fixed from the serializer before
these bytes were looked at.

## 7.12 The keyboard-key modifiers are a standard HID modifier byte  [D]

`0x0057f335` was recorded in §7.8 as a byte outside the settings record that
takes bit operations and was **unidentified** — four checkboxes reaching a global
we could not name, which is the kind of loose end that should not survive into a
config write path. It is the KEYBOARD KEY dialog's modifier set.

`0x405180`–`0x405330` reads four checkboxes and, for each, appends a caption to
a display string and ORs one bit:

| member | caption pushed | bit ORed into `0x57f335` | at |
| --- | --- | --- | --- |
| `0x14c` | `'Shift+'` (`0x556a94`) | `0x02` | `4051e7` |
| `0x1c0` | `'CTRL+'` (`0x556aa4`) | `0x01` | `40524e` |
| `0x234` | `'Win+'` (`0x556ab0`) | `0x08` | `4052af` |
| `0x2a8` | `'Alt+'` (`0x556abc`) | `0x04` | `405310` |

Those are dialog 152's four controls — IDC 1075 SHIFT, 1076 CTRL, 1077 WIN,
1078 ALT — and the owner confirmed on 2026-09-05 that those four are the only ticks on
that dialog, with no "no modifier" control.

**The encoding is the USB HID keyboard modifier byte, unchanged:**

    bit 0  0x01  LeftCtrl        <- 'CTRL+'
    bit 1  0x02  LeftShift       <- 'Shift+'
    bit 2  0x04  LeftAlt         <- 'Alt+'
    bit 3  0x08  LeftGUI         <- 'Win+'

This is worth stating carefully because it is a **different kind of evidence**
from the rest of these notes. Everything else here is derived from the vendor's
bytes and corroborated only by the vendor's other bytes. This one matches an
external, published standard exactly, in all four positions, with no
rearrangement — which is far more than chance and is not something Endgame's
code could have told us on its own. It also means the low nibble only: no
right-hand modifiers are offered, so bits 4–7 are never set here.

**Consequence for `05-buttonmapping`.** The button entry's bytes `+2`–`+5`
(§7.7, §7.11) are the fields the mapping page writes and are the last `[G]` in
the record. §7.7's evidence is that `obj[0x30 + 8k]` and `obj[0x32 + 8k]` — entry
`+2` and `+4` — are both zeroed on that path, so a keyboard mapping has to put
its keycode and its modifier byte there. From the baseline, mask `0x10` is
FORWARD, which is entry 3, which is **records `0x4c`–`0x52`**.

~~`log.txt` 05 lines 10–12 map Forward to keyboard A...~~ — the LABELS are void
under §1.1a; the BYTES they were labelling are not. `05-buttonmapping.pcapng`
writes 10, 11 and 12 move entry 3 to `02 00 04`, `02 01 04` and `02 03 04`, and
`0x03` being the bitwise OR of `0x01` and `0x02` is what makes the modifier a
bitfield — a property of the three byte triples, independent of what anyone
clicked. Predicted modifier byte, somewhere in records `0x4e`–`0x51`:

    line 10   no modifier      0x00
    line 11   CTRL             0x01
    line 12   CTRL + SHIFT     0x03

and the keycode for `A` should be `0x04` if the keycode is also HID usage rather
than a Windows virtual-key code (`VK_A` is `0x41`, so the two are easy to tell
apart and the same three lines decide it).

**REFUTED IF** those three lines move no byte in records `0x4c`–`0x52`, or the
modifier values are not `0x00`/`0x01`/`0x03`, or CTRL+SHIFT is not the bitwise
OR of the CTRL and SHIFT cases. A refutation of the last clause would mean the
field is an enumerated combination rather than a bitfield, which would make
every unobserved combination unwritable rather than merely underived.

## 7.13 The button-action menu, recovered from the string table  [D]

The action vocabulary is one contiguous UTF-16 run at file offset `0x155cde`–
`0x155e78` in cfg107, and its order is informative: **submenu items appear
first, then the name of the group they belong to.** Reading it in file order and
applying that rule gives the whole menu:

| group | items, in file order |
| --- | --- |
| **MOUSE** | LEFT CLICK, RIGHT CLICK, MIDDLE CLICK, FORWARD, BACK, SCROLL UP, SCROLL DOWN |
| **KEYBOARD KEY** | leaf — opens dialog 152 (§7.12) |
| **CPI** | CPI LOOP, FIXED CPI |
| **MEDIA** | PLAY/PAUSE, NEXT, PREVIOUS, MUTE, VOLUME UP, VOLUME DOWN, BROWSER, EXPLORER |
| **DISABLE** | leaf |

**Committed prediction: the top-level groups are, in order, `MOUSE`,
`KEYBOARD KEY`, `CPI`, `MEDIA`, `DISABLE` — five of them.**

The capture procedure asked the owner for exactly this list and **deliberately did not
tell him the answer** (§6.2: never state the expected answer in the prompt). He
had also already screenshotted the dropdown and every submenu, so it was
checkable against material that existed, by someone who was not primed.

**SCORED 4/4 EXACT, 2026-09-06**, against
`windows-run/screenshots/button-mapping{,-mouse,-CPI,-media}.png` — see
`prediction-scores.md` §7.13. The screenshots are the evidence; the request
lived in `./log.txt`, which is quarantined (§1.1a) and which the score does not
touch.

**REFUTED IF** the menu shows a group not in this list, omits one that is, or
orders them differently. Note the group *order* is the weakest part of the claim
— the string table's order is the order the items were declared, which is
usually but not necessarily the order they are added to the menu. If only the
order is wrong, that is a partial refutation and the membership claim survives.

It also supersedes an earlier rough-strings guess (in the now-quarantined
`./log.txt`, §1.1a) that named the groups `MOUSE`, `MEDIA`, `CPI` and
`KEYBOARD`. Four of the five are right; the fifth is `KEYBOARD KEY`, not
`KEYBOARD`.

### The LED page names THREE zones, and the record block has FOUR entries
`0x1925e0`–`0x192788` holds the unreachable LED page's controls: `LED effect`,
**`Scroll led`**, **`Logo led`**, **`DPI led`**, `Red:`, `Green:`, `Blue:`,
`Apply led settings`.

This is a fact worth recording precisely because it does **not** resolve §7.3,
and it would be easy to pretend it does. Three named zones against four 5-byte
colour records is a mismatch, not a match. It rules nothing in:

- it does not restore "the tail is a CPI stage index", which the owner refuted by
  direct observation and which stays refuted;
- it does not establish "the tail is a zone id", because that needs four zones
  and the vendor's own UI names three;
- it does confirm that this product family has RGB lighting the OP1 8k v2 does
  not expose, which is consistent with the block being inherited rather than
  used — but "consistent with" is not evidence for.

It also says the vendor's own name for the underside indicator is **`DPI led`**,
which is at least a shared vocabulary with the owner's observation.

Meaning stays `[G]`. §1.3 preserves the bytes. The point of writing this down is
that the next person to look at §7.3 will find the three-zone fact already
weighed rather than arriving at it fresh and reading it as a solution.

## 7.14 The button-mapping encoding, decoded from `05-buttonmapping`  [O]

**The capture existed the whole time.** §7.11 ended "…is `[G]` until that
capture lands", and §7.12 wrote a prediction to be scored "when
`05-buttonmapping` is diffed". `windows-run/05-buttonmapping.pcapng` (30,768
bytes) was uploaded with the rest of the run on 2026-09-05 and had never been
diffed. It holds **12 `A0 11` writes**, each moving 1–6 bytes of one button
entry.

~~and `windows-run/log.txt` lines 1–12 label every one of them~~ — struck
2026-09-06 under §1.1a. **The labels are void; the twelve writes are not.**
What survives, and it is enough: fourteen distinct button-entry states appear
across the baseline read and the twelve writes, and `Tests/test_button_map.py`
requires `egg-config` to reproduce all fourteen byte-for-byte without ever
asking which action produced which write. That is a stronger test than a
labelled list, because it also fails if our table can emit a state the vendor
never could.

Recorded here as a lesson as much as a finding: two sections deferred work to
evidence already sitting in the repo. **Before writing "`[G]` until X lands",
check whether X has landed.**

### The 12 writes, entry 3 (FORWARD, records `0x4c`–`0x52`)

    log line                              +0 +1 +2 +3 +4 +5 +6
     (baseline of this capture)           00 10 00 00 00 00 08
     2  Forward -> MOUSE -> BACK          00 08 00 00 00 00 08
     3  Forward -> MOUSE -> MIDDLE CLICK  00 04 00 00 00 00 08
     4  Forward -> DISABLE                ff 00 00 00 00 00 08
     5  Forward -> MEDIA -> VOLUME UP     20 e9 00 00 00 00 08
     9  Forward -> CPI -> FIXED CPI 1600  0c 00 40 06 40 06 08
    10  Forward -> KEYBOARD KEY, A        02 00 04 00 00 00 08
    11  Forward -> KEYBOARD KEY, CTRL+A   02 01 04 00 00 00 08
    12  Forward -> KEYBOARD KEY, C+S+A    02 03 04 00 00 00 08

Lines 6 and 7 move entry 4 (BACK, `0x53`): `+1` `08`→`04`→`10`. **Line 6 was
not refused** — the log flagged it as a possible refusal and it went through.
Line 8 moves entry 6 (`0x61`): `01 01`→`ff 00`.

### The field layout

`+0` is an **action-type selector**; `+1` is a discriminated second byte whose
meaning depends on `+0`; `+2`–`+5` are the payload; `+6` is the multiclick
filter of §7.11, untouched by every one of these writes.

| `+0` | action | `+1` | `+2`–`+5` | evidence |
| --- | --- | --- | --- | --- |
| `0x00` | MOUSE button | button mask | zero | `[O]` lines 2,3,6,7 |
| `0x02` | KEYBOARD KEY | HID modifier byte | `+2` = HID keycode | `[O]` lines 10–12 |
| `0x0c` | CPI → FIXED CPI | `0x00` | `+2..3` X, `+4..5` Y, u16 LE | `[O]` line 9 |
| `0x20` | MEDIA | HID Consumer usage | zero | `[O]` line 5 |
| `0xff` | DISABLE | `0x00` | zero | `[O]` lines 4, 8 |

Mouse masks, all `[O]` from the baseline and the writes:
`0x01` LEFT, `0x02` RIGHT, `0x04` MIDDLE, `0x08` BACK, `0x10` FORWARD.

### Line 1 is left-handed mode, and it swaps two entries
This capture's baseline differs from `01-baseline` in exactly one way: entries 0
and 1 read `00 02` / `00 01` where `01-baseline` reads `00 01` / `00 02`. Line 1
is "left-handed mode (clicked once)". **Left-handed mode is not a flag byte — it
is implemented by swapping the LEFT and RIGHT entries' masks.** Nothing else in
the record moves. Worth knowing before we expose the setting: there is no bit to
toggle, and a naive "restore defaults" that rewrites masks would silently undo it.

### §7.12's prediction, scored: values HIT, location MISS
§7.12 predicted from `FUN_00405180` that the keyboard modifier is the standard
USB HID modifier byte, with `0x00` / `0x01` / `0x03` on lines 10 / 11 / 12, and
that `A` would be HID usage `0x04` rather than `VK_A` `0x41`.

- modifier `0x00`, `0x01`, `0x03` — **HIT**, all three, and `0x03` is the
  bitwise OR of the CTRL and SHIFT cases, so the field is a bitfield and
  unobserved combinations are derivable rather than merely unobserved.
- keycode `0x04` for `A` — **HIT**. It is HID usage, not a virtual-key code.
- **MISS on location.** §7.12 placed the modifier "somewhere in records
  `0x4e`–`0x51`", i.e. entry `+2`–`+5`. It is at `+1`. The reasoning was that
  `obj[0x30+8k]` and `obj[0x32+8k]` are zeroed on that path, which is true and
  was the wrong inference: `+1` is zeroed on that path too and was not
  considered, because §7.3 had already labelled `+1` "the mask" and that label
  was carried forward as though it were type information. **A field's meaning is
  conditional on `+0`; naming it from one action type hid the other four.**

`0xe9` is HID Consumer Page **Volume Increment**. Like the modifier byte, this
is an external published standard matching on the nose, which is evidence of a
different and stronger kind than the vendor's bytes corroborating themselves.

### What is still `[G]`, stated precisely
1. **`+0 = 0x09`**, entry 5's default (`09 f1`). Unobserved. Entry 5 is the only
   one of the eight never touched by any capture, and no line of any log names
   it. The 8 entries are LEFT, RIGHT, MIDDLE, FORWARD, BACK, **?5**, WHEEL UP
   (line 8 proves entry 6 is Wheel Up), and presumptively WHEEL DOWN.
2. **CPI LOOP's `+0`.** §7.13's menu has two CPI items; only FIXED CPI was
   exercised. A tempting reading is that `0x09` is CPI LOOP and entry 5 is the
   underside CPI button carrying it as a default — coherent, and `[G]`.
3. **`+0 = 0x01`** (entries 6 and 7, `01 01` and `01 ff`). Reads naturally as
   scroll with a signed delta in `+1`, which would make it MOUSE → SCROLL
   UP/DOWN. Never written by any capture: line 8 set Wheel Up to DISABLE rather
   than assigning a scroll to a button.
4. **Seven of the eight MEDIA usages.** Only VOLUME UP was captured. The obvious
   HID Consumer values are guessable, but note that §7.13's menu includes
   BROWSER and EXPLORER, whose HID Consumer usages (`0x223`, `0x194`) **do not
   fit in `+1`**. So either those two use `+2`–`+3`, or the encoding is not a
   raw usage after all. **This is a real hole, not a formality.**
5. **The full keycode set.** One datapoint (`A` = `0x04`) fixes the standard;
   which keys the dialog actually offers is not derived.
6. **`+2`–`+5` for MOUSE, MEDIA and DISABLE.** Zero in every observation. Whether
   the firmware ignores them or the vendor merely never sets them is untested.

### Search for the vendor's menu→byte table: not found, and by what method
`0xe9`, `0xea`, `0xcd` appear **nowhere in cfg107's `.text` as immediates**
(`objdump -d`, `$0x..` operands). The raw file contains no byte run
`cd b5 b6 e2 e9 ea` nor its 16-bit-LE form, in either the §7.13 menu order or
adjacent-pair form, and no `0x20e9`/`0xe920` literal.

Per §1.2a this is **not** a claim that no table exists. The method sees only
immediates and those two specific byte orders; it cannot see a table built at
runtime, one in a different order, one with a stride or a bias applied, or
values reached through a menu-ID arithmetic transform. The staging globals are
identified — `0x57f377` → obj `+0x2e` (entry 0 `+0`), `0x57f378` → `+0x2f`,
`0x57f379`/`0x57f37a` → the `+2`/`+3` word, all read at `0x403e8b`–`0x403eb8`
— but no absolute write to any of them exists, so the UI reaches them through a
computed pointer. **Finding the writer is the next step and is not done.**

## 7.15 The Button Mapping page exposes SIX rows, not eight  [O]

`windows-run/screenshots/button-mapping.png`, read 2026-09-06. The page lists:

    Right Button    RIGHT CLICK
    Middle Button   MIDDLE CLICK
    Forward Button  FORWARD
    Back Button     BACK
    Wheel Up        SCROLL UP
    Wheel Down      SCROLL DOWN

plus a `Left-handed Mode` checkbox. **There is no Left Button row.** The record
has eight entries; the UI exposes six of them. The two it does not expose are
entry 0 (mask `0x01`, LEFT) and entry 5 (`09 f1`).

That resolves most of §7.14's open list:

### `+0 = 0x01` is SCROLL, and `+1` is a signed step  — now `[O]`
Entry 6 holds `01 01` and its row reads **SCROLL UP**. Entry 7 holds `01 ff` and
its row reads **SCROLL DOWN**. `0xff` is `-1`. §7.14 listed this as `[G]`
("reads naturally as scroll with a signed delta"); the vendor's own UI labels
the two bytes, so it is now observed rather than guessed. `+1` is therefore
discriminated three ways — mask under `0x00`, modifier under `0x02`, Consumer
usage under `0x20`, signed step under `0x01`.

### Entry 5 is a button the software cannot reach  — still `[G]`, and now bounded
Six rows for eight entries, with LEFT accounted for. Entry 5 (`09 f1`, action
type `0x09`, never written by any capture, named by no menu row) is a control
the config tool does not expose. `0x09` is the one plausible home for **CPI
LOOP**, the only §7.13 menu item with no observed byte — but nothing observed
connects them, and "the only unexplained value and the only unexplained item
must be each other" is precisely the reasoning §1.2a exists to stop. It stays
`[G]`, and §1.3 keeps its bytes.

### KEYBOARD KEY captures a live keypress; it is not a list  — `[O]`
`button-mapping-keyboard-key-popup.png`: one field labelled `Enter a`, showing
the last key pressed (`Left Shift` in the shot), plus the four ticks of §7.12.
So the offered key set is whatever the keyboard produces, and §7.14's open item
6 ("which keys the dialog actually offers") is answered — all of them. What we
need is the published HID keyboard usage table, and `A` = `0x04` fixes that it
*is* that table.

### MEDIA's two long usages are a confirmed problem, not a hypothetical
`button-mapping-media.png` shows all eight items including **BROWSER** and
**EXPLORER**. §7.14 flagged that their HID Consumer usages (`0x223`, `0x194`)
do not fit the single byte `+1` where VOLUME UP's `0xe9` was observed. The menu
items are real, so the encoding question is real: either those two spill into
`+2`–`+3`, or `+1` is an index into a vendor table rather than a raw usage, and
VOLUME UP matching HID exactly is then a coincidence worth doubting. **No MEDIA
value other than `0xe9` may be written until this is settled.**

## 7.16 Two findings re-derived from cfg107 after `./log.txt` was quarantined  [D]

The owner, 2026-09-06: **"you cannot trust anything in the root log.txt. so much is
wrong."** Taken at face value. `./log.txt` is quarantined as evidence — see
`working-memory.md`. This section re-establishes, from the binary alone, the two
config findings that most visibly rested on it.

### Sensor angle is two's complement, range −127…+127  — `[D]`, not `[O]`
*(§7.16: re-derived from cfg107 after `./log.txt` was quarantined, §1.1a. The
log is named below only to say what it can no longer support.)*
`0x411f62`–`0x411f8a`:

    411f62  cmpl $-0x7f, %eax               ; SIGNED compare against -127
    411f65  jge  0x411f71                   ; jge, not jae
    411f67  movl $0xffffff81, 0x374(%esi)   ; clamp low to -127
    411f71  movl $0x7f, %eax
    411f76  cmpl %eax, 0x374(%esi)
    411f7c  jle  0x411f84                   ; jle, not jbe
    411f7e  movl %eax, 0x374(%esi)          ; clamp high to +127
    411f84  movb 0x374(%esi), %dl           ; truncate to one byte
    411f8a  movb %dl, 0x57f23c              ; -> staging global

A signed clamp to a symmetric ±127 range followed by a byte truncation **is**
two's complement. The signed jumps (`jge`/`jle` rather than `jae`/`jbe`) are the
load-bearing detail; an unsigned field would use the unsigned forms and could not
clamp at a negative bound at all.

This also scores the half-prediction `./log.txt` line 190 described as "currently
unscored" — the range **is** symmetric −127…+127 — and it scores it without
needing the file that raised it.

Previously this rested on a capture datapoint (`0x70` → `0xd3`) whose meaning
came from a log line saying the tester typed `-45`. `0xd3` is `-45` in two's
complement and `211` unsigned, and **nothing in the capture distinguishes them**.
The binary does.

### The "Disable LED on Lift-Off" inversion  — `[D]`, and never depended on the log
*(§7.16, same quarantine, §1.1a.)*
`0x40ecc6`–`0x40ecd7`:

    40ecc6  pushl $0xf0                     ; BM_GETCHECK
    40eccc  calll *%ebx
    40ecd0  testl %eax, %eax
    40ecd2  sete  %cl                       ; 1 when the box is UNCHECKED
    40ecd7  movb  %cl, 0x26(%edi)

`sete` after `testl` yields 1 on zero, so the stored byte is 1 when the checkbox
is **clear**. The record byte is therefore "LED stays lit on lift-off", the
inverse of the vendor's caption — which is why `egg-config` exposes it as
`led-on-liftoff` rather than the caption. `./log.txt` also carried a physical
observation ("lifting the mouse puts the indicator out, CONFIRMED"); that was
corroboration on top of the derivation, and the naming survives without it.

### Why the config work as a whole survives the quarantine
The captures are **bytes**, and bytes do not depend on the log. What the log
supplied was **labels**. Most labels have a second, independent source:

| label source | independent of `./log.txt`? |
| --- | --- |
| record layout, field offsets, encodings | **yes** — `[D]`, cited to cfg107 addresses throughout §7 |
| LOD is eleven steps, index 0…10 | **yes** — `wire-predictions.md` derives it from the cfg107 jump table at `0x40f6c0` and the eleven `CB_ADDSTRING` pushes |
| polling option names | **yes** — strings at `0x557ea8`, `'125Hz'`…`'8000Hz'` |
| the whole button-action menu | **yes** — §7.13, one UTF-16 run, scored 4/4 against screenshots |
| page control inventories | **yes** — `windows-run/screenshots/*.png` |
| which physical action produced which write | **NO** — this is the part the log carried alone |

The last row is the real loss, and it is narrower than it sounds, because the
**order** of writes in each capture matches the order of the plan's numbered
lines in all five config captures. That is mechanical corroboration that the
script was followed, and it does not depend on any annotation in the file.

## 7.17 The complete button-action table, from cfg107  [D]

Closes the two gaps §7.14 and §7.15 left open, and it needed no capture. Every
value below is an immediate in the vendor's own menu handlers, cited by address.

### Where it lives
The Button Mapping menu is **built at runtime**, not a `MENU` resource, which is
why `rsrc.py` shows none. `0x40713e`/`0x40716d` load `CreatePopupMenu` and
`AppendMenuW` into `%ebx`/`%edi`, and `0x407130`–`0x40745x` appends every item
with its command id and caption. Resolving those captions from `.rdata` gives
the whole menu:

| group | items (command id) |
| --- | --- |
| MOUSE | LEFT CLICK `0x1f41`, RIGHT CLICK `0x1f42`, MIDDLE CLICK `0x1f43`, FORWARD `0x1f44`, BACK `0x1f45`, SCROLL UP `0x1f46`, SCROLL DOWN `0x1f47` |
| KEYBOARD KEY | `0x1f68` |
| CPI `0x1f72` | CPI LOOP `0x1f73`, FIXED CPI `0x1f74` |
| MEDIA | PLAY/PAUSE `0x1f55`, NEXT `0x1f56`, PREVIOUS `0x1f57`, MUTE `0x1f58`, VOLUME UP `0x1f59`, VOLUME DOWN `0x1f5a`, BROWSER `0x1f5b`, EXPLORER `0x1f5c` |
| DISABLE | `0x1f7c` |

This is §7.13's prediction a third time, now from the menu construction itself
rather than from string order. It agrees exactly.

### The array the handlers write
Each handler stores through **`0x57f23e(,%index,8)`** — an 8-byte-per-entry
array indexed by the button being edited (`0x380(%esi)`). Since the settings
object's button block starts at object offset `0x2e`, the object base is
`0x57f23e − 0x2e` = **`0x57f210`**, which is exactly the pointer loaded at
`0x403bfd`. The record cache is a separate buffer at **`0x57f340`**, filled by
the `rep movsl` of 0x100 dwords at `0x403c02` straight from the device read.

So the chain is: menu handler → object `0x57f210` → serializer `0x4042d0` →
record → `A0 11`. §7.14's "no absolute write to `0x57f377` exists" was correct
and was looking at the wrong buffer: `0x57f377` is in the record *cache*, which
only the device read writes.

### The table  — every value `[D]`

| action | `+0` | `+1` | at |
| --- | --- | --- | --- |
| MOUSE → LEFT CLICK | `00` | `01` | `0x40774e` |
| MOUSE → RIGHT CLICK | `00` | `02` | `0x4077f4` |
| MOUSE → MIDDLE CLICK | `00` | `04` | `0x40789a` |
| MOUSE → FORWARD | `00` | `10` | `0x407940` |
| MOUSE → BACK | `00` | `08` | `0x4079e6` |
| MOUSE → SCROLL UP | `01` | `01` | `0x407a8c` |
| MOUSE → SCROLL DOWN | `01` | `ff` | `0x407b33` |
| CPI → FIXED CPI | `0c` | `00` | `0x407c6f` |
| **CPI → CPI LOOP** | **`09`** | **`f1`** | `0x407ccf` |
| MEDIA → PLAY/PAUSE | `20` | `cd` | `0x407d76` |
| MEDIA → NEXT | `20` | `b5` | `0x407e1d` |
| MEDIA → PREVIOUS | `20` | `b6` | `0x407ec4` |
| MEDIA → MUTE | `20` | `e2` | `0x407f6b` |
| MEDIA → VOLUME UP | `20` | `e9` | `0x408012` |
| MEDIA → VOLUME DOWN | `20` | `ea` | `0x4080b9` |
| **MEDIA → BROWSER** | **`18`** | **`96`** | `0x408160` |
| **MEDIA → EXPLORER** | **`18`** | **`94`** | `0x40820a` |
| DISABLE | `ff` | `00` | `0x4082b4` |
| KEYBOARD KEY | `02` | HID modifier | `0x408690`–`0x4086cb` |

`+2`/`+4` are written as 16-bit words in every handler. For FIXED CPI they carry
X and Y; for KEYBOARD KEY `+2` is the keycode returned by `0x40a120` and `+4` is
explicitly zeroed at `0x4086c9`; for the rest both are zero.

### Three things this settles

**1. `0x09 0xf1` is CPI LOOP, and entry 5 is the CPI button.** §7.15 recorded
`09 f1` as entry 5's unexplained default and refused to call it CPI LOOP on the
grounds that "the only unexplained value and the only unexplained item must be
each other" is bad reasoning (§1.2a). It was right to refuse and the answer is
the same: the handler for command `0x1f73` writes exactly `09 f1`. The
difference is that this is now `[D]` from an immediate, not a guess from a
coincidence — and it identifies entry 5 as a control whose default action is
CPI LOOP, i.e. the underside CPI button.

**2. BROWSER and EXPLORER use a DIFFERENT action type, and that dissolves the
>8-bit problem.** §7.14 and §7.15 flagged that their HID Consumer usages
(`0x0196`, `0x0194`) cannot fit the single byte `+1` that VOLUME UP occupies,
and warned that `+1` might therefore be a vendor index rather than a raw usage.
Neither. They use type **`0x18`** where the other six use `0x20`, and their `+1`
bytes are the LOW bytes of those usages. `+1` is a raw usage low byte
throughout; the action type carries the rest.

**3. All eight MEDIA values are standard HID Consumer Page usages.** Like
§7.12's modifier byte, this is corroboration from an external published
standard rather than from the vendor's own bytes:

    cd  Play/Pause          b5  Scan Next Track     b6  Scan Previous Track
    e2  Mute                e9  Volume Increment    ea  Volume Decrement
    18/96  AL Internet Browser (0x0196)
    18/94  AL Local Machine Browser (0x0194)

Eight for eight, with the two `0x01xx` usages accounted for by the one action
type that differs. `0xe9` is the single value the capture also observed, and it
agrees.

**Consequence:** every item on the Button Mapping menu now has a `[D]` byte
pair. Nothing in the button-mapping path is `[G]` any more, so §1.3 no longer
blocks exposing the whole menu — subject to the usual read-modify-write and
diff-verify, and noting `+2`/`+4` must be zeroed for the non-payload actions
because the vendor zeroes them.

---

## 7.18 The two writable fields that cited nothing, and what auditing them found  [D]

Written 2026-09-06, from `Tools/ghidra-export/auditclaims.py`, which was built
to enforce §1.2 — "traced to a specific location in an `.exe`. Cite the
address." — against the 486 `[D]` markers in `notes/`. That rule had never been
checked by anything. The audit's first real hit was not in the notes at all: it
was in the shipping code.

**`egg-config` could write two fields that cited no address whatsoever.** `lod`
(record `0x09`) and `cpi-stage` (record `0x0d`). Their ranges came from counting
`A0 11` writes in a capture and from line numbers in the repo-root `log.txt`,
which §1.1a quarantined. Both are now derived from cfg107, and one of them
turned out to be more interesting than the number it was defending.

### 7.18.1 `lod` — record `0x09` ← object `0x27`

    cfg107 0x40ec62  cmpl $0xa, %eax          bound check: eleven items
    cfg107 0x40ec65  ja   0x40ec9e            out of range -> the 0x03 arm
    cfg107 0x40ec67  jmpl *0x40ee54(,%eax,4)  eleven-entry jump table

The eleven arms each do one `movb $imm, 0x27(%edi)`, and the immediates are
exactly `0x00`–`0x0a`:

    0x40ec6e -> 00   0x40ec56 -> 01   0x40ec5c -> 02   0x40ec9e -> 03
    0x40ec74 -> 04   0x40ec7a -> 05   0x40ec80 -> 06   0x40ec86 -> 07
    0x40ec8c -> 08   0x40ec92 -> 09   0x40ec98 -> 0a

They appear in jump-table order, not numeric order, which is why a scan for a
contiguous run of stores finds nothing and why this was missed before. The SET
is what matters, and it is exactly `{0..10}` — the same range `encodeLod`
already enforced, now for a reason rather than by a count.

`[O]` agrees, and adds the default: across every settings frame in `windows-run`
record `0x09` is `00`–`0a` and nothing else, and it is `03` in `01-baseline`,
after `A1 13`, and after the firmware flash.

### 7.18.2 The list length is CONDITIONAL, and nothing had noticed

    cfg107 0x40ec42  cmpb $0x1, 0x57f23b      object 0x2b  ->  record 0x6f
    cfg107 0x40ec4c  jne  0x40ec62            ... to the eleven-item branch

When object `0x2b` is `1`, control never reaches the bound check. The dropdown
collapses to **two** items: index 0 stores `1`, index 1 stores `2`, and the
fallthrough default is `1`. The values `0` and `3`–`10` are then unreachable
through the vendor's own UI.

Record `0x6f` reads `0x00` on this device in every capture — before and after
`A1 13`, and after the flash — so the eleven-value branch is the live one here
and `encodeLod` is correct for this mouse. **It is not unconditionally correct.**
If a future device or firmware reports `0x01` at record `0x6f`, writing anything
but `1` or `2` is a byte the vendor could not have produced, which is `[G]`
under §1.3. `egg-config` does not refuse it today; that is recorded as a live
caveat rather than silently assumed away.

Record `0x6f` was the entry in `recmap`'s hand-derived table marked
"unattributed, but its source is known". It has a name now: it selects which
lift-off-distance list the UI offers.

### 7.18.3 A SECOND encoding writes the same byte, and has never been seen

    cfg107 0x40fa7a  pushl $0x147             CB_GETCURSEL
    cfg107 0x40fa89  cmpl $0xa, %eax          also eleven items
    cfg107 0x40fa8e  jmpl *0x40fb1c(,%eax,4)
    cfg107 0x40fa95..0x40fb0d                 eleven `movb $imm, 0x57f237`

`0x57f237` is `0x57f210 + 0x27` — the same object byte, written absolutely
rather than through `%edi`. The eleven immediates are

    c2  c4  c6  c9  ca  cc  cd  d0  d4  d7  d9

and **not one of them has ever appeared on this device**, in any capture, in any
firmware, before or after a factory reset.

§1.2a governs what may be said about that. The search space here is a
whole-file scan for the 32-bit literal `0x0057f237` plus every `0x27(%reg)`
displacement form, so the two writers are the two that exist *in cfg107*; that
says nothing about cfg100/101/104, and nothing about why this path is not taken.
So: **not "dead code", not "another product".** What is `[D]` is that a second
CB_GETCURSEL handler writes the same byte a different way, and what is `[O]` is
that this mouse has never held one of its values.

The useful consequence is that **the two ranges are disjoint**. `0x00`–`0x0a`
and `0xc2`–`0xd9` cannot be confused, so a settings record showing `0xc2` there
is positive evidence that something other than the eleven-item list wrote it.
`Tests/test_citations.py` asserts the disjointness, so if a future config
version narrows the gap the test says so rather than the property quietly
lapsing.

### 7.18.4 `cpi-stage` — record `0x0d` ← object `0x0a`

    cfg107 0x40edf0-0x40ee4c   four BM_GETCHECK (message 0xf0) calls

    control 0x13f4 -> 0x40edfd  movb $0x0, 0xa(%edi)
    control 0x1468 -> 0x40ee17  movb $0x1, 0xa(%edi)
    control 0x14dc -> 0x40ee31  movb $0x2, 0xa(%edi)
    control 0x1550 -> 0x40ee4c  movb $0x3, 0xa(%edi)

Four mutually exclusive checkboxes storing an index is a radio group, so the
range is `0..3` and the value is the ACTIVE stage. §7.6 had derived the range
1..4 for `cpi-levels` (record `0x0e`) and left this one's *meaning* `[G]`; the
earlier note closed it but sourced the ordering to "the four unlabelled radio
buttons clicked top to bottom", read out of the quarantined log. The ordering is
now the vendor's own control ids, which is both stronger and independent of it.

### 7.18.5 What the audit tool does and does not decide

`auditclaims.py` reports, from raw bytes: a `[D]` claim citing no address; an
address mapped in no vendor binary; an address unmapped in the binary the prose
names but real in another; an address in `.text` that a linear sweep does not
decode; and a claim whose only source is the quarantined log. It does **not**
decide whether the instruction there means what the note says. That is a
reading, and a script cannot check a reading.

Its own blind spot, stated because §1.2a requires it: instruction boundaries
come from `objdump`'s linear disassembly, which desynchronises after data
embedded in code. It desynchronises in this very region — it renders `0x40865a`
(`movb $0x2, %cl`, the KEYBOARD action type) inside a bogus `addb`. So
"not a boundary" is a reason to look, never a proof of error, and
`Tests/test_citations.py` works from raw bytes instead (§1.2b).
