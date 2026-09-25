# Native render seam, Phase 0: naming the batches

Status: **measured 2026-09-25**, one run each of a 1P fight (auto-advanced, first
Dragon History battle, ~140 s in the fight) on the reference laptop, release
build, paraLLEl-GS. Instrument: `PS2X_SEAMPROBE=1` (`ps2xRuntime/src/lib/ps2_seamprobe.cpp`),
with `PS2X_GIFCENSUS=1` and `PS2X_VU1COST=50`. Plan and rationale in
`docs/NATIVE-RENDER-SEAM.md`.

## Result

Every VU1 microprogram start in the fight is attributed to the EE function that
built its batch. Over the last 5 s window of the run:

    mscal attributed = 587100   unattributed = 0
    constant-block unpack matched = 29400   unmatched = 1650 (all per-frame program uploads, see below)

The probe matches a batch by hashing its constant block in guest memory when the
display list closes (`sub_00100798`) and again when VIF1 unpacks it to VU1
address 0; batches whose header carries no block (CALL + MSCALF only) are matched
by stream order. Both paths are exact on this run.

## The table

Per second, in the fight, 30 game frames per second. "starts" are MSCALF at pc 0;
"continues" are MSCNT.

| builder | called from | program (md5 / fnv low32) | batches/s | starts/s | continues/s | verts/s (census) | VU1 cost share | draws |
|---|---|---|---|---|---|---|---|---|
| `sub_00123278` 0x123278 | `sub_00111358` per-model loop, ra 0x1114ac, mode 0 | 3b5dfe97 / dc59311d | 2940 | 2940 | 29817 | 597k (two passes) | 29% | characters, textured pass + toon pass |
| `FUN_00123468` 0x123468 | `sub_00111358`, ra 0x111630, mode 0 | 925edd7c / 75b22f69 | 2940 | 2940 | 29817 | 299k | 19% | characters, single pass (the second pass of the same meshes: same list address, same bone matrices) |
| `sub_00123588` 0x123588 | `sub_00115DE0` stage dispatcher, ra 0x115ee4 / 0x115ff4, no actor | 1627a6cb / 0237b48e | 7199 | 7199 | 44515 | 292k | 52% | stage geometry, per-vertex float colour, clip tested |
| `sub_00123DC8` 0x123dc8 | `FUN_00114508` effects, ra 0x1145e0, actor set | 4d070cb1 / b8434850 | 60 | 60 | 120 | 0.8k | 0.4% | procedural effect geometry (trails, aura) |

Per frame that is about 98 character batches per pass, 240 stage batches and 2
effect batches. The two character builders fire in lockstep: every mesh record
gets a 0x123278 batch and a 0x123468 batch with the same CALL target.

Not seen in this fight: `db3bf3bf`, `664061aa`, `ccb6aa07` (three of the seven
manifest programs), the mode 1 and mode 2 passes of `sub_00111358` (shadow and
second pass; `FUN_00111cf0` never ran), and any batch with a nonzero `a3`
texture argument. The builders `FUN_00123370`, `FUN_00123130`, `FUN_001236b0`,
`FUN_00123cd0` were hooked and never called. All of these need a second
scenario: a different stage, splitscreen, a story cutscene, and the menus.

The unmatched address-0 unpacks are exactly the per-frame **program uploads**
and their headers, 2 per frame each (double buffered):

| unpack size | uploader | program |
|---|---|---|
| 34 qw | `FUN_001232f8` | 3b5dfe97 |
| 19 qw | `FUN_001234e8` | two programs, 0x2bfea8 and 0x2c01c8 (925edd7c) |
| 12 qw | `sub_00123600` | 1627a6cb |
| 17 qw | `FUN_00123e40` | 4d070cb1 |
| 13 qw | `sub_001231E0` | shadow silhouette program |

So the program upload header seeds VU1 qw 0..N once per frame, and per-batch
blocks overwrite the low quadwords. That matters for the layouts below.

