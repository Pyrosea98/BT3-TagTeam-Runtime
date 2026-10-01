// ps2x_mods.h -- the runtime side of ps2x_mod_api.h: loading mods/ and the slots they register.
#pragma once
#include <cstdint>
#include <atomic>
#include "ps2x_mod_api.h"
extern std::atomic<int> g_ps2xModCurtainWant;   // a mod wants the overlay's curtain (settings overlay reads it)
void ps2xModsInstall(PS2Runtime &runtime);      // load mods/*.so|dll and run their init (once, after the function table)
void ps2xModsFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);   // the frame hooks mods registered
int ps2xModsHud(uint32_t *out, int maxEntries);                 // 0 when no mod provides overhead bars
int ps2xModsFeed(uint32_t *out, int maxEntries);                // 0 when no mod provides a kill feed
const char *ps2xModsCharName(uint32_t id);                      // "" when no mod provides names
