# PRE-REGISTERED: the 2026-09-06 capture session, on firmware 1.10

**Written and committed BEFORE the capture.** Every number below is a
prediction. Score it afterwards by diffing, and record disagreements as
disagreements -- §7's tiebreaker is the device, not this file.

## Why this session, when the last one is not being redone

`Tools/capture/whatmoved.py` partitions the 115 record bytes: 38 MOVED,
51 REACHABLE-but-never-exercised, 26 CLOSED. The 51 are the target. They are
derived from cfg107 with a cited address each -- the citations live in §7.17,
§7.19 and §7.31, not here -- and **none has ever been seen on the wire**, which
is exactly the state `fixed-cpi` was in this morning when an uncited bound
turned out to be wrong in both directions (§7.31).

**Design rule, learned from the quarantined `./log.txt` (§1.1a): ONE edit per
APPLY.** Then every write's diff names itself and no log is needed. That is why
`06-cpi-stage.pcapng` survived the quarantine intact and the rest did not.

**Bonus that applies to every line below:** all 73 captured vendor writes are
from firmware **1.07**. This device has been on **1.10** since the flash. These
would be the first vendor writes ever observed on the firmware actually running.

---

## A. CPI stages 3 and 4 -- the highest-value edits  [predicting]

Records `0x2d`-`0x31` (stage 3) and `0x32`-`0x36` (stage 4) have **never been
written by the vendor**. Each stage is `flag, Xlo, Xhi, Ylo, Yhi` (§7.8).

| step | action in cfg107's CPI page | predicted record diff, and NOTHING else |
| --- | --- | --- |
| A1 | stage 3 X = 1200 | `0x2e`/`0x2f` → `b0 04`; `0x2d` stays `00` |
| A2 | stage 3 Y = 2400 | `0x30`/`0x31` → `60 09`; **`0x2d` → `01`** |
| A3 | stage 4 X = 3000 | `0x33`/`0x34` → `b8 0b` |
| A4 | stage 4 Y = 3000 (equal to X) | `0x35`/`0x36` → `b8 0b` |

**THE COMMITTED PREDICTION, and it is a prediction that the vendor is WRONG.**
After A4, stage 4's X and Y are equal, so its flag `0x32` should read `00`.
§7.8 derives that cfg107 computes stage 4's flag from **stage 3's** boxes
(`0x40edde` loads `0x4f0`, `0x40ede4` compares `0xe84` — both stage 3's members,
where the pattern demands `0x4f4`/`0xe88`).

> **Predicted: record `0x32` reads `01` after A4, not `00`** — because stage 3
> is X≠Y. If it reads `00`, §7.8's copy-paste-bug reading is REFUTED and
> `config-protocol.md` §7.8 and §7.31's sibling note both need correcting.

Either outcome is worth the capture. This is the only prediction in the project
that says the vendor's own tool does something incorrect.

## B. FIXED CPI on a button, with X != Y  [predicting]

Entirely unobserved, and it is the code that changed today (§7.31). Every
captured FIXED CPI entry is `0c 00 40 06 40 06` — X = Y = 1600.

Pick a button with a row on the Button Mapping page (**not** LEFT, **not** the
underside CPI button). Set it to CPI → FIXED CPI, then:

| step | action | predicted entry bytes at that button's `0x37 + 7n` |
| --- | --- | --- |
| B1 | FIXED CPI, X = 2500, Y = 2500 | `0c 00 c4 09 c4 09 <+6 unchanged>` |
| B2 | change Y to 800, leave X | `0c 00 c4 09 20 03 <+6 unchanged>` |

B2 is the one that matters: it is the first evidence either way that the two
boxes reach the record independently. §7.31 derives that they do
(`0x401eb0` reads members `0x300`/`0x304`; `0x407c7e`/`0x407ca0` store them to
`+2` and `+4`).

**Also worth one edit if the box allows typing:** type `2505` and apply.
Predicted: the vendor normalises it to **`2510`** — `0x401e70` rounds to the
nearest 10 with ties going up — and the record shows `ce 09`, never `c9 09`.
That would confirm the domain our encoder now enforces.

## C. The seven button actions never captured  [predicting]

`[D]` at §7.17, each from one `movb` immediate. Low risk — all eight MEDIA
handlers are the identical instruction shape and **VOLUME UP is already
confirmed on the wire** as `20 e9`, matching `0x408012`/`0x408020`. So this is
corroboration of a strong derivation, not a live question. Cheap while in there.

Map any one eligible button to each in turn; predicted `+0`/`+1`, `+2`-`+5` zero:

| action | predicted | cited |
| --- | --- | --- |
| PLAY/PAUSE | `20 cd` | `0x407d76`/`0x407d84` |
| NEXT | `20 b5` | `0x407e1d` |
| PREVIOUS | `20 b6` | `0x407ec4` |
| MUTE | `20 e2` | `0x407f6b` |
| VOLUME DOWN | `20 ea` | `0x4080b9` |
| BROWSER | **`18 96`** | `0x408160`/`0x40816e` |
| EXPLORER | **`18 94`** | `0x40820a`/`0x408218` |

BROWSER and EXPLORER are the two worth doing if only two are done: they are the
only actions whose `+0` is `18` rather than `20`, so they are the ones a
misreading would most likely have got wrong.

## D. What NOT to recapture, so no time goes into it

Everything in `00`, `01`, `02`, `03`, `04`, `07`, `08`, `09`, `10`. Those bytes
are held and are checked by `test_config_replay.py`, `test_button_map.py`,
`test_golden_vendor.py` and `test_defaults.py`. Re-taking them buys nothing.

`05-buttonmapping` only needs the parts named in B and C above.

## E. Two things NOT to do

- **No `log.txt`.** One edit per APPLY makes every diff self-labelling, which is
  the whole point (§1.1a). A note of the ORDER of sections is enough.
- **Do not factory-reset at the end** unless wanted — it is proven (21/21) and
  costs a settings state that would otherwise be a free extra data point.

## Scoring

    python3 Tools/capture/whatmoved.py      # REACHABLE should FALL
    python3 -m unittest Tests.test_defaults # OnlyNamedBytesEverMove must still pass;
                                            # if a NEW unnamed byte moved, that is a
                                            # new field and the gate says so