## Constant-block layouts, measured

### 3b5dfe97 (builder 0x123278): UNPACK V4-32, NUM 34, to VU1 qw 0

    qw 0..3    bone matrix A: three rotation rows (w = 0), translation row (w = 1)
    qw 4..7    bone matrix B, same shape
    qw 8, 9    pivot vectors, w = 1 (both equal on a one-bone mesh)
    qw 10..13  light basis: x column of a direction (0.287, 0.094, -0.953) in rows 10..12, row 13 = (0,0,0,1)
    qw 14..17  projection matrix E (screen), w lane carries the perspective row
    qw 18..21  projection matrix F (clip test)
    qw 22, 23  (128,128,128,128): pass A colour, FTOI0 gives 0x80 RGBA
    qw 24..33  NOT WRITTEN BY THE BATCH FILL. Content is stale display-list bytes
               (DMA CNT tags, DIRECT codes, A+D GIF tags, register writes) and
               differs between two batches at different packet addresses.

The kernel in `ps2_vu1_native.cpp` reads qw 26..29 and 30..33 as the two
"normal transforms" C and D. On this evidence those reads see stale bytes.
Either the per-frame 34-qw upload header supplies them and the per-batch NUM
34 unpack is masked or shorter than it looks, or the C/D products only feed a
path that does not reach the picture. **This must be settled in Phase 2 before
writing the shader**: capture with `PS2X_VU1CAP` and compare the toon-pass ST
output against a transform that ignores qw 26..33.

### 925edd7c (builder 0x123468): UNPACK V4-32, NUM 11, to VU1 qw 0

    qw 0..3    bone matrix A
    qw 4..7    bone matrix B
    qw 8, 9    pivots
    qw 10      (128,128,128,128)

The kernel reads E at qw 11..14 and F at 15..18. Those are not in the batch
block: they come from the per-frame 19-qw upload header (`FUN_001234e8`), which
is why that header is 19 qw. So for this program the camera is per frame, the
bones per batch.

### 1627a6cb (builder 0x123588): no block

Header is `50000001 <list> 10000000 00000000 15000000 0300008d 020001b4 0`:
DMA CALL of 1 qw, FLUSHE, MSCALF, BASE 0x8d, OFFSET 0x1b4. The matrices M1 and
M2 the kernel reads at qw 0..7 come from the per-frame 12-qw upload header
(`sub_00123600`): the stage is drawn with one view-projection for the whole
frame and no per-batch constants. This is the cheapest program to move first.

### 4d070cb1 (builder 0x123dc8): no block

Same header shape with BASE 0x92, OFFSET 0x1b2; constants from the per-frame
17-qw header (`FUN_00123e40`).

## The on-disc geometry list

Two families, both confirmed by reading the CALL targets:

**Character meshes** (CALL from 0x123278 / 0x123468, list at the mesh record + 0x60):

    600000fc 00000000  6c058000 00000001   DMA RET tag (QWC 0xfc), then VIF: UNPACK V4-32 NUM 5 to TOPS+0, NOP
    00000001 10000000 0000000e 00000000    qw[0] A+D GIF tag, NLOOP 1
    21413480 20078006 00000006 00000000    qw[1] TEX0 (pass A) + register 0x06
    21313d40 2007a08e 00000007 00000000    qw[2] TEX0 (pass B) + register 0x07
    ...                                    qw[3], qw[4] geometry tags (x = count), then vertex UNPACKs

This is exactly the "batch layout at TOP" the 3b5dfe97 kernel documents
(`ps2_vu1_native.cpp:91-95`), so the list can be parsed with the kernel's own
description of the header and the VIF1 interpreter for the vertex streams.

**Stage and effect lists** (CALL from 0x123588 / 0x123dc8):

    600001db 00000000  6c018000 00008005   DMA RET tag, UNPACK NUM 1 to TOPS+0 (double-buffer bit 15 set)
    302ec000 00000412 00000000 17000000    header qw
    6c038000 00008001 10000000 0000003f    UNPACK NUM 3 ... geometry, 3 qw per vertex, chunked, MSCNT between chunks

