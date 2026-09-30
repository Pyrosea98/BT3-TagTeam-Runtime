# Contributors & roles

BT3-Recomp — a statically recompiled, native PC port of *Dragon Ball Z: Budokai
Tenkaichi 3* (PS2, USA, SLUS-21678), built on
[PS2Recomp](https://github.com/ran-j/PS2Recomp).

The areas below are taken from the commit history (who touched which part of the
tree), not from anybody's self-description — see `git shortlog -sne` and
`git log --author=<dev> --name-only`.

## Developers

| Dev | Role | Areas |
| --- | --- | --- |
| **z3xox** | Owner / Lead developer | Recompiler (`ps2xRecomp`), the runtime core (`src/lib`: EE, GS, VU1, scheduler, the whole GS replay), **the native Vulkan renderer** (the engine seam — `ps2_seamvk.cpp`) and its paraLLEl-GS counterpart, game overrides, runtime patches, the game generators (`games/bt3`), docs |
| **RexxColder** | **Supporter** / Collaborator | **The OpenGL renderer** (`src/gfx`: the GL context, the GS→GL replay, render targets, image IO, the UI bridge, the video overlay), **Optimisation** (perf/async, batching, the frame budget), the front-end / launcher / install wizard and its ISO9660 work (`src/frontend`), the native IOP modules (`src/iop_native`), input & gamepads, build & release (glibc floor gate, packaging), deploy layout, game data (AFS/AFL), **texture packs** (encoding, and the runtime-side replacement work), docs |
| **valenvivaldi** | Collaborator | macOS arm64 port, packaging, audio |
| **KaibaSammy** | Collaborator (Mods) | Mod Manager project, mod design, mod support |

## Credits

Third-party work this project builds on or ships.

| Author | Contribution | License |
| --- | --- | --- |
| **ran-j** | [PS2Recomp](https://github.com/ran-j/PS2Recomp) — static recompiler (upstream) | GPL-3.0 |
| **ViveTheModder** | NTSC-U AFS file lists (`PZS3US1.AFL`/`PZS3US2.AFL`) | Apache-2.0 |
| **Arntzen Software** | [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) — GS in Vulkan compute | LGPL-3.0-or-later |
| **Sal9im** | "4K 2D Textures Lite" — the original texture pack ([GBATemp](https://gbatemp.net/members/sal9im.672099/)). Optional and **not distributed**: drop it in `data/Textures/`. The DXT5 encoding and the runtime-side replacement work are RexxColder's | (pack's own terms) |

## License

This repository is GPL-3.0 (see `LICENSE`). *Dragon Ball Z: Budokai Tenkaichi 3*
© Spike / Bandai Namco. This project is not affiliated with or endorsed by them;
it distributes no game content — the game is recompiled at build time from the
user's own disc image, and the optional texture pack above is supplied by the
user too.
