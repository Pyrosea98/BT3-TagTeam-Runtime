# Removing raylib from the video path

Branch: `OpenGL-Test`. Goal: the game's video path (window/context, GS replay, overlay, FMV) runs
on our own layers -- `ps2x::gfx::gl` for OpenGL and `ps2x::gfx::d3d11` for D3D11 -- with **no
raylib/rlgl**. `docs/ALTGL-PRESENT-STATUS.md` covers the present (already migrated).

## Why the first attempt was dropped

A `rg*` indirection layer that renamed rlgl calls without changing their bodies was tried and
rejected: it removed **no** dependency, only churned 315 call sites. Renaming is not migration.

## The constraint that sets the order

raylib's drawing helpers are welded to `InitWindow`: `BeginTextureMode`/`EndTextureMode`/
`BeginDrawing`/`ClearBackground`/`scissor` read and write `CORE.Window` (`rcore.c:1104-1127`,
`1164-1178`), which is zero without `InitWindow`. `rlgl` itself **is** standalone
(`rlglInit(w,h)` + `rlLoadExtensions(getProc)`), and `LoadShader`/`SetShaderValue*`/
`LoadTextureFromImage`/`DrawTexturePro` only need rlgl/CPU.

So the platform cannot be swapped first while raylib still draws; the drawing must move first.
Chosen order is therefore **replay first, platform last**.

## Target platform

**SDL2** (already linked into `ps2_runtime` for the host audio/pad layers) so the same GL path
works on Windows and Linux. raylib's `LoadImage` stays until stage A4, where it is implemented
and disconnected.

## Observed issues worth tracking (with PS2X_ALTGL=1 + PS2X_UIGL=1)

- **Crash in the main menu with the texture pack ON** (`texture_pack=true`): access violation
  inside the AMD GL driver (`atio6axx.dll`). Root cause found: `GsTexCreateFromImage` uploaded
  RGBA8 from any image, but for a COMPRESSED source (DDS/BC) `ImageFormat` refuses to convert, so
  it read `w*h*4` bytes out of a much smaller BC buffer -> buffer overrun in the driver. Fixed by
  routing compressed sources to raylib's `LoadTextureFromImage` (glCompressedTexImage2D).
- **Injected MP4 (FMV override) does not run on the splash screens.** The override is served from
  `ps2x_fmv` and drawn with raylib textures (`LoadTextureFromImage`/`UpdateTexture`/
  `DrawTexturePro`) -- stage D. Needs its own investigation (it is independent of the present).
- **Texture replacements were not visibly applied on the splash screens** (the pack did index
  18,704 replacements and log hits). Could be the same compressed-upload hazard, VRAM pressure, or
  the splash simply not being a matched replacement; re-check with the crash fix in place.