Same shape as the RET-terminated sub-list `FUN_00113700` builds for trails, so
the stage lists on disc and the procedural effect lists share one format.

## Where this leaves the plan

- Phase 0 exit holds for a 1P fight on one stage: every batch named, every
  start attributed, layouts recorded. It does not hold yet for the three unseen
  programs and the shadow passes. Run the probe on a second stage, splitscreen,
  a cutscene and the menus before calling Phase 0 done.
- Phase 1 (mesh import) can start on the character list format now; the
  header is documented and the vertex stream is plain UNPACK V4-32.
- The stage program 1627a6cb is the largest VU1 cost (52%) and has the simplest
  contract (one per-frame matrix pair, no per-batch block, no skinning). It is
  the right first target for Phase 2 and 3, before skinning.
- Two open questions carry into Phase 2: the stale qw 24..33 reads in 3b5dfe97,
  and what the header qw `302ec000 00000412 0 17000000` means for the stage
  program (it is not a GIF tag).

## How to reproduce

    cd "<deploy tree>"
    PS2X_EXEDIR=$PWD PS2X_ASSETDIR=$PWD/assets LD_LIBRARY_PATH=$PWD/assets/lib \
    PS2X_LOGFILE=/tmp/seam.log PS2X_AUTOSTART=1 PS2X_FORCE_SKIP=600 \
    PS2X_SEAMPROBE=1 PS2X_GIFCENSUS=1 PS2X_VU1COST=50 \
    timeout -s INT 200 <build>/ps2xRuntime/ps2EntryRunner $PWD/data/SLUS_216.78

Then read the `[seamprobe]` reports (every 5 s), the `[seamprobe] block` and
`[seamprobe] noblock` dumps (first two per builder and mode), `[gifcensus]`
and `[vu1cost]`.

## Phases 1 to 3a for the stage program, measured 2026-09-25 (evening)

Built the same day in `ps2xRuntime/src/lib/ps2_seammesh.cpp` (`include/runtime/ps2_seammesh.h`):

- **Importer** (`seammesh::get`): parses the CALL'd on-disc list (DMA RET, unmasked
  UNPACK V4-32, MSCNT, NOP) into chunks of header + vertices, cached by list
  address with a content hash re-check on every use. `PS2X_SEAMDUMP=<dir>` writes
  the first meshes as OBJ. In a fight: 1788 meshes cached, 0 re-parses, 0 failures.
- **Host transform** (`seamxform::stageChunk`) for 1627a6cb, mirroring the native
  kernel instruction for instruction. `PS2X_SEAMVERIFY=1` runs it beside VU1 and
  compares the kicked bytes: **0 mismatches over 300k+ chunks**, 0 mismatches
  between imported vertices and what VIF landed in VU1 memory. About 2.3% of chunks
  need the software clipper and are left to VU1.
- **Skip** (`PS2X_SEAMSKIP=1`): the host submits its packet through
  `submitGifPacket(Path1)` and VU1 is not run for that chunk. Whole chunks only;
  the MSCALF setup run and clipper chunks still go to VU1.

User-driven check: two fights in skip mode, terrain looked normal, no guest crash,
no write into low memory, no restart. Auto-advanced runs sometimes hit the game's
own corrupted-fight reset in both arms; do not use `PS2X_AUTOSTART` for skip-mode
acceptance runs.

VU1 cost in a fight (`[vu1cost]`, ms of VU1 execution per second, same laptop):

| program | control | skip | note |
|---|---|---|---|
| 1627a6cb stage | 118.7 | ~30 | remaining = setup runs + clipper fallbacks |
| 3b5dfe97 characters | 65.6 | 54.1 | unchanged (scene noise) |
| 925edd7c characters | 44.9 | 34.5 | unchanged (scene noise) |
| total | ~230 | ~118 | |

