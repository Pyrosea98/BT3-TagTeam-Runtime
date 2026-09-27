# BT3-Recomp — Native renderer at the engine seam

Status: **design 2026-09-25; Phase 0, and Phases 1 to 3a for the stage program, built and
measured the same day**, see `docs/SEAM-PHASE0-BATCHES.md`. Character programs and the
GPU path are not built yet. It records
what was measured, what was read out of the recompiled code, the architecture
decided on, and the phases with their exit criteria. It is the standing handoff for
this work: read it in full before touching the render path.

## Why

The goal is to stop emulating the PS2 render pipeline for the game's 3D geometry
and draw it natively on the GPU: characters, stage and effects go from the game's
own mesh data to Vulkan, with skinning in a vertex shader. VU1, VIF1 unpack and the
GS rasteriser stop running for everything the native path covers. That is what
lets the game render at any resolution and frame rate at the cost of a modern
GPU draw call rather than a PS2 emulation.

This is not a fix for a measured bottleneck on a fast machine. On 2026-09-25 a
fight on the reference laptop (paraLLEl-GS, 1280x720, scale 1) was vsync-bound:

    game rate            30 fps (the fight's native rate, fps60 off)
    guest work           8 to 11 ms of a 33 ms frame
    frame loop           16.8 ms, of which present 16.3 ms
    KickWorker (VU1)     ~20% of one core
    GameThread (EE)      ~30% of one core
    GsThread             ~25% of one core
    [vu1jit] uncompiled  none; three programs on the native SSE kernels

So the native path is a capability change, not a rescue. It removes the per-pixel
and per-vertex emulation cost that scales with render resolution, and it removes
the VU1 worker from the low-end floor (docs/LOWCORE-2C-TESTS.md, 15 to 18 fps on
2c/4t) if that thread turns out to be the limiter there. Measure that before
claiming it.

## What the other project did (ghpc, Guitar Hero 2)

The sibling PS2Recomp port at `~/Desktop/ghpc` reached "arbitrary resolution,
uncapped frame rate" by putting a Vulkan renderer at the engine's `Rnd` layer.
Their handoff (`ghpc/NEXT.md`, `ghpc/notes/rnd-seam.md`) is worth reading. The
transferable lessons, with their evidence:

1. **A per-microprogram skip does not work.** They tried replacing one VU1
   program's MSCAL with a host transform and got 6x slower plus a broken game:
   a *different* program ran away to its cycle budget because it loaded a saved
   register frame from VU1 data memory that the skipped program should have
   written (`notes/evidence/2026-09-19-vu1-shared-state.txt`). Their rule:
   **skip everything or skip nothing.** The seam has to be above the packet,
   where no program is ever half-skipped.
2. **Capture backend inputs where the guest stages them, not where they are
   consumed.** Geometry was gone from the engine by draw time; they hooked the
   last writer.
3. **Verify the host transform against VU1's own output**, not by eye. They
   paired host-transformed vertices with the vertices VU1 actually kicked for
   the same mesh and required the median error under one GS subpixel (0.0625 px).
   That loop ran in about a second offline against captured fixtures.
4. **Coverage is incremental.** Whatever the native path does not cover keeps
   going through the emulated GS into the same frame.
5. Measurement discipline: control arm on the same binary, release builds only,
   prove a knob does what it says with a profile, three outcomes never two
   (PROGRESSED / SAME / REGRESSED / MEASUREMENT_FAILED).

ghpc had a symbolised debug ELF. BT3 has none. The next section is what replaces
those symbols.

## What BT3 actually does (read out of `games/bt3/work/output/`)

This is the finding that makes the seam tractable. All addresses are SLUS_216.78
USA. `gp = 0x304270`, vaddr = ELF file offset + 0xFF000.

### The EE never builds character vertices

Character and stage geometry is **authored on disc as ready-made VIF1 DMA lists**
(GIF tag, TEX0, STCYCL/UNPACK vertex streams, MSCAL/MSCNT). The EE does three
things per mesh:

1. allocates a 0x240-byte header packet from a bump allocator
   (`FUN_00100850`, cursor at `0x2FE898`);
2. fills a **34-quadword constant block** that VIF unpacks to VU1 data address 0:
   bone matrices, light basis, projection and camera matrices, the TEX0 value;
3. emits a **DMA CALL tag into the model's own list** and an `MSCALF`.

`sub_00123278` (0x123278) is the exact shape:

    p[0x00] = 0x50000023   DMA CALL, QWC 35
    p[0x04] = list & 0x0FFFFFFF   target = the model's prebuilt VIF list
    p[0x08] = 0x10000000   FLUSHE
    p[0x0C] = 0x6C220000   UNPACK V4-32, NUM 34, VU addr 0
    p[0x10..0x22F]         34 qw, filled by the caller (table below)
    p[0x230] = 0x15000000  MSCALF
    p[0x234] = 0x03000022  BASE 0x22
    p[0x238] = 0x020001EF  OFFSET 0x1EF

`FUN_00123370` is the same with the list at `meshHeader + 0x60`. Siblings for the
other VU1 programs: `FUN_00123468` (11 qw), `FUN_00123130` (13 qw, takes an f12
scalar), `FUN_001236b0` (4 qw), `FUN_00123cd0` (10 qw).

### The constant block (packet+0x10 == VU1 qw 0)

| VU1 qw | contents | written from |
|---|---|---|
| 0..3 | bone matrix A | `bone+0x10` via `sub_00120230` |
| 4..7 | bone matrix B | `bone+0x50` via `sub_00120230` |
| 8, 9 | two vectors | `mesh+0x10`, `mesh+0x20` (bone pivots, see kernels) |
| 10..13 | light / ambient basis | `func_120728(dst, entity+0x10, 0x2FC2C0, ...)` |
| 14 | zero, or 0x15000000 in the `FUN_00123130` variant | |
| 15..18 | projection matrix | VU0 vf24..27 via `sub_00121388` |
| 19..22 | second camera matrix | VU0 vf20..23 via `sub_001212F0` |
| 23 | constants (128.0, `entity+0x40`) | |
| 24 | constant quadword from `0x2C3440` | |
| 26 | `(tex_lo, tex_hi, 7, 0)`: the 64-bit TEX0 value | caller's a3 |

These are the same quadwords the native SSE kernels in
`ps2xRuntime/src/lib/ps2_vu1_native.cpp` read with `LQ n(vi00)`: for program
3b5dfe97, bone matrices at qw 0..3 and 4..7, normal transforms at qw 26..33,
projection at 14..17 and 18..21, pivots in vf30/vf31. The two readings agree on
the structure and disagree on some offsets; **the kernel listings are the
ground truth for what VU1 reads and the EE writes are the ground truth for who
fills them.** Reconcile per program before writing a shader (Phase 0).

### The draw loop

`sub_00111358` (0x111358, 0x430 bytes) is the per-model draw. It walks a linked
list of mesh records from `actor+0x44`, resolves each record's bone index through
the palette at `actor+0xD6C` (`sub_002505A8` is `palette[boneId]`), skips
invisible bones, picks the begin-batch builder, fills the constant block, and
advances by `record+0x00` (record size). Mesh record:

    +0x00 u32  size, next = this + size
    +0x08 u16  vertex/element count, 0 terminates
    +0x0A u16  bone index into actor+0xD6C
    +0x0C u16  flags
    +0x10/+0x20 qwords copied to VU1 qw 8, 9
    +0x60      the prebuilt VIF geometry list (DMA CALL target)

Call chain, top to bottom:

    frame drivers (0x25da58, 0x25d8d0, 0x262c30, 0x12b7f8, 0x12b9c0)
      FUN_0010ff40   scene draw root
        sub_0010FB80 -> FUN_00111c20   per character: uploads program (FUN_001232f8),
                                       then sub_00111358(actor, entity, mode 0, tex, hdr)
        sub_0010FC50 -> FUN_00111cf0   second pass (shadow / mode 1, 2)
        FUN_0010fd98                   effects: FUN_00114508 -> FUN_00113700 (procedural trails)
      frame end: sub_00100798  FINISH via DIRECT, END tag, buffer swap
                 -> FUN_00100b98  D1_TADR = list, D1_CHCR = 0x145  (the VIF1 kick)

Camera: `FUN_0023e6a0` loads vf24..27 from `cam+0x140` (projection) and vf20..23
from `cam+0x180`, and publishes the camera at `0x2FEBD0`.

Microcode upload: `sub_00100738` emits a DMA REF tag at raw `.vutext` (vaddr
0x2bf6b0..0x2c3380), which holds the programs as MPG packets. Eight uploader
functions at 0x1231e0..0x123f48 map 1:1 to programs, so **which VU1 program a
batch uses is known on the EE side from which uploader ran**, no hash needed.
`.DVP.ovlytab` lists 12 overlays; `games/bt3/vu1_programs.json` names 7. Entries
at 0x2bfea8, 0x2c1188 and 0x2c1990 are not in the json and must be accounted for.

### What the EE does emit itself

Only effect geometry: `FUN_00113700` builds a RET-terminated VIF sub-list with
three quadwords per vertex for trails, and the billboard chain
`0x132b60 -> 0x131478 (clip) -> 0x132e80 (emit)` writes camera-facing quads.
`FUN_00114860` is an in-game VIF-list walker that shows the on-disc vocabulary
(UNPACK V4-32 NUM 5 elements, 0x70000000 END).

## What the runtime already has

- **A function hook mechanism** with about 80 named BT3 addresses in
  `ps2xRuntime/src/lib/game_overrides.cpp` (`applyBt3SoundInitBypass`,
  `runtime.replaceFunction`). Hooks on `sub_00111358` and the 0x123xxx builders
  go in the same place.
- **Three documented VU1 programs** with byte-exact native kernels and their batch
  layouts (`ps2_vu1_native.cpp`): 3b5dfe97 two-bone skinned strip with a second
  toon pass, 925edd7c single-pass skinned strip, 1627a6cb strip with per-triangle
  clip and a software clipper. Four programs (4d070cb1, db3bf3bf, 664061aa,
  ccb6aa07) are undocumented; terrain walkers and the sky chunk are identified
  by signature only.
- **A per-kick oracle**: `PS2X_VU1CAP=<file>` records entry state, whole VU1 data
  memory, and every GIF packet the kick emitted, for one program at a time. The
  native kernels were validated on 2003 such kicks. This is the fixture format
  for verifying a shader.
- **EE to VIF source attribution**: `g_kickSrcMap` maps any VIF stream byte back
  to the guest address that produced it (`ps2_vif1_interpreter.cpp` `srcOf`).
- **One injection point for host packets**: `PS2Memory::submitGifPacket(Path1,
  data, size)`. Every backend (paraLLEl-GS, GL replay, software) accepts a
  host-authored GIF packet in stream order. There is no host draw-call API.
- **Input record and replay** (`PS2X_INREC` / `PS2X_INPLAY`) with a RAM hash
  determinism gate (`PS2X_DETHASH`), and a direct state jump to the Duel menu
  (`bt3MenuGoto`, `[netjump]`).
- **Present is already a separate layer**: paraLLEl-GS renders headless and the
  frame is read back and blitted; the settings overlay draws over it.

## Architecture

**Hook the engine at `sub_00111358` and the begin-batch builders. Treat the
on-disc VIF list as the mesh format. Do the VU1 program's math in a vertex
shader. Leave everything else on the emulated path until it is covered.**

Three layers, built in this order because each is verifiable on its own:

### Layer 1: mesh import
A VIF-list parser turns a model's prebuilt list (`meshHeader+0x60`) into a host
mesh once: vertex arrays (position with weight in w, normal, texcoord, or
colour for 1627a6cb), the GIF tags and TEX0 values it carries, the MSCAL entry
points it uses. Cached by list address plus a hash of the bytes, invalidated
when the loader rewrites that memory. `FUN_00114860` documents the opcode
vocabulary; the VIF1 interpreter in the runtime already implements all of it and
can be run in a "record instead of execute" mode to build the parser cheaply.

### Layer 2: native draw
A hook at each begin-batch builder captures the 34-quadword constant block after
the guest fills it (at the return of `sub_00111358`'s per-record step, or at
`func_112A30`), pairs it with the imported mesh, and records a draw. When native
draw is on, the guest's VIF1 chain for that batch is suppressed at the DMA
CALL. The draw runs the program's transform in a vertex shader: for 3b5dfe97,
`world = w * A(P - pivotA) + (1 - w) * B(P - pivotB)`, then projection, then
the GS viewport mapping, then per-vertex CLIP flags to ADC. The kernels in
`ps2_vu1_native.cpp` are the reference implementation, line for line.

### Layer 3: output
Two options, in order of risk:

- **3a, GS primitives.** The host transform emits the same GIF packet VU1 would
  have kicked and submits it through `submitGifPacket(Path1)`. No backend
  change. This deletes VU1 and VIF unpack, keeps paraLLEl-GS as the rasteriser,
  and is verifiable byte for byte against `PS2X_VU1CAP`. It is the fastest
  route to "VU1 never runs in a fight" and it is a complete, shippable state.
- **3b, native rasterisation.** The transform stays on the GPU and the geometry
  is drawn by a Vulkan pipeline that writes into the same framebuffer
  paraLLEl-GS uses, with GS-equivalent depth, alpha and texture state. This is
  what unlocks higher-than-GS precision and true resolution independence. It
  needs a backend seam in paraLLEl-GS (or a replacement renderer) and it is
  where the multi-pass feedback effects (shadow silhouette into fbp 336, DoF,
  outline) have to be honoured.

3a first. 3b is the target and is only started once 3a is byte-exact, because
3a is also the oracle for 3b.

### What stays emulated
HUD and 2D sprites (`0x218848` dispatcher, `0x109508`), movies, the procedural
effects (`FUN_00113700`, billboards) until Layer 2 covers them, the shadow
silhouette pass until its program is identified, and any batch whose program has
no shader. The frame is one GIF stream, so covered and uncovered draws interleave
in the order the guest issued them.

## Phases

One exit criterion each. Do not start the next before it holds. Every speed
claim from a release build, with a control arm on the same binary.

### Phase 0: name the batches
Instrument, do not change behaviour.

- Hook `sub_00111358` entry and the begin-batch builders. Log per record: actor,
  mesh record address, list address, bone index, which builder ran, the TEX0
  value, and the 34 quadwords at the end of the fill. Attribute each subsequent
  MSCAL to its batch with `g_kickSrcMap`.
- Run `PS2X_GIFCENSUS=1 PS2X_VU1COST=1` in a 1P fight and a splitscreen fight.
  Produce the table: program hash, uploader function, builder function,
  batches per frame, primitives per frame, what it draws. Include the three
  overlays missing from `vu1_programs.json`.
- Reconcile the constant-block offsets between the EE writes and the kernel
  listings for each program.

**Exit:** a document in `docs/` listing every VU1 program seen in a fight with its
uploader, builder, batch layout and per-frame cost, and zero unattributed MSCALs
over a 60-second fight.

*2026-09-25: holds for a 1P fight on one stage (`SEAM-PHASE0-BATCHES.md`).
Still needed: a second stage, splitscreen, a cutscene, the menus, to cover the
three programs and the shadow passes that did not run.*

### Phase 1: mesh import
- Parser for the on-disc VIF list, driven by the runtime's own VIF1 decoder in
  record mode. Cache keyed by list address and content hash.
- Dump one character's meshes to a debug OBJ and look at them.

**Exit:** every batch in a fight resolves to a cached mesh; cache hit rate over
99% after the first second; a dumped character is visibly that character.

### Phase 2: host transform, verified
- Implement the transform for 3b5dfe97 on the host (CPU first, SIMD is fine),
  reading the captured constant block and the imported mesh.
- Compare against `PS2X_VU1CAP` fixtures offline: the tool loads a capture,
  runs the host transform, matches vertices, prints median and max error in GS
  pixels, and ends in a verdict. Exit 0 only under 0.0625 px median; exit 2 if
  there are no pairs. A run with no oracle is never a pass.
- Repeat for 925edd7c and 1627a6cb. Then the four undocumented programs, each
  of which needs its listing read and a reference model first.

**Exit:** the offline verdict is SUBPIXEL for every program that draws in a fight,
on captures from at least two stages and two characters.

### Phase 3: native draw, GS output (Layer 3a)
- `PS2X_NATIVE_DRAW=2`: host transform runs and submits nothing; VU1 draws.
  This is the correctness arm and the fixture capture mode.
- `PS2X_NATIVE_DRAW=1`: host submits the GIF packet through `submitGifPacket`
  and the batch's DMA CALL is skipped. Skip the whole batch or none of it.
- Same-binary control arm. Metrics: `[vu1cost]` runs per second must drop to
  zero for covered programs, frame signature must match the control frame,
  `[logicrate]` must be unchanged.

**Exit:** a full fight with `[vu1jit]` and `[vunative]` reporting zero runs for
covered programs, identical frame hashes to the control on a recorded input
replay, and no regression on the 2c/4t floor machine.

### Phase 4: GPU transform and native rasterisation (Layer 3b)
- Move the transform to a vertex shader; the Phase 2 tool becomes a GPU test by
  reading vertices back.
- Add a host-geometry path into the backend: draw into paraLLEl-GS's framebuffer
  with GS-equivalent state, or stand up a renderer beside it. Decide this after
  Phase 3, with the multi-pass effects mapped.
- Render scale applies to native geometry with no GS quantisation.

**Exit:** native geometry at render scale 4 at 60 fps on the reference laptop,
no visual regression against Phase 3 at scale 1 on a recorded replay, fps60 mode
holding 60.

### Phase 5: coverage
Effects, shadows, HUD, movies: each either gets a native path or is proven to
be fine on the emulated one. **Exit:** nothing in a fight, a story cutscene, or
the menus renders differently from the control at scale 1.

### Phase 4 as built (2026-09-26): a native GS front-end, not a second GS

The decision after Phase 3 was "stand up a renderer beside paraLLEl-GS"
(`PS2X_SEAMVK=1`, `ps2xRuntime/src/lib/ps2_seamvk.cpp`, `ps2_seamgs.cpp`,
shaders in `src/lib/seamvk/`). What it is and is not:

- **Everything the EE builds itself is interpreted natively.** `ps2_seamgs.cpp`
  is a stateful GIF interpreter fed by the arbiter with every packet on every
  path in stream order (tag state persists across DIRECT packets). It keeps
  both GS contexts, a 4 MB VRAM mirror for host-to-local uploads and local
  copies, decodes palettes and textures from that mirror (cache keyed by TEX0
  plus the write stamps of the pages read; a stale entry gets a fresh slot,
  because BT3 uploads several border pieces to one address between sprites),
  and assembles vertex kicks (sprite, strip, fan, list; flat = last vertex)
  into a per-frame draw list. Menus, HUD, boot popups and text come this way.
- **The seam's meshes ride the same stream.** `'SVKD'` carries mesh plus
  constants; the host-transformed packet follows as `'SVKG'`, whose register
  writes (TEX0 per chunk) are applied before the draw is emitted with the state
  of that moment. The GS backend still receives that packet as PATH1.
- **Renderer.** One RGBA8 target per FRAME base, one D32 per ZBUF base, fixed
  logical 1024x512 GS pixels at `PS2X_SEAMVK_SCALE`. Alpha is stored as
  As/128 so the fixed-function blend factors are the GS's without dual-source
  blending (Granite does not enable it). CRTC compose from PMODE/DISPFB/DISPLAY
  at the swap; two circuits on one frame is one opaque blit.
- **Measured GS feature set** (tools/gscensus.py over `PS2X_GS_RECORD`
  streams, which the arbiter now records under the exclusive backend):
  menus are ~870 textured sprites/frame, T4/T8 CLUT (CSM1), CT32 uploads,
  blend (0,1,0,1), clamp/repeat, bilinear; no feedback. The fight adds ~47k
  PATH1 vertices/frame (owned by the seam), HUD sprites, and a post chain that
  reads render targets in other formats: Z24 at 0x1c00 as PSMT8H/PSMT8,
  rendered palettes at 0x2a00/0x3c00 used as CLUTs, CT32 re-viewed as CT16.
- **What it deliberately does not do:** emulate that post chain by aliasing
  render targets. The old GL renderer tried exactly that (per-fbp FBOs with
  view relocation and VRAM writeback) and was replaced by paraLLEl-GS because
  it could not be made exact (README.md, docs/ALTGL-RAYLIB-REMOVAL.md). The
  cel outline, depth-of-field and glow passes are BT3 effects, so the plan is
  to implement them as native passes over our own colour and depth targets,
  the same way the geometry was taken at the engine seam rather than re-drawn
  from GS packets. Until then paraLLEl-GS keeps running as the VRAM-exact
  reference, and `PS2X_SEAMVK` unset presents its frame.
- **Known gaps at this commit:** DATE (destination alpha test, the HUD bars),
  AFAIL modes other than KEEP, points/lines, PMODE alpha with two different
  frames, mipmaps, TEXCLUT/CSM2, dithering, wrap of draws past FBW, and any
  texture read from pages the game drew (counted as "from drawn pages").

## Rules that hold every session

- Measurement before theory. Profile with `PS2X_EEPROF=50 PS2X_FRAMEPROF=1`
  before and after; the KickWorker share is the number that says VU1 is gone.
- Pre-declare the metric before a run. Say what outcome the change makes
  impossible.
- Skip everything for a batch or nothing. Never half a program.
- Never clamp a vertex, reject it. A clamp moves geometry and bills a
  full-screen scan.
- Capture at the point where the data is complete: after the guest fills the
  block, before the DMA kick.
- Every hook keeps the generated body verbatim and adds a tap. Overrides go in
  `game_overrides.cpp`, never in `games/bt3/work/output/`, which is regenerated.
- Fixtures are the fast loop. A 4-minute game run is for confirming, not for
  finding a sign error.
- Check for a stray runner with `pgrep -x bt3-runner` before any timed run.

## Open questions to close in Phase 0

- Whether all 34 quadwords are consumed by the single UNPACK and the last
  quadword is reinterpreted as VIF codes, as the EE writes imply.
- Which uploader and builder pair with each of the four undocumented programs,
  and which draw the terrain, sky and shadow silhouette.
- Whether the bone palette at `actor+0xD6C` is stable across a frame or rebuilt
  per pass, which decides where the capture tap goes.
- The on-disc list's use of `STCYCL` and double-buffer bit 15 in UNPACK
  addresses, so the importer resolves vertex addresses the way VIF1 does.
- Whether the EE's two-matrix layout (qw 0..7) matches the kernels' pivot
  handling (`vf30`, `vf31` from qw 8, 9) for every skinned program.

## References

- ghpc method: `~/Desktop/ghpc/ghpc/NEXT.md`, `ghpc/notes/rnd-seam.md`,
  `ghpc/notes/evidence/2026-09-19-vu1-shared-state.txt`,
  `ghpc/notes/evidence/2026-09-12-bone-palette.txt`.
- Runtime assets: `ps2xRuntime/src/lib/ps2_vu1_native.cpp` (kernels and batch
  layouts), `ps2_vu1.cpp` (`PS2X_VU1CAP`, `[vu1cost]`, dispatch),
  `ps2_vif1_interpreter.cpp` (`srcOf`, MPG/MSCAL handling),
  `ps2_gif_arbiter.cpp` and `ps2_gs_pgs.cpp` (packet feed),
  `game_overrides.cpp` (hook installation).
- Recompiled functions named above: `games/bt3/work/output/<name>_0x<addr>.cpp`.
- Prior performance record: `docs/BOTTLENECK-INVESTIGATION.md` (Windows, GL
  replay, empty JIT table at the time; superseded by the 2026-09-25 profile).


## 2026-09-26 additions (native renderer)

- Destination-alpha test in-pass: the colour target is also its subpass's input attachment (Granite
  emits the colour self-dependency); a DATE draw reads its own pixel with `subpassLoad` after a
  by-region barrier. No pass break, no snapshot copy: 419 -> 5 passes per fight frame.
