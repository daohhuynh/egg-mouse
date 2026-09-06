# The firmware flash, on the wire [O]

Every byte here was read out of `windows-run/08-flash.pcapng` and
`windows-run/09-flash-again.pcapng` — Endgame's own updater 1.10 flashing the owner's
own mouse, captured with USBPcap on 2026-09-05. Two complete flashes, 1.07→1.10
and then 1.10→1.10 again.

These are `[O]` under §1.2: observed, not derived and not guessed. Where this
file and `updater-protocol.md` (which is `[D]`, from the `.exe`) disagree, §7
says the device wins, and this file is the device.

Reproduce any claim below with `Tools/capture/usbpcap.py` — it decodes, and
attaches no meaning.

## 1. Transport

Two HID **feature** reports, both on the vendor interface:

| report id | size incl. id | used for |
| --- | --- | --- |
| `0xa1` | 64 | short commands and most responses |
| `0xa0` | 1041 | anything carrying a 1024-byte block |

`SET_REPORT` is `bmRequestType 0x21, bRequest 0x09`; `GET_REPORT` is `0xA1,
0x01`; `wValue = 0x0300 | reportId`, `wIndex = 1`.

**The device omits the report-id byte from responses**, and returns
`wLength - 1` bytes: ask for 1041 on report `0xA0` and 1040 come back, first
byte `0x51`; ask for 64 on `0xA1` and 63 come back, first byte `0x50`. So an
outbound buffer begins with the report id and an inbound one begins with a
status byte.

That is the only inter-direction asymmetry, and it is at the *start* of the
buffer. **Measured on the bytes as transferred, every offset below is the same
in both directions.**

Every command is answered. The first payload byte of a response is a status
byte — `0x50` from the bootloader, `0x51` on a block read-back, `0xa1` from the
application. The second payload byte is `0x01` in every response in both
captures, 270 of 270.

## 2. The whole sequence

Timings are from 08; 09 matches to within jitter.

```
    a1 3a 00 00 00 5a a5 32          enter bootloader, sent to the APPLICATION
                                     device. 5a a5 32 is a fixed magic.
    -> a1 01 ...
    ~2.6 s, and the device re-enumerates (dev4 -> dev5)

    a0 03 <1035 zero bytes>          ERASE. blocks until done.
    -> 50 01 ...                     3.0 s later. the longest wait anywhere.

    for each of 65 blocks, index 0x34 .. 0x74 inclusive:
        a0 06 <idx16> <csum16> <10 zero> <1024 data> <1 pad>    write
        -> 50 01 <idx32> <csum16> ...                           echo
        a0 07 <idx16> <1037 zero>                               read back
        -> 51 01 <idx32> <csum16> <8 zero> <1024 data>          the block
    ~0.256 s per block, ~62 ms per transfer

    a1 08 34 74 00                   finish: first block, last block
    -> 50 01 ...
    a1 09                            leave the bootloader
    -> 50 01 ...
    ~2.4 s, and the device re-enumerates again (dev5 -> dev6)

    a1 13                            sent to the APPLICATION device
    -> 00 01 ...                     0.9 s later
```

`a1 13` is the command §1.2a of CLAUDE.md remembers as the one that had no name
in the strings and was nearly written off as absent. Here it is, on the wire,
last thing the updater does.

### 2.1 CORRECTION — "~2.6 s" and "~2.4 s" are the vendor's sleeps, not the device's

Measured 2026-09-05 from the **enumeration** traffic rather than the feature
traffic. The numbers above are the gap to the vendor's *next command*, which is
dominated by two hardcoded `Sleep(1000)` calls. The device is ready long before.

Taking the first `GET_DESCRIPTOR device` on the newly-addressed device as the
moment it is back on the bus:

| transition | capture | command ack | device back | **gap** |
| --- | --- | --- | --- | --- |
| app → bootloader (`a1 3a`) | 08 | 0.0144 s | 0.5033 s (dev5) | **489 ms** |
| app → bootloader (`a1 3a`) | 09 | 0.0057 s | 0.4540 s (dev7) | **448 ms** |
| bootloader → app (`a1 09`) | 08 | 23.2013 s | 24.0971 s (dev6) | **896 ms** |
| bootloader → app (`a1 09`) | 09 | 23.2954 s | 24.1520 s (dev8) | **857 ms** |

