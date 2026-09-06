# egg-mouse

Native macOS software for the **Endgame Gear OP1 8k v2** — a configuration tool
and a firmware flasher. Endgame ships Windows-only tools; the USB protocol here
was derived independently from their `.exe` files by static analysis, and is
being checked against captures of their software talking to the hardware.

**Status: not finished, and not yet safe to point at your mouse.** The flasher
has no `flash` verb and has never touched hardware.

The config tool has. On 2026-09-05 it read the settings record off a real OP1
8k v2, factory-reset it, and wrote a saved record back — each against a
prediction committed to git beforehand, each scoring 21 of 21 bytes. It also
established, on the device, that **a write survives a power cycle**: unplugged
8.6 s, replugged, all 1024 payload bytes unchanged. One device, one of each.
See *Staged bring-up* below for what is still untested.

---

## If your mouse ever stops working

Hold **LEFT and RIGHT mouse buttons together**. Keep holding. Plug the cable in.
Keep holding for a few more seconds, then release.

The mouse re-enumerates as ProductID `0x1977`, Product string `Bootloader`, and
can be re-flashed from there. This is forced by hardware and **does not depend
on any firmware being valid** — it is observed behaviour on the device, not an
inference. It is also the single fact that makes the rest of this project
reasonable to attempt with one mouse and no spare.

A power cycle also leaves bootloader mode on its own: the device sat in the
bootloader for four hours and returned to normal on one replug, no buttons held.

**And if it is stuck as `0x1977`, Endgame's own Windows updater will fix it.**
That is not a hopeful reading of their tool — it is a dedicated path in it. The
updater's device search returns a *mode code*, and on finding ProductID `0x1977`
it starts a different worker that goes straight to the flash sequence and skips
the mode-switch entirely. A mouse already in the bootloader is a supported
starting state for their software, not an error. (Derived from the binary, not
tested — running the vendor's tools is out of scope here — see
`notes/updater-protocol.md` §5.1a.)

---

## Build

Needs CMake ≥ 3.20, a C++20 compiler, and `hidapi`:

```sh
brew install hidapi cmake
cmake -S . -B build
cmake --build build
```

Produces `build/egg-config`, `build/egg-flash`, `build/test-flash` and
`build/test-config`. `ctest --test-dir build` runs everything — thirteen suites,
none of which needs the mouse.

## egg-config

```
egg-config devices                 list every VID 0x3367 interface
egg-config read [--save FILE]      read the settings record, dump it, save it
egg-config info                    small query (A1 02)
egg-config diff A B                compare two saved records, offline
egg-config set                     list the settable fields and their citations
egg-config set FIELD VALUE --yes   change ONE derived field
egg-config dryrun REC [F V]        offline: print the exact 1041-byte frame
egg-config encode F V IN OUT       offline: apply one field to a saved record
egg-config factory-reset --yes     device-side reset (A1 13)
egg-config restore FILE --yes      write a saved record back, then verify
```

`set` covers **twelve** fields, and a field is in the table only when its
*meaning* is derived from the vendor binary and cited to an address. Knowing
where a byte lives is not enough: all 115 record bytes are mapped, and most of
them are still nameless. `egg-config set` with no arguments prints the list with
each citation, and a second list of fields that are derived and deliberately
**withheld**, each with the reason — because "why can I not set this?" deserves
an answer in the tool, not only in the notes.

`restore` is different in kind: it sends back bytes the *device* produced, so
nothing in it is invented, even though most of it is not yet understood.

Every write path reads first, requires the read to be structurally plausible,
**self-checks the frame it is about to send against the record it just read**,
writes, reads back, and verifies the data rather than the acknowledgement. A
write that is acknowledged but does not verify is reported as a failure, loudly,
and exits non-zero. All of that lives in one file, `ConfigSession.cpp`, small
enough to audit by eye.

Two flags worth knowing:

`--vault FILE` — the **first** structurally plausible record ever read on this
machine is saved to `~/.egg-mouse-known-good.bin` automatically, by every
command that reads, and is never overwritten. It is the undo. You do not have to
remember to ask for it, because the run where nobody remembered is the run where
it mattered.

`--unknown-bytes=preserve|vendor` — record bytes `0x01..0x04` are the one place
where read-modify-write and copying the vendor produce different wire bytes: the
device reports `80 00 00 00` there and all 73 captured vendor writes carry
`00 00 00 00`. The default is `vendor`, decided on the evidence and reversible
in one line; `ConfigRecord.h` carries the argument and both arms are scored
against the captures.

**A successful read-back proves what the mouse holds now, not what survives
later.** One write has been observed to survive a power cycle intact — 8.6 s
unplugged, 0 of 1024 payload bytes changed. That is `n=1` at nine seconds and
nobody has tried overnight.

It also leaves something unexplained rather than settling it: in the capture run
the vendor's *own* writes were gone by the next session in five gaps out of six,
and power loss is now ruled out as the cause. What emptied them is not known.
`egg-config` says all of this after every successful write.