- FBA (TEST/FBA register): forces bit 7 of the alpha WRITTEN to the frame only. The blend factor
  As is the source alpha before FBA. The HUD's additive flashes (`(0,2,0,1)`, vertex alpha 0,
  FBA=1, DATE on the mask bit) add nothing but leave the mask bit; applying FBA before the blend
  made them add at full strength, which was the white HUD.
- Per-program w sign of the VU1 projections (stage/effects/two-pass character positive, single-pass
  character negative), clip against the program's clip matrix with gl_ClipDistance, saturating GS
  depth: the stage, near ground and characters render; the two-pass character program draws the
  host mesh once per distinct state (outline pass in context 1 hidden by the game's 64x64 scissor
  while the palette target is live, lit pass in context 2 with the toon ramp coordinates).
- Diagnostics: PS2X_SEAMVK_DUMPGAMEFRAME=<n> (draw list, register events, palettes, target/mirror/
  backend VRAM dumps with PS2X_SEAMVK_TEXDUMP), PS2X_SEAMVK_PROBE=x,y (every draw covering a
  pixel with its sampled texel), PS2X_SEAMVK_DATEDBG, PS2X_SEAMGS_WATCH, PS2X_SEAMGS_TEXTRACE,
  tools/run_watchdog.sh (runs under gdb, dumps threads on a hang).

