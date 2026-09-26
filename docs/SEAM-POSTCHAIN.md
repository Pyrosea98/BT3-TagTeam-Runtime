# Post chain, HUD and 2D: the EE-built GIF packets, attributed

Goal (2026-09-27): full disengagement of the EE-built packet stream, the same way the
five VU1 programs were taken at the engine seam. Hook the game code that builds each
effect, take its intent from the game's data, implement it as a native pass, and skip
the GS packets it would have emitted. What stays on the lean 2D front-end (sprites,
text, uploads) is a separate decision.

## Tooling

**The packet oracle** (`PS2X_KICKPROBE=1 PS2X_PKTORACLE=<lo>-<hi>[:<nth>][,...] PS2X_STEPORACLE_DIR=<dir>`,
ps2_gif_arbiter.cpp [pktoracle]): the run of consecutive DIRECT packets built by code in `[lo, hi)`
(a step function's extent) is bracketed, in stream order, by dumps of paraLLEl-GS's whole VRAM and
register state (`oracle_<lo>_before/after.bin/.txt`, `ps2x_pgs::dumpVramRaw`). `tools/gsvram.py`
reads a dump through the GS swizzles (CT32, Z32, CT16/Z16 views, PSMT8H) and `tools/oracle_diff.py`
diffs a pair and tests bit-level hypotheses. This is how a pass's semantics are pinned before a
native version is written, and how the native version is verified (same bracket, same diff).
Bracketing the step's *function call* on the game thread sees nothing: the step only appends to
the display list, the whole frame goes out as one DMA chain later.

`PS2X_KICKPROBE=1` (ps2_seamprobe.cpp, [kickprobe]) attributes every DIRECT packet of the
frame's VIF1 chain to the EE code that built it:

- The display-list bump pointer (`gp - 0x59d8` = 0x2fe898) is advanced by three helpers:
  the batch allocator 0x100850 (VU1 batches, 16 callers), and the packet open/close pair
  0x1006e8 / 0x100738 used by everything the EE builds itself. All three are hooked; each
  advance [before, after) and the bytes the caller wrote inline since the previous advance
  are charged to the caller.
- Generic packet emitters (88..300-byte helpers that build one sprite, quad or mask write)
  are hooked too; while one runs, advances are charged to ITS caller (the effect).
- The DMAC's chain walker publishes the chain buffer's offset -> guest map per buffer;
  the VIF1 interpreter binds it and tallies each DIRECT payload by owner with a resumable
  GIF scanner (an IMAGE upload or PACKED loop continues across payloads).
- Table every 300 frames: frame targets (fbp/psm, with mask class: rgb-only, A = alpha-only,
  m = other mask), texture formats, draw kinds, DATE / alpha-only / ATE draws, uploads.

The game sends the whole frame as ONE VIF1 chain (~1700 tags), so per-kick attribution
was useless; per-packet attribution is what the table shows.

## Families (fight, first attribution pass; sites are the emitters, callers pending)

| site | function | emits | what it is |
|---|---|---|---|
| 0x101330 | FUN_00101298 | scene downscales into 0x2a00 / 0x2e00; alpha-only writes into 0x0/0xe00 and into 0x1c00 (the Z buffer's top byte); CT16 views of the frame (psm 2, fbmsk 0x3fff); reads Z as PSMT8H and PSMZ16 | post-chain core: depth mask, Z-derived index plane |
| 0x1015c0 | FUN_00101548 | tris + sprites into 0x0/0xe00 and 0x2a00; alpha-only 131/frame; reads PSMT8 and PSMT8H | outline / glow composite back into the frame |
| 0x105240, 0x10536c | FUN_001051c8, FUN_00105250 | CT16 views of 0x2a00 with PSMT8H reads, rgb-only writes | glow / DoF processing on the downscale buffer |
| 0x105c4c | FUN_00105bd8 | alpha-only sprites into 0x0/0xe00 with CT16 textures, ATE | frame alpha mask |
| 0x108768 | FUN_00108750 | alpha-only sprites into 0x1c00 with CT32 textures | writes the Z top-byte plane |
| 0x10a0e8, 0x1121a4 | FUN_0010a0a8, FUN_00111e68 | sprites into 0x2a00 / 0x2e00 / 0x3ec0 / 0x3f00 | blur ping-pong |
| 0x245ab8 | FUN_00245a50 | PSMT8H sprites into 0x0/0xe00 | a composite reading the Z-derived plane |
| 0x116854 | FUN_00116770 | untextured sprites into 0x0/0xe00 | fades / flashes |
| 0x102c54 | FUN_00102b70 | tris + strips into 0x0/0xe00, PSMT8 | effect quads |
| 0x21c1f8..0x226b98 | FUN_0021c*, FUN_00224f00, FUN_00226978 | PSMT8 strips into 0x0/0xe00, DATE draws, their own alpha-only mask writes | the HUD (self-contained masks) |
| 0x1014a0, unattributed | FUN_00101400, (list built elsewhere) | PSMT4 sprites, ~250 uploads/frame (2 MB) | text / glyph streaming |
| 0x12350c | FUN_001234e8 | sprites into 0x2a00 | character silhouettes for the glow (with the seam's prog 3 draws) |

Next: the same table one level up (the callers of the emitters), then per family:
find the parameters (which buffer, thresholds, blend weights, colours) in the caller's
data, and write the native pass.

## Effect level (fight, second pass: the emitters' callers), per frame

| caller | function | emits | reading |
|---|---|---|---|
| 0x102160, 0x1021a4 | FUN_00102120 | downscales into 0x2a00 (30) and 0x2e00 (15); rgb-only and alpha-only sprites into 0x0/0xe00 (15+15 each); 96 untextured alpha-only sprites into 0x0/0xe00 | CT32 (80), PSMT8H (30) |
| 0x105d44..0x105e74 | FUN_00105cd8 | CT16 views of 0x2a00 with rgb-only masks (blur in 16-bit); 3 x 32 alpha-only sprites into 0x0/0xe00 with ATE | PSMT8H (96), CT16 (96) |
| 0x1098bc | FUN_00109848 | 64 sprites through the CT16 view of 0x0/0xe00 (fbmsk 0x3fff) | PSMZ16 (64): the depth-mask pass, Z -> alpha MSB |
| 0x24b144 | FUN_0024b118 | 32 alpha-only sprites into 0x1c00 (the Z buffer's top byte) + 28 into 0x0/0xe00 | CT32 (32), PSMT8H (28) |
| 0x101330 | FUN_00108750 family | 64 alpha-only sprites into 0x1c00 | PSMT8H (32) |
| 0x245ab8, 0x245b14 | FUN_00245a50 | 32 sprites + 32 alpha-only sprites into 0x0/0xe00 | PSMT8H (64): the outline ink from the Z-derived plane |
| 0x106c3c | FUN_00106ba8 | 32 alpha-only sprites into 0x0/0xe00 | PSMT8H |
| 0x103098 | FUN_00103070 | 32 sprites into 0x2a00, 8+8 rgb-only into 0x0/0xe00 | CT32: glow composite back into the frame |
| 0x1121a4 | FUN_00111e68 | 61 sprites into the scratch buffers and the frame | CT32: blur ping-pong |
| 0x10a24c | FUN_0010a218 | 39 sprites into 0x2a00/0x2e00/0x3ec0/0x3f00 | CT32: blur ping-pong |
| 0x114244 | FUN_001141f0 | 32 untextured sprites into 0x2a00 | silhouette / glow source |
| 0x1027b0 | FUN_00102740 | 88 tris + 143 strips into 0x0/0xe00 | PSMT8: effect quads |
| 0x115f5c.. | FUN_00115de0 (2028 bytes) | 90 tris with PSMT8, untextured sprites | effects |
| 0x10a4dc, 0x10a4f4 | FUN_0010a480 | 1300 PSMT4 sprites into 0x0/0xe00 | text / glyphs |
| 0x21c1f8..0x21c5a4, 0x224ff0, 0x226a88 | FUN_0021c0xx, FUN_00224f00, FUN_00226978 | PSMT8 strips + sprites, DATE draws, own alpha-only masks | the HUD |
| unattributed | (a second list, not via the hooked helpers) | 409 PSMT4 sprites, 350 uploads (2.3 MB) per frame | text / glyph streaming |

## Orchestrators (static caller graph of the step functions)

- `sub_00247578` calls func_109848 (depth mask), func_245a50 (outline ink), func_103070
  (glow composite). FUN_00247660 / FUN_00247720 call func_109848 too.
- `FUN_0010ff40` calls func_102120 (downscale + composites) and sub_0024B118, which calls
  func_105cd8 (16-bit view processing, alpha masks) and func_108750 (Z top-byte plane).
- `FUN_0010fd98` calls func_102120 and func_111e68 (blur); `sub_0010CC88` calls func_102120.
- Blur sprites (func_10a218) come from FUN_00111cf0, FUN_00115c30, FUN_00126628, sub_00126620,
  sub_00126880.
- Effects (func_115de0) from FUN_0012b9c0, FUN_0025da58, FUN_00262c30, sub_0012B7F8, sub_0025D8D0.
- HUD (func_224f00) from FUN_00225f28, sub_00225A50.

The hook point for skipping a whole effect is the orchestrator; the native replacement reads
the same parameters the orchestrator reads.

## Step semantics from the one-frame transcript (docs/evidence/postchain-transcript-*.txt)

Coordinates: XYOFFSET 0x7000/0x7200, so x 1792 = 0, y 1824 = 0. Sprites are 32-px vertical strips
over the whole 512x448 frame unless noted. Palettes are the VRAM mirror's contents on that frame.

| step | writes | reads | operation |
|---|---|---|---|
| depth mask (sub_00109848, in sub_00247578) | FRAME 0xe00 as **CT16** (psm 2, fbw 8, 512x896 view), fbmsk 0x3fff: only bits 14..15 of each 16-bit half | TEX0 Z buffer 0x1c00 as **PSMZ16** 1024x1024, decal, TEXA ta0=0 ta1=0x80 | **PINNED by the packet oracle (2026-09-26, tools/oracle_diff.py on a PS2X_PKTORACLE=109848-109938 pair): over the whole frame, frame.A := Z24[15:8], RGB and Z untouched, every pixel exact.** The 16-bit-view strips are only the GS mechanism for that byte copy. Native pass: alpha plane := middle byte of Z. |
| **Pinned by the packet oracle (2026-09-26, live fight, tools/oracle_diff.py; "exact" = every pixel of the frame):** sub_00109848 depth mask: frame.A := Z24[15:8] (exact). FUN_00106ba8: frame.A := 0 (exact). sub_0024B118: Ztop := frame.A, where Ztop is byte 3 of the Z buffer READ THROUGH THE CT32/PSMT8H LAYOUT at 0x1c00 (exact; through the Z32 layout it looks permuted per page, which is what "needs the block tables" meant). FUN_00245a50: frame.A := CLUT_0x3e84[Ztop].A, the depth->blur-weight ramp (exact on both runs); nothing else changes, there is no RGB ink here. FUN_00103070: (1) 0x2a00 (fbw 4, 256x224) := 2:1 downscale of the scene frame (32 strips, u=2x, v=2y, bilinear; visually identical to a 2x2 box, GS filter rounding still to pin: MAD 2.2), alpha 0x80; (2) frame.rgb := floor(frame + (round(U) - frame) * frame.A / 128) with U = 0x2a00 sampled bilinearly at (x/2, y/2) (exact on all three channels). FUN_00105cd8: alpha-only writes of 0x80 / 0x30 (TEXA ta1/ta0 of a CT16 view of 0x2a00, ATE) on ~11k px; mapping not yet pinned (93% with (x/2,y/2), the changed pixels themselves mostly wrong). FUN_00102120, FUN_00111e68, FUN_00108750 emitted nothing in the live fights captured: they need their trigger (aura / special move) first. | | | |
| Z top-byte plane (sub_0024B118 first part, in FUN_0010ff40) | FRAME 0x1c00 (the Z buffer as CT32), fbmsk 0x00ffffff: alpha only | TEX0 frame 0xe00 CT32 512x512, decal, ALPHA (0,1,0,1) FIX 0x80 | Ztop(x,y) := frame.A(x,y): the scene's per-material alpha becomes an 8-bit id plane. |
| material tint (sub_0024B118 second part) | FRAME 0xe00 RGB(A) | TEX0 **PSMT8H** at 0x1c00 (Ztop) through CLUT 0x3e94; ALPHA (0,1,0,1) | frame := mix(frame, CLUT[Ztop].rgb, CLUT[Ztop].a/128). CLUT 0x3e94: entries 0..243 = 0; 244..254 = bright colours with alpha 0x30 (ki / aura material ids). |
| ink (FUN_00245a50 at 0x245ab8) | FRAME 0xe00 | PSMT8H Ztop through CLUT 0x3e64; ALPHA 0x48 = (0,2,0,1): Cs*As + Cd | frame.rgb += CLUT[Ztop].rgb; CLUT 0x3e64: 0..199 black (alpha 0x80), 200..255 a rising blue ramp. |
| ink alpha (0x245b14) | FRAME 0xe00 alpha-only | PSMT8H through 0x3e64, ALPHA (0,2,0,1) FIX 0x80 | alpha plane update from the same lookup. |
| 0x106c3c (FUN_00106ba8) | FRAME 0xe00 alpha-only | PSMT8H Ztop through CLUT 0x3e90 (all 0x00ffffff: alpha 0) | frame.A := 0 where the lookup applies (mask clear). |
| downscale (FUN_00102120 at 0x102160) | FRAME 0x2a00 fbw 4 (256 wide), then 0x2e00 fbw 2 (128 wide), scissor 0..255 / 0..127 | frame 0xe00 CT32 512x512, CLAMP region, TEX1 bilinear (0x60) | 2:1 then 4:1 box downscales of the scene (u 0..32 -> x 0..16 per strip). Also PSMT8H through CLUT 0x3e98 into the frame with (0,1,0,1): a fading white by material id. |
| alpha clears (0x1021a4) | FRAME 0xe00 alpha-only, untextured | | alpha := 0xff, then 0, then 0 over the whole frame (mask initialisation between steps). |
| 16-bit mask work (FUN_00105cd8 at 0x105d44/0x105e24) | FRAME 0x2a00 as **CT16** with rgb-only masks; then FRAME 0xe00 alpha-only with ATE (!= 0) and TEXA 0x8030 | PSMT8H through CLUT 0x3e8c (grey ramp, alpha 0x80); then TEX0 0x2a00 as **CT16** 512x512 (the downscale buffer's halves) | builds the DoF/glow mask from the downscale buffer's bit fields; aliased, semantics to pin like the depth mask. |
| glow composite (FUN_00103070 at 0x103098) | FRAME 0xe00 rgb-only; also 0x2a00 fbw 4 | frame 0xe00 CT32 (TEST ATST 5), and 0x2a00 as CT32 256x256 bilinear, ALPHA 0x54 = (0,1,1,1) FIX 0x80: dst alpha weighted | frame.rgb := mix(frame, blurred, frame.A/128): the DoF/glow blend keyed by the alpha mask. |
| blur ping-pong (FUN_0010a218, FUN_00111e68) | 0x2a00, 0x2e00, 0x3ec0, 0x3f00 | those buffers, bilinear | separable blurs on the downscale chain; local-copy setups (BITBLTBUF 0x2a00..) precede them. |

Native design: keep five planes on our targets (frame RGB, frame alpha = material id, Z24 from the
depth image, Ztop 8-bit, the downscale/blur chain) and implement each row as a fullscreen fragment
pass with the same blend/mask rules and the live palette (read from the mirror or the game's RAM at
the hook). Hook points: sub_00247578, FUN_0010ff40, FUN_00247660 (skip the game's steps, run ours in
that frame position). The two aliased rows are the only ones whose bit semantics are not yet pinned;
PS2X_PGS_VRAMPROBE (ps2_gs_pgs.cpp) already reads the frame alpha and Z top bytes after the depth-mask
pass under paraLLEl-GS and is the validation oracle.