**Entry re-enumerates in ~450–500 ms; exit in ~860–900 ms.** Use these to size a
*timeout*; do not copy 2.6 s and call it the device's latency. In `09` the old
application device's last packet is at 0.2167 s, so the detach happens ~210 ms
after the ack and the bus is quiet for only ~240 ms.

### 2.2 Response byte 0 is mode-dependent, and in application mode it is NOT stable [O]

Histogram over every inbound feature transfer in both flash captures:

| mode | length | byte 0 |
| --- | --- | --- |
| bootloader (dev 5 / 7) | 63 | `0x50` — **68 of 68** |
| bootloader (dev 5 / 7) | 1040 | `0x51` — **65 of 65** |
| application (dev 4 / 6 / 8) | 63 | `0xa1` once, `0x00` three times |

Two things follow.

1. **Endgame's own captures show byte 0 varying for the same command.** The
   `a1 3a` reply is `a1 01 …` in `08` and `00 01 …` in `09` — same tool, same
   device, same command, different byte 0. This is independent Windows-side
   confirmation of what was observed on macOS (`wire-observed.md` §2.3), and it
   means the two checks that once demanded `buf[0] == 0xA0` would have failed
   intermittently on **every** platform, not just ours.
2. **`0x50` in byte 0 of a 63-byte reply is a positive bootloader signature**,
   68 for 68, with no counter-example in either capture. It is not proof — two
   clean runs of one firmware — but it is a *second* identity check that costs
   nothing beyond a reply we already have to read.

### 2.3 The entry command was sent exactly once in each capture [O]

The vendor's builder has a nine-attempt retry loop (`updater-protocol.md` §5.3).
Neither capture exercised it: one `a1 3a` out, one reply back, both times. So
the retry path is **[D] and untested on the wire** — our own retry logic cannot
be validated against these captures and must be exercised against the mock.

## 3. Block format

Both are given in **transferred-buffer offsets** — the bytes as they appear in
the capture, report id included where the wire carries one.

Write buffer, 1041 bytes:

| offset | size | meaning |
| --- | --- | --- |
| 0 | 1 | `0xA0` report id |
| 1 | 1 | `0x06` |
| 2 | 2 | block index, LE |
| 4 | 2 | checksum, LE |
| 6 | 10 | zero |
| **16** | 1024 | data |
| 1040 | 1 | zero pad |

Read-back response, 1040 bytes:

| offset | size | meaning |
| --- | --- | --- |
| 0 | 1 | `0x51` status |
| 1 | 1 | `0x01` |
| 2 | 4 | block index, LE |
| 6 | 2 | checksum, LE |
| 8 | 8 | zero |
| **16** | 1024 | data |

**Corrected 2026-09-05.** An earlier version of this section claimed a one-byte
stagger — data at +15 outbound and +16 inbound. That was wrong, and it was wrong
in a specific and instructive way: it measured the two directions from different
origins, excluding the write's report-id byte while including the read's leading
status byte. An independent check refuted it from raw file bytes, using the
vendor's own checksum as the discriminator: `sum(buf[16:1040]) & 0xFFFF` matches
the declared checksum on 65 of 65 blocks in both directions in both captures,
and at offset 15 it matches 0 of 65.

The two headers are also not one layout shifted by a byte — the write has a
1-byte opcode and a 2-byte index, the response a 2-byte status pair and a 4-byte
index. They are different headers that happen to be the same length.

The lesson generalises past this one field: **state which origin you are
counting from, every time.** The same confusion produced two separate false
alarms while reading this capture, in opposite directions.

### The checksum

**Sum of the 1024 data bytes, truncated to 16 bits.** Nothing more — no seed, no
carry fold, no CRC. (An independent check specifically tested CRC-16/CCITT
against these blocks and refuted it.)

Checked on all 130 block writes across both flashes: 130 of 130 agree, and the
device echoed both the index and the checksum back correctly every time. The 10
zero bytes at +5 and the pad at +1039 were zero in all 130.

### The read-back is real

For all 130 blocks in both runs, the 1024 bytes returned by `07` equal the 1024
bytes sent by `06`. The vendor verifies **every** block immediately after
writing it, in the same round trip pair. §4.2 says to mirror whatever
verification the vendor provides — this is it, and it is free.

