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

**The direction is asymmetric and getting it wrong loses a byte of alignment.**
Outbound, byte 0 of the buffer is the report id and the payload starts at byte
1. Inbound, the report id is not in the returned buffer and the payload starts
at byte 0. So an outbound 1041-byte transfer and an inbound 1040-byte transfer
carry the same 1040-byte payload.

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

## 3. Block format

Write payload, 1040 bytes:

| offset | size | meaning |
| --- | --- | --- |
| 0 | 1 | `0x06` |
| 1 | 2 | block index, LE |
| 3 | 2 | checksum, LE |
| 5 | 10 | zero |
| 15 | 1024 | data |
| 1039 | 1 | zero pad |

Read-back response payload, 1040 bytes:

| offset | size | meaning |
| --- | --- | --- |
| 0 | 1 | `0x51` |
| 1 | 1 | `0x01` |
| 2 | 4 | block index, LE |
| 6 | 2 | checksum, LE |
| 8 | 8 | zero |
| 16 | 1024 | data |

Note the one-byte stagger: data sits at +15 going out and +16 coming back,
because the response carries an extra status byte. Assuming a symmetric layout
shifts every byte by one and the checksum still passes on the block you built
from the wrong offset.

### The checksum

**Sum of the 1024 data bytes, truncated to 16 bits.** Nothing more — no seed, no
carry fold, no CRC.

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

### Open, and it matters [G]

Why does the updater ship two-then-three per-release images? If 140 versus 142
is chosen at runtime from something about the device — a hardware revision, a
sensor variant — then 140 is the right image for *the owner's* mouse and we have no
evidence it is right for anyone else's. Our constant is correct either way for
the one device that exists, which is what §2 scopes to. Finding the selection
site in the `.exe` is the next question, and it is `[D]` work that needs no
hardware.

## 6. What is still not known

- **Erase granularity.** `a0 03` carries 1035 zero bytes and no address. It may
  erase the whole application region or only what the following writes cover.
  Nothing in the capture distinguishes them.
- **Failure behaviour.** Both captures are clean runs. No response to a bad
  checksum, a bad index, or a write outside `0x34`–`0x74` was ever seen. The
  mock in §4.3 has to invent these, and must not pretend they are observed.
- **Whether the bootloader validates anything.** Still `[G]`, still assumed no
  (§2). Two flashes of an image the device already had tell us nothing here.
- `a1 09` versus `a1 13` — both plausibly "reset", sent 2.4 s apart to two
  different enumerations. What each does separately is untested.
