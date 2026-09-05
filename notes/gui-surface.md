# The config tool's user-visible surface

CLAUDE.md §1.2a requires the user-visible surface to be checked before any claim
that a capability is missing. This is that check for the config tools, done
properly on 2026-09-05 after three errors showed it had never been done at all.

**A resource is not a screen.** Every mistake corrected here has the same shape:
a dialog template exists in the `.exe`, so the plan assumed the user could reach
it. Reachability is a separate claim and needs separate evidence — which code
creates the dialog, and with what parent.

## 1. What the user actually sees in 1.07

Main window is DIALOG 102. It holds a `SysTabControl32` (id 1060), the firmware
and software version readouts, a **`Factory Reset`** button (1039) and a single
**`APPLY`** button (1043). Four tab pages, created at `0x4133e0`–`0x413490`,
each pushed with parent id `0x424` = 1060:

| tab caption | dialog | contents |
| --- | --- | --- |
| `  Basic  ` | 135 | CPI levels 1–4 + X/Y, LOD, polling rate, angle snapping, ripple control, `Disable LED on Lift-Off`, four unlabelled radio buttons |
| `  Advanced Sensor  ` | 140 | motion sync, motion jitter filter, force max sensor fps, sensor glass mode, sensor angle tuning, CPI downshift tuning, smoothing tuning |
| `  Buttons  ` | 153 | slamclick filter, five multiclick-filter sliders, two `SPDT:` combos, the "I understand…" acknowledgement |
| `  Button Mapping  ` | 139 | left-handed mode, six per-button assignment dropdowns |

The captions are literals at `0x1573e8`. [D]

The owner confirmed the same four tabs and the same four names by looking at the
running program before being told what they were. [O]

Two buttons on the Basic page carry no `WS_VISIBLE`: `Apply CPI settings` (1059)
and `Surface Calibration` (1061). That is a prediction that they are invisible,
not an observation — a program can call `ShowWindow` at runtime. The session log
asks the owner to look. [D]

## 2. Button assignment is a nested menu

Six dropdowns, one per remappable button: right, middle, forward, back, wheel
up, wheel down. **There is no entry for the left button.** [D]

The menu is **nested** — a top-level group opens to reveal functions. the owner
observed the shape directly: Right Button → `MOUSE` → `LEFT CLICK`. [O]

The function names are UTF-16 literals in one run at `0x155cde`, in this order:
[D]

> LEFT CLICK, RIGHT CLICK, MIDDLE CLICK, FORWARD, BACK, SCROLL UP, SCROLL DOWN,
> MOUSE, KEYBOARD KEY, CPI LOOP, FIXED CPI, CPI, PLAY/PAUSE, NEXT, PREVIOUS,
> MUTE, VOLUME UP, VOLUME DOWN, BROWSER, EXPLORER, MEDIA, DISABLE

`MOUSE`, `CPI` and `MEDIA` sit immediately after the group they plausibly head,
which is why they are read as group headings. That reading is **[G]**; the
screenshot settles it.

Two entries open a further modal:

- **`FIXED CPI`** → DIALOG 150, caption `FIXED CPI`: a value EDIT + trackbar, an
  `X/Y Settings` checkbox with its own X and Y pair, and `OK`. [D]
- **`KEYBOARD KEY`** → DIALOG 152, caption `KEYBOARD KEY`: `Enter a key:`, a
  key display, four checkboxes `SHIFT` (1075), `CTRL` (1076), `WIN` (1077),
  `ALT` (1078), and `OK`. [D]

  the owner confirmed this modal exists and that **the four modifier boxes are
  independent, not mutually exclusive**. [O] All four are `AUTOCHECKBOX` in the
  template, which agrees. Whether the wire encoding is a bitmask or an
  enumeration does **not** follow from that and is [G] — ticking two at once is
  the experiment that decides it, and it is line 12 of `05-buttonmapping`.

So a button assignment cannot be one byte in general: `FIXED CPI` carries a CPI
value and `KEYBOARD KEY` carries a keycode plus modifiers.

## 3. The LED page exists in every version and is reachable in none

This is a negative claim, so per §1.2a here is the search space before the
conclusion.

**The claim.** DIALOG 137 — `LED On / Off`, an `LED effect` combo, `Scroll led`,
`Logo led`, `DPI led`, Red/Green/Blue EDITs, `Apply led settings` — is present
in the resources of cfg100, cfg101, cfg104 and cfg107, and is created as a tab
page by none of them.

**Method, and what it can and cannot see.**