## 4. The image

65 blocks × 1024 = **66,560 bytes**, block indices `0x34`–`0x74`.

The index looks like a 1 KiB unit of flash address (`0x34 << 10 = 0xD000`), but
that is `[G]` — nothing observed says the index is an address rather than an
ordinal, and nothing needs it to be.

Blocks `0x51`–`0x73` all carry checksum `0x0088`, which reads like padding but
is not: the data is high-entropy and differs block to block. It is 35 blocks
that happen to sum alike, and it is a good adversarial test case for our
verifier, since a checksum-only check treats them as interchangeable.

**Both flashes sent identical bytes.** 269 of 270 payloads are equal
byte-for-byte, and the exception is a response, not a request: the answer to
`a1 3a` was `a1 01 …` when the mouse was running 1.07 and `00 01 …` when it was
already running 1.10. Everything we would send is deterministic, so the golden
file in §4.3 is viable, and it can be the whole stream rather than a sample.

That also answers the owner's question about flashing twice. The second run really did
erase and rewrite all 65 blocks — it did not detect a matching version and skip.
There is no second copy of the firmware on the device; the same region was
written twice with the same bytes.

## 5. Where the image comes from, and the wrong-image guard

The 66,560 bytes flashed are **byte-identical to `FWFILE` resource id 140** in
`Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe`, at file offset `0x193950`.
Not similar, not a prefix — equal, and equal at offset 0.

```
sha256(FWFILE 140, updater 1.10) = 8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0
```

That is the constant §1.4 asks for, and it is now `[O]`.

**The hazard §2 warned about is real and it is sitting right next to the right
answer.** The updater carries six `FWFILE` resources, every one of them exactly
66,560 bytes:

| id | 1.04 | 1.06 | 1.07 | 1.10 |
| --- | --- | --- | --- | --- |
| 133 | `09ba5297` | `09ba5297` | `09ba5297` | `09ba5297` |
| 135 | `4fb2c174` | `4fb2c174` | `4fb2c174` | `4fb2c174` |
| 137 | `9cf31ca8` | `9cf31ca8` | `9cf31ca8` | `9cf31ca8` |
| **140** | `41d5397e` | `9f01d732` | `3922b141` | **`8148ebe9`** |
| 142 | `b09b0ac0` | `defb2243` | `05c051d9` | `42660cc0` |
| 143 | — | — | — | `13c13a62` |

133/135/137 never change across four releases. 140 and 142 change every time.
143 is new in 1.10.

Any of the five wrong ones is the same length, would produce valid per-block
checksums, and would read back exactly as written. **Every check in the vendor's
own protocol passes on the wrong image.** The only thing that separates them is
knowing which id, and the only reason we know is that we watched.

So the guard is: hardcode the sha256 above, compare before the first byte goes
out, and refuse. Not a warning, a refusal.

### Which id, and why the other five are not a risk [D]

**Cross-reference: `notes/updater-protocol.md` §8 derived this first, from the
same `.text` reference.** What follows is a second, independent pass done from
raw bytes on 2026-09-05 -- it agrees, and it adds the typing of all 13
resource-lookup sites and the capture test. This section previously asked the
question §8 had already answered; if you change one, change both.

Resolved 2026-09-05. **The selection is a compile-time constant in the vendor's
binary too.** One `push imm32` feeds `FindResourceW`, and nothing reaches it:

```
00403207  68 c0 47 54 00    push 0x5447c0     ; lpType -> L"FWFILE"
0040320c  68 8c 00 00 00    push 0x8c         ; lpName  = MAKEINTRESOURCE(140)
00403211  6a 00             push 0            ; hModule = NULL
00403213  ff 15 30 b2 51 00 call [0x51b230]   ; KERNEL32!FindResourceW
```

No table, no argument, no computation, nothing read from the device. 1.04 is a
separate code base and hardcodes the same 140, at file offset `0x373a`
(`68 d8 1c 5a 00 / 68 8c 00 00 00 / 6a 00 / 8b f1 / ff 15 9c e2 56 00`).

**The other five are unreachable, and this is the absence claim §1.2a is about,
so here is the search space.** Four mechanical scans over the whole 2,133,504-byte
file, not over Ghidra's view of it (§1.2b):

