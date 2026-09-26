# Third-party notices

BT3-Recomp bundles or builds against the third-party components listed below.
This file is a summary for attribution; the **full license texts** ship with each
component and at the links provided. Nothing here is game content — the game is
recompiled at build time from the user's own disc dump.

If you redistribute a release, keep this file (and the license texts it points
to) alongside it.

## Project license

BT3-Recomp is **GPL-3.0** (see [`LICENSE`](LICENSE)). Because it is built on
[PS2Recomp](https://github.com/ran-j/PS2Recomp) (GPL-3.0) and combines
[paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs)
(LGPL-3.0-or-later), the combined work is distributed under **GPL-3.0**.

## Shipped with the runtime

| Component | License | Source |
| --- | --- | --- |
| FFmpeg 7.1.5 — `avcodec`/`avformat`/`avutil`/`swresample`/`swscale` | LGPL-3.0 | https://ffmpeg.org |
| libarchive 3.8.1 + liblzma 5.6.2 + zlib 1.3.1 (statically embedded, install wizard) | BSD-2-Clause / 0BSD | https://libarchive.org |
| SDL2 — `SDL2.dll` | zlib | https://libsdl.org |
| OpenEXR / Imath / Iex / IlmThread 3.3 | BSD-3-Clause | https://openexr.com |
| libjxl — `jxl*` | BSD-3-Clause | https://github.com/libjxl/libjxl |
| libwebp — `libwebp*`, `libsharpyuv` | BSD-3-Clause | https://developers.google.com/speed/webp |
| Brotli — `brotli*` | MIT | https://github.com/google/brotli |
| FreeType — `freetype.dll` | FreeType License (FTL) or GPL-2.0 | https://freetype.org |
| HarfBuzz — `harfbuzz.dll` | MIT | https://harfbuzz.github.io |
| zlib — `zlib1.dll` | zlib | https://zlib.net |
| Microsoft Visual C++ Runtime — `msvcp140*.dll`, `vcruntime140*.dll` | Microsoft redistributable | https://learn.microsoft.com/cpp/windows |
| Mesa (lavapipe, software Vulkan ICD, Windows) | MIT | https://www.mesa3d.org |
| paraLLEl-GS (Vulkan GS backend, when built in) | LGPL-3.0-or-later | https://github.com/Arntzen-Software/parallel-gs |
| raylib (vendored in `ps2xRuntime/third_party/bt3gl`) | zlib | https://www.raylib.com |

## Bundled assets

| Asset | License | Source |
| --- | --- | --- |
| `PZS3US1.AFL`, `PZS3US2.AFL` (NTSC-U AFS file lists) | Apache-2.0 | [ViveTheModder](https://github.com/ViveTheModder/vivethemodder.github.io) (full text in `ps2xRuntime/assets/NOTICE`) |
| `RussoOne-Regular.ttf` | SIL Open Font License 1.1 | https://fonts.google.com/specimen/Russo+One |
| `background.png`, `icon.png` | project assets (see note below) | this repository |

> **Note on app art:** if `background.png` / `icon.png` are derived from the
> game's art, they must be replaced with original or freely-licensed artwork
> before distribution. Verify their origin before shipping a release.

## Build-time dependencies

Used to build from source; not necessarily shipped:

| Component | License | Source |
| --- | --- | --- |
| PS2Recomp (upstream static recompiler) | GPL-3.0 | https://github.com/ran-j/PS2Recomp |
| nlohmann/json | MIT | https://github.com/nlohmann/json |
| xxHash | BSD-2-Clause | https://github.com/Cyan4973/xxHash |
| pefile (PE gate) | MIT | https://github.com/erocarrera/pefile |

## Game & trademark

*Dragon Ball Z: Budokai Tenkaichi 3* © Spike / Bandai Namco Entertainment. This
project is **not affiliated with or endorsed by** them. It exists for
preservation and interoperability, and distributes **no game content**: the game
is recompiled at build time from the user's own disc dump
(PS2, USA, SLUS-21678). "Dragon Ball", "Budokai Tenkaichi" and related marks are
the property of their respective owners and are used descriptively.
