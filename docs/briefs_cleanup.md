# Shared brief: near-miss cleanup (twelfth step)

Both binaries are fully linked and byte-identical (main executable 86.91%, overlay 97.16%).
What is left are 183 functions kept as `INCLUDE_ASM` inside linked C files, each with a C
attempt in `#if 0` above it and a note on what differs. Your job: make as many of YOUR files'
functions match as you can.

Read docs/agent_rules.md, then ALL of docs/decomp_guide.md: its "Matching lessons" and
"Linking lessons" sections are the collected experience of about 150 agents and most
near-misses fall to one of them (struct nesting and the multiple-of-16 offset split; blocks
duplicated in both arms; `static inline` helpers; index-at-every-use versus pointer; variables
set twice; declaration order; `do { } while (0)`; attributes standing in for a visible
definition; one-iteration loops; store order). docs/open_questions.md has a row per function.

Rules:
- Edit ONLY the source files named in your task (and a header of theirs only when a struct
  must change: say so in the report). Do not run ninja, configure.py or state-changing git.
  Scratch work goes in build/scratch_cleanup2_<your tag>/ ; never put temporary files under
  src/ or the repo root; never type a bare `import ...` line in the shell.
- Work on a scratch COPY (fdiff accepts any path; it picks -G0 for paths containing src/menu/,
  src/cri/ or src/sys/late_a, else -G8): enable the attempt, remove the INCLUDE_ASM line,
  iterate with `python3 scripts/fdiff.py <copy>` plus an aligned diff (e.g.
  build/scratch_menu_q/adiff.py or build/scratch_menu_o/adiff.py) and `-da` RTL dumps when
  registers are the issue. Only when a function prints OK, apply it to the real file: delete
  its INCLUDE_ASM line and `#if 0` wrapper, and delete any `LIT4_WORD` / `INCLUDE_RODATA` /
  named-string helper lines that existed only for it.
- Before finishing, every real file you touched must be in a consistent state and
  `python3 scripts/fdiff.py <real file>` must print OK for every function in it.
- A function now emitted by C changes the object's data: for EVERY function you make live,
  report what data it now emits (.lit4 words, .rodata tables / strings / jump tables, .sdata)
  with the original addresses, so the linker layout can be fixed (the yaml lists .lit4 /
  .rodata subsegments per file, and the INCLUDE_ASM function's data currently lives in an
  assembly chunk or its .s file). ALWAYS compare the emitted bits with the original data.
- Order: start with the functions that are closest (a few instructions, "registers only"),
  then the rest. Budget your effort: if a function has had 60+ serious variants without
  progress, write down what you learned in its note and move on. If an attempt turns out to be
  behaviourally WRONG compared with the disassembly, fix it and say so prominently: that
  matters more than a match.
- Where no match is found, leave INCLUDE_ASM in place with the improved attempt and note.

Report (short): per function: matched (and the trick that did it) or not (what differs now);
data changes for the linker as above; any behavioural error found in an old attempt; new
lessons worth adding to the guide.
