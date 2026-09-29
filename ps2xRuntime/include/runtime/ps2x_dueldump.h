#pragma once

// [dueldump] Total capture of the Duel menu flow, for reverse-engineering it end to end.
//
// Arms when the main-menu cursor sits on the Duel entry (row 3 -> state 0x26), captures the
// whole flow (0x26 -> 0x27 -> 0x28/0x29) and disarms when it returns to 0x04. Everything is
// gated by PS2X_DUELDUMP=1 (default OFF); the module costs a single pointer compare per frame
// when disabled.
//
// Artifacts (PS2X_DUELDUMP_DIR, default dumps/duel_<timestamp>/):
//   ram/arm_<frame>.bin                    full EE RAM (32 MB) at arm
//   ram/delta_<state>_<frame>.bin(+.idx)    changed ranges vs the previous dump
//   hex/*.txt                               annotated hex of the menu/duel structures
//   events.csv                              state/cursor/entry/handler/duel fields per change
//   vram/vram_<state>_<frame>.bin           GS VRAM (4 MB) at each state transition
//   textures.csv                            every texture sample (from the raster workers)
//   png/<hash>.png                          decoded texture, once per unique hash
//   meta.txt                                build/env/arm-disarm/timing
//
// Compiled into ps2_runtime. All file writes happen here, on the frame hook (guest thread);
// the raster workers only push POD records into a locked queue.

#include <cstdint>

struct R5900Context;
class PS2Runtime;

namespace ps2x_dueldump
{
    bool enabled();

    // Where the main menu's cursor is pointing, using the game's own row formula
    // (0x33643C..0x33648C). entry = RAM address in the jump table, handler = the per-row
    // confirm handler stored there, target = the screen state that row jumps to (0xFFFFFFFF
    // for the hidden Network row, which has none). Lived in ps2x_net_menu.h until the Dragon
    // Net entry was retired; this dump was its only consumer.
    struct CursorInfo
    {
        uint32_t row = 0xFFFFFFFFu, idx = 0xFFFFFFFFu;
        uint32_t entry = 0xFFFFFFFFu, handler = 0u, target = 0xFFFFFFFFu;
        bool valid = false;
    };
    bool queryCursor(const uint8_t *rdram, CursorInfo &out);

    // One call per frame from the frame hook (game_overrides.cpp).
    void tick(uint8_t *rdram, PS2Runtime *runtime);

    // Texture sample record, pushed from GSRasterizer::applyTexReplacement (raster workers).
    // `how` is a short tag for the sampling path ("decal"/"sprite"/"3d"/"rt"/"fb"...).
    struct TexSample
    {
        uint64_t frame = 0, hash = 0, texKey = 0, clutKey = 0;
        uint32_t tbp0 = 0, tbw = 0, psm = 0, tcc = 0, cbp = 0, cpsm = 0;
        uint32_t w = 0, h = 0, srcAddr = 0;
        int32_t  fbp = -1, zbp = -1, tfx = -1;
        uint8_t  swizzle = 0, packHit = 0, cacheHit = 0;
        char     how[12] = {0};
    };
    void pushTexSample(const TexSample &s);

    // Called from the two texture call sites (GSRasterizer inline decode + the decode pool)
    // with the decoded RGBA in hand. Computes the PCSX2-compatible identity, records the
    // sample and offers the decode for a one-per-hash PNG. Cheap no-op when not armed.
    void offerTextureSample(const uint8_t *vram, uint32_t tbp0, uint32_t tbw, uint32_t psm,
                            uint32_t tw, uint32_t th, const uint32_t *clut,
                            uint32_t ta0, uint32_t aem, uint32_t ta1,
                            uint32_t cbp, uint32_t csa, uint32_t csm, uint32_t cpsm,
                            uint64_t texKey, int w, int h, const uint8_t *rgba, const char *how);

    // Decoded-texture hook: called where the renderer has the decoded RGBA in hand (so PNGs can
    // be written once per unique hash while armed). `rgba` is w*h*4 bytes.
    void offerDecodedTexture(uint64_t hash, const uint8_t *rgba, uint32_t w, uint32_t h);

    // [duelsettings] PS2X_DUELSETTINGS=1: independent of PS2X_DUELDUMP. Every time the Battle
    // Settings cursor (duelObj+0x13C) changes it snapshots duelObj[0..0x400] + stateObj+0x600..0x660
    // to dumps/duelsettings_<ts>/ and logs the offsets that changed vs the previous snapshot -- the
    // diff is what reveals the per-row VALUE fields (Duel Time, COM level, referee, ...).
    void tickSettings(uint8_t *rdram);

    // [dueltime] PS2X_DUELTIME=1: log the Duel Time plumbing while the Duel flow is up -- the
    // live cursor (duelObj+0x13C), the value table the main executable holds (RAM 0x2C3480, u16[5]
    // = [INF,60,90,180,240]) and the committed copies (stateObj+0x620..0x640), on every change, to
    // dumps/dueltime_<ts>/time.csv. This is what shows WHERE the chosen time is committed, since
    // writing +0x13C alone did not stick.
    void tickTime(uint8_t *rdram);
}
