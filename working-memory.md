# Working memory

State, not a log. Rules in `CLAUDE.md` §1.7. **Trim this file.** Delete finished
items; rewrite entries in place; if it only ever grows it has failed. Reconcile
it before every commit and commit it in the same commit.

**Last updated:** initial setup. No analysis has been done yet.

---

## Now
Nothing in flight.

## Next
Per `prompt.txt`, in order:

1. Import `Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe` into Ghidra and run
   full auto-analysis with FunctionID. Record the total function count and the
   FunctionID match rate for this binary.
2. Define the candidate set (functions Ghidra did not name). Report its size
   before reading anything. If it is too large to read exhaustively, say so with
   the number rather than sampling (§6).
3. Write an extraction script. It must record structure without encoding any
   belief about what the structure means (§7.1).
4. Read every candidate function. Document per `prompt.txt`, tagging each claim
   `[D]` with a cited address or `[G]`.
5. Repeat for updaters 1.07, 1.06, 1.04, then cross-version diff. Diff is valid
   in one direction only (§ the three signals in `prompt.txt`).
6. Config tools after the updaters. Do not let config work delay flashing.

## Blocked / needs an answer from Dao
- **Is the OP1 8k v2 physically in hand?** Everything in `CLAUDE.md` §5 is
  unreachable until it is, and the staged bring-up in §4.4 cannot start. Static
  analysis does not depend on this.

## Ruled out / do not retry
- (nothing yet)

## Would cost an hour to re-derive
- (nothing yet. Anything durable goes in `notes/`, not here.)
