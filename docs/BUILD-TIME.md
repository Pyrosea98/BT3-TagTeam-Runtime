# Build time: where a from-scratch setup.py build spends its minutes, and what was done about it

Measured 2026-09-30 on Linux, gcc 16, `ninja -j8`, the same CMake flags setup.py uses
(`-DCMAKE_BUILD_TYPE=Release -DPS2X_DISABLE_PGS=ON`). CPU-seconds come from `.ninja_log`.

## Where the time went (before)

| part | objects | cpu-s | note |
|---|---|---|---|
| generated runner code (`src/runner`, unity batches of 8) | 976 | ~2600 | 7,808 files; 0.8 s of each file's compile was parsing the same 200k preprocessed lines of runtime headers |
| `register_functions.cpp` | 1 | 338 | one constructor with 92,000 table assignments; gcc -O3 chews on it for 5.6 minutes (clang: 16 s) |
| overlay module (`overlay_functions.cpp`) | 1 | 301 | one 360k-line TU, serial, the last thing the build waited on |
| IOP modules, runtime library, Granite, SDL, ImGui, front end | ~700 | ~600 | |

## What changed

1. **Precompiled header** for the generated sources only (`PS2X_RUNNER_PCH`, default ON): the five
   runtime headers every generated file starts with. Parse cost per TU 0.81 s -> 0.12 s. `main.cpp`,
   the front end and the IOP modules keep their own includes (`SKIP_PRECOMPILE_HEADERS`).
2. **Table registrations compiled at -O0** (same option): `register_functions.cpp`,
   `overlay_register.cpp` and the IOP `register_functions.cpp` files run once at startup. 338 s -> 4 s.
3. **Overlay module installed as 16 files** by `games/bt3/gen_overlay.py` (`OVERLAY_CHUNKS`), split
   at function boundaries with the include preamble repeated; `apply_overlay_patches.py` looks for
   each anchor in every chunk. The 5 serial minutes become 16 parallel jobs of 15-60 s.

Nothing about the generated code changes: same functions, same flags, same output.

## Result (this machine, 8 jobs, from an empty build directory, runner target)

| build | wall | cpu-s |
|---|---|---|
| before (`-DPS2X_RUNNER_PCH=OFF`, single overlay file, -O3) | 12 min 26 s | 4972 |
| PCH + -O0 registrations + 16 overlay files, generated code still -O3 | 7 min 07 s | 3359 |
| the same with the generated code at -O1 (`PS2X_RUNNER_OPT`, now the default) | 3 min 17 s | 1555 |

In the "before" build the last 2 minutes were one unity batch: the one holding
`register_functions.cpp`, 482 s on its own after everything else had finished.

## `PS2X_RUNNER_OPT=-O1` is the default for the generated code

The runtime library (rasterizer, GS, VIF, VU1, memory) keeps its own -O2; only the 7,808 generated
files and the overlay drop to -O1. Measured on the fight replay (`work/rig/seambench.sh`,
`caps/aura_042611.inrec`, 75 s, native renderer, same machine, one run each):

| generated code at | mean guest ms/frame, whole run | mean guest ms/frame, fight | process CPU s over 75 s |
|---|---|---|---|
| -O3 | 2.47 | 4.14 | 33.3 |
| -O1 | 2.29 | 3.76 | 32.3 |

No frame-rate cost, if anything less time in the guest (less code to fetch). `-DPS2X_RUNNER_OPT=`
(empty) restores -O3 for an A/B.

## Reading a build's own numbers

```
python3 - <<'EOF'
import collections
last={}
for line in open('build/.ninja_log'):
    if line.startswith('#'): continue
    p=line.rstrip('\n').split('\t')
    if len(p)>=4: last[p[3]]=(int(p[0]),int(p[1]))
tot=collections.Counter()
for o,(s,e) in last.items():
    c='runner' if 'Unity/unity_' in o or '/src/runner/' in o else 'overlay' if 'runner_overlay' in o else 'rest'
    tot[c]+=(e-s)/1000
print(tot)
EOF
```
