# Rules for a decomp agent working alongside others

Read docs/decomp_guide.md first (workflow, tool, compiler and assembler quirks). Finished examples:
src/sys/heap.c, src/sys/file.c, src/sys/pad.c, src/battle/btl_pool.c and their headers.

- Run everything from the repo root. `python3 scripts/fdiff.py <file.c> [function ...]` is the
  ONLY build tool you run; it is safe in parallel. Do NOT run configure.py, splat, ninja or git,
  and do not edit config/*.yaml, configure.py, include/gcc_prelude.inc, existing sources/headers,
  other agents' symbol files, or anything under asm/ or build/.
- Write only the files your brief names: your C files, your headers, your one symbol file.
- Disassembly: asm/cod/*.s (main executable; find your range with
  `grep -ln "^glabel NAME$" asm/cod/*.s`) and asm/dbzp/000000.s (menu overlay, which calls into
  the main executable). The files are huge: slice with awk/grep/python, never print them whole.
    awk '/^glabel NAME$/{p=1} p; p&&/^endlabel/{exit}' asm/cod/FILE.s
  Data and strings: asm/data/cod/*.s (grep the D_ symbol). Callers: grep `jal +NAME`.
- Existing names with their evidence: config/symbols/*.txt and config/symbol_addrs.txt. Use the
  headers under include/ for modules already decompiled. Names must be unique across config/
  (grep before adding). fdiff looks functions up by NAME in the symbol files, so add a name to
  your symbol file before diffing a function you named.
- Every function ends with a descriptive name (`Module_VerbNoun`, globals `gName`) and a one-line
  comment in the C saying what it does. Structs go in the header with field offsets in comments;
  unknown fields are `unkXX`. Mark guessed names `// guess` in the symbol file, and say in the
  comment above each group what the evidence is.
- A C file covers one contiguous address range, in address order, nothing skipped. Several
  consecutive files with no gap are fine when the code is clearly several source files.
- A function that will not match after real effort: INCLUDE_ASM as the guide describes, with your
  best attempt above it in `#if 0` and a note on what differs. Do not let one hard function block
  the rest; do the small ones first.
- String literals, float constants (.lit4) and jump tables your C emits must be listed in the
  report with their original addresses: the integrator has to place them.
- Do not claim a match you did not see fdiff print `OK` for.
- If another agent's header changes under you, do not depend on it: declare a local view and say so.

## More rules (added after the third batch)
- Background on what is already known: docs/README.md and docs/systems/*.md. Read the system doc
  for your area before starting; treat "inferred" items as leads.
- Scratch files go in your OWN subfolder of the scratchpad or of build/ (e.g.
  `build/scratch_<yourmodule>/`), never the shared root. Run Python scratch files with
  `python3 file.py`. Never use `pkill`/`killall`; if you must stop a process, kill the exact PID
  you started.
- Do NOT run scripts/apply_names.py (it edits other agents' files). If a function you call was
  named by someone else while you worked, use the name that is in config/ when you finish.
- Float constants: fdiff masks constant relocations, so it cannot see a wrong VALUE. For every
  `.lit4` / float constant your C emits, compare the bits with the original data
  (asm/data/cod/*.lit4.s) and say in the report that you did.
- Shared structs that several agents touch (the fighter object above all): do not create a
  shared header for them. Declare a partial view local to your module with a module-specific
  name, and give a table of every field you can justify (offset, type, meaning, evidence) in
  the report, so one unified header can be built afterwards.
- Functions with INCLUDE_ASM: fdiff cannot assemble the file until the integrator re-splits.
  Verify by temporarily enabling your `#if 0` attempt, then restore the INCLUDE_ASM, and say so.

## Report (final message)
Each file's exact address range (start, end exclusive); functions OK vs INCLUDE_ASM (name those,
say what differs); data structures identified; what the code does, separating what the matching
C verifies from what you infer; emitted strings/floats/jump tables with original addresses;
guessed names; existing names or comments you believe are wrong; anything the integrator must know.
Also include a "Doc notes" section: the facts a reader of docs/systems/ should learn from your
module, each marked verified (by your matching C) or inferred, written so it can be pasted into
the docs.

## Working while the build is in use (added for the fifth batch)
- Another process may be re-splitting and relinking. Read disassembly from the frozen snapshot
  your brief names (same layout as asm/), not from the live asm/ folder. fdiff reads the
  original bytes from disc/, so it is unaffected.
- File names: use exactly the file stem your brief assigns (you may add a suffix after it if
  you split, e.g. `<stem>_b.c`). Never create a file under a different stem.
- Fighter-side conventions already established (see docs/systems/fighter.md and the headers
  under include/battle/): the fighter object is 0x1600 bytes; flags are tested with
  `BtlChar_TestFlag(chr, n)`, set with `BtlChar_SetFlag` / `BtlChar_SetHeldFlag`, cleared with
  `BtlChar_ClearFlag`; `BtlChar_GetObj(chr)` returns its battle object; the active member's
  gauge block is `func_001CE1B8(chr)`; +Y is down; angles are radians; 30 frames per second.
