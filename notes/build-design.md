# Build design — `EGGCore`, `EGGFlashCore`, `EGGConfigCore`

**Status: a document for review, per LIST 1 item 18. No code has been written.**
Building is a LIST 2 item and needs the owner. Nothing here has touched hardware, and
nothing here may reach hardware until the device exists and §4.4's stages have
run in order.

This is the design that follows from what has actually been derived. Every
protocol constant below cites the section that derives it; anything without a
citation is a design choice, not a finding, and is marked as such.

---

## 0. What the derivation licenses us to build

The flasher is the priority (`CLAUDE.md` §4.4) and it is buildable now, because
the parts it needs are `[D]`:

| we know | from |
|---|---|
Every row states the binary it was derived from. A number without its scope is
the failure mode `CLAUDE.md` §6 names, and this table is where it would do the
most damage.

| we know | scope — which binary | from |
|---|---|---|
| the seven updater commands and their byte layouts | updater 1.10, confirmed in 1.04 | `updater-protocol.md` §3, §3.7a, §5.7 |
| that seven is *all* of them, over the whole `.text`, by two independent methods | updater 1.10 (1,154,048 `.text` bytes) and 1.04 | §3.7a |
| the two feature-report lengths, `0x411` and `0x40` | updater 1.10 and cfg107 | §2, §3; `config-protocol.md` §1 |
| the response convention: `resp[1]`, `0x01` ready, `0x04` busy | **updater only** — cfg107's busy byte is `0x03` | §2; `config-protocol.md` §1 |
| the busy-poll shape: +100 ms per pass, 2000 ms budget | **updater only** — cfg107's budget is 1000 ms | §2; `config-protocol.md` §1 |
| the transport retry set `0x15 0x17 0x1D 0x57 0x65B`, 4 attempts, 50 ms | updater 1.10 **and** cfg107 — shared | §2; `config-protocol.md` §1 |
| block numbering and the `0x34` base | updater 1.10 | §3.8 |
| the device-selection predicate — VID + PID + UsagePage `0xFF01` + Usage `0x02` | **both, independently**: updater 1.10 `0x401000` (`0x4010ff`, `0x401109`, `0x40113c`, `0x401145`) and cfg107 `0x4035f0` (§3a). Different functions, identical four-part test | `config-protocol.md` §3a; `updater-protocol.md` §1 |
| that exactly **14** functions can reach the device | **updater 1.10** (raw call edges; 1.04's closure is 10, cfg107's is 38) | §9 |
| that the device presents a **second** vendor collection, `0xFF02`/`0x01`, carrying 8-byte input reports | **cfg100/101/104/107** — no updater touches it | `config-protocol.md` §10 |
| that the config tools cannot flash (no `FWFILE` resource in any of the four) | cfg100/101/104/107 | §6.2.1 |

**What we do not know, and must not invent**, is in §6 "Not yet derived" and §5
of this document.

---

## 1. Two executables, one core

`CLAUDE.md` §3 fixes this and the derivation has not disturbed it. Restating only
what changed:

`EGGCore` carries the transport **and the device-selection predicate**, which is
now a derived four-part test rather than a VID/PID pair:

```
VID        == 0x3367
PID        == <compile-time constant per mode>
UsagePage  == 0xFF01
Usage      == 0x02
```

`config-protocol.md` §3a. The usage pair is what selects the vendor collection
out of the several a gaming mouse presents; **matching VID/PID alone opens the
wrong interface**, and on macOS `IOHIDManager` will happily hand us that wrong
interface. This is the single most likely way to send a correct frame to the
wrong endpoint.

**And this is not hypothetical.** `config-protocol.md` §10: the same VID/PID
carries a *second* vendor collection, `UsagePage 0xFF02` / `Usage 0x01`, which
the config tool opens separately and polls for 8-byte input reports. Two vendor
collections on one PID means an enumerator that stops at the first VID/PID
match has a coin-flip, not a bug it will notice. `EGGCore` must require all
four fields and must **fail loudly if more than one interface matches**, rather
than taking the first.

