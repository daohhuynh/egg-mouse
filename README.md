# egg-mouse

Native macOS software for the **Endgame Gear OP1 8k v2** — a configuration tool
and a firmware flasher. Endgame ships Windows-only tools; the USB protocol here
was derived independently from their `.exe` files by static analysis, and is
being checked against captures of their software talking to the hardware.

**Status: both tools work, and both have been run end to end on real
hardware.** On 2026-09-05 `egg-flash` performed a complete firmware update of an
OP1 8k v2 on macOS — 65 blocks written, 65 verified, zero retries, zero
reconnects, the device's own whole-image checksum matching, and the mouse back
in application mode ~900 ms later.

The outbound byte stream is not merely *similar* to Endgame's. It is the same
135 frames they send, and the approval token the tool demands before writing —
`ecfc8f88` — **is the SHA-256 of Endgame's own captured traffic to this mouse**,
computed by a decoder that shares no code with the flasher. Change one byte of
the plan and the token changes.

`egg-config` read the settings record, factory-reset it, and wrote a saved
record back, each against a prediction committed to git beforehand, each scoring
21 of 21 bytes. After the flash, the post-reset record scored 21/21 against a
table derived from *Endgame's own post-flash capture* — so this software leaves
the mouse byte-identical to where theirs does.

One device, no spare. Everything here was derived from static analysis; nothing
was copied from any third-party implementation.

---

## If your mouse ever stops working

Hold **LEFT and RIGHT mouse buttons together**. Keep holding. Plug the cable in.
Keep holding for a few more seconds, then release.

The mouse re-enumerates as ProductID `0x1977`, Product string `Bootloader`, and
can be re-flashed from there. This is forced by hardware and **does not depend
on any firmware being valid** — it is observed behaviour on the device, not an
inference. It is also the single fact that makes the rest of this project
reasonable to attempt with one mouse and no spare.

**How you got into the bootloader decides whether you can leave it.** A
*button* entry is not latched — the device sat in the bootloader for four hours
and returned to normal on one replug, no buttons held. A *software* entry
(`A1 3A`) **is** latched, survives power loss, and is cleared only by completing
a flash. Two power cycles failed to clear it; a completed flash did, twice —
once by Endgame's updater and once by this one. Identical USB identity does not
mean identical state (`notes/bootloader-observed.md` §5a, §5b).

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
`build/test-config`. `ctest --test-dir build -E mutants` runs the thirteen fast
suites; none of them needs the mouse. Dropping `-E mutants` adds the mutation
run, which takes ~10 minutes because it rebuilds the tree once per planted bug.

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
later.** Two writes have survived a power cycle intact, both with 0 of 1024
payload bytes changed: one at **8.6 s** unplugged and one at **1706.8 s — 28.4
minutes**.

That second test also closed the project's longest-running open question. In the
capture run the vendor's *own* writes were gone by the next session in five gaps
out of six, and duration, power loss, re-enumeration and launch-time writes were
each eliminated in turn. What was left was the obvious explanation nobody had
checked: **the tester reset the settings between capture sections** for a clean
per-section baseline, and the single gap that *held* is the one he thinks he
missed. No device mechanism, and none was ever found. Recollection rather than a
log, and recorded as such (`notes/config-wire-observed.md` §5.1).

`egg-config` prints all of this after every successful write.

## egg-flash

```
egg-flash image  <updater.exe>     validate the firmware image and stop
egg-flash dryrun <updater.exe>     emit the exact byte stream, send nothing
egg-flash stream <updater.exe>     the same stream in full, one frame per line
egg-flash enter-bootloader --yes   send ONE report (A1 3A) and confirm the mode
egg-flash leave-bootloader --yes   the way back (A1 09)
egg-flash read-firmware [OUT]      read the resident image back with A0 07
egg-flash flash <updater.exe> --backup FILE --confirm TOKEN
egg-flash help
```

**A flash is two runs, on purpose.** `read-firmware` takes the backup §4.2
requires; `flash` only *checks* it. Taking the backup inside the flash would put
65 frames the vendor never sends in front of the erase, and the whole reason to
trust the sequence is that it matches a capture of Endgame doing it.

```sh
egg-flash enter-bootloader --yes            # A1 3A. THIS LATCHES.
egg-flash read-firmware backup.bin
python3 Tools/score-read-firmware.py backup.bin
egg-flash flash "...Updater 1.10.exe" --backup backup.bin --confirm ecfc8f88
egg-config restore ~/.egg-mouse-known-good.bin --yes
```

