// ps2x_mod_api.h -- the runtime's interface for loadable game mods (mods/*.so, mods/*.dll).
//
// A mod is a shared library exporting `int ps2xModInit(const Ps2xModApi *api)`. The runtime loads every library in
// the mods/ folder next to the executable (PS2X_MODS_DIR overrides the folder, PS2X_MODS=0 skips them all,
// PS2X_MODS_DISABLE=name,name skips the named ones) once the game's function table is registered, and calls init.
// Init returns 0 on success. The table is owned by the runtime and lives for the whole run; a mod keeps the
// pointer. Everything a mod needs from the process goes through this table: it must not call runtime symbols
// directly (the executable exports none on Windows). The struct only ever grows; `size` and `version` tell a mod
// what it was given.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
struct R5900Context; class PS2Runtime;
extern "C" {
#else
typedef struct R5900Context R5900Context; typedef struct PS2Runtime PS2Runtime;
#endif

#define PS2X_MOD_API_VERSION 1u
#if defined(_WIN32)
#define PS2X_MOD_EXPORT __declspec(dllexport)
#else
#define PS2X_MOD_EXPORT __attribute__((visibility("default")))
#endif

// A recompiled guest routine or a hook standing in for one: (rdram, context, runtime).
typedef void (*Ps2xGuestFn)(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

typedef struct Ps2xModApi
{
    uint32_t size;              // sizeof(Ps2xModApi) as the runtime built it
    uint32_t version;           // PS2X_MOD_API_VERSION
    PS2Runtime *runtime;        // opaque; pass it back to the calls below
    uint32_t ramSize;           // bytes of guest RAM (the Tag Team mod needs 128 MB)

    // --- guest function table: hooks ---
    // Replace the routine at a function entry address; returns 1 on success. Look up what is there (the native
    // routine, or an earlier hook), so a hook can chain. hasFunction: 1 when the address is a known entry.
    int (*replaceFunction)(PS2Runtime *rt, uint32_t address, Ps2xGuestFn fn);
    Ps2xGuestFn (*lookupFunction)(PS2Runtime *rt, uint32_t address);
    int (*hasFunction)(PS2Runtime *rt, uint32_t address);
    int (*stopRequested)(PS2Runtime *rt);   // 1 while the runtime is shutting down

    // --- process facts ---
    uint64_t (*frameCount)(void);           // the game's frame-kick counter
    double (*secondsSinceBoot)(void);       // monotonic seconds since the runtime started
    const char *(*exeDir)(void);            // where the executable lives (honours PS2X_EXEDIR)
    // Live host input for a player (0..3): the 16-bit active-low PS2 button word plus analog bytes.
    uint32_t (*livePadButtons)(int player, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry);
    void (*log)(const char *line);          // one line to the runtime's log (stderr)

    // --- registrations (each slot holds one callback; the last mod to set it wins) ---
    void (*setFrameHook)(Ps2xGuestFn fn);                       // called every frame kick (FUN_00100ab8), before the frame gate
    void (*setOverlayCurtain)(int want);                        // 1: the host overlay draws its loading curtain, 0: lift it
    void (*setHudProvider)(int (*fn)(uint32_t *out, int maxEntries));    // overhead health bars: 8 words per entry (see the overlay)
    void (*setFeedProvider)(int (*fn)(uint32_t *out, int maxEntries), const char *(*name)(uint32_t id));   // kill feed lines + id -> name
} Ps2xModApi;

// Exported by every mod.
typedef int (*Ps2xModInitFn)(const Ps2xModApi *api);

#ifdef __cplusplus
}
#endif