1. `L"FWFILE"` occurs **twice** in the file: `0x5447c0` in `.rdata` and
   `0x56cd3e`, which is the type-name string inside `.rsrc` itself. Scanning
   every 4-byte little-endian occurrence of each VA anywhere in the file:
   `0x5447c0` is referenced **once**, at `0x2608`, the byte after the `68` above.
   `0x56cd3e` is referenced **zero** times.
2. **All 13 resource-lookup call sites were typed**, by reading the `lpType`
   push at each: twelve `FindResourceW` and one `FindResourceExW`, found by
   scanning for `ff 15 <IAT slot>` against slots recovered from the import
   directory. Six push `5` (RT_DIALOG), two push `6` (RT_STRING), one each
   `0xf0`, `0xf1` and `0xfc11` (MFC private types), and one pushes a global at
   `0x56751c` — an MFC `CString` constructed at `0x118e1e` from `L"PNG"`. Exactly
   one pushes `0x5447c0`. **An integer type can never equal a string pointer**,
   so no other site can name FWFILE whatever its `lpName` turns out to be.
3. `EnumResourceNamesW/A`, `EnumResourceTypesW` and `EnumResourceLanguagesW` are
   **not imported** — checked by name against the whole file, both encodings. So
   the resource directory is never walked; ids can only be named literally.
4. Independent `[O]` corroboration, which needs none of the above. Split all six
   images into 65 blocks each and ask which blocks appear verbatim among the
   1024-byte payloads the host actually sent in `08-flash.pcapng`:

   | id | blocks found in the capture |
   | --- | --- |
   | 133, 135, 137, 142, 143 | 0 / 65 each |
   | **140** | **65 / 65** |

So 140 is what this updater sends to **any** device it talks to, not just to
The owner's. `§1.4`'s compile-time constant is right, and it matches the vendor's.

What remains `[G]` is *why* five unreachable images ship — nothing here says the
OP1 8k v2 is the only product 1.10 was built for, only that 1.10 has exactly one
reachable image. That question no longer gates anything.

### The guard must pin resources, not code

**Updater 1.10 has `.text` byte-identical to 1.06 and 1.07 (same SHA-256), and
yet 1.10 added a sixth FWFILE that the other two do not have.** A guard keyed on
code identity would have accepted 1.10 without noticing a new firmware blob had
appeared in it. Pin the resource set and its hashes; never `.text`.

The trap in the table above is 142's lockstep with 140 — it changes on exactly
the same releases, which is the pattern that invites "the updater must use
both". It does not. Only 140 is ever named.

`Tools/pe/fwfile.py` lists and extracts these; `Tests/test_fwfile_set.py` pins
the whole 1.10 set by hash so a swapped file is loud rather than silent.

## 6. What is still not known

- **Erase granularity.** `a0 03` carries 1035 zero bytes and no address. It may
  erase the whole application region or only what the following writes cover.
  Nothing in the capture distinguishes them.
- **Failure behaviour.** Both captures are clean runs. No response to a bad
  checksum, a bad index, or a write outside `0x34`–`0x74` was ever seen. The
  mock in §4.3 has to invent these, and must not pretend they are observed.
- **Whether the bootloader validates anything.** Still `[G]`, still assumed no
  (§2). Two flashes of an image the device already had tell us nothing here.
- ~~`a1 09` versus `a1 13` — both plausibly "reset"~~ **`a1 09` is the exit,
  and the binary says so explicitly.** After `a1 09` is acknowledged with
  `resp[1] == 1`, fw110 polls for **PID 0x1978** — the application — at
  `0x00403c7c` and again in a loop at `0x00403c92`, sleeping 800 ms, 1600 ms, …
  up to 16000 ms, and fails to `0x403e6f` if it never appears. The capture
  agrees: the device reappears as a new address ~880 ms later and `a1 13` is
  then sent to *that* device. So `a1 09` = leave bootloader, run the
  application. `[D]` for the intent, `[O]` for the re-enumeration.
  What `a1 13` does is separately settled — it is Factory Reset — so the pair is
  no longer ambiguous. **What remains `[G]` is what `a1 09` does to a bootloader
  that was never flashed**, which is the one thing the captures cannot show
  because the vendor only ever sends it after a completed write.
