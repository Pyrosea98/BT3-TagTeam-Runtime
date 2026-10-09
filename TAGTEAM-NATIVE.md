# BT3 Tag Team Native: runtime branch

This repository is a fork-style copy of **[z3xox/BT3-Recomp](https://github.com/z3xox/BT3-Recomp)** (GPL-3.0) with the history preserved. The `tagteam-native` branch (the default here) adds the native host side of the **Tag Team mod** for the Power Scale BETA 1.5.1 version of Budokai Tenkaichi 3:

- the Tag Team bridge and match boundary handling (`ps2x_tagteam_bridge.cpp`),
- the native Vulkan interface: glyph atlas, overhead HUD markers, fusion and revive indicators, loading cover, menus and credits (`ps2_ui_*`),
- interpreter budgeting and a straight-line block cache for the mod's injected code,
- native four-seat pad configuration and the private seat mailbox service,
- diagnostics and self tests.

The mod's game logic lives in the companion repository **[BT3-TagTeam-Native](https://github.com/Pyrosea98/BT3-TagTeam-Native)**, derived from **[The Mufti's Tag Team Mod](https://github.com/tehmufti/Budokai-Tenkaichi-3-Tag-Team-Mod-PCSX2-)** (GPL-3.0). Credit for the mod and its gameplay belongs to The Mufti; Power Scale is by LetsPlayBt3.

**No game files are in this repository.** The recompiled game code and all game data are generated or read locally from the user's own disc image (see the `.gitignore` entries for the generated files). Unofficial fan project, not affiliated with the owners of Dragon Ball or Budokai Tenkaichi.

Licence: GPL-3.0 (see `LICENSE`). Third-party notices: `THIRD-PARTY-NOTICES.md`.