## 2026-09-26 (evening): static-scene comparison via input replay; the ground shadow

Recording: the user played from boot into Ultimate Training with `PS2X_INREC=caps/ultimate.inrec`
(2076 game frames, fight from frame ~1630). Every run since replays it (`PS2X_INPLAY=<file>`, no
AUTOSTART) so native and paraLLEl-GS are compared on the identical static frame
(`PS2X_PGS_DUMP` writes pgs_/ref_ PNG pairs of the same swap; `PS2X_SEAMVK_DUMPGAMEFRAME=2150`
for the draw list). The replay landed in the fight on every run.

The ground shadow pipeline (per character, per frame): a 256x256 clear at 0x2a00 (fbw 4), the
character drawn again through the SINGLE-PASS program (prog 3) with an orthographic E matrix
(w = -862 constant, z = -9.4e6, XYOFFSET 1920,1920), colour 0x80 grey; then TEX0 reads 0x2a00 as
PSMCT24 with TEXA = ta0 0x80, AEM 1, ta1 0x80, and the effects program (prog 1) draws a 100x100
ground quad with projective texcoords (C matrix), atst NOTEQUAL 0, blend (0,1,0,1).
Three things were wrong in native, each pinned by a dump before the fix:
1. `seam.vert` negated w for prog 3 by program, so the orthographic pass became w < 0 and was
   Vulkan-clipped: an orthographic screen matrix (rows 0..2 have no w term) now takes its sign from
   row 3 instead of the program default.