- **Injected MP4 (FMV override) shows BLACK while its AUDIO is replaced correctly.**
  What is already known / done:
  - The decoder side is fine: the log shows `[fmvoverride] VIDEO INIT -> injecting ...mp4`,
    `decoding 2880x2156, dur=93.88s, codec=av1` and the ADX track is the pack's (audio OK).
  - The override replaces the game's `ZS3USOP.PSS`; the shipped setup uses a **black PSS with the
    MP4 composited on top**, so the MP4 draw is what is missing.
  - Stage **B** now draws the video through `gfx/video_overlay` (VideoBlit interface + GL
    implementation over the shared gfx::gl context and the existing blit shader), i.e. without
    raylib, in the GL path only. **The video is still black through that path too.**
  - Remaining hypotheses, in order:
    1. **Decode throughput**: a 2880x2156 AV1 software decode may not keep up, leaving frames
       empty/black (audio is a separate path and unaffected). Test with a 1080p H.264 MP4, or a
       lower resolution, before touching more GL code.
    2. The blend/tint: `of.alpha` (fade) or the alpha blend killing the quad -- the previous raylib
       path and the new Gfx path both blend with SRC_ALPHA.
    3. Ordering: the video quad is drawn before the present; with `fmvDrew` the GS present is
       skipped, so if the quad fails nothing repaints the frame.
  - Next: run with a 1080p H.264 clip (and/or log the decoded frame's maxRGB) to split decode vs
    draw, then, if it is the draw, add the D3D11 VideoBlit and compare.

## Isolated problem: the gfx::gl replay backend (A3.2b, opt-in)

`PS2X_GSBACKEND=gl` routes the main triangle emitter through `gfx::gl` and is **NOT stable yet**;
the default rlgl path is unaffected (every entry point no-ops unless the env var is set).

Symptom: the frame alternates between correct, zoomed/cropped and fully black, at any
`render_scale`.

What is already understood (with evidence):
- **Fixed**: the rlImGui overlay appearing ~2x offset after boot. `GsRtEnd` restored only the
  viewport; raylib's `EndTextureMode` calls `SetupViewport` (rcore.c:3537), which ALSO restores the
  window **orthographic projection** (`rlOrtho(0,render.w,render.h,0,0,1)`) and the modelview.
  Commit `14fdb72`.
- **X/Y transform is equivalent**: rlgl's MVP is `rlScalef(N) x ortho(0,physW,physH,0,0,1)`, giving
  `x_ndc = 2N.x/physW - 1`; our folded ortho over `physW/N` gives the same. So the zoom/crop does
  not come from the formula but from **using the wrong target's framing at draw time**.
- **Prime suspect**: the replay binds framebuffers with **raw `rlEnableFramebuffer` (58 sites)**
  that never go through `GsRtBegin`, so our `mvp` stays that of the PREVIOUS target -> wrong scale
  and offset for those draws.
- **Z differs** (inert while depth is off): rlgl maps `z_ndc = -2z - 1`; ours maps `z_ndc = -z`.
- Diagnostic: `[gsgl] target WxH scale=N -> logical ...` (first 60 framings) shows each FBO bound
  both with the `GsRtBegin` default (scale 1) and with `beginFbp`'s real scale.

Planned alternatives (to pick when we resume):
1. Re-derive the framing at draw time from `GL_DRAW_FRAMEBUFFER_BINDING` + `GL_VIEWPORT` + the FBO
   size map, so it no longer depends on where the FBO was bound.
2. Match rlgl's Z mapping (`z_ndc = 2z - 1`).
3. Only then re-enable batching (flushing on every framebuffer bind).

## Resolution (2026-09-17): raylib is vendored in-tree, GLFW is gone

The endgame changed: instead of re-implementing rlgl/raylib piece by piece (A2/A3/A4/D), **raylib's
source tree was vendored 1:1** into `ps2xRuntime/third_party/bt3gl/` and compiled by our own target
(`VENDORED.md` documents the provenance: upstream 5.5, commit c1ab645, zlib, with
`patches/raylib-5.5-ps2x.patch` already applied). raylib is no longer a dependency: no FetchContent,
no patch step, no network. The vendored copy IS our code, and the replay keeps submitting through
rlgl -- one single GL state owner, which is what the whole A3.2b saga was fighting for.

What that retired:

- **The renamed `gsrl` copy is dormant** behind `-DPS2X_USE_GSRL` (OFF). It is kept for A/B only.
- **The hand-rolled `gfx::gl` batcher / `reframe`** was the wrong lever: re-implementations of rlgl
  kept disagreeing with rlgl's cache (garbled overlay, 2x projection, title black bands, present in
  the corner) and, once two live rlgl stacks coexisted, the AMD driver took an access violation.
- **GLFW is gone** (phase B for the platform): raylib is built with `PLATFORM_DESKTOP_SDL`, so the
  window/input/GL context come from the SDL2 we already fetch for gamepad/audio; `external/glfw/` was
  pruned from the vendored tree and the pad fallback's GLFW side channel was replaced by raylib's API.
- **A1.1 is complete** (`461ab6c`): the render-target contract (`GsRtInfo`/`GsRtSetLogical`) plus our
  own sampleable-depth targets, so the default config (PS2X_DOFMASK=2) no longer builds FBOs with
  raylib. `image_io`, `video_overlay` and the altGL present stay as they are.

Two traps this move created, both fixed and worth remembering:

- **`GetWindowHandle()` changed meaning**: the GLFW platform returned the HWND, the SDL platform
  returns the `SDL_Window*`. Anything that hands the handle to the OS must resolve it first --
  `ps2xNativeWindowHandle()` in `src/lib/ps2_host_window.{h,cpp}` does that through
  `SDL_GetWindowWMInfo`, and the D3D11 device is created with it. (Its own TU on purpose:
  `SDL_syswm.h` pulls in `<windows.h>`, whose `Rectangle` clashes with raylib's.)
- **`fps60_sites.txt` was only staged next to the build's exe**, never into the deploy, so
  `[fps60] CANNOT ENABLE: ... not found` left the 60-fps/halfstep mode silently inert with
  `fps60 = true` in settings. The deploy now copies it next to the runner too
  (`games/bt3/fps60_sites.txt` is the repo copy; `savedata/` stays a fallback for
  installs staged before the move) and `[halfstep] ON: 94 float sites halved, ...` confirms
  the table loads from beside the executable.

### B: the API naming

Our call sites no longer read raylib either, without touching the vendor: `src/gfx/bt3gl_api.h`
(GENERATED by `tools/vendor/rename_to_bt3api.ps1`, which is the versioned recipe to re-apply after a
vendor update) aliases what we use to our own spelling — functions `Xxx` -> `bt3Xxx`, rlgl's `rlXxx`
-> `bt3rlXxx`, constants `KEY_*` -> `BT3_KEY_*` and `RL_*` -> `BT3RL_*`, and the types we touch
(`Vector2` -> `bt3Vector2`, …). 15 TUs, 116 functions, 57 constants, 7 types. The vendor keeps its
names byte-for-byte so `VENDORED.md` stays true and updates stay mechanical; symbols our own code
defines (`Color`, `Sound`, the `Renderer::*` members) are detected and left alone, and TUs that pull
`<windows.h>` never get the bridge (raylib's `Rectangle` clashes with wingdi's).

### C: the overlay

`rlImGui` is gone from the build. The overlay is ImGui over our own backends: `imgui_impl_sdl2` for the
platform/input and `imgui_impl_opengl3` (or `imgui_impl_dx11` on the native path) to draw, so it no
longer draws through rlgl either — which removes the class of state fights where the overlay followed our
raw-GL draws with rlgl's stale cache. `src/lib/ui_sdl.{h,cpp}` feeds ImGui from an SDL event WATCH: a
watch sees every event while it is pumped and leaves it queued, so ImGui gets its input while raylib
keeps receiving the game's. The raylib-fed input (`feedImGuiInput`) stays as the fallback when the SDL
platform is unavailable (`PS2X_UISDL=0` forces it).


## Stages (each keeps the game rendering and is A/B-able)

Flag: `PS2X_GSBACKEND=gl` selects the gfx::gl replay backend; default stays rlgl until a stage is
verified.

| Stage | Scope | Status |
|-------|-------|--------|
| A1 base | shared `gfx::gl` context (`gl_context`) + `GsTexture`/`GsRenderTarget` resources | done (`2b5b565`, `5a3c4dd`) |
| A1.1 | convert the replay's render-target/texture call sites (`LoadRenderTexture` 24, `Begin/EndTextureMode` 22/21, `UpdateTexture` 11, `SetTextureFilter` 18, `DrawTexturePro` 12) to `GsRenderTarget`/`GsTexture` | **done** (`a13f104`, `cf27b1d`, `9a1ab09`, `461ab6c`) |
| A2 | shaders: `LoadShader` / `BeginShaderMode` / `SetShaderValue*` (81) → `gfx::gl::Shader` by name | moot: rlgl (vendored) is ours |
| A3 | immediate-mode submit: `rlBegin/End` (13) + `rlVertex*` (58) + `rlTexCoord2f` (49) + `rlColor4ub` (16) + `rlSetTexture` (25) → `gfx::gl::Renderer` **with own batching** | moot: the vendored rlgl IS the submitter |
| A4 | CPU/text: `LoadImageFromTexture`/`ExportImage`/`LoadImage` → own loader; `DrawText`/font | partial (`019eda7`, `7e5188a`, `132b23b`, `7de75f6`, `a23551b`) + vendored rtext |
| B | platform: SDL2 window + GL 3.3 core + swap + input + timing; delete `InitWindow` and every `CORE.Window`-dependent call | **done for the platform** (raylib runs on `PLATFORM_DESKTOP_SDL`, GLFW removed); a fully raylib-free window is still open |
| C | overlay: `imgui_impl_opengl3` + `imgui_impl_sdl2` (drop rlImGui) | GL backend done (default with `PS2X_ALTGL=1`); SDL2 backend pending |
| D | FMV / utilities → `gfx::gl` + own file IO | FMV override done (`8d993f4`, `4d2ba17`, `4058802`, `1df14ce`) |
| E | remove `raylib` from `CMakeLists.txt` (and the `vbring` patch) | **done differently**: raylib is a local target over the vendored 1:1 tree (`a10cbcd`) |

## Guardrails learned this session

- **Never touch GL with raw calls while rlgl is alive** without re-asserting rlgl's cached state
  through rlgl's own APIs. rlgl only re-applies what it thinks changed, so a raw
  `glDisable(GL_BLEND)` left the rlImGui overlay drawing with blending off (every glyph an opaque
  block). This is exactly why the replay must eventually stop sharing the context with rlgl.
- The **altGL present is the reference implementation** of how to use `gfx::gl` (context, shader
  compile with dual-source outputs, texture adopt/sampler, orthogonal mapping, restoring raylib's
  state afterwards).
- Uniform setters must run with their program bound (`gfx::gl::Shader::Set*` auto-binds).

## Verification per stage

1. `ps2xGlProbe` still passes.
2. Deploy: boot screen → overlay (legible text) → a 3D fight, compared against the previous build.
3. `[fps]` not worse than the current baseline (~60 fps menus, ~24-30 fps fights).
4. A/B screenshots via `PS2X_SHOT_DIR`.

## How the deploy is launched (for verification)

```powershell
$env:PS2X_D3D11="0"; $env:PS2X_ALTGL="1"; $env:PS2X_GPU="1"
$env:PS2X_EXEDIR="D:\Dragon Ball Budokai Tenkaichi 3 Recompiled"
$env:PS2X_ASSETDIR="$env:PS2X_EXEDIR\assets"
& "$env:PS2X_EXEDIR\Dragon Ball Z Budokai Tenkaichi 3 - Recompiled.exe" "$env:PS2X_EXEDIR\data\SLUS_216.78"
```

Success line: `[altgl] present init ok (GL 3.3.0 Core ...)`.
