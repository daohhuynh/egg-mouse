# Config tool — protocol notes

Derived independently from the vendor `.exe` files, statically. Nothing here has
touched hardware: there is **no `[O]` in this file**. Provenance tags per
`CLAUDE.md` §1.2.

Scope so far: **configuration tool v1.07 only** (`cfg107`). The other three
(1.04, 1.01, 1.00) are not yet read; nothing here should be assumed to hold for
them until it is checked.

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

## 2. Device-facing surface is nine references [D]

Exhaustive 4-byte-literal scan of cfg107's `.text` (VA `0x401000`–`0x52c600`,
1,226,240 bytes) for each HID import's IAT slot:

| import | IAT slot | referencing sites |
| --- | --- | --- |
| `HidD_SetFeature` | `0x0052d1dc` | `0x403887`, `0x4038de` — both inside one wrapper |
| `HidD_GetFeature` | `0x0052d1d8` | `0x403957`, `0x4039a4`, `0x403a42`, `0x403a56`, `0x40428e` |
| `HidP_GetCaps` | `0x0052d1d0` | `0x403773` |
| `HidD_GetAttributes` | `0x0052d1e0` | `0x0052d1e0` → `0x403724` |

All nine lie in `[0x403724, 0x40428e]`. Device handle global is `0x0057f338`.

**Caveat, stated rather than glossed:** this bounds where HID *calls* are, which
is what matters for the wire format. It does not yet bound where the settings
data is built — that is upstream of these sites and is not yet read.

## 3. The send wrapper — `0x00403850` [D]

`__fastcall`-ish: **`EBX` = buffer, `ESI` = length**, read off the push order at
`0x403882`–`0x403885` (`push %esi; push %ebx; push %ecx` → `HidD_SetFeature(
handle, buffer, length)`).

Guards on the handle global `0x0057f338` being non-null. On failure calls
`GetLastError` (`0x403895`) and retries only for
`0x15, 0x17, 0x1D, 0x57, 0x65B`; second attempt then `Sleep(0x32)` = 50 ms
(`0x4038ed`). Exactly **4 direct callers**, found by exhaustive `E8`-rel32 scan:
`0x403b91`, `0x404243`, `0x404676`, `0x4047a6`.

## 4. The four commands cfg107 sends [D]

Each read off the `movl` immediate that initialises byte 0 and byte 1 together.
Little-endian, so `$0x12a1` writes `a1 12 00 00`.

| site | immediate | report | cmd | length | follow-up |
| --- | --- | --- | --- | --- | --- |
| `0x403b8a` | `$0x12a1` | `0xA1` | `0x12` | `0x40` | `Sleep(0x50)` = 80 ms |
| `0x404218` | `$0x11a0` | `0xA0` | `0x11` | `0x411` | `Sleep(arg)`, then a 64-byte `0xA1` read |
| `0x40466c` | `$0x2a1` | `0xA1` | `0x02` | `0x40` | `Sleep(0x32)` = 50 ms |
| `0x40479f` | `$0x13a1` | `0xA1` | `0x13` | `0x40` | failure path pushes `0x44c` |

**`0xA0 0x11` carries 1024 bytes** copied to buffer `+0x10` by `rep movsl` of
`0x100` dwords (`0x404222`–`0x404233`) — the same payload geometry as the
updater's `0xA0 0x06` block write.

**What any of these four commands mean is `[G]` and stays `[G]`.** The names are
not in the binary and nothing here has been on a wire. Per §1.3 none of them may
inform a write. The `0xA0 0x11` payload size and offset are `[D]`; calling it
"the settings blob" would be a guess, and the obvious guess is exactly the kind
this project is trying not to make.

## 5. `A1 13` is sent by both tools — a lead, not a conclusion

`notes/updater-protocol.md` §3 records `0xA1 0x13` as the updater's post-success
command and tags its meaning `[G]` because the updater does nothing with the
reply. cfg107 sends the same `0xA1 0x13`, 64 bytes, at `0x40479f`.

That is a genuine lead: two independent call sites in two different tools, and
cfg107's surrounding code may constrain the meaning in a way the updater's
cannot. **It is not yet a finding.** Same report ID and command byte in a shared
transport is strong, but a device is free to overload a command by context, and
`[G]` + `[G]` is not `[D]`. Reading `0x00404770`'s caller and what it does with
the result is the next step.

## 6. Not yet derived
- Whether cfg107 ever looks for the bootloader PID `0x1977`. Literal scan finds
  `0x1978` at 7 sites as a 32-bit immediate but **zero** 32-bit `0x1977` — which
  is suggestive of a config tool with no bootloader awareness, but a 2-byte
  immediate or a computed value would not show up that way, so this is **not**
  established. It is a scan result, not a proof.
- The four `HidD_GetFeature` sites at `0x403957`–`0x403a56`: their wrappers,
  lengths, and polling behaviour.
- Everything upstream: how the settings structure is built, validated, and
  displayed. This is where factory reset (§4.1) will be, and §4.1 requires it
  working before any other config write.
- Config tools 1.04, 1.01, 1.00.