2. prog 3's clip planes: the single-pass program computes clip flags but never acts on them
   (`charChunk1`: "this program never marks ADC"), and the silhouette relies on it (z far outside
   |w|). prog 3 now sets gl_ClipDistance to +1.
3. AEM was ignored for 24-bit reads in both `rtdecode.frag` and the CPU decoder: with TEXA
   ta0 0x80 / AEM 1 the cleared black area must read alpha 0 (that is what hides the quad outside
   the silhouette). Fixed in both.
The striped trapezoid over the left half of the native frame was NOT the shadow: the pixel probe
(`PS2X_SEAMVK_PROBE=120,300`) showed the post chain's 16-bit passes (draws into 0x2a00 as PSMCT16,
fbw 8, skipped as aliased-format) followed by their read-back as a PSMCT16 texture, which decoded
the target's stale 32-bit content (silhouette / downscaled scene) as 16-bit pixels. Interim guard:
a target that received a skipped aliased-format draw is `aliasedDirty` until its next own-format
draw, and a decode from a dirty target yields transparent black (stats: "rt decodes (N stale)").
The proper form is the GPU implementation of those passes.
Result: native == paraLLEl-GS on the static frame except the post chain (outline/ink on the far
character, depth-of-field tone), which is the remaining open item. `PS2X_SEAMVK_DUMPCONSTS=1`
prints the constants and per-vertex clip values of the first effect / off-screen character draws.

