# Video & Controllers UI

The front-end and the in-game overlay share one layout language: a **STATUS** summary with colour dots,
the destructive/rarely-touched controls in **popups**, and only the switches you flip often left on the
tab. Both write the same `savedata/settings.toml`, so a change made in either shows up in the other.

## Video tab

```
STATUS
  * Renderer   OpenGL (New)   own GL present
  * Monitor    1 - 24'TV      1440x900 @60Hz
  * Resolution 1366 x 768     windowed
  * Upscale    x2             applies on restart
[ Display settings... ]   [ Visual Effects... ]
RENDERER
  Renderer   (OpenGL (New) / Software (CPU) / paraLLEl-GS (Vulkan))
```

The dots are green / amber / red:

| Row | green | amber | red |
| --- | --- | --- | --- |
| Renderer | running as configured / present ok | fell back to another renderer | no present |
| Monitor | the requested monitor exists | requested monitor missing: clamped | not detected |
| Resolution | matches the configured window | clamped or custom | - |
| Upscale | active (paraLLEl-GS: samples per pixel) | applies on restart (OpenGL) | not available (software renderer) |

The overlay reads the *live* runtime state (`runtime/ps2_video_status.h`, logged once as `[vstatus]`);
the front-end shows what it is about to ask the runtime to run, with real monitor names from SDL2.

### Display settings...

Resolution presets, render scale **x1 / x2 / x3** (mutually exclusive), monitor (name + mode), and the
window mode: **Windowed (resizable)**, **Borderless**, **Fullscreen**.

### Visual Effects...

Cel Outline (+ ink strength/width/colour), Texture Replacement, 60 FPS, Character Shadows,
Depth-of-Field Blur (+ reach), Glow, and the filtering toggles (Bilinear, Force Filtering, Half-texel,
Skip post, Skip stale VRAM).

### Popup buttons

| Button | Meaning |
| --- | --- |
| **Reset** | back to the values the popup opened with |
| **Close** | discard and close |
| **Apply** | apply live (the overlay writes `m_dirty`, the front-end writes through) |
| **Save** | apply **and persist** to `savedata/settings.toml`, then close |

## Controllers tab

```
STATUS
  * P1  |  Xbox Wireless Controller  |  deadzone 15%  |  overlay on
[ Player & Device... ]   [ Button Bindings... ]
GAMEPAD TEST   (live buttons / sticks / triggers)
```

`Player & Device...` owns the player picker, the device combo and the deadzone slider; `Button
Bindings...` opens the binding editor. The live test stays on the tab on purpose: it is what you watch
while binding.

## Window mode at startup

`[video] window_mode` (0 windowed, 1 borderless, 2 fullscreen) and `[video] monitor` are read by the
runtime before the window is created (`ps2_runtime.cpp`, one `[winmode]` log line). **Windowed is
resizable and decorated** -- it used to come up fixed and without a title bar. Borderless fills the
chosen monitor with no chrome; fullscreen uses the monitor's own mode. The legacy `video.fullscreen`
bool is kept in sync for old readers, and `PS2X_WINDOW_MODE` / `PS2X_MONITOR` / `PS2X_WINDOW_W` /
`PS2X_WINDOW_H` still override the file.

The same keys are editable live from the overlay: switching modes clears the previous window state
(fullscreen and borderless are SDL window states, not flags you can simply add on top).

## Settings keys

```toml
[video]
renderer = "opengl"      # opengl | software | parallelgs
window_mode = 2          # 0 windowed, 1 borderless, 2 fullscreen
monitor = 1
window_w = 1366
window_h = 768
render_scale = 2
fullscreen = true        # legacy mirror of window_mode == 2
widescreen = true
texture_pack = true
fps60 = true
glow = true
glowfix = true
shadows = true
dof_blur = true
dof_zfar = 200000
```

See `docs/DEPLOY.md` for where the file lives and `docs/ALTGL-RAYLIB-REMOVAL.md` for the renderer
history.
