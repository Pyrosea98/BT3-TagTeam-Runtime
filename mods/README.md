# mods/

Loadable game mods. Every `*.so` (Linux) / `*.dll` (Windows) in this folder next to the runner is loaded at
startup and gets the runtime's mod API (`ps2xRuntime/include/ps2x_mod_api.h`): a versioned table of function
pointers for installing hooks on the recompiled game's function table, reading the frame counter and the live
pads, registering a per-frame hook, and feeding the host overlay (overhead bars, kill feed, loading curtain).
A mod exports one symbol, `int ps2xModInit(const Ps2xModApi *api)`, and never links against the runner itself.

Knobs: `PS2X_MODS=0` loads nothing, `PS2X_MODS_DISABLE=name,name` skips the named files, `PS2X_MODS_DIR=<dir>`
reads another folder (the test rig uses it).

Shipped mods:

- `tagteam.so` / `tagteam.dll` -- the Tag Team mod: up to six fighters per match (Team Battle, Free-for-all,
  Co-op), a TAG TEAM entry on the main menu. Source: `ps2xRuntime/mods/tagteam/`. Needs the 128 MB guest RAM
  build. `PS2X_TAGTEAM=0` declines the load; `PS2X_TAGTEAM_*` knobs as documented in the source.
