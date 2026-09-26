# altGL — standalone OpenGL layer and frame present (F3a)

> **Status update (2026-09-17)**: raylib is now **vendored 1:1** in
> `ps2xRuntime/third_party/bt3gl/` and compiled by a local target, so it is no longer a dependency;
> the window/input/GL context come from **SDL2** (`PLATFORM_DESKTOP_SDL`, GLFW removed) and the
> renamed `gsrl` copy is **dormant** (`-DPS2X_USE_GSRL`, OFF). The authoritative write-up is
> `docs/ALTGL-RAYLIB-REMOVAL.md` (see "Resolution").

Branch: `OpenGL-Test`. The goal is to replace raylib with our own OpenGL layer
(`ps2x::gfx::gl`, mirroring `ps2x::gfx::d3d11`) so the GS renderer can be backend-agnostic.
This document covers the first production slice: presenting the game frame through `gfx::gl`.

## What altGL is

- `src/gfx/gl/GlApi.{h,cpp}` — self-contained GL 3.3 core entry-point table. No glad/GLFW: every
  function is resolved through a platform loader (`wglGetProcAddress` on Windows,
  `glXGetProcAddress`/SDL on Linux) with a client-library fallback for GL 1.1.
- `src/gfx/gl/GlDevice.{h,cpp}` — window/context/swap callbacks (during the transition it borrows
  raylib's window and context).
- `src/gfx/gl/GlGfx.{h,cpp}` — `Texture` / `RenderTarget` / `Shader` / `Renderer`, the mirror of
  `gfx/d3d11/D3D11Gfx.h`.
- `src/gfx/gl/gl_shader_glsl.h` — blit VS/FS used by the present.
- `src/gfx/gl/gl_gs_shader_glsl.h` — GLSL port of the GS replay shader
  (`ps2_gs_gpu_renderer.cpp` `kVertShader`/`kFragShader`), same uniform names, dual-source outputs.
- `tools/gl_probe/` — validation harness (`ps2xGlProbe`): loads the loader, compiles the GS
  shader, draws and self-checks the centre pixel; also a calibration sequence of solid squares.

## Frame present (F3a)

`ps2_runtime.cpp` gains `AltGlPresent()`. When `PS2X_ALTGL=1` the final blit uses `gfx::gl`
instead of raylib's `DrawTexturePro`; the GS still renders through rlgl, only the present moves.
The presented raylib texture is adopted by GL name (never freed by us) and the quad reuses the
existing letterbox/widescreen/atlas math. Without `PS2X_ALTGL` the raylib path is untouched.

### Launch recipe (how the front-end starts the game)

The front-end (inside `Dragon Ball Z Budokai Tenkaichi 3 - Recompiled.exe`) runs with working directory = deploy root, and when the user
presses PLAY it re-executes itself with:

```
PS2X_EXEDIR=<deploy root>          PS2X_ASSETDIR=<deploy root>/assets
argv[1]    =<deploy root>/data/SLUS_216.78
```

For the altGL present the GL path must be active, so D3D11 has to be off:

```powershell
$env:PS2X_D3D11="0"; $env:PS2X_ALTGL="1"; $env:PS2X_GPU="1"
$env:PS2X_EXEDIR="D:\Dragon Ball Budokai Tenkaichi 3 Recompiled"
$env:PS2X_ASSETDIR="$env:PS2X_EXEDIR\assets"
& "$env:PS2X_EXEDIR\Dragon Ball Z Budokai Tenkaichi 3 - Recompiled.exe" "$env:PS2X_EXEDIR\data\SLUS_216.78"
```

Success is `[altgl] present init ok (GL 3.3.0 Core ...)` in the log.

## Bugs found during bring-up (all fixed)

1. **Uniform setters without a bound program.** `glUniform*` with no current program raises
   `GL_INVALID_OPERATION` and leaves the uniform at its default; for the GS shader `uAtst` stayed
   `0`, so the alpha test (`ATST=0`) discarded every fragment and the draw vanished. `Shader::Set*`
   now bind their program on demand (`ensureCurrent`).
2. **Dual-source outputs.** `layout(location=0, index=1)` is not honoured by all drivers on its
   own: the two outputs alias and a single-draw-buffer draw is dropped. `Shader::Compile` now
   takes `outIndex0/outIndex1` and calls `glBindFragDataLocationIndexed` **before** linking
   (required by `ARB_blend_func_extended`). Also requires `GL_BLEND` enabled with a `SRC1_*`
   factor at draw time.
3. **raylib leaves `GL_CULL_FACE` enabled.** `glClear` ignores culling, draws do not — the probe
   disables it explicitly.
4. **Present UV maths.** raylib's `DrawTexturePro` does *not* negate V for a negative-height source
   rect: it moves `source.y -= source.height` and then uses `source.y/height` and
   `(source.y+source.height)/height`, keeping V inside `[0,1]`. Replicating that exactly turned
   the black screen into a correct frame.

## Result

Real drawing through `gfx::gl` verified end to end on BT3: memory-card boot screen, the in-game
settings overlay and a live 3D fight all present through the altGL path.

Performance in the same scenes measured from the `[fps]` line: `GAME ≈ 24–60` depending on the
scene (the pre-altGL figure the tester reported for the fight was ~11). A like-for-like
altGL on/off A/B in the same fight is still pending; the remaining overlay-text issue below is
unrelated to the frame rate.

## Known issues / next

- **Front-end Video tab** has the `OpenGL (altGL)` selector (renderer 4 → `PS2X_D3D11=0`, `PS2X_ALTGL=1`),
  so the mode can be picked without environment variables.
- Migrate the GS replay itself off rlgl to `gfx::` (`rg*` wrappers, then the D3D11/GL backends).

## Fixed: garbled overlay text

The in-game settings overlay (drawn by rlImGui through rlgl) came out with every glyph as an
opaque block once the game was booted. Root cause: our altGL present sets GL blend state with
**raw GL calls** (`glDisable(GL_BLEND)`), but rlgl caches its own blend mode and therefore
re-applies nothing; the overlay then rendered with blending off, so each glyph quad wrote its
full quad as an opaque rectangle of the text colour instead of an alpha-blended glyph. rlgl's
cache is a lie whenever GL is touched directly, so the fix re-asserts the state through **rlgl's
own APIs** right after the altGL draw:

```cpp
rlEnableColorBlend();
rlSetBlendMode(RL_BLEND_ALPHA);
rlActiveTextureSlot(0); rlDisableTexture(); rlDisableShader();
rlDrawRenderBatchActive();
```

Diagnosed by logging the GL state before/after `rlImGuiEnd()` (`PS2X_ALTGL=1`): `blend=0` with
`cull`/`vao`/`prog` mismatched versus rlgl's cache, identical pre-boot and post-boot, which
pointed at the blend state rather than the font atlas or texture units.