### 1.1 The busy convention is a parameter, not a constant

The two tools disagree and both are `[D]`:

| | ready | busy | budget | first sleep |
|---|---|---|---|---|
| updater `0x401330` | `0x01` | **`0x04`** | 2000 ms | none |
| config `0x403920` | `0x01` | **`0x03`** | 1000 ms | 100 ms |

`config-protocol.md` §1. A transport that hardcodes one is wrong for the other
executable. These belong in the per-tool protocol table (`CLAUDE.md` §3: "as
data tables, not scattered through code").

### 1.2 What `EGGCore` must refuse to do

- Never send a frame whose report id is not `0xA0` or `0xA1`.
- Never send a length other than the one the table gives for that report id.
- Never treat a transport success as a device success. §3.4a of the updater
  notes records that the vendor's own wrappers return values that are *not*
  device status bytes; a caller that conflates them cannot tell a failed send
  from a device that said no.

---

## 2. `EGGFlashCore`

### 2.1 Preflight — every one of these aborts, and abort is free

Before erase nothing has changed on the device, so `CLAUDE.md` §4.2 makes any
single failure an abort with no partial-pass path.

1. **Image identity.** The image is a compile-time constant (`§1.4`). Not a
   filename, not a resource index chosen at runtime, not an argument.
2. **Image size** matches the constant the block arithmetic expects (§3.8).
3. **Device identity** by the full four-part predicate above.
4. **Report descriptor check.** macOS gives us the descriptor *before* any
   write. Compare the device's declared feature-report lengths against `0x411`
   and `0x40` and abort on mismatch. §5.9 of the updater notes calls this free
   safety the vendor left on the table; it also resolves §6.1 from our side
   without needing the device to tell us anything.
5. **Round-trip a benign query** and require a structurally plausible response —
   not merely a transport success.
6. **Re-confirm identity after any re-enumeration** caused by entering the
   bootloader.
7. **Refuse an unrecognised bootloader identity of any kind.**

### 2.2 The point of no return is `A0 03`, not the erase

Derived in §10.2 of the updater notes: recovery is identical either side of the
actual erase instant, so the boundary that matters to our code is the `A0 03`
start command. Everything before it aborts; from it onward §4.2's second regime
applies and **quitting is the bug**.

### 2.3 The write phase, in one file, ~100 lines

`CLAUDE.md` §4.3. The sequence is fixed by §5.4 and §3.8 and must read as one
list, not a call graph:

```
enter bootloader        A1 3A 00 00 00 5A A5 32     §3.1
  poll for PID 0x1977 re-enumeration               §5.3
start                   A0 03 + count               §3.2
for each block i:
  write                 A0 06, index 0x34+i, 1024 B at +0x10, 16-bit sum at +4
  verify                A0 07, index 0x34+i        §3.4, §5.5
  on mismatch: retry this block, do not advance
whole-image checksum    A1 08 34 <last>            §3.5
complete                A1 09                      §3.6
```

**We deviate from the vendor in exactly one place and it is deliberate.** §5.6
records a defect in the vendor's repair loop; we do not copy it. The deviation
must be commented at the site with a pointer to §5.6, or a later reader will
"fix" our code back into the bug.

### 2.4 `A1 13` after a successful flash — a decision for the owner

The vendor sends `A1 13` after a successful flash (§5.4a), and `A1 13` is
**factory reset** (`config-protocol.md` §5, §7.4). So a normally-completing
vendor flash resets the user's settings.

Two facts make this a real decision rather than a detail:

- There is a reachable vendor path that flashes successfully and **skips** the
  reset (§5.4a's gate (c)), so "the vendor always does it" is false.
- The updater's `A1 13` is fire-and-forget, while the config tool's waits 1100 ms
  and requires `resp[1] == 1` (`config-protocol.md` §4). Same command byte,
  different caller discipline; if we send it, we should use the checked form.

**This is Flagged-for-decision item #1 and is not decided here.**

---

## 3. What actually protects against our own bugs

`CLAUDE.md` §4.3 lists these; this section says what each one concretely
contains, because "write a mock" is not a specification.

### 3.1 The mock bootloader — the highest-value artefact, and it needs no hardware

Adversarial, not cooperative. The failure matrix, each entry a case the real
device could produce and our code must survive:

| injected failure | correct behaviour |
|---|---|
| rejects a command outright | before `A0 03`: abort. After: retry, never return. |
| returns a malformed/short response | treat as failure; never index past the response |
| reports `resp[1] == 0x01` but writes nothing | caught by per-block verify (§2.3) |
| reports success for a block it corrupted | caught by per-block verify |
| goes silent mid-write | retry, re-establish, keep driving |
| disconnects and reappears with a different PID | re-enumerate, re-confirm identity, continue |
| stalls past every timeout | after `A0 03` there is no timeout that gives up |
| returns `0x04` busy forever | budget expires → loud unrecoverable state, not a clean exit |
| returns a status byte that is neither `0x01` nor `0x04` | unrecognised → treat as failure (the vendor handles no other value, §2) |
| accepts the frame but returns the wrong block index | mismatch → do not advance |

**The last two are the ones a cooperative mock never tests**, and they are
exactly the shapes that a real bootloader in a bad state produces.

### 3.2 Invariants, asserted over randomised runs — not examples

`CLAUDE.md` §4.3's list, made checkable:

1. No write is emitted unless preflight passed. (Assert on the emitted stream,
   not on a flag.)
2. No return from the post-`A0 03` phase without either a verified image or an
   explicit unrecoverable state.
3. The byte stream for a given input is **identical every run** — no timestamps,
   no map iteration order, no address-dependent padding.
4. The firmware resource identifier is never anything but the single hardcoded
   constant.
5. Every emitted frame's (report id, length) pair is one of the two table rows.
6. Block indices are strictly `0x34 + i`, monotonically non-decreasing, and
   never advance past a failed verify.

### 3.3 Golden file

Freeze the byte stream for the known image and diff every future run. This is
what catches an "innocent" refactor changing a padding byte. The golden file is
worthless unless invariant 3 holds, so 3 is a prerequisite, not a nicety.

### 3.4 Dry-run

Emit the exact bytes that would be sent, to stdout, with no device open at all.
This is the artefact the owner can review before anything is plugged in, and it is the
input to the golden file.

---

## 4. Order of work

Follows `CLAUDE.md` §4.4, with what is now known:

1. **Mock + dry-run + golden + invariants.** No hardware. This is most of the
   work and all of it is possible today.
2. **Stage 1**, one read-only round trip. A bootloader-mode query does this
   without touching config at all (§4.4).
3. **Stage 2**, bootloader entry and exit only. Exercises the PID `0x1977`
   re-enumeration, which is the part unique to flashing.
4. **Stage 3**, flash read-back via `A0 07` if the bootloader allows it with no
   prior write. Validates block arithmetic at zero risk.
5. **Stage 4**, a real flash.

Config work proceeds in parallel on the same transport (proved shared, §1 of
`config-protocol.md`). **Factory reset must work before any config write; that
gate does not apply to flashing** (§4.4).

---

## 5. Open questions this design cannot close

Stated here so they are not silently resolved by an implementation detail:

- **Whether the bootloader validates the image it is given.** Assumed *not*
  (`CLAUDE.md` §2, decided 2026-09-03). Every guard is therefore ours and runs
  before the first byte.
- **Whether `A0 07` read-back works before any write.** If it does not, stage 3
  collapses into stage 4 and we lose the last zero-risk check.
- **The report descriptor's actual feature-report lengths** — device-gated (§5).
- **Whether entering the bootloader re-enumerates on macOS the way it does on
  Windows**, and how long it takes. The vendor polls with 500 ms sleeps to a
  10-second ceiling (§5.3); we have no basis for a different number.
- **Whether we send `A1 13` at all** — Flagged for decision #1.
- **Settings preservation policy if the pre-flash capture fails** — Flagged for
  the owner #2, and a genuine tradeoff: `§4.1` says never write after a failed read,
  which if applied here lets a config failure block a firmware update.
