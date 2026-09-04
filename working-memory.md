# Working memory

**This file belongs to the AI agent working on this project.** It is external
memory, not documentation. Read it before doing anything else in a session.
Rules in `CLAUDE.md` §1.7.

State, not a log. **Trim it.** Delete finished items, rewrite entries in place,
reconcile it before every commit and commit it in the same commit. If it only
ever grows, it has failed.

**Last updated:** initial setup. No analysis has been done yet.

---

## Now
Nothing in flight.

## Next
1. Import `Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe` into Ghidra and run
   full auto-analysis with FunctionID. Record the total function count and the
   FunctionID match rate for this binary.
2. Define the candidate set (functions Ghidra did not name). Report its size
   before reading anything. If it is too large to read exhaustively, say so with
   the number rather than sampling. `CLAUDE.md` §6.
3. Write an extraction script. It must record structure without encoding any
   belief about what the structure means. `CLAUDE.md` §7.1.
4. Read every candidate function. Per function: what it does, every constant it
   uses, callers and callees, and how it differs across versions. Tag every
   claim `[D]` with a cited address or `[G]`. `CLAUDE.md` §1.2, §1.3.
5. Repeat for updaters 1.07, 1.06, 1.04, then cross-version diff. The diff is
   valid in one direction only: differs between versions implies vendor code;
   identical between versions does **not** imply library code, because the most
   important vendor code is likely to be the most stable.
6. Config tools 1.07, 1.04, 1.01, 1.00 after the updaters. Do not let config
   work delay the flashing side. `CLAUDE.md` §4.4.

## Blocked / needs an answer from Dao
- **Is the OP1 8k v2 physically in hand?** Everything in `CLAUDE.md` §5 is
  unreachable until it is, and the staged bring-up in §4.4 cannot start. Static
  analysis does not depend on this.

## Ruled out / do not retry
- (nothing yet)

## Would cost an hour to re-derive
- (nothing yet. Anything durable goes in `notes/`, not here.)