## 2026-09-26 (night): the post chain on the GPU; native outline and depth-mask steps

Three routes now exist for a post-chain step in the native renderer, in order of preference:
1. **Native step** (engine seam): the arbiter's `[postnative]` interception neutralises the step's kicks and calls
   `seamvk::onNativeStep(id)`; the front end records a kind-3 draw at that stream position and seamvk runs a
   fullscreen pass. Steps: 0 = depth mask (`depthmask.frag`, frame.A := Z24[15:8] from the depth image, one draw
   instead of the game's 64) and 5 = outline (`outline.frag`, ink alpha 0x80 / 0x30 from the edges of the depth
   ramp CLUT_0x3e8c[Ztop] at native resolution, both sides, radius PS2X_SEAMVK_OUTLINE=<core>,<fringe>, default
   2,3). Enable with PS2X_POSTNATIVE=0x21 PS2X_KICKPROBE=2 (2 = owner attribution only, no GIF census).
   The paraLLEl-GS reference keeps its own host pass for step 0 but loses the outline (its kicks are neutralised).
2. **alias16** (`alias16.frag`): any GS draw through a 16-bit FRAME view of a 32-bit target runs as a gather pass
   in the target's own pass (inverse 16-bit swizzle per dword half, triangles at pixel centres, GS blend / 16-bit
   FBMSK / ATE). Generic fallback for unpinned 16-bit steps; 64 single-triangle draws cost ~5 fps, which is why
   the depth mask went native.