The KickWorker thread share did not move measurably: VIF unpack of the stage
lists still runs, and the thread is far from saturated on this machine. The
next cost to remove is the two character programs (skinning), and the next
structural step is dropping the VIF unpack for owned batches, which needs the
CALL to be skipped at the DMA level.

## Stage program complete: zero VU1 runs (2026-09-25, late)

The host now owns the whole 1627a6cb batch: the MSCALF setup, the setup call
that stores the fan tag, every strip chunk, and the software clipper. The
clipper was ported from the listing (`ps2xRuntime/tools/vudis.py` on the raw
image): a 1.005-scaled copy of the straddling triangle, Sutherland-Hodgman
against six planes in clip space in program order (z-, z+, x-, x+, y-, y+), a
fan emitted through a third per-frame matrix at qw 8..11, an empty EOP tag
before each fan, and the CLIP-flag history carried through so the strip's later
FCOR tests see what the VU would.

Verify: **0 mismatches** over four 5 s windows including ~9500 clipper chunks per
window, once two non-issues were normalised in the compare: the ST quadword's w
lane (a stale VU register) and signed zeros in ST x/y/z (the VU zeroes vf31 by
multiplying it, which keeps the sign the previous program left there, and adds
it to copy vertices; the GS treats -0 and +0 alike). Two real bugs were found
and fixed on the way: a one-bit-off immediate (the VU's one third is
0x3eaaaaaa, not the nearest float) and a stream reader that refused payloads
starting in the DMA tag.

Skip (`PS2X_SEAMSKIP=1`), auto-advanced fight, 170 s: `[vu1cost]` reports no
1627a6cb runs at all; ~100k stage chunks per second submitted from the host;
no guest crash, no restart, no write into low memory; screenshot mid-fight
shows terrain, trees, rocks and sky correct.

Remaining VU1 in that fight, ms of VU1 per second: 3b5dfe97 55, 925edd7c 36,
4d070cb1 2, ccb6aa07 4 (this last one, the cutscene/prefight program, was not
seen in the earlier fight). Next: the two character programs.

## Character programs complete (2026-09-26)

3b5dfe97 (two-pass skinned, toon) and 925edd7c (single-pass skinned) are host-owned in
`ps2_seammesh.cpp` (`seamxform::charChunk2/charChunk1`, constants from `charConsts`).
Both loops are ports of the native kernels; the setup runs are folded into the
constants. Three things had to be right for byte-exactness:

- **qw 26..33 are written by the VU's own setup run**, not by the EE: C = light basis
  (qw10..13) applied to each row of bone matrix A, D likewise for B, row 3 = light
  row 3. This closes the Phase 0 open question; the batch fill never needed them.
- **Rounding mode.** `VU1Interpreter::execute` (MSCAL) runs under round-toward-zero
  with FTZ/DAZ (`VuRoundScope`, `PS2X_VUROUND`); `resume` (MSCNT) does not. The
  setup-derived constants are therefore computed under RZ and the chunk loops under
  round-to-nearest. Found by reproducing one mismatching value offline.
- **Identical constant blocks.** Meshes on one bone produce byte-identical blocks;
  the probe now matches equal hashes in list order (FIFO), which is the VIF order.

The CLIP-flag history the FCAND tests read at a chunk's first vertices is carried on
the host across owned runs and re-seeded from the real VU register after any run the
host does not own (`ps2xSeamVu1Clip`).

Verify: **0 mismatches** for all three programs over three windows; derived C/D
identical to VU memory; 0 import mismatches. Skip run in a fight: `[vu1cost]`
reports only 4d070cb1 (~1-4 ms/s) and ccb6aa07 (~4-5 ms/s); no crash, no restart,
no low-memory write; screenshot correct (fighters with toon shading and outlines,
stage, sky).