Everything that can fail is checked **before** `A1 3A` goes out — image hash,
block range, backup file, approval token, settings undo, device count — so the
window in which the mouse is not a mouse contains only the commands that have to
be there. After the erase the tool does not stop: no cancel, no timeout that
gives up, and `SIGINT`/`SIGTERM`/`SIGHUP` are ignored until the image is
resident, because with the application erased, exiting cleanly guarantees the
bad outcome.

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
last command is `A1 13` — byte for byte the config tool's Factory Reset. That
used to be a guess "not testable without flashing". It has now been tested by
flashing: the post-flash record scored **21/21** against the same predicted byte
table, so `A1 13` does mean the same thing in both places. **Flashing wipes your
settings.** `egg-config restore` puts them back and is verified by read-back, but
only from a file you already have — so run `egg-config read` first, and `flash`
refuses outright if no undo exists.

That message is on stderr and not stdout deliberately: `stream` exists so the
whole outbound byte sequence can be diffed against a capture of the vendor's
tool doing the same flash, and a warning on stdout would make that diff depend
on whether a backup file happened to exist. A test pins it.

## Tests

```sh
ctest --test-dir build -E mutants   # thirteen suites, no hardware needed
ctest --test-dir build              # adds the mutation run (~10 min: it
                                    # rebuilds the tree once per planted bug)
```

or individually:

```sh
./build/test-flash             # byte maps, invariants, adversarial bootloader
./build/test-config            # the same, for the config write path
./Tests/mutants.sh             # do the suites actually catch anything?
./Tests/test_config_set.sh     # everything `set` must refuse
./Tests/test_flash_undo.sh     # the A1 13 settings warning
./Tests/test_flash_backup.sh   # every way the backup gate must refuse
python3 -m unittest Tests.test_golden_vendor    # our bytes vs Endgame's capture
python3 -m unittest Tests.test_config_replay    # replay the vendor's own writes
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

`test_golden_vendor.py` is the strongest check the flasher has, and it is not a
frozen golden file. The reference is what Endgame's updater actually sent to
this mouse: it diffs every outbound frame against the capture, requires both
vendor flashes to be byte-identical to each other, and asserts that the approval
token equals the SHA-256 of the vendor's 135 frames (136,627 bytes) as decoded
by its own pcapng reader, which shares no code with the flasher. A frozen golden
file cannot tell you the derivation was wrong on day one. This can. Verified it
can fail: a one-byte change to the plan moves the token and the test catches it.

`test_config_replay.py` is the equivalent on the config side, because the
reference is again not something we produced: the captures give 33 pairs of (record
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
suite to notice. It currently kills **43 of 43** real defects and correctly lets **5**
provably-equivalent mutants survive. **If either number moves, suspect the
harness before the tests** — it has now been wrong five times, and every one of
those bugs made it misreport its own reliability, which is the only thing it
exists to measure. The most recent (a mutated file left out of the restore list)
graded every later mutant against an already-failing suite and reported a clean
sweep. It was caught only because two *equivalent* mutants came back "killed":
those exist precisely to detect the harness passing everything, so do not tidy
them away.

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

The order was fixed, and each stage existed so the next had less untested code
in it. **All four are complete, all on 2026-09-05, all on the one device:**

1. One read-only command round trip. ✅
2. Bootloader entry and exit only, no erase, no write. ✅
3. Flash read-back compared against a reference image, zero writes. ✅
4. A real flash. ✅

Stage 3 is worth its own line because it is what made stage 4 safe to attempt.
`read-firmware` was run twice and returned **byte-identical** images both times;
64 of the 65 blocks matched the reference image extracted from the vendor `.exe`
exactly. A wrong payload offset, index mapping or base address produces garbage
in every block, so 64 exact matches against a reference never sent to the device
is what proved the addressing before anything was erased.

The 65th block (device index `0x74`, the last) differs, stably, and the
pre-registered guess that it holds settings was **refuted** — the settings record
is nowhere in it, and the whole firmware image is encrypted or compressed
(entropy 7.80 bits/byte across every block). What it is: the firmware writes that
block at runtime. The vendor's own flash overwrites it and the device rebuilds
it, which is exactly what ours did.

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
Tools/pe/               PE resource lister/extractor for the FWFILE images
Tools/ghidra-export/    static-analysis tooling over the vendor binaries
Tools/device/           read-only device observation (opens nothing)
Tools/score-*.py        scorers for the pre-registered predictions, each
                        written and committed BEFORE the run it grades
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