1. Full `objdump -d` of the entire cfg107 file, all sections, 410,190 lines.
   Because objdump prints normalised immediates, this sees every encoding form
   at once — `push imm8`, `push imm32`, `movl $imm32`, `movl $imm32, disp(%reg)`
   — which is the exact failure mode recorded for 2026-09-03.
2. The LED dialog is constructed as a member of the main window at offset
   `+0x166c` (ctor at `0x405690`, called from `0x412dfa`). Every instruction in
   the whole file mentioning `0x166c` was enumerated: **five**, being the
   constructor, the destructor, one call at `0x413c9e` shared with all the tab
   pages, and two exception-unwind funclets. None creates it as a tab.
3. The tab-page creation run at `0x4133e0`–`0x413490` makes exactly four pages,
   ids `0x87`, `0x8c`, `0x99`, `0x8b` — 135, 140, 153, 139. `0x89` is absent.
4. The tab-selection handler at `0x413cb3` sends `0x130B` (`TCM_GETCURSEL`) and
   dispatches through a **four**-entry jump table, bounded by `cmpl $0x3`.
5. cfg100, cfg101 and cfg104 have only **three** tab captions
   (`  Basic Settings  `, `  Advanced Settings  `, `  Button Mapping  `) and
   three-page creation runs (135, 140, 139). DIALOG 137 is absent from those
   too, and those builds have no DIALOG 153 at all.
6. the owner independently reported no LED controls anywhere in the running program,
   and no visible lights or light apertures on the mouse. [O]

**What the method cannot see:** a path that reaches the dialog through a stored
pointer rather than a literal member offset, or a page added by a build variant
not in hand. So the honest statement is **not found by an exhaustive literal
scan over every section, which cannot see a computed or pointer-mediated path**
— not "does not exist".

**Why it is there at all.** cfg107 also contains the literals
`' 3950 Configuration Tool'` and `'50 Gaming Mouse software'`. This is a shared
code base across Endgame products, and the OP1 8k v2 build inherits UI for
hardware it does not have.

**Consequence.** The RGB-looking five-byte records at wire `0x1d` in the
settings record (`wire-observed.md` §5.1) have no reachable control behind them
and cannot be attributed by diff, because no capture can move them. They stay
**[G]** and §1.3 forbids writing them. Read-modify-write preserves them anyway,
so nothing is blocked.

The one LED setting that *is* reachable is `Disable LED on Lift-Off` (checkbox
1066) on the Basic page. Given there are no visible lamps, the plausible reading
is the sensor's own illumination LED rather than decorative lighting — [G], and
it does not matter, because the checkbox is reachable and its byte will fall out
of the `02-basic` diff like any other.

## 4. Corrections this file records

Three claims in `windows-session.md` were wrong and are fixed:

| was | is |
| --- | --- |
| an LED window to capture as `06-led.pcapng` | no such window; capture deleted |
| `04-buttons` = the button-assignment tab | `04-buttons` = the Multiclick/SPDT tab (153) |
| button mapping has no dropdown, only push buttons | it has nested dropdowns [O] |

The third is worth naming precisely because it is the most instructive. The
button-assignment combos on DIALOG 139 genuinely do lack `WS_VISIBLE` in the
template, and six push buttons genuinely are present and labelled with function
names. That is real evidence, and the conclusion drawn from it was still wrong:
the program clearly makes the dropdowns visible at runtime. A missing style bit
supports "hidden as shipped"; it never supports "hidden while running", and
`dlgdump.py` prints the bit precisely because it is a template fact.


## 5. The APPLY button is gated on dirty state  [O]

The owner, 2026-09-05, at the machine: *"the APPLY button is greyed out if i havent
changed anything, if i apply after ticking angle snapping then i cant click
apply again."*

Small, and it settles two things that were open.

**No APPLY in any capture is a no-op.** Every `A0 11` WRITE frame in the session
captures follows a real user change, so the diff chain in `fieldmap.py` has no
empty links by construction. An empty diff is therefore a symptom — the setting
was already at that value, or the write was refused — and not the ordinary case.
`fieldmap.py` already prints exactly that, and this observation is why that
message is a diagnosis rather than a shrug.

**It also means a log line can have no frame behind it.** A setting that turns
out not to exist gets a numbered line and no APPLY, and there is no way to
produce a filler frame. That is handled in `fieldmap.py` by the `NO_APPLY`
marker rather than in the capture procedure; see the comment there for why the
tool absorbs this and the log does not.

