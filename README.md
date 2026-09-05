# egg-mouse

Native macOS software for the **Endgame Gear OP1 8k v2** — a configuration tool
and a firmware flasher. Endgame ships Windows-only tools; the USB protocol here
was derived independently from their `.exe` files by static analysis, and is
being checked against captures of their software talking to the hardware.

**Status: not finished, and not yet safe to point at your mouse.** The flasher
has no `flash` verb. The config tool can read, and can write only bytes the
device itself produced. See *Staged bring-up* below for why.

---

## If your mouse ever stops working

Hold **LEFT and RIGHT mouse buttons together**. Keep holding. Plug the cable in.
Keep holding for a few more seconds, then release.

The mouse re-enumerates as ProductID `0x1977`, Product string `Bootloader`, and
can be re-flashed from there. This is forced by hardware and **does not depend
on any firmware being valid** — it is observed behaviour on the device, not an
inference. It is also the single fact that makes the rest of this project
reasonable to attempt with one mouse and no spare.

---

## Build

Needs CMake ≥ 3.20, a C++20 compiler, and `hidapi`:

```sh
brew install hidapi cmake
cmake -S . -B build
cmake --build build
```

Produces `build/egg-config`, `build/egg-flash`, and `build/test-flash`.

## egg-config

```
egg-config devices                 list every VID 0x3367 interface
egg-config read [--save FILE]      read the settings record, dump it, save it
egg-config info                    small query (A1 02)
egg-config diff A B                compare two saved records, offline
egg-config factory-reset --yes     device-side reset (A1 13)
egg-config restore FILE --yes      write a saved record back, then verify
```

There is deliberately **no `set`**. Naming one field means knowing which byte it
is, which is what the capture work produces. Until then, writing a named field
would mean writing a byte whose meaning is a guess, and that is the one thing
this project will not do.

`restore` is different in kind: it sends back bytes the *device* produced, so
nothing in it is invented, even though most of it is not yet understood.

Every write path reads first, requires the read to be structurally plausible,
writes, reads back, and diffs. A write that is acknowledged but does not verify
is reported as a failure, loudly, and exits non-zero.

## egg-flash

```
egg-flash image  <updater.exe>     validate the firmware image and stop
egg-flash dryrun <updater.exe>     emit the exact byte stream, send nothing
egg-flash help
```

The image is always `FWFILE` resource **140** of the vendor `.exe` you name, and
that is a compile-time constant — there is no way to select a different one. Its
SHA-256 is pinned, and an image that does not match is refused before any byte
goes out.

That guard is not decoration. Updaters 1.04, 1.06 and 1.07 each ship their own
`FWFILE` 140; every one is 66560 bytes, exactly 65 blocks, remainder zero. Every
structural check passes on all of them. **Only the hash separates the right
firmware from the wrong one**, because the device is assumed to validate nothing
it is given.

There is no `flash` verb yet, on purpose.

## Tests

```sh
./build/test-flash          # byte maps, invariants, adversarial device
./Tests/mutants.sh          # does test-flash actually catch anything?
python3 Tests/test_fieldmap.py
```

`test-flash` drives the flasher against a mock bootloader that rejects commands,
returns malformed responses, **reports success for writes it did not store**,
goes silent mid-write, disconnects, stalls past every timeout, corrupts what it
stores, and lies about checksums. None of it needs hardware.

`mutants.sh` exists because `test-flash` passed 47/47 on its first run, and a
suite that has never failed has not been shown to work. It plants ten known bugs
— several of them the vendor's own — and requires the suite to notice. It
currently kills 8 of 8 real defects and correctly lets 2 provably-equivalent
mutants survive. **If that number drops, a test stopped working.**

## Staged bring-up

The order is fixed, and each stage exists so that the next one has less
untested code in it:

1. One read-only command round trip. ✅ built, **not yet run against the device**
2. Bootloader entry and exit only. No erase, no write.
3. Flash read-back, compared against a reference image. Zero writes.
4. A real flash.

By stage 4 the only untested code is erase and write.

## Layout

```
Sources/EGGCore/        HID transport, enumeration, device identity, protocol
                        constants as DATA with a citation on every value
Sources/EGGFlashCore/   image handling, command builders, the write phase,
                        the adversarial mock
Sources/egg-config/     the config CLI
Sources/egg-flash/      the flasher CLI
Tools/capture/          pcapng reader, exchange extractor, record differ
Tools/ghidra-export/    static-analysis tooling over the vendor binaries
Tools/device/           read-only device observation (opens nothing)
notes/                  the derivation, with provenance tags on every claim
```

## How claims are tagged

Every protocol claim in `notes/` carries one of:

- **[O] Observed** — read off the physical device.
- **[D] Derived** — traced to a specific address in a vendor `.exe`, cited.
- **[G] Guess** — convention or inference.

**A [G] never reaches the hardware.** Read-modify-write preserves bytes we do
not understand rather than normalising them.

The vendor binaries are not in this repository and never will be. `notes/` cites
addresses; reproducing any of it needs your own copy.

## Licence

Not yet chosen. The vendor binaries are Endgame Gear's and are not redistributed
here. Protocol facts about hardware are not copyrightable, and this is an
independent implementation from documented facts.


## Changing one setting

```
egg-config set                      # list the settable fields
egg-config set polling 1000         # dry run: says what it would write
egg-config set polling 1000 --yes   # read, change one byte, write, verify
```

**The list is short on purpose.** `CLAUDE.md` §1.3 forbids writing a byte whose
meaning is a guess, so a field is settable only when its meaning was derived
from the vendor's own binary and can be cited to an address — the tool prints
that citation next to every field. The 115-byte settings record is fully
*mapped* (`notes/config-protocol.md` §7.3) and mostly not *named*, and mapped is
not good enough to write.

Everything else is still reachable, through `egg-config restore`, which sends
back bytes the device itself produced — so no byte in it is a guess even where
we cannot say what it means.

Each `set` reads first and refuses to write after a failed or implausible read,
checks that its outgoing frame differs from what it read in exactly one byte at
the cited offset, then reads back and verifies. If more bytes changed on the
device than the one it sent, it says so.
