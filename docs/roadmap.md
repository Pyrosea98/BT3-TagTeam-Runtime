# Roadmap

Agreed order of work (2026-10-03). The goal is a PC port with new online play.

## 1. Finish the fight simulation

- Fighter core (in progress): status, hit handling, collision, members and gauges, action
  queries, flags, movement, the action state machine; the CPU opponent.
- Action handlers and techniques, roughly 0x1E3000..0x212000.
- Effect tasks, 0x12DD80..0x1AE200: the largest block of game code. Effects hit, and hold
  nearly all the `rand()` calls, so they are simulation.
- Stage, 0x23FB20..0x248F28: collision, bounds, destruction, the debris physics caller.
- Consolidation alongside: one unified fighter header in place of the per-file local views;
  real names for placeholder names; C files owning their data.

## 2. Headless simulation on PC (first port milestone)

Run the fight natively with no graphics or sound: stub the hardware calls, feed a battle setup
and an input stream, step it. Validate against the game's own replay format: record a replay
on the original, play it back natively, compare state frame by frame. This checks the decomp's
logic and measures determinism before any netcode exists. Started in parallel with stage 3,
since it needs a platform layer rather than more matching.

## 3. What is seen and heard

- 3D renderer, models, animation (0x102F28..0x11EC10, 0x24BBE8..0x2527B0) and the nine VU1
  microprograms. The PS2 pipeline has to be reimplemented, not translated.
- Asset formats: catalogue the archives; the loaders define the formats.
- HUD (0x2129C8..0x239FF0).

## 4. Everything around the fight

- The menu overlay `DBZP.BIN` (737 functions): character select, story, shop, options.
- Menu support code in the main executable, the memory card module, remaining script users.

## 5. Netplay

Built on a simulation that already runs and can be compared. Look at the Wii build's game-side
netcode at this point to see what the developers themselves synchronised. Requirements are
collected in netplay_notes.md.

## Why this order

The simulation is where a mistake is invisible until two machines disagree. Rendering and menus
are more work in total, but errors there show on screen.

## Added 2026-10-04: required before the headless simulation

The animation player inside the battle object (functions around 0x24D498, 0x24D518, 0x24D610,
0x250570, 0x2505A8, 0x250940, 0x250B88, 0x2500E8; only partly decompiled in btl_obj.c) is
simulation input: fighter and effect code query animation events, hit-event counts and frames,
and model node positions through it every frame. Schedule it with the stage collision queries,
the projectile hit detection (0x1AE200..0x1B4140) and the stage rigid bodies, before the
headless build.

## Status 2026-10-04 (evening): simulation code decompiled and linked

Linked and byte-identical: 68.41% of the main executable's game code (152 C files, 5405
functions diff clean, 185 functions in linked files still INCLUDE_ASM, all but two with a C
attempt). Everything identified as fight simulation is in that set: battle loop and sequence,
input and replay, fighters (actions, movement, hits, damage, gauges, techniques), AI, story
script, effect scene core (task tree, hit records, projectile types, technique events, hit
detection, beam struggle), stage (bounds, zones, collision, destructible objects, way-points),
collision primitives, battle object (animation player, nodes, hit volumes).

Next, in order:
1. Near-miss cleanup, simulation first: `BtlObjAnim_SamplePosRot`, `BtlObjAnim_SamplePose`,
   `BtlObjXf_Update`, `BtlStage_UpdateObjs`, `BtlStage_BreakObj`, `EftHit_ClashTech`,
   `EftHit_SetTaskFlag`, `EftHit_IsStoppedByHit`, `EftDisc_Home`, `EftDisc_Update`,
   `EftBlastObj_Init`, `EftSweep_AddMark`, `StgCol_SplitStep`, `StgCol_FighterBreakObj`,
   `StgCol_TraceZone`, `StgNav_FindPath`, `BtlAiSeq_PushRule`, the six `AiThink_*`,
   `BtlAct_GuardHandler`, `BtlAct_GrabDash`, `BtlActB_TickMemberChange`. Their behaviour is
   described from disassembly today.
2. One unified fighter header and one battle-object header (every file has its own view).
3. The hand-written VU0 vector / matrix routines (0x11FA40..0x122900): exact C equivalents.
4. Extract the two large archives from the ISO (PZS3US1.AFS, PZS3US2.AFS) and document the
   asset formats the simulation loads (character packs, animations, stage files, effect packs,
   AI data).
5. Headless build: 32-bit host build, platform layer (files, jobs, pools), stub renderer and
   sound, technique timers moved out of the draw callback, separate generators for visual
   effects; then replay validation.

Not needed for the headless build and still assembly: HUD, model renderer, most of the sound
and graphics layers, movie playback, and the menu overlay (DBZP.BIN, 0%).

## Requirement added 2026-10-04: input / output equality of the port's maths

The user's bar for the vector / matrix routines: the port's functions must give the same
outputs as the originals for the same inputs, bit for bit. The Wii cross-check of the maths
library was dropped as not needed (it cannot answer PS2 rounding questions).

How to prove it (not started): build a small PS2 test program with the project toolchain that
links the ORIGINAL routines (from the matching build) and runs them over a fixed set of test
vectors, including the edge cases (zero vectors, x * 1.0, overflow, division by zero, angles
near 0 and pi), writing the result bits out; run it in an emulator (and on a console if one is
available); run the same vectors through `src/port/vu0_*.c` on the PC and compare. Differences
settle the three open points in docs/systems/math.md (add / subtract guard bits, the
multiplier quirk, the overflow value). An emulator's own float accuracy settings must be at
their most accurate for the result to mean anything; a console run is the final word.
The same harness then covers `Mathf_*` and any other routine whose bits matter.

## Priority change 2026-10-04 (late): finish the matching decompilation first

The user decided: "lets first completely decompile byte by byte then think about it".
The port work (reference maths validation, headless build) waits until the decompilation is
complete. Order now:
1. The rest of the main executable's game code (about 300 KB in seven assembly ranges:
   0x102F28..0x115170, 0x115478..0x11EC10, 0x2129C8..0x215420, 0x2187E0..0x22FD10,
   0x252F68..0x254A20, 0x25C2A8..0x263098, 0x268248..0x273CA0), brief in
   docs/briefs_remaining_main.md.
2. The near-miss functions inside linked files (cleanup agents).
3. The menu overlay DBZP.BIN (508 KB, 737 functions).
4. Only then: unified headers, asset formats, validation harness, headless build.
The SDK and middleware libraries after 0x273CF0 (Sony libs, CRI ADX) are not game code; they
are matched as library objects where needed, not decompiled, unless the user asks otherwise.
