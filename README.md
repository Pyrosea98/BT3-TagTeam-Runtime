# BT3 Tag Team + Real Power Scale — native PC source

This branch contains the Tenkaichi3Decomp-based v0.4.1 port. The older `tagteam-native` branch retains the previous BT3-Recomp implementation; do not mix their installers or data.

- Windows installer and instructions: https://github.com/Pyrosea98/BT3-TagTeam-Native/releases
- Engine upstream: https://github.com/z3xox/Tenkaichi3Decomp
- Original Tag Team mod: https://github.com/tehmufti/Budokai-Tenkaichi-3-Tag-Team-Mod-PCSX2-
- Power Scale: LetsPlayBt3; special thanks to RidJuampa for helping me understand the code: https://www.youtube.com/@RidJuampa

Includes the current native gameplay, fusion/defusion/revival, CPU behavior, team input, assists and clash QTE, HUD/menu/localization and startup-credit implementations. The installer repository owns disc import, normal/2x map selection, save/settings seeding and Windows packaging. Android packaging is separate and maintained by Claude.

See `README.upstream.md`, `docs/port/README.md`, `port/release/Dockerfile` and `port/release/inside.sh` for the engine build. Supply your own supported game data for runtime tests. No ISO, BIOS, AFS archives or extracted game data is included in this source tree. Release binaries fetch game data from the user's authenticated import.

Preserve the AGPL-3.0 license and all upstream notices. The installer includes attribution and component licenses. See `SOURCE_REVISION.json` for the exact upstream base and modified-source manifest.