3. Plain draws: alpha-only writes, Ad blends (now exact through the in-pass self-read), rt decodes.

Pinned along the way (see memory bt3-gs-sampling-conventions): attributes at pixel centres; 16-bit FBMSK converts
GSdx-style; a target keeps its own row width and 16-bit views have 64-px page rows; the GPU decode takes geometry
from the image; depth is fetched through the Z32 block table (CT32 ^ 0x18), the top byte through CT32.
Diagnostics: PS2X_SEAMVK_DUMPAT=<i>,... dumps every target just before draw i of the dump frame (device idle
wait); PS2X_SEAMVK_DUMPCONSTS=1 prints seam constants and per-vertex clip values; the draw lines carry mmag.
Result on the Ultimate Training replay: native == paraLLEl-GS incl. outline and shading; median fight fps 26.9
(29.8 before the post chain existed natively). Open: depth-of-field looks blocky at scale 2 (rt decodes are at GS
resolution) -> native DoF step or native-res decodes; the far character's ink/blur is lighter than the reference.

## 2026-09-27: what paced the native renderer, and the defaults now

Measured on the Ultimate Training replay (fight-only medians; the laptop must be plugged in, on battery the game
thread alone drifted 6 -> 10 ms between identical runs):
- Texture decode on the CPU was 8-11 ms/frame on the GS thread -> `rtdecode.frag FROM_VRAM`: the front end
  snapshots a texture's pages when their write stamp moved (8 KB each, in stream order) into a 4 MB storage
  buffer and the decode runs on the GPU (PS2X_SEAMVK_GPUDECODE_MIN, default 65536 texels; smaller textures
  still decode on the CPU: 2.6 ms). 
