# Performance metrics (user reports)

Reported measurements per renderer / per upscale, kept so the performance work has real targets and
baselines. The wording is the users'; "good" / "fluid" are theirs.

---

## Linux — Ryzen 3 3200G (Vega 8, 2 GB VRAM) + 8 GB RAM — Kubuntu 24.04 LTS

Monitor maximum resolution.

| renderer | window x1 | fullscreen x1 | window x2 | fullscreen x2 | window x3 | fullscreen x3 |
|---|---|---|---|---|---|---|
| **Vulkan** | good, fluid (30/60) | good, slightly less fluid | bad in combat | worse | menus slower, <=10 fps or freeze | menus slower, <=10 fps or freeze |
| **OpenGL** | better than Vulkan at every upscale (only slightly worse at x3 in splitscreen) | | | | | |
| **Software** | very slow | | | | | |

- With the **HD 2D textures**: a bit slower on Vulkan; no change on OpenGL.
- **OpenGL**'s only problem here: frames momentarily freeze (one could say it pauses) and then it
  comes back.
- **Software**: very slow, and it shows a weird purple screen before starting.

## Windows — 1080p @ 2x vs 720p @ 1x
> Performance was AWFUL on 1080p 2x res. It works amazingly good on 720p 1x res.

---

## To measure locally

Target machine for a new performance pass:

| part | model |
|---|---|
| CPU | Intel **Core i5-4440S** (4c/4t, Haswell) |
| RAM | **2 x 8 GB DDR3-1600** (16 GB) |
| GPU | **ASUS GeForce GT 1030 2 GB GDDR5** |

Record: fps at 1x and 2x, whether the bottleneck is CPU or GPU, the `[frameprof]` / `[ftspike]`
picture, and the thread/contention numbers on a real 4c/4t box.

---

---

## Windows - Ryzen 5 5500 + Radeon RX 580 8 GB (2026-09)

**The numbers from this machine are void.** They were taken by launching
`Dragon Ball Z Budokai Tenkaichi 3 - Recompiled.exe data/SLUS_216.78`, which
skips the front-end. Measured that way the boot stalls in `MEMCARD_CHECK`,
never reaches the menu, and reports 5-10 GAME fps no matter what the layout or
the settings are. Launched the normal way (front-end, then PLAY) the same build
behaves differently, so the harness was the variable, not the game.

What survives from that run, because it is about the harness and not the game:

- A direct launch is not a valid way to measure or even to start this game. Any
  "it runs at 5 fps" report that came from `<exe> <elf>` is measuring the wrong
  thing.
- The A/B still holds as a layout result: disc layout and flat `data/` both
  resolved every file (0 `sceCdSearchFile failed`), so the flat layout is sound.
  See DEPLOY.md for the two resolver fallbacks it needs.
