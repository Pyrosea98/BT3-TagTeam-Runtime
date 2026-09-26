# Recompilation orchestrator (progressive, coverage-driven)

Goal: drive the project from "CPU mostly recompiled, some code still interpreted" to
"everything native", by **capturing what still runs interpreted / is serviced by an HLE
shim**, recompiling those units ahead-of-time, and repeating until the capture is empty.
Over time this is also what lets the project own its codegen and drop the upstream
recompiler dependency.

## The idea: sample once, discard duplicates, build progressively

Each real play session is a *sample*. `orchestrate.py` keeps a persistent corpus
(`state.json`) of every unit ever seen:

- a unit **never seen before** is *sampled* and enters the plan as `pending`;
- a unit **already known** is *discarded* (only its hit count is bumped) — duplicates
  never re-enter the plan.

So the build is **progressive**: `plan` lists only the still-pending units, hottest
first, and you build/validate them one (module/microprogram/overlay) at a time.

> Runs should be **natural gameplay** — do **not** use `PS2X_AUTOSTART`: autostart makes
> every run near-identical, so after the first sample there is nothing new to sample and
> the orchestrator would discard everything. Playing normally is what surfaces new units.

## The loop

```
# 1. play a session with capture (no autostart)
PS2X_COVERAGE=/tmp/cov.tsv ./"Dragon Ball Z Budokai Tenkaichi 3 - Recompiled" data/SLUS_216.78

# 2. sample it (new units -> pending, duplicates -> discarded)
python3 tools/orchestrator/orchestrate.py ingest /tmp/cov.tsv \
        --corpus tools/orchestrator/corpus

# 3. progressive plan (only pending, hottest first; folds in the IRX modules as candidates)
python3 tools/orchestrator/orchestrate.py plan \
        --data "<deploy>/data" --functions games/bt3/functions.csv

# 4. recompile / regenerate the top unit(s), then close them
python3 tools/orchestrator/orchestrate.py done iop:SIO2MAN.IRX
python3 tools/orchestrator/orchestrate.py done --area ee_overlay

# 5. gate (clean once nothing is pending) and repeat
python3 tools/orchestrator/orchestrate.py check
```

Per-area handling:

- `ee_overlay` -> statically recompile the overlay entry points (EE).
- `vu1` / `vu0` -> regenerate the microprograms (`gen_vu1.py`).
- `iop` -> recompile the IRX module to native (the HLE shim for it is retired).
- SPU2 / hardware: separate workstream, same capture file.

`coverage.py <tsv> [--out report.md]` still produces a standalone human-readable report.

## Capture format

`PS2X_COVERAGE=<file>` (runtime, `ps2xRuntime/src/lib/ps2_coverage.cpp`) writes TSV,
line-buffered so it survives a kill:

```
kind        arg1                  arg2
ee_overlay  0x00265a20                   # a MIPS pc interpreted by ps2_interp
vu1         4e955b2c4dbf5328      0x1234 # an MPG hash not in vujit::kPrograms
vu0         <hash>                <extent>
iop         DBCMAN                0      # an IOP module still serviced by the HLE
```

Deduplicated, bounded (`PS2X_COVERAGE_MAX`, default 200000).