- The paraLLEl-GS reference ran in full beside the native renderer: 4.1 ms of packet processing + 4.9 ms of
  flush/scanout per frame on the GS thread plus its GPU work. Off by default now (PS2X_SEAMVK_REF=1 keeps it
  for side-by-side dumps); the swap handler then advances Granite's frame context itself (the backend's
  frame-boundary flush did that before; without it every submit blocked in the driver).
- The CPU scratch raster (2.2 ms) is redundant since targets decode on the GPU: off by default
  (PS2X_SEAMGS_SCRATCH=1). The draw State was rebuilt per primitive: now cached until a register write.
- The 60 fps mode is ON in the saved settings ("[fps60] ON" in the log): what looks like "43 fps in a 30 fps
  fight" is that mode running as fast as the pipeline allows. The frame gate's fight latch now also counts the
  seam's mesh chunks (its VU1 programs are skipped, so the old VU1-pair latch never fired).
- Result: wall 40 ms -> 25 ms per game frame; 60 fps mode median 43.5 fps (was 21-25); GS thread parse 8.8 ms
  + record 4 ms + GPU wait 8 ms: the GPU is now the limiter.
Defaults with PS2X_SEAMVK=1: light kick probe, PS2X_POSTNATIVE=0x21 (depth mask + outline native), no
reference, no scratch raster. Launch: PS2X_SEAMVK=1 and nothing else renderer-related.

