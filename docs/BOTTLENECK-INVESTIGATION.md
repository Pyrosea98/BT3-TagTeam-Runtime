# BT3-Recomp — Performance Bottleneck Investigation (Windows)

Status: **root cause identified**. This document is the complete record of the profiling
work done to explain why the Windows build runs at ~20-25 fps in a fight while the Linux
build reaches 60 fps (at 3x internal resolution), and what the fix is.

## TL;DR

The bottleneck is **not** the renderer, the D3D present bridge, the thread pools, or vsync.
It is the **VU1 emulation**: the fight's VU1 microprogram is **not in the static recompiler
(JIT) table**, so it runs on the **interpreter** at ~104,000 interpreted runs/s. That single
serial interpreter, running on the "KickWorker" thread, gates the frame.

Fix: **compile that microprogram** (and any others) into `vu1_jit_gen.inc` with the existing
`ps2xRuntime/tools/gen_vu1.py`, then rebuild. This is a code-quality/speed fix, not a
thread-distribution problem.

## Objective / context

* Goal of the broader work: replace raylib/OpenGL video with native D3D11 (present first,
  then the GS replay). raylib keeps window/input/audio.
* Symptom investigated here: **performance**. Fight = ~22-27 fps, vs. Linux 3x@60 fps.
* Default Windows config used: `PS2X_GPU=1`, `PS2X_PGS=0`, D3D11 present on (default),
  window 1440x900, fight.
* `PS2X_PGS` (paraLLEl-GS / Vulkan compute) was **retired on Windows** during this work;
  Linux still uses it, which is a large part of why Linux is fast (see below).
* 2026-09-25: paraLLEl-GS is no longer **built** on any platform by default
  (`PS2X_DISABLE_PGS=ON`). The AMD Vulkan ICD (amdvlk64.dll, driver 2.0.279, RX 580) faults
  inside `DllMain` on a worker thread right after device creation, killing the runner at ~15 s
  with an access violation under `vk_icdGetInstanceProcAddrSG`. The backend is being rewritten;
  with `PS2X_HAVE_PGS` undefined, `applyRenderer()` maps a configured `parallel-gs` to the
  OpenGL present, so old settings keep working. `-DPS2X_DISABLE_PGS=OFF` brings it back.

## Measurement method

* Built-in frame profiler: `PS2X_FRAMEPROF=1` → `[frameprof]` / `[frameprof2]` lines
  (loop / present / bar / pre / wait / other, and a split of "other" into
  sbb / pad / begin / blit / ui / audio / RENDER). Plus the periodic `[fps]` line
  (`GAME`, `guest_ms`, `wall_ms`, `gpu_ms`, `glcalls/sec`, `glflush/sec`, `flush_ms/s`, ...).
