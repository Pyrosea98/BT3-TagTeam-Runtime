// [wshud] The widescreen HUD squeeze and the other packet edits (ink darkener, shadow/DoF drops, viewport keep) applied
// by REWRITING GIF packets before a backend parses them. Extracted from ps2_gs_pgs.cpp ([standalone] phase 2): the native
// renderer and paraLLEl-GS share it; neither owns it.
#pragma once
#include <cstddef>
#include <cstdint>
namespace ps2x_wshud
{
#if defined(PS2X_HAVE_SEAMVK)
    bool preprocess(uint8_t pathId, const uint8_t *&data, size_t &size, bool hostGif = false);   // the arbiter, before the native front end (data/size may move to a rebuilt packet); true = edited
    bool apply(const uint8_t *&data, size_t &size, bool hostGif = false);                         // the paraLLEl-GS path, per gif_transfer
    float rawInv(uint32_t fbw);      // [nativehud] the frame's raw HUD squeeze factor (1.0 = none), from the present size
    bool vpKeepOn();                 // [nativehud] PS2X_VPKEEP: the packet walker still blanks the other viewport
    uint32_t inkColor();             // [pgsink] the overlay's outline colour 0xRRGGBB (0 = the game's)
    int inkWidthPct();               // [pgsink] the overlay's outline stroke width in % of a PS2 texel
    void setInkColor(uint32_t rgb);
    void setInkWidthPct(int pct);
    void setPresentSize(uint32_t w, uint32_t h);   // the window's present size (the squeeze factor derives from it)
    void presentSize(uint32_t &w, uint32_t &h);
    float lastInv();                 // stats: the squeeze last applied
    void printStats(double dt);      // stats: the walker's counters (reset per window)
    void noteSwap(uint32_t baseH);   // once per frame swap, with the scanout height in the 1x domain (0 = keep)
#else
    inline bool preprocess(uint8_t, const uint8_t *&, size_t &, bool = false) { return false; }
    inline bool apply(const uint8_t *&, size_t &, bool = false) { return false; }
    inline float rawInv(uint32_t) { return 1.0f; }
    inline bool vpKeepOn() { return false; }
    inline uint32_t inkColor() { return 0u; }
    inline int inkWidthPct() { return 100; }
    inline void setInkColor(uint32_t) {}
    inline void setInkWidthPct(int) {}
    inline void setPresentSize(uint32_t, uint32_t) {}
    inline void presentSize(uint32_t &w, uint32_t &h) { w = h = 0; }
    inline float lastInv() { return 1.0f; }
    inline void printStats(double) {}
    inline void noteSwap(uint32_t) {}
#endif
}
