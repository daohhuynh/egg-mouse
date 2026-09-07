# Config-tool adoption record

**What this file is.** One row per Endgame configuration tool whose settings
layout has been re-derived and found to still match what `egg-config` writes.
`Tools/pe/ingest_config.py` prints the row; a person pastes it here.

**Why it exists.** The firmware half of the update pipeline leaves a committed,
reviewable trace when a release is adopted — `Tools/pe/ingest.py` prints a
manifest row that becomes a source change. The config half printed a verdict
that changed no bytes and was recorded nowhere, so six months later nobody could
tell whether a given version had ever been vetted. A verdict nobody can look up
is not much better than one nobody computed.

**What a row does NOT mean.** It is not permission for anything at run time.
Nothing in `Sources/` reads this file, and nothing should: the config tool is
static analysis input, not a table the software consults. A row means one
person ran the check on that exact file, on that date, and it came back SAFE.

**What SAFE means, precisely** (`ingest_config.py`'s own docstring has the
full argument): the complete record ← object map re-derives from that binary's
own bytes and is identical to the reference, and every field `egg-config` can
write is located by what it MEANS — the bound check, the jump table, the clamp,
the `BM_GETCHECK` quartet — and lands on the record offset this build ships. It
says nothing about fields we do not understand, and nothing about firmware.

**How to check a row.** `python3 Tools/pe/ingest_config.py <the .exe>` and
compare. `Tests/test_adoption.py` does exactly that in `ctest` for every file
listed here that is present in the repo, so a stale or mistyped row fails.

| file | version | checked | sha-256 (first 16) | record map | vs. reference |
| --- | --- | --- | --- | --- | --- |
| `Endgame Gear OP1 8k v2 Configuration Tool v1.00.exe` | 1.00 | 2026-09-06 | 00ee4b2067b004e5... | 111 offsets, high 0x72 | identical to cfg107 |
| `Endgame Gear OP1 8k v2 Configuration Tool v1.01.exe` | 1.01 | 2026-09-06 | 1f2a20a4577cb226... | 111 offsets, high 0x72 | identical to cfg107 |
| `Endgame Gear OP1 8k v2 Configuration Tool v1.04.exe` | 1.04 | 2026-09-06 | 38330b5c19dd424b... | 111 offsets, high 0x72 | identical to cfg107 |
| `Endgame Gear OP1 8k v2 Configuration Tool v1.07.exe` | 1.07 | 2026-09-06 | e0494dd635e0029f... | 111 offsets, high 0x72 | identical to cfg107 |

**cfg107 is the reference, so its own row is trivially "identical".** It is
listed anyway, because it is the binary every `[D]` config claim in
`notes/config-protocol.md` cites, and a reader should not have to infer that it
was checked. The other three are real comparisons: four versions spanning the
product's life all place the same 111 record offsets, which is the measured
basis for the claim that the layout is stable — not an assumption.

**Not adopted, and not a gap:** `XM1r_Flash_Upgrade_1.9.46.exe` is a different
product and is held only as a negative control. `old-firmware-executables/*`
are updaters; they go through `Tools/pe/ingest.py` and
`Sources/EGGFlashCore/src/FirmwareManifest.cpp`, not through this file.