* Process/thread CPU: sampled `.NET ProcessThread.TotalProcessorTime` deltas.
* Per-thread attribution by **name**: `PS2X_EEPROF=50` (the runtime's built-in sampling
  profiler; names the guest/worker threads and reports each one's sample distribution).
* Address resolution: added a linker **map** (`/MAP`) and resolved the sampled RIPs in
  `ps2EntryRunner.map`.

## Results (all in a fight)

| Config | GAME (fps) | CPU (cores) | wall_ms | gpu_ms | glcalls/s | glflush/s |
|---|---|---|---|---|---|---|
| Baseline: GL replay + D3D present | 22–27 | 2.71 | 61–94 | 16–19 | 46k–56k | 46k–55k |
| Pools reduced (`RASTER_THREADS=2`, `DECPOOL=1`, `TEXPACK_THREADS=1`) | 24–26 | 2.76 | 58–65 | ~15 | ~50k | ~50k |
| vsync OFF (`PS2X_VSYNC=0`), 1440x900 | **19–20** | 3.58 | 59–95 | 10–12 | 40k | ~40k |
| Present via raylib GL (`PS2X_D3D11=0`) | 24–25 | 2.67 | 59–94 | 17 | ~50k | ~50k |
| **Native D3D GS replay** (`PS2X_D3D11_GS=1`) | 23–30 | 2.86 | 70–107 | 16–43 | **~5** | ~5 |

Key deductions:

1. **Not the render path.** The native D3D replay issues **~5 GL calls/s** (vs ~50k) and
   still lands at the same ~25-30 fps. Replacing the GL draw submission changes nothing.
2. **Not the D3D present bridge.** `PS2X_D3D11=0` (present through raylib GL, no GL→D3D
   readback/upload) gives the same fps.
3. **Not thread oversubscription.** Dropping the worker pools (38 → 30 threads) changed
   neither CPU nor fps.
4. **Not vsync.** Turning vsync off made it slightly *worse* (19-20 fps) and raised CPU;
   the frame is ~60 ms of real work, not a vblank stall.
5. The process only uses ~2.7-3.9 of 12 cores, and lowering the pools didn't help → the
   limit is a **serial stage**, not available parallelism.

## Per-thread analysis (fight)

OS-thread CPU sample (`%` of one core):

```
tid 3208  92.4     tid 5972  81.8     tid 7212  34.1
tid 10260 91.7     tid 6928  65.9     rest < 8%      sum ≈ 386% (~3.86 cores)
```

`[eeprof]` sample distribution (time, by name):

* **KickWorker: 100% in real code** (`ps2EntryRunner+0x4b9x000`) → the only thread truly
  burning CPU.
* GameThread: ~75-77% **blocked** in `ntdll` (a wait; not spinning).
* GsThread: ~78% **blocked** in `ntdll`.
* DecPool: idle.

So the guest and the GS thread are mostly **waiting**; the CPU is spent in **KickWorker**.

## Root cause

Resolving the KickWorker hot addresses with the linker map put every one of them inside
`ps2_vu1.cpp`, around `VU1Interpreter::run` — i.e. the **VU1 microcode interpreter**.

The runtime then says it explicitly:

```
[vu1jit] uncompiled microcode 2e0c5ce4b567755c extent 0x328 (gen 147) dumped to ./vumicro_2e0c5ce4b567755c.bin
[vu1jit] uncompiled 2e0c5ce4b567755c: 104480 interpreted runs/s (7736498 total)
```

* The VU1 runs a **static recompiler** (`vujit`, default ON, env `PS2X_VUJIT`) whose output
  is `ps2xRuntime/src/lib/vu1_jit_gen.inc`. On an MPG microcode upload it matches the
  program by hash; on a miss it **falls back to the interpreter** and dumps the image.
* The compiled table currently holds **7 programs**; the fight's microprogram
  (`2e0c5ce4b567755c`) is **not** among them → it is interpreted ~104k runs/s.

### Why this can't be "spread across cores"

* A VU1 is a **single co-processor**; it executes microcode **in order**. The GS command
  stream must also be applied in order. So the KickWorker's stream is inherently serial, and
  adding worker threads does not help (confirmed empirically above).
* The win is **eliminating the interpreter**, i.e. running the recompiled body (typically
  an order of magnitude faster), not parallelizing.

## The fix

1. Dump all microprograms (so the already-compiled 7 are recovered too, plus the new ones).
   The missdump lives on the **JIT-miss** path, so simply setting `PS2X_VUJIT=0` does *not*
   dump (it disables the whole JIT block). Force misses instead by temporarily emptying the
   compiled table: set `const int kProgramCount = 0;` in `vu1_jit_gen.inc` and rebuild.
   Then run with `PS2X_VUMICRO_DIR=<dir>` through boot → menus → **fight**.
   Each miss writes `vumicro_<fnv>.bin` and logs `... extent 0x<len> ...`.
2. Assemble the `image.bin:extent_hex` pairs from the log.
3. Regenerate: `python ps2xRuntime/tools/gen_vu1.py ps2xRuntime/src/lib/vu1_jit_gen.inc <img:extent> ...`
   (include **all** images, old and new, or the old programs are dropped).
4. Rebuild → the fight's VU1 runs JIT-compiled.

### Current temporary state (MUST be aware)

`vu1_jit_gen.inc` currently has `kProgramCount = 0;` (temporary, so every microcode misses
and dumps). It is a **generated file**; regenerate it in step 3 to restore/expand the table.
Until then, all VU1 microcodes run on the interpreter (slow but functional).

### Candidate set already captured (from the miss log)

```
01e2e7558cba9da6:0x328   d0c1b46aed90b076:0x328   7a07521fe999d309:0x3f8
285eaf99dc69164e:0xc78   2e0c5ce4b567755c:0x328   57dbb9cd47f04d66:0x340
b96e78a9bb58ca50:0xe18   c5c10e68b369c13b:0x3f8   ffd6c19ded3c5844:0xc78
```

(The 7 already-compiled programs must also be included when regenerating; their images are
recovered by the `kProgramCount=0` dump run.)

## Secondary findings (render port, for context)

* Native D3D11 **present** works and is the Windows default; the GL-replay + D3D-present
  path renders the whole game correctly (confirmed A/B).
* The **native D3D GS replay** (`PS2X_D3D11_GS=1`) renders 2D/HUD/menus but the **fight 3D
  is black**; see `docs/D3D11-NATIVE-STATUS.md`. Fixes landed so far: GS CLAMP wrap per draw,
  indexed-RT CLUT (`uPal`) binding, `isVramBlit` native blit, and the leak/BARBLOCK fixes.
* Per the measurements above, finishing the native replay will **not** by itself improve fps
  — the fps ceiling is the VU1 interpreter.

## Instrumentation added (kept)

* `PS2X_FRAMEPROF` breakdown, `PS2X_EEPROF` per-thread sampling (pre-existing), linker
  `/MAP` for RIP resolution, `PS2X_VSYNC` (0 = no vsync), `PS2X_WINDOW_W/H` window override,
  `PS2X_D3DDUMP` (present RT → PNG), `gfx::ReadbackRGBA` + `d3dDumpPresent`.

## Next steps (tomorrow)

1. Complete the all-microcode dump run (fight) and regenerate `vu1_jit_gen.inc` with all
   images; rebuild; re-measure the fight fps.
2. If fps improves as expected, resume the native D3D 3D work (view routing, auxiliary
   passes) knowing the perf ceiling is lifted.
