# Config tool — protocol notes

Derived independently from the vendor `.exe` files, statically. Nothing here has
touched hardware: there is **no `[O]` in this file**. Provenance tags per
`CLAUDE.md` §1.2.

Scope: **all four configuration tools** — cfg107, cfg104, cfg101, cfg100. Claims
state which binaries they were checked against. cfg100 is a **separate code
base** (11,071 functions vs ~9,500) and is the cross-check on the other three,
the way fw104 is for the updaters.

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

## 2. Device-facing surface — 13 functions, not 9 references [D]

> **CORRECTED 2026-09-04.** This section previously said "nine references" and
> listed only IAT-slot users. That was an undercount with a specific cause: the
> config tools resolve eleven HID entry points through `GetProcAddress` and call
> them via `.data` slots, which no IAT scan can see. Full account of the error
> and the corrected method: `notes/updater-protocol.md` §9.3.

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

**The corrected seed set is 13 functions** for every config version — the union
of static-IAT users and dynamic-slot users:

```
cfg107  0x4027d0 0x402980 0x402f60 0x403320 0x403420 0x403460 0x4034a0
        0x4034e0 0x403550 0x4035f0 0x403850 0x403920 0x404180
cfg100  0x4038a0 0x403a30 0x403fe0 0x404590 0x404770 0x404800 0x404890
        0x404920 0x4049c0 0x404a50 0x404ce0 0x404db0 0x405660
```

Device handle global: cfg107 `0x0057f338`.

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

This corroborates §4's set as complete rather than merely as what was found.

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