## 2026-09-27 (night): where the GPU time went, and the 60 fps cap

The native renderer was GPU-bound at ~17 ms/frame at scale 2 and, oddly, ~16 ms at scale 1. Attribution (PS2X_SEAMVK_GPUTIME=2/3
per-category timestamps, PS2X_SEAMVK_SKIP=<mask> to drop parts of the frame) put 13-15 ms in the 16 decodes per frame that
rebuild post-chain textures from the frame/depth targets (rtdecode.frag, non-VRAM path) -- 0.8 ms each for a 512x512 output.
Not the layout transitions (targets in GENERAL layout: same), not the pass breaks (VRAM decodes hoisted before the first pass,
58 -> 34 passes: same), not the scatter (a dummy source at the same pass sizes: 1.3 ms). The SPIR-V headers had been
generated without `-O`; with `tools/shaders.sh` (glslc -O) the same decodes cost 1.6 ms and the GPU frame is 6.2 ms.
Result on the Ultimate Training replay, fps60 mode: native 60 fps (vsync cap, GPU wait 0) at 1.56 cores / 40% GPU;
paraLLEl-GS 60 fps at 0.92 cores / 75% GPU. GPU: native wins. CPU: the GsThread parse (9 ms/frame: CPU texture decode 2.3,
scratch raster 1.9, GIF parse + state + page hashing ~4.8) and seamvk record (2.4 ms) are the excess; next.
Fight-load freeze (5 of ~14 runs tonight): the GameThread spins in game code (FUN_0024c958 / 0x256e00 / 0x2baae8) with the
DMA counter frozen and every renderer thread idle -- guest-side, pre-existing; `REGWD=1 tools/regcheck.sh` leaves the dump.
Later that night: PS2X_SEAMVK_GPUDECODE_MIN=0 (every texture on the GPU, -0.16 cores) is NOT the default: small textures at a
block-aligned base decoded white (their pages wrap into the next page; fixed: the page after a mid-page base is listed too) and
the opponent's HUD portrait still vanished afterwards even with cpuTargetOk decoding a GPU texture on the CPU on demand -- open.
Default stays 65536 texels; TEX0 same-value dirty skipping was tried and reverted (no lookup reduction, stale boots texture).