## egg-flash

```
egg-flash image  <updater.exe>     validate the firmware image and stop
egg-flash dryrun <updater.exe>     emit the exact byte stream, send nothing
egg-flash stream <updater.exe>     the same stream in full, one frame per line
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

`egg-flash` also tells you, on **stderr**, whether a settings undo exists. Its
last command is `A1 13` — byte for byte the config tool's Factory Reset. Whether
it means the same thing in both places is a guess and is not testable without
flashing. If it does, flashing wipes your settings. `egg-config restore` is now
a confirmed way back, but only from a file you already have, so run
`egg-config read` first.

That message is on stderr and not stdout deliberately: `stream` exists so the
whole outbound byte sequence can be diffed against a capture of the vendor's
tool doing the same flash, and a warning on stdout would make that diff depend
on whether a backup file happened to exist. A test pins it.

There is no `flash` verb yet, on purpose.

## Tests

```sh
ctest --test-dir build      # all thirteen suites, no hardware needed
```

or individually:

```sh
./build/test-flash          # byte maps, invariants, adversarial bootloader
./build/test-config         # the same, for the config write path
./Tests/mutants.sh          # does test-flash actually catch anything?
./Tests/test_config_set.sh  # everything `set` must refuse
./Tests/test_flash_undo.sh  # the A1 13 settings warning
python3 -m unittest Tests.test_config_replay   # replay the vendor's own writes
```

`test-flash` drives the flasher against a mock bootloader that rejects commands,
returns malformed responses, **reports success for writes it did not store**,
goes silent mid-write, disconnects, stalls past every timeout, corrupts what it
stores, and lies about checksums. None of it needs hardware.

`test-config` does the same for the config path, against a device that
acknowledges writes it never performed, corrupts what it stores, changes an
extra byte of its own accord, returns garbage, returns a short record, and goes
silent — plus a **cooperative** control, because if the friendly case failed
too, none of the hostile ones would prove anything. Its invariants run over
seeds rather than examples: never-writes-after-a-bad-read over 200 seeds,
only-sanctioned-bytes-differ over 400 runs, and bit preservation checked
exhaustively over all 256×256 byte pairs.

`test_config_replay.py` is the strongest check either tool has, because the
reference is not something we produced: the captures give 33 pairs of (record
before, record after) where exactly one setting changed and we know which. Our
own composition code is handed the before-record and the same change, and must
reproduce the vendor's after-record.

Under `--unknown-bytes=vendor` it matches all 33 **whole frames** — all 1041
bytes, header included, byte for byte against what Endgame's tool actually put
on the wire. That last part is new and it checks something nothing else could:
the 16-byte header was *derived* from `FUN_00404180` and had never been compared
against a capture, because `encode` writes a record and a record is only the
payload. `egg-config dryrun` emits the real frame, so now it can be.

`mutants.sh` exists because `test-flash` passed 47/47 on its first run, and a
suite that has never failed has not been shown to work. `test-config` then
passed on *its* first run too, for the same reason. So the harness plants known
bugs in both — several of them the vendor's own — and requires the relevant
suite to notice. It currently kills **25 of 25** real defects and correctly lets
**5** provably-equivalent mutants survive. **If that number drops, a test
stopped working.**

It earns its keep. On the config side's first run it found **three real holes**:
nothing reached the "wrote, and cannot say what the device holds" branch; a
policy that zeroed one byte too many was invisible because record `0x00` is
`0x00` in all nine captured reads and all 73 captured writes; and a redundant
`set` would have put a frame on the wire just to normalise four bytes. Two are
now tested. The third — the pre-send self-check — is labelled *equivalent*
rather than fixed, because no reachable input can make the frame differ from
what was intended, so a test that "caught" it would be asserting something
untrue. It guards against a future bug in the frame builder, and the four
frame-builder mutants are what show such bugs exist to be caught.

## Staged bring-up

The order is fixed, and each stage exists so that the next one has less
untested code in it:

1. One read-only command round trip. ✅ **done on the device, 2026-09-05**
2. Bootloader entry and exit only. No erase, no write. ← next
3. Flash read-back, compared against a reference image. Zero writes.
4. A real flash.

By stage 4 the only untested code is erase and write.

Stage 1 is complete and then some: every frame the *config* tool sends has now
been on the wire against real hardware — the 64-byte queries, `A1 13`, and the
1041-byte `A0 11` write. That was the point of doing it first. What remains
untested in either tool is bootloader entry, erase, and flash write.

## Layout

```
Sources/EGGCore/        HID transport, enumeration, device identity, protocol
                        constants as DATA with a citation on every value
Sources/EGGConfigCore/  the config write path: read-modify-write, validation,
                        diff-verify, the settable field table, the known-good
                        vault, and a config device that lies on demand
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
