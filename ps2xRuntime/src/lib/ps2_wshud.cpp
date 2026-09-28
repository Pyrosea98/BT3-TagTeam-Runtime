// [wshud] see include/runtime/ps2_wshud.h. Body extracted verbatim from ps2_gs_pgs.cpp; `State` became the walker's own
// WsHudState (the HUD tracker, the rebuilt-packet buffer, the swap counter and the scanout height it used from the backend).
#include "runtime/ps2_wshud.h"
#include "runtime/ps2_gs_gpu_renderer.h"   // GsGpuRenderer statics: outline / shadows / DoF toggles, ink strength
#include "runtime/ps2_netplay.h"   // [vpdrop] follow the netplay player
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

extern float g_ps2xWsHudInv;   // [wshud] per-frame HUD squeeze factor from the present (1.0 = off), ps2_runtime.cpp
extern std::atomic<int> g_wsHudLayout;   // overlay: 0 centered, 1 edge-pinned, 2 custom (-1 = unset)
extern std::atomic<int> g_wsHudOffLQ, g_wsHudOffCQ, g_wsHudOffRQ;   // custom offsets x16
bool ps2FightUpdateRecent();   // ps2_stepcensus.cpp [wshudmenu]: the fight update ran within the last 2 render frames (menus: never)

namespace ps2x_wshud
{
namespace
{
static std::atomic<uint32_t> g_presentW{0}, g_presentH{0};   // on-screen size of the presented frame
static std::atomic<uint32_t> g_inkColor{0};   // [pgsink] outline colour 0xRRGGBB (0 = the game's black); the darkener subtracts, so its RGB is modulated by the complement
static std::atomic<int> g_inkWidthPct{100};   // [pgsink] outline stroke width, % of a PS2 texel (the edge-detect shift); 100 = native
struct WsHudState
{
    std::mutex mtx;
    // [pgswshud] widescreen HUD squeeze by packet rewrite: the OpenGL renderer squeezes HUD draws per primitive
    // (ps2_gs_gpu_renderer.cpp [wshud]); paraLLEl-GS draws what it is given, so the same rule is applied by
    // rewriting the X of HUD vertices inside the GIF packets before they reach the backend.
    struct WsHud
    {
        struct Ctx { uint32_t fbp = 0, fbw = 0, fpsm = 0, zte = 0, ztst = 0; float ofx = 0.f, ofy = 0.f; uint64_t test = 0, frame = 0, alpha = 0, tex0 = 0, scissor = 0; size_t tex0Off = ~size_t(0), alphaOff = ~size_t(0); };
        Ctx ctx[2];
        uint64_t primRaw = 0, prmode = 0; bool prmodeCont = true;
        struct V { size_t off = 0; bool packed = false, mapped = false; uint16_t x = 0, y = 0; uint32_t z = 0; };
        V q[3]; int qn = 0;
        bool frameHad3d = false, active = false; int no3dRun = 0; uint64_t lastSwap = ~0ull;
        uint64_t mappedVerts = 0, hudPrims = 0, scissorsMapped = 0, splitPrims = 0; float lastInv = 1.0f;
        // [wsjit] debounce of the raw squeeze: menus draw the HUD with alternating frame widths
        // (512/640), which made inv oscillate frame-to-frame (visible jitter). Only commit a value
        // that is stable for two consecutive evaluations.
        float lastRawInv = 0.f; int invRun = 0; bool invInit = false;
        // [pgsink] RGBAQ writes since the last kick (packet offsets; packed = 16-byte qword, else 8-byte reg), for the darkener
        struct RgbaW { size_t off; bool packed; }; RgbaW rgba[8]; int rgbaN = 0;
        RgbaW uv[8]; int uvN = 0;   // [pgsink] UV writes since the last kick (the edge-detect shift rewrite)
        uint64_t lastRgbaq = 0x3f80000080808080ull;   // last RGBAQ value seen (A+D / REGLIST form), for register restores
        uint64_t inkDropped = 0, inkScaled = 0, inkShifted = 0, shadowDropped = 0, dofDropped = 0, maskNeutralized = 0, bloomEdits = 0, bloomRestores = 0;
    } wshud;
    std::vector<uint8_t> wsBuf;   // [pgswshud] rebuilt packet when quads are subdivided at layout breakpoints
    uint32_t baseH = 0;      // scanout height in the 1x domain (448 NTSC)
    uint64_t swaps = 0;      // frame swaps seen (the walker resets its per-frame flags on a new one)
};
WsHudState &state() { static WsHudState *s = new WsHudState; return *s; }
}
static float wsHudMapX(float x, float W, float inv)
{
    const float half = 0.5f * W, k = W / 512.0f;
    auto cen = [&](float v) { return half + (v - half) * inv; };
    int layout = g_wsHudLayout.load(std::memory_order_relaxed); if (layout < 0) layout = 0;
    if (layout <= 0) return cen(x);
    const float offL = g_wsHudOffLQ.load(std::memory_order_relaxed) / 16.0f, offC = g_wsHudOffCQ.load(std::memory_order_relaxed) / 16.0f, offR = g_wsHudOffRQ.load(std::memory_order_relaxed) / 16.0f;
    const float s1 = 124.f * k, s2 = 216.f * k, s3 = 296.f * k, s4 = 388.f * k;
    float t0 = 0.f, t1 = s1 * inv, t2 = cen(s2), t3 = cen(s3), t4 = W - (W - s4) * inv, t5 = W;
    if (layout >= 2)
    {
        t0 += offL * k; t1 += offL * k; t2 += offC * k; t3 += offC * k; t4 += offR * k; t5 += offR * k;
        if (t1 > t2 - 2.f) t1 = t2 - 2.f;
        if (t3 > t4 - 2.f) t4 = t3 + 2.f;
    }
    if (x <= s1) return t0 + (x - 0.f) * (t1 - t0) / s1;
    if (x <= s2) return t1 + (x - s1) * (t2 - t1) / (s2 - s1);
    if (x <= s3) return t2 + (x - s2) * inv;
    if (x <= s4) return t3 + (x - s3) * (t4 - t3) / (s4 - s3);
    return t4 + (x - s4) * inv;
}
// One primitive is complete: decide with the OpenGL renderer's rule and map its vertices' X in place.
static void wsHudKickLocked(WsHudState &s, uint8_t *data, float inv)
{
    WsHudState::WsHud &h = s.wshud;
    const uint32_t primType = uint32_t(h.primRaw & 7u);
    const uint64_t attr = h.prmodeCont ? h.primRaw : h.prmode;
    const bool fst = ((attr >> 8) & 1u) != 0;
    const uint32_t ci = uint32_t((attr >> 9) & 1u);
    const WsHudState::WsHud::Ctx &c = h.ctx[ci];
    const bool isTri = (primType >= 3u && primType <= 5u), isSprite = (primType == 6u);
    if (isTri && c.zte && c.ztst >= 2u) h.frameHad3d = true;
    {   // [pgsink] outline EDGE-DETECT shift (PS2X_PGS_INKSHIFT=<texels>, experiment): BT3's chain draws page 336's CT16 view
        // with fbmsk ffff00ff and a Cd - Cs blend (0x62), the texture read one texel to the right of the destination;
        // that one texel is the stroke width. Rewrite U := X + shift for those sprites (needs PS2X_PGS_SSTEX=1 so the
        // silhouette is read per sample).
        static const float s_inkShiftEnv = [](){ const char *v = std::getenv("PS2X_PGS_INKSHIFT"); return v && v[0] ? float(std::atof(v)) : 0.0f; }();
        const float s_inkShift = s_inkShiftEnv > 0.0f ? s_inkShiftEnv : float(g_inkWidthPct.load(std::memory_order_relaxed)) / 100.0f;
        const bool tmeK = ((attr >> 4) & 1u) != 0, fstK = ((attr >> 8) & 1u) != 0;
        if (s_inkShift > 0.0f && s_inkShift < 1.0f && isSprite && tmeK && fstK && c.fbp == 336u && c.fpsm == 2u && uint32_t(c.frame >> 32) == 0xffff00ffu && (c.alpha & 0xFFu) == 0x62u && h.qn >= 2 && h.uvN >= 2)
        {
            for (int i = 0; i < 2 && i < h.uvN; i++)
            {
                uint8_t *q = data + h.uv[i].off;
                uint64_t lo; std::memcpy(&lo, q, 8);
                const uint32_t u = uint32_t(lo & 0x3FFFu);
                const float x16 = float(h.q[i].x);   // raw 12.4 X (offset included); U is in texels 12.4 relative to the texture
                (void)x16;
                // the game's U = X_frame + 1 texel: keep everything but replace the +1 texel by +shift
                const int32_t nu = int32_t(u) - 16 + int32_t(s_inkShift * 16.0f + 0.5f);
                const uint64_t nlo = (lo & ~0x3FFFull) | uint64_t(uint32_t(nu < 0 ? 0 : nu) & 0x3FFFu);
                std::memcpy(q, &nlo, 8);
            }
            h.inkShifted++;
        }
    }
    h.uvN = 0;
    {   // [pgsfx] DoF off, mode 3 (BLOOM): rebuild the blur buffer from bright pixels only, so the composite has nothing
        // blurred to drag along with an aura or a dash trail. (a) the buffer's clear sprites paint the threshold grey,
        // (b) the downsample sprites subtract the destination ((Cs - Cd) * FIX/128, FIX = 0x80 -> scene - grey, clamped),
        // (c) the composite becomes additive ((Cs - 0) * As + Cd), (d) the depth-mask sprites write "far" everywhere
        // (TCC := 0, vertex alpha 0) so the composite passes everywhere. PS2X_PGS_BLOOMTHR = the grey (default 0x90).
        static const int s_dofMode4 = [](){ const char *e = std::getenv("PS2X_PGS_DOFMODE"); return e && e[0] ? std::atoi(e) : 2; }();
        static const uint8_t s_thr = [](){ const char *e = std::getenv("PS2X_PGS_BLOOMTHR"); const int v = e && e[0] ? int(std::strtol(e, nullptr, 0)) : 0x90; return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }();
        if (s_dofMode4 == 3 && !GsGpuRenderer::dofBlurEnabled() && isSprite && h.qn >= 2)
        {
            const bool tmeB = ((attr >> 4) & 1u) != 0, abeB = ((attr >> 6) & 1u) != 0;
            const uint32_t tpsmB = uint32_t((c.tex0 >> 20) & 0x3Fu), ttbp0 = uint32_t(c.tex0 & 0x3FFFu), ttw = uint32_t((c.tex0 >> 26) & 0xFu), tth = uint32_t((c.tex0 >> 30) & 0xFu), ttbw = uint32_t((c.tex0 >> 14) & 0x3Fu);
            const bool blurBuf = c.fbp == 336u && c.fbw == 4u && c.fpsm == 0u;
            // (a) clear of the 256x256 buffer: untextured, no blend, into fbp 336 fbw 4
            if (blurBuf && !tmeB && !abeB)
            {
                for (int i = 0; i < h.rgbaN; i++) { uint8_t *q = data + h.rgba[i].off; if (h.rgba[i].packed) { q[0] = s_thr; q[4] = s_thr; q[8] = s_thr; } else { q[0] = s_thr; q[1] = s_thr; q[2] = s_thr; } }
                h.bloomEdits++;
            }
            // (b) downsample of the scene: textured from a scene buffer as CT32 512x512, blended, into the blur buffer
            else if (blurBuf && tmeB && abeB && tpsmB == 0u && ttw == 9u && tth == 9u && (ttbp0 == 0u || ttbp0 == 3584u))
            {
                if (c.alphaOff != ~size_t(0) && c.alpha != 0x80000000A4ull) { const uint64_t a = 0x80000000A4ull; std::memcpy(data + c.alphaOff, &a, 8); h.ctx[ci].alpha = a; }
                h.bloomEdits++;
            }
            // (c) the composite: CT24 256x256 read of block 10752 with a destination-alpha test into a scene buffer
            else if ((c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u) && tmeB && abeB && tpsmB == 1u && ttbp0 == 10752u && ttbw == 4u && ttw == 8u && tth == 8u && ((c.test >> 14) & 1u))
            {
                if (c.alphaOff != ~size_t(0) && (c.alpha & 0xFFu) != 0x48u) { const uint64_t a = (c.alpha & ~0xFFull) | 0x48ull; std::memcpy(data + c.alphaOff, &a, 8); h.ctx[ci].alpha = a; }
                h.bloomEdits++;
            }
            // (d) the depth-mask sprites: write "far" everywhere
            else if (tmeB && (c.fpsm == 2u || c.fpsm == 10u) && (c.fbp == 0u || c.fbp == 112u) && uint32_t(c.frame >> 32) == 0x3fffu && (tpsmB == 50u || tpsmB == 58u))
            {
                if (c.tex0Off != ~size_t(0) && (c.tex0 & (1ull << 34))) { uint64_t t0 = c.tex0 & ~(1ull << 34); std::memcpy(data + c.tex0Off, &t0, 8); h.ctx[ci].tex0 = t0; }
                for (int i = 0; i < h.rgbaN; i++) { uint8_t *q = data + h.rgba[i].off; if (h.rgba[i].packed) q[12] = 0x00; else q[3] = 0x00; }
                h.maskNeutralized++;
            }
        }
    }
    {   // [pgsfx] DoF off, mode 2: the depth-mask sprites (16-bit view of a scene buffer, FBMSK 0x3fff, reading the Z
        // buffer as PSMZ16) get their texture alpha turned off (TEX0.TCC := 0) and a vertex alpha of 0x80, so they write
        // "near" (alpha bit 7 = 1) everywhere: no far blur, while draws after them (the aura) still open the mask.
        static const int s_dofMode3 = [](){ const char *e = std::getenv("PS2X_PGS_DOFMODE"); return e && e[0] ? std::atoi(e) : 2; }();   // 2 = mask pass writes near (default), 1 = mask pass off, 0 = drop the composite (kills the glow)
        const bool tmeM = ((attr >> 4) & 1u) != 0;
        const uint32_t tpsmM = uint32_t((c.tex0 >> 20) & 0x3Fu);
        if (s_dofMode3 == 2 && !GsGpuRenderer::dofBlurEnabled() && isSprite && tmeM && (c.fpsm == 2u || c.fpsm == 10u) && (c.fbp == 0u || c.fbp == 112u)
            && uint32_t(c.frame >> 32) == 0x3fffu && (tpsmM == 50u || tpsmM == 58u))
        {
            if (c.tex0Off != ~size_t(0) && (c.tex0 & (1ull << 34)))
            {   // clear TCC once per TEX0 write
                uint64_t t0 = c.tex0 & ~(1ull << 34);
                std::memcpy(data + c.tex0Off, &t0, 8);
                h.ctx[ci].tex0 = t0;
            }
            for (int i = 0; i < h.rgbaN; i++) { uint8_t *q = data + h.rgba[i].off; if (h.rgba[i].packed) q[12] = 0x80; else q[3] = 0x80; }
            h.maskNeutralized++;
        }
    }
    {   // [nodashblur] PS2X_NODASHBLUR=1: the dash motion blur -- while dashing the game replaces the frame with its 4:1
        // box downscale (16 full-frame sprites reading the 128-px CT32 buffer at 0x2e00 into fbp 0/112 with ALPHA
        // (0,1,0,1), vertex alpha 0x80) before the characters are drawn. Vertex alpha := 0 makes the blend a no-op
        // (Cv = Cd); the sprites still run, nothing else changes. The native renderer skips the same draws.
        static const bool s_noDash = [](){ const char *v = std::getenv("PS2X_NODASHBLUR"); return v && v[0] && v[0] != '0'; }();
        const bool tmeD = ((attr >> 4) & 1u) != 0;
        if (s_noDash && isSprite && tmeD && (c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u)
            && uint32_t(c.tex0 & 0x3FFFu) == 0x2e00u && uint32_t((c.tex0 >> 20) & 0x3Fu) == 0u && uint32_t((c.tex0 >> 26) & 0xFu) == 7u)
        {
            for (int i = 0; i < h.rgbaN; i++) { uint8_t *q = data + h.rgba[i].off; if (h.rgba[i].packed) q[12] = 0x00; else q[3] = 0x00; }
            h.maskNeutralized++;
        }
    }
    {   // [pgsfx] Character Shadows / Depth-of-Field Blur toggles (the OpenGL renderer's draw classes, ps2_gs_gpu_renderer.cpp
        // PS2X_NODECAL=1 and PS2X_NODOF): the shadow decal tiles are triangles sampling block 10752 as PSMCT24 256x256 into
        // the scene; the DoF composite samples block 10752 as PSMCT32 into the scene. Off = collapse the primitive.
        const bool tmeF = ((attr >> 4) & 1u) != 0;
        const bool sceneF = (c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u);
        if (tmeF && sceneF && (isTri || isSprite) && uint32_t(c.tex0 & 0x3FFFu) == 10752u)
        {
            const uint32_t tpsm = uint32_t((c.tex0 >> 20) & 0x3Fu), tw = uint32_t((c.tex0 >> 26) & 0xFu), th = uint32_t((c.tex0 >> 30) & 0xFu);
            // CT24 256x256 reads of the blur buffer: with a destination-alpha test they are the blur/glow COMPOSITES
            // (full-screen, lerp where the depth mask allows); without it, the shadow decal tiles on the ground.
            const bool dateF = ((c.test >> 14) & 1u) != 0;
            const bool shadowTile = isTri && tpsm == 1u && tw == 8u && th == 8u && !dateF;
            const bool blurComp = tpsm == 1u && tw == 8u && th == 8u && dateF;
            // the DoF composite lerps by destination alpha (0x54 / 0x68); the Kaioken glow reads the same page additively
            const uint32_t bm = uint32_t(c.alpha & 0xFFu);
            const bool abeF = ((attr >> 6) & 1u) != 0;
            const uint32_t tbw = uint32_t((c.tex0 >> 14) & 0x3Fu);
            {   // PS2X_PGS_WSHUDLOG=1: the classes of scene draws that read block 10752 (DoF copy vs glow blur vs shadow tiles)
                static const bool s_fl = [](){ const char *v = std::getenv("PS2X_PGS_WSHUDLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_fn = 0;
                static uint64_t s_seen[32]; static int s_seenN = 0;
                const uint64_t key = (uint64_t(tpsm) << 40) | (uint64_t(tbw) << 32) | (uint64_t(tw) << 28) | (uint64_t(th) << 24) | (uint64_t(bm) << 8) | uint64_t(primType) | (uint64_t(abeF) << 4) | (uint64_t((c.test >> 14) & 1u) << 5);
                bool seen = false; for (int i = 0; i < s_seenN; i++) if (s_seen[i] == key) seen = true;
                if (s_fl && !seen && s_seenN < 32 && s_fn < 32) { s_seen[s_seenN++] = key; s_fn++; std::fprintf(stderr, "[pgsfx] read of 10752: psm %u tbw %u %ux%u blend %02x abe %d prim %u fbp %u test %llx w %.0f\n", tpsm, tbw, 1u << tw, 1u << th, bm, abeF ? 1 : 0, primType, c.fbp, (unsigned long long)c.test, (h.qn >= 2 ? std::fabs(float(h.q[1].x) - float(h.q[0].x)) / 16.0f : 0.f)); }
            }
            // the DoF composite reads the 512-wide (tbw 8) half-height scene copy and lerps by destination alpha; the
            // Kaioken glow reads the 256x256 blur (tbw 4)
            static const int s_dofMode2 = [](){ const char *e = std::getenv("PS2X_PGS_DOFMODE"); return e && e[0] ? std::atoi(e) : 2; }();   // 2 = mask pass writes near (default), 1 = mask pass off, 0 = drop the composite (kills the glow)
            const bool dofComp = s_dofMode2 == 0 && (blurComp || (tpsm == 0u && abeF && (bm == 0x54u || bm == 0x68u)));
            if ((shadowTile && !GsGpuRenderer::shadowsEnabled()) || (dofComp && !GsGpuRenderer::dofBlurEnabled()))
            {
                const int n = isSprite ? 2 : 3;
                if (h.qn >= n) { const uint16_t x0 = h.q[0].x; for (int i = 0; i < n; i++) { std::memcpy(data + h.q[i].off, &x0, 2); h.q[i].x = x0; h.q[i].mapped = true; } if (shadowTile) h.shadowDropped++; else h.dofDropped++; }
                h.rgbaN = 0; h.uvN = 0;
                return;
            }
        }
    }
    {   // [pgsink] the cel-outline DARKENER (the OpenGL path's gate): untextured, blended, ALPHA 0x52 = Cd - Cs * Ad into a
        // scene buffer. Cel Outline OFF collapses its vertices (zero area, nothing drawn); ink strength scales its
        // vertex colour by pct/199 (199% is the hardware coefficient, which the backend already applies).
        const bool tme = ((attr >> 4) & 1u) != 0, abe = ((attr >> 6) & 1u) != 0;
        if (!tme && abe && (c.alpha & 0xFFu) == 0x52u && (c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u) && (isTri || isSprite))
        {
            const int n = isSprite ? 2 : 3;
            if (!GsGpuRenderer::outlineEnabled())
            {
                if (h.qn >= n) { const uint16_t x0 = h.q[0].x; for (int i = 0; i < n; i++) { std::memcpy(data + h.q[i].off, &x0, 2); h.q[i].x = x0; h.q[i].mapped = true; } h.inkDropped++; }
            }
            else
            {
                const int pct = GsGpuRenderer::inkStrengthPct();
                const uint32_t col = g_inkColor.load(std::memory_order_relaxed);
                if (pct != 199 || col != 0u)
                {
                    const float k = float(pct) / 199.0f;
                    // colour: the draw SUBTRACTS Cs, so keep of the game's Cs only the complement of the wanted colour
                    const float kc[3] = { k * float(255u - ((col >> 16) & 0xFFu)) / 255.0f, k * float(255u - ((col >> 8) & 0xFFu)) / 255.0f, k * float(255u - (col & 0xFFu)) / 255.0f };
                    for (int i = 0; i < h.rgbaN; i++)
                    {
                        uint8_t *q = data + h.rgba[i].off;
                        if (h.rgba[i].packed) { for (int ch = 0; ch < 3; ch++) { const float v = float(q[ch * 4]) * kc[ch]; q[ch * 4] = uint8_t(v > 255.f ? 255.f : v + 0.5f); } }
                        else { for (int ch = 0; ch < 3; ch++) { const float v = float(q[ch]) * kc[ch]; q[ch] = uint8_t(v > 255.f ? 255.f : v + 0.5f); } }
                    }
                    if (h.rgbaN) h.inkScaled++;
                }
            }
            h.rgbaN = 0;
            return;
        }
        h.rgbaN = 0;
    }
    {   // [primlog] PS2X_PGS_PRIMLOG=<swap>: every textured primitive in the top band (y1 < 110) from that swap on --
        // box, projection type (fst: 1 = UV sprite, 0 = STQ / 3D-projected) -- to compare a menu row at rest vs scrolling.
        // Placed before the scene/HUD gates on purpose: menus have no 3D-tested scene and the gates close.
        static const unsigned long long s_from = [](){ const char *v = std::getenv("PS2X_PGS_PRIMLOG"); return v && v[0] ? std::strtoull(v, nullptr, 10) : ~0ull; }();
        static unsigned s_n = 0;
        const int np = isSprite ? 2 : (isTri ? 3 : 0);
        if (np && s.swaps >= s_from && s_n < 1500000u && ((attr >> 4) & 1u) && h.qn >= np)
        {
            float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f;
            for (int i = 0; i < np; i++) { const float x = h.q[i].x / 16.0f - c.ofx, y = h.q[i].y / 16.0f - c.ofy; bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y); }
            static const uint32_t s_tbp = [](){ const char *v = std::getenv("PS2X_PGS_PRIMLOG_TBP"); return v && v[0] ? (uint32_t)std::strtoul(v, nullptr, 16) : 0xFFFFFFFFu; }();   // PS2X_PGS_PRIMLOG_TBP=<hex>: any band, prims texturing from that base
            const bool tbpHit = (uint32_t)(c.tex0 & 0x3FFFu) == s_tbp;
            if (tbpHit || (by1 < 110.f && by0 > -10.f && bx1 - bx0 > 4.f))
            {
                s_n++;
                std::fprintf(stderr, "[primlog] swap=%llu prim=%u fst=%d ctx=%u fbp=%u fbw=%u zte=%u ztst=%u z=%u/%u/%u active=%d inv=%.3f box=(%.1f,%.1f)-(%.1f,%.1f) w=%.1f h=%.1f tex0=%llx\n",
                             (unsigned long long)s.swaps, primType, fst ? 1 : 0, ci, c.fbp, c.fbw, c.zte, c.ztst, h.q[0].z, h.q[1].z, np > 2 ? h.q[2].z : 0u, h.active ? 1 : 0, inv, bx0, by0, bx1, by1, bx1 - bx0, by1 - by0, (unsigned long long)(c.tex0 & 0xFFFFFFFFFFull));
            }
        }
    }
    // [wshudmenu] the squeeze is for the FIGHT HUD only. The scene gate (h.active) also opens in the character
    // select, whose 3D model preview counts as a scene, and then the row tiles of the roster (64x64 top-band
    // strips) were mapped whenever the gate happened to be on -- the row visibly narrowed while it scrolled and
    // snapped back at rest (user, widescreen). Menus never run the fight update, so gate on it -- on the plain
    // "ran recently" form: the 60-frame streak gate left the HUD unsqueezed for the fight's first second (user).
    if (!h.active || inv >= 0.999f || !::ps2FightUpdateRecent()) return;
    if (!(isTri || isSprite)) return;
    if (!(c.fbp == 0u || c.fbp == 112u) || !(c.fpsm == 0u || c.fpsm == 1u)) return;
    const float W = (c.fbw * 64u >= 320u && c.fbw * 64u <= 1024u) ? float(c.fbw * 64u) : 512.0f;
    const int n = isSprite ? 2 : 3;
    if (h.qn < n) return;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; bool zAllZero = true;
    for (int i = 0; i < n; i++)
    {
        const float x = h.q[i].x / 16.0f - c.ofx, y = h.q[i].y / 16.0f - c.ofy;
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        if (h.q[i].z != 0u) zAllZero = false;
    }
    const float w = x1 - x0, hh = y1 - y0;
    bool hud = false;
    if (isSprite) hud = (w > 0.f && w < 0.8f * W && hh > 0.f && hh < 300.f && y1 < 96.f && (!c.zte || c.ztst == 1u));
    // triangles: flat (z exactly 0), UV-mapped, in the top band. Width: narrower than 0.8 W, OR wide but not
    // full-width -- BT3's bar chain draws its gate/backing as ONE quad spanning both bars (x 59..530); left
    // unsqueezed it exposes the layer beneath at the squeezed bar's end. Full-frame fades never fit the band.
    else hud = (fst && zAllZero && hh < 300.f && y1 < 96.f && (w < 0.8f * W || x0 > 8.f));   // the gate quads run 59..572, past the frame edge
    {   // PS2X_PGS_WSHUDLOG=1: every top-band primitive on the scene buffers with its attributes and the decision
        static const bool s_log = [](){ const char *v = std::getenv("PS2X_PGS_WSHUDLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_n = 0;
        // the sliver region: the left end of the opponent's bar (frame x 250..330, y 10..70), pre-map coordinates
        if (s_log && s_n < 400 && y1 > 10.f && y0 < 70.f && x1 > 250.f && x0 < 330.f && (c.fbp == 0u || c.fbp == 112u))
        {
            s_n++;
            std::fprintf(stderr, "[wshudlog] #%u prim %u fst %d tme %d abe %d ctx %u fbp %u fbmsk %08x test %llx alpha %llx tex0 %llx scissor %llx z %u/%u/%u box (%.1f,%.1f)-(%.1f,%.1f) -> %s\n",
                         s_n, primType, fst ? 1 : 0, int((attr >> 4) & 1u), int((attr >> 6) & 1u), ci, c.fbp, uint32_t(c.frame >> 32), (unsigned long long)c.test, (unsigned long long)c.alpha,
                         (unsigned long long)c.tex0, (unsigned long long)c.scissor, h.q[0].z, h.q[1].z, n > 2 ? h.q[2].z : 0u, x0, y0, x1, y1, hud ? "MAP" : "keep");
        }
    }
    if (!hud) return;
    h.hudPrims++;
    {   // [hudchips] a small element inside a bridge zone (between a pinned group and the centre piece) moves with its group at
        // the group's scale instead of being stretched with the zone: the buff chips next to the ki gauge (42x22 quads at
        // x 100..214) came out 1.5x wide under the edge layout. Same rule as the native front end (ps2_seamgs.cpp [hudchips]).
        int layout = g_wsHudLayout.load(std::memory_order_relaxed); if (layout < 0) layout = 0;
        if (layout > 0 && w <= 64.f)
        {
            const float k = W / 512.0f, s2 = 216.f * k, s3 = 296.f * k, s4 = 388.f * k;
            const float offL = layout >= 2 ? g_wsHudOffLQ.load(std::memory_order_relaxed) / 16.0f * k : 0.f, offR = layout >= 2 ? g_wsHudOffRQ.load(std::memory_order_relaxed) / 16.0f * k : 0.f;
            const float t4 = W - (W - s4) * inv + offR;
            const bool left = x1 <= s2, right = x0 >= s3;
            if (left || right)
            {
                for (int i = 0; i < n; i++)
                {
                    WsHudState::WsHud::V &v = h.q[i];
                    if (v.mapped) continue;
                    const float fx = v.x / 16.0f - c.ofx;
                    float mx = ((left ? offL + fx * inv : t4 + (fx - s4) * inv) + c.ofx) * 16.0f;
                    if (mx < 0.f) mx = 0.f; if (mx > 65535.f) mx = 65535.f;
                    const uint16_t nx = uint16_t(mx + 0.5f);
                    std::memcpy(data + v.off, &nx, 2);
                    v.x = nx; v.mapped = true; h.mappedVerts++;
                }
                return;
            }
        }
    }
    static const bool s_vlog = [](){ const char *v = std::getenv("PS2X_PGS_WSHUDLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_vn = 0;
    const bool vlog = s_vlog && s_vn < 300 && y1 > 15.f && y0 < 60.f && x1 > 270.f && x0 < 330.f;
    if (vlog) { s_vn++; std::fprintf(stderr, "[wshudv] #%u prim %u tme %d abe %d fbp %u fbmsk %08x test %llx tex0 tbp0 %u psm %u %ux%u ofx %.2f verts:", s_vn, primType, int((attr >> 4) & 1u), int((attr >> 6) & 1u), c.fbp, uint32_t(c.frame >> 32), (unsigned long long)c.test, uint32_t(c.tex0 & 0x3FFFu), uint32_t((c.tex0 >> 20) & 0x3Fu), 1u << ((c.tex0 >> 26) & 0xFu), 1u << ((c.tex0 >> 30) & 0xFu), c.ofx); }
    for (int i = 0; i < n; i++)
    {
        WsHudState::WsHud::V &v = h.q[i];
        if (v.mapped) { if (vlog) std::fprintf(stderr, " [x %.2f already]", v.x / 16.0f - c.ofx); continue; }
        const float fx = v.x / 16.0f - c.ofx;
        float mx = (wsHudMapX(fx, W, inv) + c.ofx) * 16.0f;
        if (mx < 0.f) mx = 0.f; if (mx > 65535.f) mx = 65535.f;
        const uint16_t nx = uint16_t(mx + 0.5f);
        std::memcpy(data + v.off, &nx, 2);   // packed and reglist both keep X in the low 16 bits
        if (vlog) std::fprintf(stderr, " [x %.2f y %.2f -> %.2f]", fx, v.y / 16.0f - c.ofy, nx / 16.0f - c.ofx);
        v.x = nx; v.mapped = true; h.mappedVerts++;
    }
    if (vlog) std::fprintf(stderr, "\n");
}
static void wsHudVertexLocked(WsHudState &s, uint8_t *data, size_t off, bool packed, bool xyzf, bool kick, float inv)
{
    WsHudState::WsHud &h = s.wshud;
    uint64_t lo, hi = 0; std::memcpy(&lo, data + off, 8); if (packed) std::memcpy(&hi, data + off + 8, 8);
    WsHudState::WsHud::V v; v.off = off; v.packed = packed;
    if (packed) { v.x = uint16_t(lo & 0xFFFFu); v.y = uint16_t((lo >> 32) & 0xFFFFu); v.z = xyzf ? uint32_t((hi >> 4) & 0xFFFFFFu) : uint32_t(hi & 0xFFFFFFFFu); }
    else { v.x = uint16_t(lo & 0xFFFFu); v.y = uint16_t((lo >> 16) & 0xFFFFu); v.z = xyzf ? uint32_t((lo >> 32) & 0xFFFFFFu) : uint32_t(lo >> 32); }
    const uint32_t primType = uint32_t(h.primRaw & 7u);
    const int need = (primType == 6u) ? 2 : (primType >= 3u) ? 3 : (primType == 1u || primType == 2u) ? 2 : 1;
    if (h.qn >= 3) { h.q[0] = h.q[1]; h.q[1] = h.q[2]; h.qn = 2; }   // overflow: keep the newest two (strip semantics)
    h.q[h.qn++] = v;
    if (!kick || h.qn < need) return;
    wsHudKickLocked(s, data, inv);
    // queue maintenance per primitive type
    switch (primType)
    {
    case 4: h.q[0] = h.q[1]; h.q[1] = h.q[2]; h.qn = 2; break;          // triangle strip: keep the last two
    case 5: h.q[1] = h.q[2]; h.qn = 2; break;                          // triangle fan: keep first + last
    case 2: h.q[0] = h.q[1]; h.qn = 1; break;                          // line strip
    default: h.qn = 0; break;                                           // lists, sprites, points
    }
}
// A+D register write; `data + off` is the 64-bit value in the packet (rewritable). SCISSOR writes that look like the
// HUD's tight bar scissors (top band, narrower than 0.8 W) follow the layout map, as the OpenGL path's [wsscissor]:
// the squeezed bar otherwise extends past the unmapped scissor and its outer strip is clipped, exposing the layer
// underneath (the yellow sliver at the left end of the opponent's bar).
// [vpdrop] PS2X_VPKEEP=1|2: which player's viewport this client keeps. Paired with PS2X_NETVIEW,
// which makes the LOCAL player's viewport full-width (0..511) in the guest; the OTHER player's
// stays half-width, and we blank it here by rewriting its SCISSOR to an empty rect (x0 > x1).
// This lives in the paraLLEl-GS packet path because the GL renderer's vertexKick drop never runs
// under the PGS backend in EXCLUSIVE mode ("our GS parse skipped") -- the first live test drew
// BOTH views for exactly that reason.
static int ps2xVpKeep()
{
    static const int s_env = [](){ const char *e = std::getenv("PS2X_VPKEEP");
                                   return (e && e[0]) ? std::atoi(e) : 0; }();
    if (s_env) return s_env;
    return ps2NetActive() ? ps2NetLocalPlayer() : 0;   // follow netplay: host = 1, joiner = 2
}

static void wsHudRegLocked(WsHudState &s, uint32_t reg, uint64_t v, uint8_t *data, size_t off, float inv, bool rewrite = true)
{
    WsHudState::WsHud &h = s.wshud;
    switch (reg)
    {
    case 0x00: h.primRaw = v; h.qn = 0; break;
    case 0x01: h.lastRgbaq = v; if (rewrite && data && h.rgbaN < 8) { h.rgba[h.rgbaN].off = off; h.rgba[h.rgbaN].packed = false; h.rgbaN++; } break;   // A+D RGBAQ: 64-bit value in the low half (R,G,B,A bytes 0..3)
    case 0x40: case 0x41:
    {
        h.ctx[reg - 0x40].scissor = v;
        if (const int keep = ps2xVpKeep(); keep && rewrite && data)
        {   // full-height, half-width == a splitscreen viewport. The local player's is already
            // 0..511 under PS2X_NETVIEW, so it never matches here.
            const uint32_t sx0 = uint32_t(v & 0x7FFu), sx1 = uint32_t((v >> 16) & 0x7FFu);
            const uint32_t sy0 = uint32_t((v >> 32) & 0x7FFu), sy1 = uint32_t((v >> 48) & 0x7FFu);
            {   // [vpcensus] the distinct full-height scissors this path sees (first 16): which values the fight views carry
                static std::vector<uint64_t> s_seen;
                if (sy1 >= 400u && s_seen.size() < 16u && std::find(s_seen.begin(), s_seen.end(), v) == s_seen.end())
                { s_seen.push_back(v); std::fprintf(stderr, "[vpcensus] scissor_%u x %u..%u y %u..%u\n", reg - 0x40 + 1, sx0, sx1, sy0, sy1); }
            }
            const bool leftVp  = (sy1 >= 400u && sx0 == 0u && sx1 > 0u && sx1 <= 255u);
            const bool rightVp = (sy1 >= 400u && sx0 >= 256u);
            const bool p1Full  = (sy1 >= 400u && sx0 == 0u && sx1 == 511u && sy0 == 1u);   // [netview] symmetric full-screen views: marked in y0
            const bool p2Full  = (sy1 >= 400u && sx0 == 0u && sx1 == 511u && sy0 == 2u);
            if ((keep == 1 && (rightVp || p2Full)) || (keep == 2 && (leftVp || p1Full)))
            {
                const uint64_t empty = (v & ~(0x7FFull | (0x7FFull << 16))) | 0x7FFull;   // x0 = 2047 > x1 = 0
                std::memcpy(data + off, &empty, sizeof empty);
                h.ctx[reg - 0x40].scissor = empty;
                static std::atomic<uint32_t> s_said{0};
                if (s_said.fetch_add(1u) < 4u)
                    std::fprintf(stderr, "[vpdrop] keeping player %d: blanking the other viewport (x %u..%u, y %u..%u)\n", keep, sx0, sx1, sy0, sy1);
                break;
            }
        }
        if (!rewrite) break;
        {
            static const bool s_log2 = [](){ const char *v2 = std::getenv("PS2X_PGS_WSHUDLOG"); return v2 && v2[0] && v2[0] != '0'; }(); static unsigned s_n2 = 0;
            if (s_log2 && h.active && s_n2 < 150) { s_n2++; std::fprintf(stderr, "[wshudlog] SCISSORALL_%u x %u..%u y %u..%u fbp %u\n", reg - 0x40 + 1, uint32_t(v & 0x7FFu), uint32_t((v >> 16) & 0x7FFu), uint32_t((v >> 32) & 0x7FFu), uint32_t((v >> 48) & 0x7FFu), h.ctx[reg - 0x40].fbp); }
        }
        if (!h.active || inv >= 0.999f) break;
        const WsHudState::WsHud::Ctx &c = h.ctx[reg - 0x40];
        // scene buffers only: the character-palette render (FRAME 480, 64x64 at the origin) also sets a tight scissor
        if (!(c.fbp == 0u || c.fbp == 112u) || !(c.fpsm == 0u || c.fpsm == 1u)) break;
        const float W = (c.fbw * 64u >= 320u && c.fbw * 64u <= 1024u) ? float(c.fbw * 64u) : 512.0f;
        const uint32_t x0 = uint32_t(v & 0x7FFu), x1 = uint32_t((v >> 16) & 0x7FFu), y1 = uint32_t((v >> 48) & 0x7FFu);
        if (x0 < 8u || x1 < x0 + 16u) break;   // origin-anchored / tiny rects are render-to-texture work, not bar scissors
        {
            static const bool s_log = [](){ const char *v = std::getenv("PS2X_PGS_WSHUDLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_n = 0;
            if (s_log && s_n < 120 && y1 < 96u) { s_n++; std::fprintf(stderr, "[wshudlog] SCISSOR_%u x %u..%u y %u..%u -> %s\n", reg - 0x40 + 1, x0, x1, uint32_t((v >> 32) & 0x7FFu), y1, (x1 < x0 || float(x1 - x0 + 1u) >= 0.8f * W) ? "keep" : "MAP"); }
        }
        if (x1 < x0 || y1 >= 96u || float(x1 - x0 + 1u) >= 0.8f * W) break;
        float mx0 = std::floor(wsHudMapX(float(x0), W, inv)), mx1 = std::ceil(wsHudMapX(float(x1 + 1u), W, inv)) - 1.0f;
        if (mx0 < 0.f) mx0 = 0.f; if (mx1 > 2047.f) mx1 = 2047.f; if (mx1 < mx0) mx1 = mx0;
        const uint64_t nv = (v & ~(0x7FFull | (0x7FFull << 16))) | uint64_t(uint32_t(mx0)) | (uint64_t(uint32_t(mx1)) << 16);
        std::memcpy(data + off, &nv, 8);
        h.scissorsMapped++;
        break;
    }
    case 0x18: case 0x19: h.ctx[reg - 0x18].ofx = float(v & 0xFFFFu) / 16.0f; h.ctx[reg - 0x18].ofy = float((v >> 32) & 0xFFFFu) / 16.0f; break;
    case 0x1A: h.prmodeCont = (v & 1u) != 0; break;
    case 0x1B: h.prmode = (h.prmode & 7u) | (v & ~7ull); break;
    case 0x06: case 0x07: h.ctx[reg - 0x06].tex0 = v; h.ctx[reg - 0x06].tex0Off = (rewrite && data) ? off : ~size_t(0); break;
    case 0x42: case 0x43: h.ctx[reg - 0x42].alpha = v; h.ctx[reg - 0x42].alphaOff = (rewrite && data) ? off : ~size_t(0); break;
    case 0x47: case 0x48: h.ctx[reg - 0x47].test = v; h.ctx[reg - 0x47].zte = uint32_t((v >> 16) & 1u); h.ctx[reg - 0x47].ztst = uint32_t((v >> 17) & 3u); break;
    case 0x4C: case 0x4D:
    {   // [pgsfx] DoF off, mode 1: the depth-mask pass (FRAME = 16-bit view of a scene buffer, FBMSK 0x3fff) is made to
        // write nothing, so the composite sees the scene's own alpha instead of the Z-derived far mask
        static const int s_dofMode = [](){ const char *e = std::getenv("PS2X_PGS_DOFMODE"); return e && e[0] ? std::atoi(e) : 2; }();   // 2 = mask pass writes near (default), 1 = mask pass off, 0 = drop the composite (kills the glow)
        const uint32_t fpsm = uint32_t((v >> 24) & 0x3Fu), fbp = uint32_t(v & 0x1FFu), fbmsk = uint32_t(v >> 32);
        if (s_dofMode == 1 && rewrite && data && !GsGpuRenderer::dofBlurEnabled() && (fpsm == 2u || fpsm == 10u) && (fbp == 0u || fbp == 112u) && fbmsk == 0x3fffu)
        {
            v = (v & 0xFFFFFFFFull) | (0xFFFFFFFFull << 32);
            std::memcpy(data + off, &v, 8);
            h.maskNeutralized++;
        }
    }
    h.ctx[reg - 0x4C].frame = v; h.ctx[reg - 0x4C].fbp = uint32_t(v & 0x1FFu); h.ctx[reg - 0x4C].fbw = uint32_t((v >> 16) & 0x3Fu); h.ctx[reg - 0x4C].fpsm = uint32_t((v >> 24) & 0x3Fu); break;
    default: break;
    }
}
// [pgssplit] Piecewise layouts (edge-pinned / custom) map each vertex through a map with breakpoints; a quad that
// spans a breakpoint gets a straight interpolation between its mapped corners and drifts off the exactly-mapped
// elements drawn on it (the frame backing runs 257..513). This pre-pass rebuilds the packet with such HUD quads
// (four-vertex strips in the game's Z order, and sprites) subdivided at the breakpoints, attributes interpolated,
// so the mapper afterwards moves every piece exactly. PACKED tags only; anything unusual is copied verbatim.
struct WsVert
{
    uint8_t loop[16 * 16]; uint32_t nreg = 0;
    int xyzIdx = -1, uvIdx = -1, stIdx = -1, rgbaIdx = -1; bool xyzf = false;
    float x = 0, y = 0;   // frame space
};
static void wsVertLerp(const WsVert &a, const WsVert &b, float t, WsVert &o, float xFrame, float ofx)
{   // o = copy of a with X = xFrame and the interpolable attributes at parameter t between a and b
    o = a;
    auto qw = [&](WsVert &v, int idx) { return v.loop + size_t(idx) * 16u; };
    auto lerp = [&](float p, float q) { return p + (q - p) * t; };
    {
        uint8_t *q = qw(o, o.xyzIdx); const uint8_t *qa = a.loop + size_t(a.xyzIdx) * 16u, *qb = b.loop + size_t(b.xyzIdx) * 16u;
        float mx = (xFrame + ofx) * 16.0f; if (mx < 0.f) mx = 0.f; if (mx > 65535.f) mx = 65535.f;
        const uint16_t nx = uint16_t(mx + 0.5f); std::memcpy(q, &nx, 2);
        uint64_t ha, hb; std::memcpy(&ha, qa + 8, 8); std::memcpy(&hb, qb + 8, 8);
        uint64_t ho; std::memcpy(&ho, q + 8, 8);
        if (o.xyzf) { const uint32_t za = uint32_t((ha >> 4) & 0xFFFFFFu), zb = uint32_t((hb >> 4) & 0xFFFFFFu); const uint32_t z = uint32_t(lerp(float(za), float(zb)) + 0.5f) & 0xFFFFFFu; ho = (ho & ~(0xFFFFFFull << 4)) | (uint64_t(z) << 4); }
        else { const uint32_t za = uint32_t(ha & 0xFFFFFFFFu), zb = uint32_t(hb & 0xFFFFFFFFu); const double z = double(za) + (double(zb) - double(za)) * t; const uint32_t zi = uint32_t(z + 0.5); ho = (ho & ~0xFFFFFFFFull) | zi; }
        std::memcpy(q + 8, &ho, 8);
    }
    if (o.uvIdx >= 0)
    {
        uint8_t *q = qw(o, o.uvIdx); const uint8_t *qa = a.loop + size_t(a.uvIdx) * 16u, *qb = b.loop + size_t(b.uvIdx) * 16u;
        uint64_t la, lb; std::memcpy(&la, qa, 8); std::memcpy(&lb, qb, 8);
        const float ua = float(la & 0x3FFFu), ub = float(lb & 0x3FFFu), va = float((la >> 32) & 0x3FFFu), vb = float((lb >> 32) & 0x3FFFu);
        const uint64_t u = uint64_t(uint32_t(lerp(ua, ub) + 0.5f) & 0x3FFFu), v = uint64_t(uint32_t(lerp(va, vb) + 0.5f) & 0x3FFFu);
        uint64_t lo; std::memcpy(&lo, q, 8); lo = (lo & ~(0x3FFFull | (0x3FFFull << 32))) | u | (v << 32); std::memcpy(q, &lo, 8);
    }
    if (o.stIdx >= 0)
    {
        uint8_t *q = qw(o, o.stIdx); const uint8_t *qa = a.loop + size_t(a.stIdx) * 16u, *qb = b.loop + size_t(b.stIdx) * 16u;
        float fa[3], fb[3], fo[3]; std::memcpy(fa, qa, 12); std::memcpy(fb, qb, 12);
        for (int k = 0; k < 3; k++) fo[k] = lerp(fa[k], fb[k]);
        std::memcpy(q, fo, 12);
    }
    if (o.rgbaIdx >= 0)
    {
        uint8_t *q = qw(o, o.rgbaIdx); const uint8_t *qa = a.loop + size_t(a.rgbaIdx) * 16u, *qb = b.loop + size_t(b.rgbaIdx) * 16u;
        for (int k = 0; k < 4; k++) q[k * 4] = uint8_t(lerp(float(qa[k * 4]), float(qb[k * 4])) + 0.5f);
    }
}
// REGLIST variant: one loop = nreg 64-bit registers (two per qword)
struct WsVertRL
{
    uint64_t r[16]; uint32_t nreg = 0;
    int xyzIdx = -1, uvIdx = -1, stIdx = -1, rgbaIdx = -1; bool xyzf = false;
    float x = 0, y = 0;
};
static void wsVertLerpRL(const WsVertRL &a, const WsVertRL &b, float t, WsVertRL &o, float xFrame, float ofx)
{
    o = a;
    auto lerp = [&](float p, float q) { return p + (q - p) * t; };
    {
        uint64_t &v = o.r[o.xyzIdx]; const uint64_t va = a.r[a.xyzIdx], vb = b.r[b.xyzIdx];
        float mx = (xFrame + ofx) * 16.0f; if (mx < 0.f) mx = 0.f; if (mx > 65535.f) mx = 65535.f;
        const uint64_t nx = uint64_t(uint16_t(mx + 0.5f));
        if (o.xyzf) { const uint32_t za = uint32_t((va >> 32) & 0xFFFFFFu), zb = uint32_t((vb >> 32) & 0xFFFFFFu); const uint64_t z = uint64_t(uint32_t(lerp(float(za), float(zb)) + 0.5f) & 0xFFFFFFu); v = (v & ~(0xFFFFull | (0xFFFFFFull << 32))) | nx | (z << 32); }
        else { const double za = double(uint32_t(va >> 32)), zb = double(uint32_t(vb >> 32)); const uint64_t z = uint64_t(uint32_t(za + (zb - za) * t + 0.5)); v = (v & 0xFFFF0000ull) | nx | (z << 32); }
    }
    if (o.uvIdx >= 0)
    {
        const uint64_t ua = a.r[a.uvIdx], ub = b.r[b.uvIdx];
        const float u0 = float(ua & 0x3FFFu), u1 = float(ub & 0x3FFFu), v0 = float((ua >> 16) & 0x3FFFu), v1 = float((ub >> 16) & 0x3FFFu);
        o.r[o.uvIdx] = (ua & ~(0x3FFFull | (0x3FFFull << 16))) | uint64_t(uint32_t(lerp(u0, u1) + 0.5f) & 0x3FFFu) | (uint64_t(uint32_t(lerp(v0, v1) + 0.5f) & 0x3FFFu) << 16);
    }
    if (o.stIdx >= 0)
    {
        float fa[2], fb[2], fo[2]; std::memcpy(fa, &a.r[a.stIdx], 8); std::memcpy(fb, &b.r[b.stIdx], 8);
        fo[0] = lerp(fa[0], fb[0]); fo[1] = lerp(fa[1], fb[1]); std::memcpy(&o.r[o.stIdx], fo, 8);
    }
    if (o.rgbaIdx >= 0)
    {
        const uint64_t ca = a.r[a.rgbaIdx], cb = b.r[b.rgbaIdx]; uint64_t co = 0;
        for (int k = 0; k < 4; k++) co |= uint64_t(uint8_t(lerp(float((ca >> (8 * k)) & 0xFFu), float((cb >> (8 * k)) & 0xFFu)) + 0.5f)) << (8 * k);
        float qa, qb; std::memcpy(&qa, reinterpret_cast<const uint8_t *>(&ca) + 4, 4); std::memcpy(&qb, reinterpret_cast<const uint8_t *>(&cb) + 4, 4);
        const float qo = lerp(qa, qb); uint32_t qi; std::memcpy(&qi, &qo, 4);
        o.r[o.rgbaIdx] = co | (uint64_t(qi) << 32);
    }
}
// [pgsfx] bloom mode: does the tracked state + this tag's primitive describe one of the draws whose shared registers
// the mapper rewrites (blur-buffer clear / scene downsample / composite / depth-mask sprites)?
static int wsBloomTargetState(const WsHudState::WsHud &h, uint64_t primRaw)
{
    const uint64_t attr = h.prmodeCont ? primRaw : h.prmode;
    const uint32_t primType = uint32_t(primRaw & 7u), ci = uint32_t((attr >> 9) & 1u);
    const bool tme = ((attr >> 4) & 1u) != 0, abe = ((attr >> 6) & 1u) != 0;
    const WsHudState::WsHud::Ctx &c = h.ctx[ci];
    const uint32_t tpsm = uint32_t((c.tex0 >> 20) & 0x3Fu), ttbp0 = uint32_t(c.tex0 & 0x3FFFu), ttw = uint32_t((c.tex0 >> 26) & 0xFu), tth = uint32_t((c.tex0 >> 30) & 0xFu), ttbw = uint32_t((c.tex0 >> 14) & 0x3Fu);
    const bool sprite = primType == 6u, tri = primType >= 3u && primType <= 5u;
    const bool blurBuf = c.fbp == 336u && c.fbw == 4u && c.fpsm == 0u;
    if (blurBuf && sprite && !tme && !abe) return 1;                                                                    // clear
    if (blurBuf && sprite && tme && abe && tpsm == 0u && ttw == 9u && tth == 9u && (ttbp0 == 0u || ttbp0 == 3584u)) return 2;   // downsample
    if ((c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u) && (sprite || tri) && tme && abe && tpsm == 1u && ttbp0 == 10752u && ttbw == 4u && ttw == 8u && tth == 8u && ((c.test >> 14) & 1u)) return 3;   // composite
    if (sprite && tme && (c.fpsm == 2u || c.fpsm == 10u) && (c.fbp == 0u || c.fbp == 112u) && uint32_t(c.frame >> 32) == 0x3fffu && (tpsm == 50u || tpsm == 58u)) return 4;   // depth mask
    return 0;
}
static bool wsHudSubdivideLocked(WsHudState &s, const uint8_t *data, size_t size, float inv, bool hostGif = false)
{
    WsHudState::WsHud &h = s.wshud;
    const int layout = g_wsHudLayout.load(std::memory_order_relaxed);
    static const int s_dofModeP = [](){ const char *e = std::getenv("PS2X_PGS_DOFMODE"); return e && e[0] ? std::atoi(e) : 2; }();
    const bool bloom = s_dofModeP == 3 && !GsGpuRenderer::dofBlurEnabled();
    const bool splitting = layout > 0 && h.active && inv < 0.999f;
    if (!splitting && !bloom) return false;
    const WsHudState::WsHud saved = h;   // the pre-pass tracks state like the mapper; restore afterwards so the mapper sees the same start
    std::vector<uint8_t> &out = s.wsBuf; out.clear(); out.reserve(size + 4096);
    bool changed = false;
    size_t off = 0;
    while (off + 16 <= size)
    {
        uint64_t lo, hi; std::memcpy(&lo, data + off, 8); std::memcpy(&hi, data + off + 8, 8);
        const uint32_t nloop = uint32_t(lo & 0x7FFFu), flg = uint32_t((lo >> 58) & 3u);
        uint32_t nreg = uint32_t((lo >> 60) & 0xFu); if (nreg == 0) nreg = 16;
        if ((lo >> 46) & 1u) h.primRaw = (lo >> 47) & 0x7FFu;
        const size_t tagOff = off; off += 16;
        size_t lastHdr = out.size();   // header position of the last tag emitted for this input tag (splits emit two)
        size_t bytes = 0;
        if (flg == 0u) bytes = size_t(nloop) * nreg * 16u;
        else if (flg == 1u) bytes = (size_t(nloop) * nreg + 1u) / 2u * 16u;
        else bytes = size_t(nloop) * 16u;
        if (off + bytes > size) { out.insert(out.end(), data + tagOff, data + size); off = size; break; }
        bool handled = false;
        if (flg == 0u && nloop > 0)
        {
            // register layout of one loop
            int xyzIdx = -1, uvIdx = -1, stIdx = -1, rgbaIdx = -1; bool xyzf = false, other = false;
            for (uint32_t i = 0; i < nreg; i++)
            {
                const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu);
                if (r == 0x4 || r == 0x5) { xyzIdx = int(i); xyzf = (r == 0x4); }
                else if (r == 0x3) uvIdx = int(i);
                else if (r == 0x2) stIdx = int(i);
                else if (r == 0x1) rgbaIdx = int(i);
                else if (r == 0xE) { other = true; }
                else if (r == 0xC || r == 0xD) { other = true; }
                else if (r == 0x0 || r == 0xA || r == 0xF) {}
                else other = true;
            }
            const uint32_t primType = uint32_t(h.primRaw & 7u);
            {   // PS2X_PGS_WSHUDLOG=1: the structure of vertex tags while the squeeze is active (why does nothing split?)
                static const bool s_tl = [](){ const char *v = std::getenv("PS2X_PGS_WSHUDLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_tn = 0;
                uint64_t xl0 = 0; if (xyzIdx >= 0) std::memcpy(&xl0, data + off + size_t(xyzIdx) * 16u, 8);
                const float fy0 = float((xl0 >> 32) & 0xFFFFu) / 16.0f - h.ctx[0].ofy;
                uint32_t z0 = 0; if (xyzIdx >= 0) { uint64_t hz; std::memcpy(&hz, data + off + size_t(xyzIdx) * 16u + 8, 8); z0 = xyzf ? uint32_t((hz >> 4) & 0xFFFFFFu) : uint32_t(hz & 0xFFFFFFFFu); }
                if (s_tl && s_tn < 80 && xyzIdx >= 0 && z0 == 0u && fy0 > -40.f && fy0 < 96.f)
                {
                    s_tn++;
                    char regs[40]; int rp = 0; for (uint32_t i = 0; i < nreg && rp < 36; i++) rp += std::snprintf(regs + rp, sizeof(regs) - rp, "%x", uint32_t((hi >> (4 * i)) & 0xFu));
                    uint64_t xl; std::memcpy(&xl, data + off + size_t(xyzIdx) * 16u, 8);
                    std::fprintf(stderr, "[wshudtag] #%u nloop %u nreg %u regs %s prim %u pre %d other %d first x %.1f y %.1f fbp %u\n", s_tn, nloop, nreg, regs, primType, int((lo >> 46) & 1u), other ? 1 : 0,
                                 float(xl & 0xFFFFu) / 16.0f - h.ctx[uint32_t(((h.prmodeCont ? h.primRaw : h.prmode) >> 9) & 1u)].ofx, float((xl >> 32) & 0xFFFFu) / 16.0f - h.ctx[0].ofy, h.ctx[0].fbp);
                }
            }
            if (xyzIdx >= 0 && !other && (primType == 4u || primType == 6u) && nreg <= 16)
            {
                // the HUD rule (same as the mapper) on the tag's vertices, per primitive group
                const uint64_t attr = h.prmodeCont ? h.primRaw : h.prmode;
                const bool fst = ((attr >> 8) & 1u) != 0;
                const uint32_t ci = uint32_t((attr >> 9) & 1u);
                const WsHudState::WsHud::Ctx &c = h.ctx[ci];
                const float W = (c.fbw * 64u >= 320u && c.fbw * 64u <= 1024u) ? float(c.fbw * 64u) : 512.0f;
                const bool sceneBuf = (c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u);
                std::vector<WsVert> vs(nloop);
                for (uint32_t l = 0; l < nloop; l++)
                {
                    WsVert &v = vs[l]; v.nreg = nreg; v.xyzIdx = xyzIdx; v.uvIdx = uvIdx; v.stIdx = stIdx; v.rgbaIdx = rgbaIdx; v.xyzf = xyzf;
                    std::memcpy(v.loop, data + off + size_t(l) * nreg * 16u, size_t(nreg) * 16u);
                    uint64_t xl; std::memcpy(&xl, v.loop + size_t(xyzIdx) * 16u, 8);
                    v.x = float(xl & 0xFFFFu) / 16.0f - c.ofx; v.y = float((xl >> 32) & 0xFFFFu) / 16.0f - c.ofy;
                }
                const float k = W / 512.0f;
                const float cuts[4] = { 124.f * k, 216.f * k, 296.f * k, 388.f * k };
                std::vector<uint8_t> tagOut;
                uint32_t newLoops = 0;
                auto emit = [&](const WsVert &v) { tagOut.insert(tagOut.end(), v.loop, v.loop + size_t(nreg) * 16u); newLoops++; };
                bool tagChanged = false;
                if (primType == 6u && (nloop % 2u) == 0u)
                {
                    for (uint32_t l = 0; l + 1 < nloop; l += 2)
                    {
                        const WsVert &a = vs[l], &b = vs[l + 1];
                        const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
                        const bool hud = sceneBuf && (x1 - x0) > 0.f && (x1 - x0) < 0.8f * W && (y1 - y0) > 0.f && (y1 - y0) < 300.f && y1 < 96.f && (!c.zte || c.ztst == 1u);
                        std::vector<float> ts;
                        if (hud && splitting) for (float cx : cuts) if (cx > x0 + 1.f && cx < x1 - 1.f) ts.push_back((cx - a.x) / (b.x - a.x));
                        if (ts.empty()) { emit(a); emit(b); continue; }
                        std::sort(ts.begin(), ts.end());
                        float tPrev = 0.f; WsVert p0, p1;
                        for (size_t i = 0; i <= ts.size(); i++)
                        {
                            const float tNext = i < ts.size() ? ts[i] : 1.f;
                            wsVertLerp(a, b, tPrev, p0, a.x + (b.x - a.x) * tPrev, c.ofx);
                            wsVertLerp(a, b, tNext, p1, a.x + (b.x - a.x) * tNext, c.ofx);
                            // sprite: first vertex keeps a's y/v/t, second keeps b's (the lerp only moved x/u/s along the span)
                            std::memcpy(p1.loop + size_t(xyzIdx) * 16u + 4, b.loop + size_t(xyzIdx) * 16u + 4, 4);
                            if (uvIdx >= 0) std::memcpy(p1.loop + size_t(uvIdx) * 16u + 4, b.loop + size_t(uvIdx) * 16u + 4, 4);
                            if (stIdx >= 0) std::memcpy(p1.loop + size_t(stIdx) * 16u + 4, b.loop + size_t(stIdx) * 16u + 4, 4);
                            std::memcpy(p0.loop + size_t(xyzIdx) * 16u + 4, a.loop + size_t(xyzIdx) * 16u + 4, 4);
                            emit(p0); emit(p1); tPrev = tNext;
                        }
                        tagChanged = true; h.splitPrims++;
                    }
                    handled = true;
                }
                else if (primType == 4u && nloop == 4u)
                {
                    const WsVert &t0 = vs[0], &t1 = vs[1], &b0 = vs[2], &b1 = vs[3];
                    const bool axisQuad = std::fabs(t0.y - t1.y) < 0.07f && std::fabs(b0.y - b1.y) < 0.07f && std::fabs(t0.x - b0.x) < 0.07f && std::fabs(t1.x - b1.x) < 0.07f;
                    const float x0 = std::min(t0.x, t1.x), x1 = std::max(t0.x, t1.x), y0 = std::min(t0.y, b0.y), y1 = std::max(t0.y, b0.y);
                    uint32_t zt0; std::memcpy(&zt0, t0.loop + size_t(xyzIdx) * 16u + 8, 4);
                    bool zAllZero = true; for (const WsVert &v : vs) { uint32_t z; std::memcpy(&z, v.loop + size_t(xyzIdx) * 16u + 8, 4); if (xyzf) z = (z >> 4) & 0xFFFFFFu; if (z) zAllZero = false; }
                    const bool hud = sceneBuf && fst && zAllZero && (y1 - y0) < 300.f && y1 < 96.f && ((x1 - x0) < 0.8f * W || x0 > 8.f);
                    std::vector<float> ts;
                    if (hud && axisQuad && splitting) for (float cx : cuts) if (cx > x0 + 1.f && cx < x1 - 1.f) ts.push_back((cx - t0.x) / (t1.x - t0.x));
                    if (!ts.empty())
                    {
                        std::sort(ts.begin(), ts.end());
                        std::vector<float> tt; tt.push_back(0.f); for (float t : ts) tt.push_back(t); tt.push_back(1.f);
                        WsVert top, bot;
                        for (float t : tt)
                        {
                            wsVertLerp(t0, t1, t, top, t0.x + (t1.x - t0.x) * t, c.ofx);
                            wsVertLerp(b0, b1, t, bot, b0.x + (b1.x - b0.x) * t, c.ofx);
                            emit(top); emit(bot);
                        }
                        tagChanged = true; h.splitPrims++;
                    }
                    else for (const WsVert &v : vs) emit(v);
                    handled = true;
                }
                if (handled)
                {
                    uint64_t nlo = (lo & ~0x7FFFull) | uint64_t(newLoops & 0x7FFFu);
                    uint8_t hdr[16]; std::memcpy(hdr, &nlo, 8); std::memcpy(hdr + 8, &hi, 8);
                    lastHdr = out.size();
                    out.insert(out.end(), hdr, hdr + 16);
                    out.insert(out.end(), tagOut.begin(), tagOut.end());
                    if (tagChanged) changed = true;
                }
            }
            // state tracking for the classification (registers inside this tag), no scissor rewrite
            bool skipTag = false;
            if (hostGif)
            {   // [wshudhost] a vertex-only tag of the seam's mesh packet carries no state the walker needs
                skipTag = true;
                for (uint32_t i = 0; i < nreg && skipTag; i++) { const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu); if (!(r == 0x1 || r == 0x2 || r == 0x3 || r == 0x4 || r == 0x5 || r == 0xA)) skipTag = false; }
            }
            for (uint32_t l = 0; l < (skipTag ? 0u : nloop); l++)
                for (uint32_t i = 0; i < nreg; i++)
                {
                    const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu);
                    const size_t q = off + (size_t(l) * nreg + i) * 16u;
                    if (r == 0x0) { uint64_t v; std::memcpy(&v, data + q, 8); h.primRaw = v & 0x7FFu; }
                    else if (r == 0x1) { const uint8_t *pq = data + q; h.lastRgbaq = uint64_t(pq[0]) | (uint64_t(pq[4]) << 8) | (uint64_t(pq[8]) << 16) | (uint64_t(pq[12]) << 24) | (0x3f800000ull << 32); }
                    else if (r == 0x6 || r == 0x7) { uint64_t v; std::memcpy(&v, data + q, 8); h.ctx[r - 0x6].tex0 = v; }
                    else if (r == 0xE) { uint64_t v, a; std::memcpy(&v, data + q, 8); std::memcpy(&a, data + q + 8, 8); wsHudRegLocked(s, uint32_t(a & 0xFFu), v, nullptr, 0, inv, false); }
                }
        }
        if (flg == 1u && nloop == 1u && nreg <= 16)
        {   // [pgssplit] BT3's HUD quads: ONE REGLIST loop = [RGBAQ, A+D, TEX0, PRIM, (UV, XYZ2) x 4]. Split the loop into a
            // setup tag (the prefix registers) and a vertex tag (one UV+XYZ2 pair per loop) so vertices can be added.
            std::vector<int> xyzPos;
            for (uint32_t i = 0; i < nreg; i++) { const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu); if (r == 0x4 || r == 0x5) xyzPos.push_back(int(i)); }
            if (xyzPos.size() >= 2)
            {
                const int g = xyzPos[1] - xyzPos[0];
                bool regular = g >= 1 && xyzPos[0] >= g - 1;
                for (size_t i = 1; i < xyzPos.size() && regular; i++) if (xyzPos[i] - xyzPos[i - 1] != g) regular = false;
                const int prefixN = regular ? xyzPos[0] - (g - 1) : 0;
                const uint32_t V = uint32_t(xyzPos.size());
                // the group's descriptors and attribute indices (relative to the group)
                int uvIdx = -1, stIdx = -1, rgbaIdx = -1; bool xyzf = false, other = false;
                if (regular)
                    for (int i = 0; i < g; i++)
                    {
                        const uint32_t r = uint32_t((hi >> (4 * (prefixN + i))) & 0xFu);
                        if (i == g - 1) xyzf = (r == 0x4);
                        else if (r == 0x3) uvIdx = i; else if (r == 0x2) stIdx = i; else if (r == 0x1) rgbaIdx = i; else if (r == 0xA || r == 0xF) {} else other = true;
                    }
                // the prefix's PRIM (reg 0) decides the primitive for these vertices
                uint64_t primHere = h.primRaw;
                for (int i = 0; i < prefixN; i++) if (uint32_t((hi >> (4 * i)) & 0xFu) == 0x0) { uint64_t v; std::memcpy(&v, data + off + size_t(i) * 8u, 8); primHere = v & 0x7FFu; }
                const uint32_t primType = uint32_t(primHere & 7u);
                const uint64_t attr = h.prmodeCont ? primHere : h.prmode;
                const bool fst = ((attr >> 8) & 1u) != 0;
                const uint32_t ci = uint32_t((attr >> 9) & 1u);
                const WsHudState::WsHud::Ctx &c = h.ctx[ci];
                const float W = (c.fbw * 64u >= 320u && c.fbw * 64u <= 1024u) ? float(c.fbw * 64u) : 512.0f;
                const bool sceneBuf = (c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u);
                if (regular && !other && (primType == 4u || primType == 6u))
                {
                    std::vector<WsVertRL> vs(V);
                    for (uint32_t vi = 0; vi < V; vi++)
                    {
                        WsVertRL &v = vs[vi]; v.nreg = uint32_t(g); v.xyzIdx = g - 1; v.uvIdx = uvIdx; v.stIdx = stIdx; v.rgbaIdx = rgbaIdx; v.xyzf = xyzf;
                        for (int i = 0; i < g; i++) std::memcpy(&v.r[i], data + off + size_t(prefixN + vi * g + i) * 8u, 8);
                        v.x = float(v.r[g - 1] & 0xFFFFu) / 16.0f - c.ofx; v.y = float((v.r[g - 1] >> 16) & 0xFFFFu) / 16.0f - c.ofy;
                    }
                    const float k = W / 512.0f;
                    const float cuts[4] = { 124.f * k, 216.f * k, 296.f * k, 388.f * k };
                    std::vector<uint64_t> regsOut; uint32_t newV = 0; bool tagChanged = false;
                    auto emit = [&](const WsVertRL &v) { for (int i = 0; i < g; i++) regsOut.push_back(v.r[i]); newV++; };
                    auto zOf = [&](const WsVertRL &v) { return xyzf ? uint32_t((v.r[g - 1] >> 32) & 0xFFFFFFu) : uint32_t(v.r[g - 1] >> 32); };
                    if (primType == 6u && (V % 2u) == 0u)
                    {
                        for (uint32_t l = 0; l + 1 < V; l += 2)
                        {
                            const WsVertRL &a = vs[l], &b = vs[l + 1];
                            const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
                            const bool hud = sceneBuf && (x1 - x0) > 0.f && (x1 - x0) < 0.8f * W && (y1 - y0) > 0.f && (y1 - y0) < 300.f && y1 < 96.f && (!c.zte || c.ztst == 1u);
                            std::vector<float> ts;
                            if (hud && splitting) for (float cx : cuts) if (cx > x0 + 1.f && cx < x1 - 1.f) ts.push_back((cx - a.x) / (b.x - a.x));
                            if (ts.empty()) { emit(a); emit(b); continue; }
                            std::sort(ts.begin(), ts.end());
                            float tPrev = 0.f; WsVertRL p0, p1;
                            for (size_t i = 0; i <= ts.size(); i++)
                            {
                                const float tNext = i < ts.size() ? ts[i] : 1.f;
                                wsVertLerpRL(a, b, tPrev, p0, a.x + (b.x - a.x) * tPrev, c.ofx);
                                wsVertLerpRL(a, b, tNext, p1, a.x + (b.x - a.x) * tNext, c.ofx);
                                p0.r[g - 1] = (p0.r[g - 1] & ~(0xFFFFull << 16)) | (a.r[g - 1] & (0xFFFFull << 16));
                                p1.r[g - 1] = (p1.r[g - 1] & ~(0xFFFFull << 16)) | (b.r[g - 1] & (0xFFFFull << 16));
                                if (uvIdx >= 0) { p0.r[uvIdx] = (p0.r[uvIdx] & ~(0x3FFFull << 16)) | (a.r[uvIdx] & (0x3FFFull << 16)); p1.r[uvIdx] = (p1.r[uvIdx] & ~(0x3FFFull << 16)) | (b.r[uvIdx] & (0x3FFFull << 16)); }
                                if (stIdx >= 0) { p0.r[stIdx] = (p0.r[stIdx] & 0xFFFFFFFFull) | (a.r[stIdx] & ~0xFFFFFFFFull); p1.r[stIdx] = (p1.r[stIdx] & 0xFFFFFFFFull) | (b.r[stIdx] & ~0xFFFFFFFFull); }
                                emit(p0); emit(p1); tPrev = tNext;
                            }
                            tagChanged = true; h.splitPrims++;
                        }
                        handled = true;
                    }
                    else if (primType == 4u && V == 4u)
                    {
                        const WsVertRL &t0 = vs[0], &t1 = vs[1], &b0 = vs[2], &b1 = vs[3];
                        const bool axisQuad = std::fabs(t0.y - t1.y) < 0.07f && std::fabs(b0.y - b1.y) < 0.07f && std::fabs(t0.x - b0.x) < 0.07f && std::fabs(t1.x - b1.x) < 0.07f;
                        const float x0 = std::min(t0.x, t1.x), x1 = std::max(t0.x, t1.x), y0 = std::min(t0.y, b0.y), y1 = std::max(t0.y, b0.y);
                        bool zAllZero = true; for (const WsVertRL &v : vs) if (zOf(v)) zAllZero = false;
                        const bool hud = sceneBuf && fst && zAllZero && (y1 - y0) < 300.f && y1 < 96.f && ((x1 - x0) < 0.8f * W || x0 > 8.f);
                        std::vector<float> ts;
                        if (hud && axisQuad && splitting) for (float cx : cuts) if (cx > x0 + 1.f && cx < x1 - 1.f) ts.push_back((cx - t0.x) / (t1.x - t0.x));
                        if (!ts.empty())
                        {
                            std::sort(ts.begin(), ts.end());
                            std::vector<float> tt; tt.push_back(0.f); for (float t : ts) tt.push_back(t); tt.push_back(1.f);
                            WsVertRL top, bot;
                            for (float t : tt)
                            {
                                wsVertLerpRL(t0, t1, t, top, t0.x + (t1.x - t0.x) * t, c.ofx);
                                wsVertLerpRL(b0, b1, t, bot, b0.x + (b1.x - b0.x) * t, c.ofx);
                                emit(top); emit(bot);
                            }
                            tagChanged = true; h.splitPrims++;
                        }
                        else for (const WsVertRL &v : vs) emit(v);
                        handled = true;
                    }
                    if (handled)
                    {
                        if (!tagChanged) { out.insert(out.end(), data + tagOff, data + off + bytes); }   // untouched: copy the original tag
                        else
                        {
                            // tag A: the prefix registers (setup), EOP cleared; tag B: the vertices, g regs per loop, original EOP
                            if (prefixN > 0)
                            {
                                uint64_t alo = (lo & ~(0x7FFFull | (1ull << 15) | (0xFull << 60))) | 1ull | (uint64_t(prefixN & 15) << 60);
                                uint64_t ahi = hi & ((prefixN >= 16) ? ~0ull : ((1ull << (4 * prefixN)) - 1ull));
                                uint8_t hdr[16]; std::memcpy(hdr, &alo, 8); std::memcpy(hdr + 8, &ahi, 8);
                                out.insert(out.end(), hdr, hdr + 16);
                                std::vector<uint64_t> pre; for (int i = 0; i < prefixN; i++) { uint64_t v; std::memcpy(&v, data + off + size_t(i) * 8u, 8); pre.push_back(v); }
                                if (pre.size() & 1u) pre.push_back(0);
                                const uint8_t *pb = reinterpret_cast<const uint8_t *>(pre.data()); out.insert(out.end(), pb, pb + pre.size() * 8u);
                            }
                            uint64_t blo = (lo & ~(0x7FFFull | (1ull << 46) | (0x7FFull << 47) | (0xFull << 60))) | uint64_t(newV & 0x7FFFu) | (uint64_t(g & 15) << 60);
                            uint64_t bhi = 0; for (int i = 0; i < g; i++) bhi |= ((hi >> (4 * (prefixN + i))) & 0xFull) << (4 * i);
                            uint8_t hdr[16]; std::memcpy(hdr, &blo, 8); std::memcpy(hdr + 8, &bhi, 8);
                            lastHdr = out.size();
                            out.insert(out.end(), hdr, hdr + 16);
                            if (regsOut.size() & 1u) regsOut.push_back(0);
                            const uint8_t *rb = reinterpret_cast<const uint8_t *>(regsOut.data()); out.insert(out.end(), rb, rb + regsOut.size() * 8u);
                            changed = true;
                        }
                    }
                }
            }
            // state: PRIM / RGBAQ / TEX0 inside the loop
            for (uint32_t i = 0; i < nreg; i++)
            {
                const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu); uint64_t v; std::memcpy(&v, data + off + size_t(i) * 8u, 8);
                if (r == 0x0) h.primRaw = v & 0x7FFu; else if (r == 0x1) h.lastRgbaq = v; else if (r == 0x6 || r == 0x7) h.ctx[r - 0x6].tex0 = v;
            }
        }
        if (!handled && flg == 1u && nloop > 0 && nreg <= 16)
        {
            int xyzIdx = -1, uvIdx = -1, stIdx = -1, rgbaIdx = -1; bool xyzf = false, other = false;
            for (uint32_t i = 0; i < nreg; i++)
            {
                const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu);
                if (r == 0x4 || r == 0x5) { xyzIdx = int(i); xyzf = (r == 0x4); }
                else if (r == 0x3) uvIdx = int(i);
                else if (r == 0x2) stIdx = int(i);
                else if (r == 0x1) rgbaIdx = int(i);
                else if (r == 0x0 || r == 0xA || r == 0xF) {}
                else other = true;
            }
            const uint32_t primType = uint32_t(h.primRaw & 7u);
            {
                static const bool s_tl = [](){ const char *v = std::getenv("PS2X_PGS_WSHUDLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_tn = 0;
                if (s_tl && s_tn < 80 && xyzIdx >= 0)
                {
                    uint64_t x0v; std::memcpy(&x0v, data + off + size_t(xyzIdx) * 8u, 8);
                    const float fy0 = float((x0v >> 16) & 0xFFFFu) / 16.0f - h.ctx[0].ofy;
                    const uint32_t z0 = xyzf ? uint32_t((x0v >> 32) & 0xFFFFFFu) : uint32_t(x0v >> 32);
                    if (z0 == 0u && fy0 > -40.f && fy0 < 96.f)
                    {
                        s_tn++;
                        char regs[40]; int rp = 0; for (uint32_t i = 0; i < nreg && rp < 36; i++) rp += std::snprintf(regs + rp, sizeof(regs) - rp, "%x", uint32_t((hi >> (4 * i)) & 0xFu));
                        std::fprintf(stderr, "[wshudtag] REGLIST #%u nloop %u nreg %u regs %s prim %u pre %d other %d first x %.1f y %.1f\\n", s_tn, nloop, nreg, regs, primType, int((lo >> 46) & 1u), other ? 1 : 0, float(x0v & 0xFFFFu) / 16.0f - h.ctx[0].ofx, fy0);
                    }
                }
            }
            if (xyzIdx >= 0 && !other && (primType == 4u || primType == 6u))
            {
                const uint64_t attr = h.prmodeCont ? h.primRaw : h.prmode;
                const bool fst = ((attr >> 8) & 1u) != 0;
                const uint32_t ci = uint32_t((attr >> 9) & 1u);
                const WsHudState::WsHud::Ctx &c = h.ctx[ci];
                const float W = (c.fbw * 64u >= 320u && c.fbw * 64u <= 1024u) ? float(c.fbw * 64u) : 512.0f;
                const bool sceneBuf = (c.fbp == 0u || c.fbp == 112u) && (c.fpsm == 0u || c.fpsm == 1u);
                std::vector<WsVertRL> vs(nloop);
                for (uint32_t l = 0; l < nloop; l++)
                {
                    WsVertRL &v = vs[l]; v.nreg = nreg; v.xyzIdx = xyzIdx; v.uvIdx = uvIdx; v.stIdx = stIdx; v.rgbaIdx = rgbaIdx; v.xyzf = xyzf;
                    for (uint32_t i = 0; i < nreg; i++) std::memcpy(&v.r[i], data + off + (size_t(l) * nreg + i) * 8u, 8);
                    v.x = float(v.r[xyzIdx] & 0xFFFFu) / 16.0f - c.ofx; v.y = float((v.r[xyzIdx] >> 16) & 0xFFFFu) / 16.0f - c.ofy;
                }
                const float k = W / 512.0f;
                const float cuts[4] = { 124.f * k, 216.f * k, 296.f * k, 388.f * k };
                std::vector<uint64_t> regsOut; uint32_t newLoops = 0; bool tagChanged = false;
                auto emit = [&](const WsVertRL &v) { for (uint32_t i = 0; i < nreg; i++) regsOut.push_back(v.r[i]); newLoops++; };
                auto zOf = [&](const WsVertRL &v) { return xyzf ? uint32_t((v.r[xyzIdx] >> 32) & 0xFFFFFFu) : uint32_t(v.r[xyzIdx] >> 32); };
                if (primType == 6u && (nloop % 2u) == 0u)
                {
                    for (uint32_t l = 0; l + 1 < nloop; l += 2)
                    {
                        const WsVertRL &a = vs[l], &b = vs[l + 1];
                        const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
                        const bool hud = sceneBuf && (x1 - x0) > 0.f && (x1 - x0) < 0.8f * W && (y1 - y0) > 0.f && (y1 - y0) < 300.f && y1 < 96.f && (!c.zte || c.ztst == 1u);
                        std::vector<float> ts;
                        if (hud && splitting) for (float cx : cuts) if (cx > x0 + 1.f && cx < x1 - 1.f) ts.push_back((cx - a.x) / (b.x - a.x));
                        if (ts.empty()) { emit(a); emit(b); continue; }
                        std::sort(ts.begin(), ts.end());
                        float tPrev = 0.f; WsVertRL p0, p1;
                        for (size_t i = 0; i <= ts.size(); i++)
                        {
                            const float tNext = i < ts.size() ? ts[i] : 1.f;
                            wsVertLerpRL(a, b, tPrev, p0, a.x + (b.x - a.x) * tPrev, c.ofx);
                            wsVertLerpRL(a, b, tNext, p1, a.x + (b.x - a.x) * tNext, c.ofx);
                            // keep each corner's own y / v / t (only x, u, s move along the span)
                            p0.r[xyzIdx] = (p0.r[xyzIdx] & ~(0xFFFFull << 16)) | (a.r[xyzIdx] & (0xFFFFull << 16));
                            p1.r[xyzIdx] = (p1.r[xyzIdx] & ~(0xFFFFull << 16)) | (b.r[xyzIdx] & (0xFFFFull << 16));
                            if (uvIdx >= 0) { p0.r[uvIdx] = (p0.r[uvIdx] & ~(0x3FFFull << 16)) | (a.r[uvIdx] & (0x3FFFull << 16)); p1.r[uvIdx] = (p1.r[uvIdx] & ~(0x3FFFull << 16)) | (b.r[uvIdx] & (0x3FFFull << 16)); }
                            if (stIdx >= 0) { p0.r[stIdx] = (p0.r[stIdx] & 0xFFFFFFFFull) | (a.r[stIdx] & ~0xFFFFFFFFull); p1.r[stIdx] = (p1.r[stIdx] & 0xFFFFFFFFull) | (b.r[stIdx] & ~0xFFFFFFFFull); }
                            emit(p0); emit(p1); tPrev = tNext;
                        }
                        tagChanged = true; h.splitPrims++;
                    }
                    handled = true;
                }
                else if (primType == 4u && nloop == 4u)
                {
                    const WsVertRL &t0 = vs[0], &t1 = vs[1], &b0 = vs[2], &b1 = vs[3];
                    const bool axisQuad = std::fabs(t0.y - t1.y) < 0.07f && std::fabs(b0.y - b1.y) < 0.07f && std::fabs(t0.x - b0.x) < 0.07f && std::fabs(t1.x - b1.x) < 0.07f;
                    const float x0 = std::min(t0.x, t1.x), x1 = std::max(t0.x, t1.x), y0 = std::min(t0.y, b0.y), y1 = std::max(t0.y, b0.y);
                    bool zAllZero = true; for (const WsVertRL &v : vs) if (zOf(v)) zAllZero = false;
                    const bool hud = sceneBuf && fst && zAllZero && (y1 - y0) < 300.f && y1 < 96.f && ((x1 - x0) < 0.8f * W || x0 > 8.f);
                    std::vector<float> ts;
                    if (hud && axisQuad && splitting) for (float cx : cuts) if (cx > x0 + 1.f && cx < x1 - 1.f) ts.push_back((cx - t0.x) / (t1.x - t0.x));
                    if (!ts.empty())
                    {
                        std::sort(ts.begin(), ts.end());
                        std::vector<float> tt; tt.push_back(0.f); for (float t : ts) tt.push_back(t); tt.push_back(1.f);
                        WsVertRL top, bot;
                        for (float t : tt)
                        {
                            wsVertLerpRL(t0, t1, t, top, t0.x + (t1.x - t0.x) * t, c.ofx);
                            wsVertLerpRL(b0, b1, t, bot, b0.x + (b1.x - b0.x) * t, c.ofx);
                            emit(top); emit(bot);
                        }
                        tagChanged = true; h.splitPrims++;
                    }
                    else for (const WsVertRL &v : vs) emit(v);
                    handled = true;
                }
                if (handled)
                {
                    uint64_t nlo = (lo & ~0x7FFFull) | uint64_t(newLoops & 0x7FFFu);
                    uint8_t hdr[16]; std::memcpy(hdr, &nlo, 8); std::memcpy(hdr + 8, &hi, 8);
                    lastHdr = out.size();
                    out.insert(out.end(), hdr, hdr + 16);
                    if (regsOut.size() & 1u) regsOut.push_back(0);   // odd register count: the last half-qword is padding
                    const uint8_t *rb = reinterpret_cast<const uint8_t *>(regsOut.data());
                    out.insert(out.end(), rb, rb + regsOut.size() * 8u);
                    if (tagChanged) changed = true;
                }
            }
            // state: PRIM / RGBAQ / TEX0 via REGLIST regs
            for (uint32_t l = 0; l < nloop; l++)
                for (uint32_t i = 0; i < nreg; i++)
                {
                    const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu); uint64_t v; std::memcpy(&v, data + off + (size_t(l) * nreg + i) * 8u, 8);
                    if (r == 0x0) h.primRaw = v & 0x7FFu; else if (r == 0x1) h.lastRgbaq = v; else if (r == 0x6 || r == 0x7) h.ctx[r - 0x6].tex0 = v;
                }
        }
        if (!handled) out.insert(out.end(), data + tagOff, data + off + bytes);
        if (bloom && flg <= 1u && nloop > 0)
        {   // vertex tag of a bloom target: append an A+D tag restoring ALPHA / RGBAQ / TEX0 (originals) so the mapper's
            // per-draw rewrites of those shared registers cannot leak into the draws that follow
            bool hasXyz = false;
            for (uint32_t i = 0; i < nreg; i++) { const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu); if (r == 0x4 || r == 0x5) hasXyz = true; }
            const int cls = hasXyz ? wsBloomTargetState(h, h.primRaw) : 0;
            if (cls != 0)
            {
                const uint64_t attr = h.prmodeCont ? h.primRaw : h.prmode;
                const uint32_t ci = uint32_t((attr >> 9) & 1u);
                // only what the mapper changes for this class: clear -> RGBAQ; downsample / composite -> ALPHA;
                // depth mask -> TEX0 (CLD cleared: a TEX0 write with its load bits set reloads the palette from memory
                // that may no longer hold it) + RGBAQ
                std::vector<std::pair<uint64_t, uint64_t>> regs;
                if (cls == 1 || cls == 4) regs.emplace_back(h.lastRgbaq, 0x01ull);
                if (cls == 2 || cls == 3) regs.emplace_back(h.ctx[ci].alpha, 0x42ull + ci);
                if (cls == 4) regs.emplace_back(h.ctx[ci].tex0 & ~(7ull << 61), 0x06ull + ci);
                // move the tag's EOP (if any) onto the restore tag
                uint64_t tlo; std::memcpy(&tlo, out.data() + lastHdr, 8);
                const bool eop = ((tlo >> 15) & 1u) != 0;
                if (eop) { tlo &= ~(1ull << 15); std::memcpy(out.data() + lastHdr, &tlo, 8); }
                uint64_t rlo = uint64_t(regs.size()) | (eop ? (1ull << 15) : 0ull) | (1ull << 60);   // NLOOP n, PACKED, NREG 1
                uint64_t rhi = 0xEull;                                                                 // A+D
                uint8_t hdr[16]; std::memcpy(hdr, &rlo, 8); std::memcpy(hdr + 8, &rhi, 8);
                out.insert(out.end(), hdr, hdr + 16);
                for (const auto &r : regs) { uint8_t q[16]; std::memcpy(q, &r.first, 8); std::memcpy(q + 8, &r.second, 8); out.insert(out.end(), q, q + 16); }
                changed = true; h.bloomRestores++;
            }
        }
        off += bytes;
    }
    if (off < size) out.insert(out.end(), data + off, data + size);
    h = saved;
    return changed;
}
// Walk one GIF packet (PACKED / REGLIST) and rewrite HUD vertex X in place. Cheap: a few branches per qword.
void wsHudRewriteLocked(WsHudState &s, uint8_t *data, size_t size, bool hostGif = false)
{
    WsHudState::WsHud &h = s.wshud;
    if (h.lastSwap != s.swaps)
    {   // frame boundary: fold the finished frame's verdict into the sticky "scene present" gate (5-frame hysteresis)
        if (h.lastSwap != ~0ull) { if (h.frameHad3d) { h.active = true; h.no3dRun = 0; } else if (++h.no3dRun >= 5) h.active = false; }
        h.lastSwap = s.swaps; h.frameHad3d = false;
    }
    // The squeeze factor in FRAME pixels (HUD coordinates): desired h-scale (authentic TV pixel = v-scale x k) over
    // the actual h-scale of the full-window stretch. The present's g_ps2xWsHudInv is computed from the SCANOUT size,
    // which already carries the CRTC's 512 -> 640 magnification, so it under-squeezes the backend by 1.25.
    static const float s_pixk = [](){ const char *v = std::getenv("PS2X_PIXK"); const float f = v ? float(std::atof(v)) : 1.08f; return (f > 0.5f && f < 2.0f) ? f : 1.08f; }();
    float inv = 1.0f;
    {
        const uint32_t dw = g_presentW.load(std::memory_order_relaxed), dh = g_presentH.load(std::memory_order_relaxed);
        const float fw = (h.ctx[0].fbw * 64u >= 320u && h.ctx[0].fbw * 64u <= 1024u) ? float(h.ctx[0].fbw * 64u) : 512.0f;
        const float fh = s.baseH ? float(s.baseH) : 448.0f;
        if (g_ps2xWsHudInv < 0.999f && dw && dh)
        {
            float raw = (float(dh) / fh * s_pixk) / (float(dw) / fw);
            if (raw < 0.4f) raw = 0.4f; if (raw > 1.0f) raw = 1.0f;
            // [wsjit] debounce: commit the raw squeeze only once it repeats; hold the last value
            // otherwise. Kills the frame-to-frame 512/640 alternation without changing steady state.
            if (!h.invInit) { h.lastInv = raw; h.lastRawInv = raw; h.invRun = 1; h.invInit = true; }
            else if (raw == h.lastRawInv) { if (++h.invRun >= 2) h.lastInv = raw; }
            else { h.lastRawInv = raw; h.invRun = 1; }
            inv = h.lastInv;
            static const bool s_jit = [](){ const char *v = std::getenv("PS2X_WSJIT"); return v && v[0] && v[0] != '0'; }();
            if (s_jit)
            {
                static float s_lr = -1.0f;
                if (raw != s_lr)
                {
                    s_lr = raw;
                    std::fprintf(stderr, "[wsjit] dw=%u dh=%u fw=%.0f fh=%.0f raw=%.4f committed=%.4f\n",
                                 dw, dh, fw, fh, raw, inv);
                }
            }
        }
    }
    h.lastInv = inv;
    size_t off = 0;
    while (off + 16 <= size)
    {
        uint64_t lo, hi; std::memcpy(&lo, data + off, 8); std::memcpy(&hi, data + off + 8, 8);
        const uint32_t nloop = uint32_t(lo & 0x7FFFu), flg = uint32_t((lo >> 58) & 3u);
        uint32_t nreg = uint32_t((lo >> 60) & 0xFu); if (nreg == 0) nreg = 16;
        if ((lo >> 46) & 1u) { h.primRaw = (lo >> 47) & 0x7FFu; h.qn = 0; }   // PRE: the tag carries PRIM
        off += 16;
        if (nloop == 0) continue;
        if (flg == 0u)
        {   // PACKED
            const size_t bytes = size_t(nloop) * nreg * 16u;
            if (off + bytes > size) break;
            if (hostGif)
            {   // [wshudhost] the seam's own mesh packet: its vertices are 3D and never squeezed, only its register writes matter
                // to the walker's state -- a vertex-only tag (RGBAQ/ST/UV/XYZ/FOG) is skipped whole (100k vertices a frame otherwise)
                bool vtxOnly = true;
                for (uint32_t i = 0; i < nreg && vtxOnly; i++) { const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu); if (!(r == 0x1 || r == 0x2 || r == 0x3 || r == 0x4 || r == 0x5 || r == 0xA)) vtxOnly = false; }
                if (vtxOnly) { off += bytes; h.qn = 0; h.rgbaN = 0; h.uvN = 0; continue; }
            }
            for (uint32_t l = 0; l < nloop; l++)
                for (uint32_t i = 0; i < nreg; i++)
                {
                    const uint32_t r = uint32_t((hi >> (4 * i)) & 0xFu);
                    const size_t q = off + (size_t(l) * nreg + i) * 16u;
                    switch (r)
                    {
                    case 0x0: { uint64_t v; std::memcpy(&v, data + q, 8); h.primRaw = v & 0x7FFu; h.qn = 0; break; }
                    case 0x6: case 0x7: { uint64_t v; std::memcpy(&v, data + q, 8); h.ctx[r - 0x6].tex0 = v; h.ctx[r - 0x6].tex0Off = q; break; }
                    case 0x1: { const uint8_t *pq = data + q; h.lastRgbaq = uint64_t(pq[0]) | (uint64_t(pq[4]) << 8) | (uint64_t(pq[8]) << 16) | (uint64_t(pq[12]) << 24) | (0x3f800000ull << 32); if (h.rgbaN < 8) { h.rgba[h.rgbaN].off = q; h.rgba[h.rgbaN].packed = true; h.rgbaN++; } break; }
                    case 0x3: if (h.uvN < 8) { h.uv[h.uvN].off = q; h.uv[h.uvN].packed = true; h.uvN++; } break;
                    case 0x4: case 0x5: case 0xC: case 0xD:
                    {
                        uint64_t qhi; std::memcpy(&qhi, data + q + 8, 8);
                        const bool xyzf = (r == 0x4 || r == 0xC), kick = (r == 0x4 || r == 0x5) && ((qhi >> 47) & 1u) == 0u;
                        wsHudVertexLocked(s, data, q, true, xyzf, kick, inv);
                        break;
                    }
                    case 0xE: { uint64_t v, a; std::memcpy(&v, data + q, 8); std::memcpy(&a, data + q + 8, 8); wsHudRegLocked(s, uint32_t(a & 0xFFu), v, data, q, inv); break; }
                    default: break;
                    }
                }
            off += bytes;
        }
        else if (flg == 1u)
        {   // REGLIST: 64-bit registers, two per qword
            const size_t nregs = size_t(nloop) * nreg, bytes = (nregs + 1u) / 2u * 16u;
            if (off + bytes > size) break;
            for (size_t k = 0; k < nregs; k++)
            {
                const uint32_t r = uint32_t((hi >> (4 * (k % nreg))) & 0xFu);
                const size_t q = off + k * 8u;
                switch (r)
                {
                case 0x0: { uint64_t v; std::memcpy(&v, data + q, 8); h.primRaw = v & 0x7FFu; h.qn = 0; break; }
                case 0x4: case 0x5: case 0xC: case 0xD: wsHudVertexLocked(s, data, q, false, (r == 0x4 || r == 0xC), (r == 0x4 || r == 0x5), inv); break;
                case 0x6: case 0x7: { uint64_t v; std::memcpy(&v, data + q, 8); h.ctx[r - 0x6].tex0 = v; h.ctx[r - 0x6].tex0Off = q; break; }
                case 0x1: { uint64_t v; std::memcpy(&v, data + q, 8); h.lastRgbaq = v; if (h.rgbaN < 8) { h.rgba[h.rgbaN].off = q; h.rgba[h.rgbaN].packed = false; h.rgbaN++; } break; }
                case 0x3: if (h.uvN < 8) { h.uv[h.uvN].off = q; h.uv[h.uvN].packed = false; h.uvN++; } break;
                default: break;   // A+D is not valid in REGLIST
                }
            }
            off += bytes;
        }
        else off += size_t(nloop) * 16u;   // IMAGE / disabled
    }
}

bool applyLocked(WsHudState &s, const uint8_t *&data, size_t &size, bool hostGif)
{
    static const bool s_wshud = [](){ const char *v = std::getenv("PS2X_PGS_WSHUD"); return !(v && v[0] == '0'); }();
    static const bool s_inkShiftEnvOn = [](){ const char *v = std::getenv("PS2X_PGS_INKSHIFT"); return v && v[0] && std::atof(v) > 0.0; }();
    const bool inkWork = !GsGpuRenderer::outlineEnabled() || GsGpuRenderer::inkStrengthPct() != 199 || s_inkShiftEnvOn || g_inkWidthPct.load(std::memory_order_relaxed) < 100 || g_inkColor.load(std::memory_order_relaxed) != 0u
                      || !GsGpuRenderer::shadowsEnabled() || !GsGpuRenderer::dofBlurEnabled();   // [pgsink] [pgsfx]
    // [vpdrop] force the pass on for PS2X_VPKEEP: with inv==1.0 and stock ink/fx this gate is
    // otherwise CLOSED (and self-latching -- wshud.active is only set from inside the pass).
    if (!((s_wshud && (g_ps2xWsHudInv < 0.999f || s.wshud.active || inkWork)) || ps2xVpKeep())) return false;
    const uint8_t *xdata = data; size_t xsize = size;
    if (wsHudSubdivideLocked(s, data, size, s.wshud.lastInv, hostGif)) { xdata = s.wsBuf.data(); xsize = s.wsBuf.size(); }
    wsHudRewriteLocked(s, const_cast<uint8_t *>(xdata), xsize, hostGif);   // the packet buffer is the arbiter's copy (or our rebuilt one)
    data = xdata; size = xsize;
    return true;
}

// [nativehud] what the native front end needs from this side to do the widescreen HUD layout itself
float rawInv(uint32_t fbw)
{
    static const float s_pixk = [](){ const char *v = std::getenv("PS2X_PIXK"); const float f = v ? float(std::atof(v)) : 1.08f; return (f > 0.5f && f < 2.0f) ? f : 1.08f; }();
    WsHudState &s = state();
    const uint32_t dw = g_presentW.load(std::memory_order_relaxed), dh = g_presentH.load(std::memory_order_relaxed);
    const float fw = (fbw * 64u >= 320u && fbw * 64u <= 1024u) ? float(fbw * 64u) : 512.0f;
    const float fh = s.baseH ? float(s.baseH) : 448.0f;
    if (!(g_ps2xWsHudInv < 0.999f && dw && dh)) return 1.0f;
    float raw = (float(dh) / fh * s_pixk) / (float(dw) / fw);
    if (raw < 0.4f) raw = 0.4f; if (raw > 1.0f) raw = 1.0f;
    return raw;
}
bool vpKeepOn() { return ps2xVpKeep() != 0; }
uint32_t inkColor() { return g_inkColor.load(std::memory_order_relaxed); }
int inkWidthPct() { return g_inkWidthPct.load(std::memory_order_relaxed); }
void setInkColor(uint32_t rgb) { g_inkColor.store(rgb & 0xFFFFFFu, std::memory_order_relaxed); }
void setInkWidthPct(int pct) { g_inkWidthPct.store(pct, std::memory_order_relaxed); }
void setPresentSize(uint32_t w, uint32_t h) { g_presentW.store(w, std::memory_order_relaxed); g_presentH.store(h, std::memory_order_relaxed); }
void presentSize(uint32_t &w, uint32_t &h) { w = g_presentW.load(std::memory_order_relaxed); h = g_presentH.load(std::memory_order_relaxed); }
float lastInv() { return state().wshud.lastInv; }
void printStats(double dt)
{   // the paraLLEl-GS stats line's walker section (counters reset per window)
    WsHudState &st = state(); std::lock_guard<std::mutex> lk(st.mtx); auto &h = st.wshud;
    std::fprintf(stderr, " | wshud: inv %.3f (present %.3f) active %d prims/s %.0f verts/s %.0f", h.lastInv, g_ps2xWsHudInv, h.active ? 1 : 0, h.hudPrims / dt, h.mappedVerts / dt);
    std::fprintf(stderr, " scissors/s %.0f splits/s %.0f | ink: outline %d strength %d%% width %d%% dropped/s %.0f scaled/s %.0f shifted/s %.0f | fx: shadows %d dof %d dropped/s %.0f/%.0f masks/s %.0f bloom/s %.0f restores/s %.0f",
                 h.scissorsMapped / dt, h.splitPrims / dt, GsGpuRenderer::outlineEnabled() ? 1 : 0, GsGpuRenderer::inkStrengthPct(), g_inkWidthPct.load(), h.inkDropped / dt, h.inkScaled / dt, h.inkShifted / dt,
                 GsGpuRenderer::shadowsEnabled() ? 1 : 0, GsGpuRenderer::dofBlurEnabled() ? 1 : 0, h.shadowDropped / dt, h.dofDropped / dt, h.maskNeutralized / dt, h.bloomEdits / dt, h.bloomRestores / dt);
    h.bloomEdits = h.bloomRestores = 0; h.hudPrims = h.mappedVerts = h.scissorsMapped = h.splitPrims = h.inkDropped = h.inkScaled = h.inkShifted = h.shadowDropped = h.dofDropped = h.maskNeutralized = 0;
}
void noteSwap(uint32_t baseH) { WsHudState &s = state(); std::lock_guard<std::mutex> lk(s.mtx); s.swaps++; if (baseH) s.baseH = baseH; }
bool apply(const uint8_t *&data, size_t &size, bool hostGif) { WsHudState &s = state(); std::lock_guard<std::mutex> lk(s.mtx); return applyLocked(s, data, size, hostGif); }
bool preprocess(uint8_t pathId, const uint8_t *&data, size_t &size, bool hostGif)
{   // [seamwshud] for the native renderer: the arbiter calls this on every packet (host draws included: the walker tracks the
    // register state) before the native front end parses it. The same rewrite the paraLLEl-GS path applies -- edge/centered
    // HUD layouts included.
    if (!data || size < 16 || pathId < 1 || pathId > 3) return false;
    WsHudState &s = state();
    std::lock_guard<std::mutex> lk(s.mtx);
    return applyLocked(s, data, size, hostGif);
}
}