VU1 execution time in a fight went from ~230 ms/s to ~6 ms/s. The KickWorker
thread's sample count did not fall: the host transforms cost about what the SSE
kernels cost, the VIF unpack of every list still runs, and that thread is far from
saturated on this machine. The remaining VU1 work is the effects program and
ccb6aa07; the remaining CPU work is the unpack, which goes when the CALL is skipped
at the DMA level.

## All five fight programs host-owned (2026-09-26)

ccb6aa07 (prefight / mode-1 pass; builder 0x123370) and 4d070cb1 (effects; builder
0x123dc8) are ported in `ps2_seammesh.cpp` (`charChunk3`, `effectsChunk`).

- ccb6aa07 is the two-pass character program with a 2D toon lookup per vertex
  from the light basis, the second pass's TEX0 from the EE-filled qw26, and an
  early-out that emits an empty 16-byte tag when the first normal's w bits are
  zero (its trailing words are stale output memory; treated as don't-care).
- 4d070cb1 is the stage program with 4 qw per vertex [P (w = int16 flag), normal,
  colour, T], texcoords generated per vertex from M4 = qw12..15 scaled by qw16.x
  and written back into the vertex's T slot (the clipper reads them from there),
  ADC forced by the flag, no NOP-packet kick, and the same clipper and fan.
- The importer now carries the raw vector count per chunk; consumers take the
  vertex count from the geometry tag as the VU does.

Two more runtime facts had to be matched exactly:

- **DIV by zero** in the JIT takes its sign from the numerator alone
  (`vu1_jit_ops.inc`): +max for a >= 0 including -0. The stage clipper used a
  two-operand sign and mismatched on ~80 chunks per fight.
- **Effects lists are procedural.** `FUN_00113700` rebuilds them every frame at the
  same address and size, so the cache's cheap head/tail check returned last
  frame's vertices. Lists from the effects builder are now re-hashed in full on
  every use (`seammesh::get(..., volatileList)`).

Verify: **0 mismatches for all five programs over 21 consecutive 5 s windows**
(~590k matched chunks per window at the end), 0 import mismatches.

Skip run, auto-advanced fight, 170 s, all five programs owned: `[vu1cost]` printed only
twice in the whole run, once reporting 0 runs and once a brief 115 runs/s of 664061aa
(0.3 ms/s, a transition-screen program not yet ported); during the fight itself VU1
executed nothing. ~790k chunks per 5 s submitted from the host, no crash, no restart,
no low-memory write, screenshot correct (fighters mid-combo, stage, sky, HUD).
KickWorker samples in the fight: 42 to 47 per 10 s, against 73 to 97 in the control.

**VU1 in a fight is at zero.** What remains on VU1 anywhere: 664061aa and db3bf3bf
(menus / transitions, not yet seen in a fight) and the shadow-silhouette program.

## Lists no longer unpacked by VIF (2026-09-26)

In skip mode the VIF1 DMA chain walker (`ps2_memory.cpp`, CALL tag) asks
`seam::dmaCallSkip` whether the CALL target is a mesh list of an owned batch
(`seamprobe::builderOfList` + a successful import). If so the tag's own data (the
constant block and the MSCALF) is kept and the walk continues past the CALL instead of
following it; the skipped list goes into a FIFO in stream order, and at the MSCALF the
host pops it and emits every chunk of the batch at once. `PS2X_SEAMKEEPCALL=1` restores
the per-MSCNT behaviour for A/B runs.

Auto-advanced fight, 170 s: no `[vu1cost]` line at all (VU1 executed nothing), ~607k
chunks per 5 s from the host, no crash, no restart, no low-memory write, screenshot
correct. KickWorker 37 to 46 samples per 10 s (control 73 to 97; per-MSCNT skip 42 to
47): the unpack was a small share on this runtime. GsThread unchanged at ~50.

State of the seam: in a fight, VU1 runs nothing and VIF1 unpacks no mesh; every 3D
primitive reaches the GS as a host-built packet from host-side meshes. That is the
input the GPU path needs.