Mechanism not derived. MFC's usual shape is `SetModified(TRUE)` on each control
notification with the sheet enabling `ID_APPLY_NOW`, which would fit, but no
address has been traced and it is **[G]** whether that is what this program
does. Nothing depends on it.


## 6. `X/Y Settings` is a host-side control that writes nothing  [O]

The owner, 2026-09-05, mid-capture: *"the X/Y settings ticking does not enable apply
because i guess it doesnt count as changing anything"*. Earlier the same
session: *"the slider snaps the other axis if X/Y settings is not ticked and if
it is ticked then it doesnt snap the other"*.

Together those say the checkbox changes **host behaviour only** — whether the
dialog mirrors one axis into the other — and touches no byte of the settings
record. `gui-surface.md` §5's dirty-state gate is what makes this observable:
a control that writes nothing cannot dirty the page, so APPLY stays greyed.

**It agrees with the wire, which was the prediction.** The CPI table has held
*two* independent 16-bit values per stage since the first capture — `90 01 90
01` for 400/400 (`wire-observed.md` §4, record `0x24`–`0x37` in
`config-protocol.md` §7.3) — so X and Y have always been stored separately on
the device. There was never a "locked" representation for a checkbox to switch
into. The lock is the host mirroring, and the tick turns the mirroring off.

Consequence for the capture: the tick gets `NO APPLY` on its own line, and the
following lines set X and Y to different values, which dirty normally. It also
means the **fifth byte of each 5-byte CPI record is still unexplained** — it was
the obvious candidate for an X/Y-independent flag, and this rules that out: a
control that never writes cannot be what sets it.


## 7. The Multiclick Filter acknowledgement is one-shot  [O]

The owner, 2026-09-05: *"after you tick i understand and click apply, the whole line
disappears"*. The `I understand the multiclick filter is not a traditional
debounce slider...` checkbox gates the multiclick sliders, and once acknowledged
and applied it is removed from the page permanently — it cannot be un-ticked.

Two consequences, both practical.

**It can be captured exactly once, ever.** Every later run of `04-buttons` will
find it absent, so that numbered line becomes `NOT PRESENT` with no APPLY behind
it. This is the case `fieldmap.py`'s `NO_APPLY` marker exists for, and it is why
the marker had to be a property of the log rather than of the procedure — no
amount of care at the machine can produce that frame a second time.

**Whether it reaches the device at all is open, and the capture answers it for
free.** If the APPLY that accompanies the tick moves no record byte, the
acknowledgement is host-side state (registry or an ini) and the mouse never
learns about it. `fieldmap.py` already prints `NO BYTES CHANGED` with exactly
that reading. A one-shot UI gate that persists across reinstalls would have to
live on the device; one that does not, would not — so this single diff
distinguishes them.

Not derived. No address is offered for the gate and none was looked for.


## 8. GX Safe forces that button's multiclick filter to 8  [O]

The owner, 2026-09-05: *"changing to gx safe mode locks the multiclick filter at 8 and
greys it out so you cant change it"*, and it is per-button — *"if i do the first
SPDT it does that to the left button filter"*.

So the SPDT mode combo (`Off` / `GX Speed` / `GX Safe`) and the multiclick
filter are **coupled**: selecting GX Safe writes the filter as well as the mode.

**Consequence for the capture,** and it is why this had to be written down the
moment it appeared rather than discovered in the diff: a GX Safe line changes
*two* record fields, breaking the one-change-per-APPLY property the whole method
rests on. `fieldmap.py` already refuses to reduce a multi-run diff to one and
prints both runs, so nothing is silently mis-attributed — but a note on the log
line is what turns "two runs moved, cause unknown" into an attribution.

Avoidable for free: set that button's filter to 8 *before* selecting GX Safe,
and the mode change then moves one byte.

**Consequence for our own writes, which is the more important half.** This is a
constraint the *host* enforces by greying out a control. If it is only host-side,
the device will happily accept a filter value other than 8 while in GX Safe, and
`egg-config set` would then produce a state the vendor tool cannot represent —
not corrupt, but outside anything the vendor has been observed to produce, which
§2's threat model says we should not be the first to try. If instead the device
enforces it, a write would be silently overridden and a read-back diff would
show it.

**Which of those is true is `[G]`.** Neither multiclick nor SPDT mode is in
`egg-config set`'s table (§1.3 — neither meaning is derived), so nothing is at
risk today. If either is ever added, this coupling has to be settled first, and
the read-back-and-verify in `set` is what would catch it.
