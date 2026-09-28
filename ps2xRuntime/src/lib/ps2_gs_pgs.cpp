// [pgs] paraLLEl-GS backend -- see include/runtime/ps2_gs_pgs.h. First-light integration (2026-09-10):
//   * one Vulkan device (Granite, headless -- no surface; the GL window is untouched),
//   * every arbiter packet goes to GSInterface::gif_transfer on its own path index,
//   * the privileged registers are shadowed from the guest's stores and copied in at each swap,
//   * at the swap: flush + vsync, then a synchronous readback of the scanout to an RGBA8 buffer that the present
//     thread uploads as a texture. The readback is a full GPU sync per frame -- fine for first light, not for perf.
#include "runtime/ps2_gs_pgs.h"
#include "runtime/ps2_seamvk.h"   // [seamvk]
#include "runtime/ps2_wshud.h"   // [wshud] the HUD packet walker, shared with the native renderer
#include "runtime/ps2_netplay.h"   // [vpdrop] follow the netplay player
#include "runtime/ps2_memory.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_psmct32.h"   // [targetdump]        // [pgs-texreplace] GS (VRAM, palettes), register structs
#include "runtime/ps2_gs_gpu_renderer.h"   // [pgsink] the overlay's Cel Outline / ink strength (static getters)
#include "runtime/ps2_gs_rasterizer.h" // GSRasterizer::fillClutFrom
#include "runtime/ps2_texreplace.h"    // ps2tex::identify / loadReplacement
#include <unordered_map>
#include <string>
#include "gs_interface.hpp"
#include "device.hpp"
#include "context.hpp"
#include "thread_id.hpp"
#include <algorithm>
#include <atomic>
#include <new>
#include <vector>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
const char *ps2xRtStalePageInfo(const GS *gs, uint32_t pg);   // [rtstale] debug text, defined in ps2_gs_gpu.cpp
bool ps2xGsRegionDrawnSinceWrite(const GS *gs, uint32_t bp, uint32_t bw, uint8_t psm, uint32_t w, uint32_t h, uint32_t *stalePg);   // [rtstale]

extern float g_ps2xWsHudInv;   // [wshud] per-frame HUD squeeze factor from the present (1.0 = off), ps2_runtime.cpp
extern std::atomic<int> g_wsHudLayout;   // overlay: 0 centered, 1 edge-pinned, 2 custom (-1 = unset)
extern std::atomic<int> g_wsHudOffLQ, g_wsHudOffCQ, g_wsHudOffRQ;   // custom offsets x16
bool ps2FightUpdateRecent();   // ps2_stepcensus.cpp [wshudmenu]: the fight update ran within the last 2 render frames (menus: never)

extern std::atomic<uint64_t> g_bt3FrameCount;   // [presentlat] game_overrides.cpp: the game's frame counter (file scope, outside every namespace)
namespace ps2x_pgs
{
static std::atomic<int> g_enabled{-1};

static std::atomic<int> g_packOn{1};          // [pgslive] the overlay's Texture Replacement toggle (hook returns nothing when off)
static std::atomic<int> g_packFlushReq{0};    // [pgslive] toggle changed: drop the backend's cached textures at the next transfer
static std::atomic<int> g_wantScale{0};       // [pgslive] the overlay's Internal Resolution (1..4; 0 = not set, env/default apply)
static uint32_t scaleToSsaa(int scale) { return scale >= 4 ? 16u : scale == 3 ? 8u : scale == 2 ? 4u : 1u; }
std::atomic<int> g_pgsProbeReq{0};            // [vramprobe] set by the GS parse after the 32-sprite depth-mask pass (ps2x_pgs::)
std::atomic<unsigned> g_pgsProbeFbp{0}, g_pgsProbeZbp{0};
// [vramprobe] paraLLEl-GS VRAM byte address of a 32-bit pixel (PSMCT32 / PSMZ32 layout, the fork's swizzle_PS2)
static inline uint32_t pgsAddr32(uint32_t basePage, uint32_t pageStride, uint32_t x, uint32_t y, bool z)
{
    const uint32_t pageIndex = (y >> 5) * pageStride + (x >> 6);
    const uint32_t bx = (x & 63u) >> 3, by = (y & 31u) >> 3, col = (y & 7u) >> 1, px = x & 7u, py = y & 1u;
    uint32_t block = (bx & 1u) | ((by & 1u) << 1) | ((bx & 2u) << 1) | ((by & 2u) << 2) | ((bx & 4u) << 2);
    block += basePage * 32u;
    if (z) block ^= 0x18u;
    const uint32_t pix = (px & 1u) | ((py & 1u) << 1) | ((px & 2u) << 1) | ((px & 4u) << 1);
    return pageIndex * 8192u + block * 256u + col * 64u + pix * 4u;
}
namespace
{
using namespace Vulkan;
using namespace ParallelGS;

struct Signals final : SignalInterface
{   // exclusive mode: our GS parse is skipped, so FINISH/SIGNAL/LABEL must reach the guest's CSR from here
    GSRegisters *regs = nullptr;
    bool on_signal(uint64_t payload) override
    {
        if (!regs) return false;
        const uint32_t id = uint32_t(payload), mask = uint32_t(payload >> 32);
        uint32_t lo = uint32_t(regs->siglblid & 0xFFFFFFFFu);
        lo = (lo & ~mask) | (id & mask);
        regs->siglblid = (regs->siglblid & 0xFFFFFFFF00000000ull) | lo;
        regs->csr.fetch_or(0x1);
        return false;
    }
    bool on_finish(uint64_t) override { if (regs) regs->csr.fetch_or(0x2); return false; }
    bool on_label(uint64_t payload) override
    {
        if (!regs) return false;
        const uint32_t id = uint32_t(payload), mask = uint32_t(payload >> 32);
        uint32_t hi = uint32_t(regs->siglblid >> 32);
        hi = (hi & ~mask) | (id & mask);
        regs->siglblid = (uint64_t(hi) << 32) | (regs->siglblid & 0xFFFFFFFFu);
        return false;
    }
};

#if defined(PARALLEL_GS_TEXREPLACE)
// [pgs-texreplace] Texture pack on the paraLLEl-GS path. paraLLEl-GS asks at every texture-cache miss; we hash the
// texture exactly as the GL renderer does (PCSX2-compatible XXH3 over the swizzled VRAM blocks + the palette, from OUR
// GS parse running state-only in pack mode) and hand back a Vulkan image of the replacement, any size -- the
// ubershader samples with normalized coordinates, so a 4x image drops in without shader changes.
// [gatealpha] BC1/BC2/BC3 -> RGBA8 (raylib PixelFormat 14/15 = DXT1, 16 = DXT3, 17 = DXT5). Needed to rewrite the alpha of
// compressed replacements; only gate-style assets go through this (a few textures), everything else stays compressed.
static void bcDecodeColor(const uint8_t *b, bool bc1Mode, uint8_t (*out)[4])
{
    const uint32_t c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    auto expand = [](uint32_t c, uint8_t *rgb) { rgb[0] = uint8_t(((c >> 11) & 31) * 255 / 31); rgb[1] = uint8_t(((c >> 5) & 63) * 255 / 63); rgb[2] = uint8_t((c & 31) * 255 / 31); };
    uint8_t pal[4][4] = {};
    expand(c0, pal[0]); expand(c1, pal[1]); pal[0][3] = pal[1][3] = 255;
    if (!bc1Mode || c0 > c1)
    {
        for (int k = 0; k < 3; k++) { pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3); pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3); }
        pal[2][3] = pal[3][3] = 255;
    }
    else
    {
        for (int k = 0; k < 3; k++) { pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
        pal[2][3] = 255; pal[3][3] = 0;
    }
    const uint32_t idx = b[4] | (b[5] << 8) | (b[6] << 16) | (uint32_t(b[7]) << 24);
    for (int i = 0; i < 16; i++) { const uint32_t k = (idx >> (2 * i)) & 3u; out[i][0] = pal[k][0]; out[i][1] = pal[k][1]; out[i][2] = pal[k][2]; out[i][3] = pal[k][3]; }
}
static bool bcDecode(int fmt, const std::vector<uint8_t> &src, int w, int h, std::vector<uint8_t> &rgba)
{
    const size_t blockBytes = (fmt == 14 || fmt == 15) ? 8u : 16u;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (src.size() < size_t(bw) * bh * blockBytes) return false;
    rgba.assign(size_t(w) * h * 4u, 0);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
        {
            const uint8_t *b = src.data() + (size_t(by) * bw + bx) * blockBytes;
            uint8_t tex[16][4];
            uint8_t alpha[16];
            if (fmt == 14 || fmt == 15) { bcDecodeColor(b, true, tex); for (int i = 0; i < 16; i++) alpha[i] = tex[i][3]; }
            else if (fmt == 16)
            {   // BC2: 4-bit explicit alpha
                for (int i = 0; i < 16; i++) { const uint32_t nib = (b[i / 2] >> ((i & 1) * 4)) & 15u; alpha[i] = uint8_t(nib * 17u); }
                bcDecodeColor(b + 8, false, tex);
            }
            else
            {   // BC3: interpolated alpha
                const uint32_t a0 = b[0], a1 = b[1];
                uint8_t ramp[8]; ramp[0] = uint8_t(a0); ramp[1] = uint8_t(a1);
                if (a0 > a1) for (int k = 1; k < 7; k++) ramp[k + 1] = uint8_t(((7 - k) * a0 + k * a1) / 7);
                else { for (int k = 1; k < 5; k++) ramp[k + 1] = uint8_t(((5 - k) * a0 + k * a1) / 5); ramp[6] = 0; ramp[7] = 255; }
                uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = ramp[(bits >> (3 * i)) & 7u];
                bcDecodeColor(b + 8, false, tex);
            }
            for (int i = 0; i < 16; i++)
            {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w || y >= h) continue;
                uint8_t *d = rgba.data() + (size_t(y) * w + x) * 4u;
                d[0] = tex[i][0]; d[1] = tex[i][1]; d[2] = tex[i][2]; d[3] = alpha[i];
            }
        }
    return true;
}
struct Replacer final : TextureReplacementInterface
{
    GS *gs = nullptr;
    std::unordered_map<std::string, ImageHandle> cache;   // name -> image (null = known miss)
    uint64_t hits = 0, misses = 0, skipped = 0, rtstale = 0, gateSkips = 0;
    ImageHandle replace(const TextureDescriptor &desc, uint64_t liveTex0, uint64_t liveTexclut, Device &device) override
    {
        if (!gs || !ps2tex::replacementsEnabled()) { skipped++; return {}; }
        if (!g_packOn.load(std::memory_order_relaxed)) { skipped++; return {}; }   // [pgslive] toggle off: native decode
        const uint32_t psm = uint32_t(desc.tex0.desc.PSM);
        if (psm != GS_PSM_T8 && psm != GS_PSM_T4) { skipped++; return {}; }   // identify() hashes paletted textures only
        GSTex0Reg t{};
        t.tbp0 = uint32_t(desc.tex0.desc.TBP0); t.tbw = uint8_t(desc.tex0.desc.TBW); t.psm = uint8_t(psm);
        t.tw = uint8_t(desc.tex0.desc.TW); t.th = uint8_t(desc.tex0.desc.TH); t.tcc = uint8_t(desc.tex0.desc.TCC); t.tfx = uint8_t(desc.tex0.desc.TFX);
        {   // [rtstale] pages the game drew into since their last upload/copy hold pixels our mirror never saw: the
            // hash would be of whatever texture lived there before (the portraits and energy bars that showed up on
            // the post-process quads). Refuse them; the backend samples its own VRAM.
            uint32_t stalePg = ~0u;
            if (ps2xGsRegionDrawnSinceWrite(gs, t.tbp0, t.tbw, uint8_t(psm), 1u << t.tw, 1u << t.th, &stalePg))
            {
                rtstale++;
                static const bool s_log = [](){ const char *v = std::getenv("PS2X_PGS_PACKLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_n = 0;
                if (s_log && s_n < 40) { s_n++; std::fprintf(stderr, "[pgs-rtstale] #%u refused tex tbp0=%u tbw=%u psm=%u tw=%u th=%u: %s\n", s_n, t.tbp0, t.tbw, psm, t.tw, t.th, ps2xRtStalePageInfo(gs, stalePg)); }
                return {};
            }
        }
        // palette address fields come from the LIVE TEX0 (the descriptor zeroes them: paraLLEl-GS keys palettes by bank)
        Reg64<TEX0Bits> lt; lt.bits = liveTex0;
        t.cbp = uint32_t(lt.desc.CBP); t.cpsm = uint8_t(lt.desc.CPSM); t.csm = uint8_t(lt.desc.CSM);
        t.csa = uint8_t(lt.desc.CSA); t.cld = uint8_t(lt.desc.CLD);
        if (ps2xGsRegionDrawnSinceWrite(gs, t.cbp, 1u, GS_PSM_CT32, 16u, 16u, nullptr))
        {   // [rtstale] a palette the game rendered (BT3 draws its outline CLUTs); a CSM1 CLUT is a 16x16 CT32 block group
            rtstale++;
            static const bool s_log = [](){ const char *v = std::getenv("PS2X_PGS_PACKLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_n = 0;
            if (s_log && s_n < 40) { s_n++; std::fprintf(stderr, "[pgs-rtstale] #%u refused CLUT cbp=%u for tex tbp0=%u: %s\n", s_n, t.cbp, t.tbp0, ps2xRtStalePageInfo(gs, t.cbp / 32u)); }
            return {};
        }
        Reg64<TEXCLUTBits> tc; tc.bits = liveTexclut;
        GSTexClutReg texclut{uint8_t(tc.desc.CBW), uint8_t(tc.desc.COU), uint16_t(tc.desc.COV)};
        GSTexaReg texa{uint8_t(desc.texa.desc.TA0), desc.texa.desc.AEM != 0, uint8_t(desc.texa.desc.TA1)};
        uint32_t clut[256] = {};
        const int n = GSRasterizer::fillClutForBackend(clut, gs->vramData(), texa, texclut, t);
        ps2tex::TexIdent id;
        if (n <= 0 || !ps2tex::identify(gs->vramData(), t.tbp0, t.tbw, t.psm, t.tw, t.th, clut, texa.ta0, texa.aem, texa.ta1, id)) { skipped++; return {}; }
        const std::string name = id.name();
        {   // PS2X_PGS_PACKLOG=1: the first identifications, to compare against the pack's file names
            static const bool s_log = [](){ const char *v = std::getenv("PS2X_PGS_PACKLOG"); return v && v[0] && v[0] != '0'; }(); static unsigned s_n = 0;
            static const unsigned s_max = [](){ const char *v = std::getenv("PS2X_PGS_PACKLOGN"); return v && v[0] ? unsigned(std::atoi(v)) : 40u; }();
            if (s_log && s_n < s_max && !cache.count(name)) { s_n++; std::fprintf(stderr, "[pgs-pack] #%u %s psm=%u tbp0=%u tbw=%u tw=%u th=%u cbp=%u csa=%u cpsm=%u bank=%u\n", s_n, name.c_str(), psm, t.tbp0, t.tbw, t.tw, t.th, t.cbp, t.csa, t.cpsm, unsigned(desc.palette_bank)); }
        }
        auto it = cache.find(name);
        if (it != cache.end()) { if (it->second) hits++; else misses++; return it->second; }
        // [gatealpha] the OpenGL path's rule (ps2_gs_rasterizer.cpp [texreplace]): a texture whose native alpha is
        // BINARY (every palette entry clear or solid) is a destination-alpha gate asset -- BT3's health-bar strips
        // and plaques are written as a DATE gate first and colour-filled through it. A filtered, upscaled
        // replacement puts partial alpha and a differently drawn edge into that gate and the fills no longer
        // meet it (the yellow sliver at the opponent bar's cap). Uncompressed replacements get their alpha
        // snapped; a compressed one (the whole pack is DXT5) keeps the game's own texture, as in OpenGL.
        bool gateAlpha = false; uint32_t aSolid = 0, aClear = 255;
        {
            bool clear = false, solid = false, mid = false; uint32_t amax = 0;
            for (int i = 0; i < n; i++) amax = std::max(amax, clut[i] >> 24);
            const uint32_t loT = amax > 0x80u ? 4u : 2u, hiT = amax > 0x80u ? 251u : 0x7eu;   // expanded (0..255) or PS2 (0..0x80) range
            for (int i = 0; i < n && !mid; i++)
            {
                const uint32_t a = clut[i] >> 24;
                if (a <= loT) { clear = true; aClear = std::min<uint32_t>(aClear, a); }
                else if (a >= hiT) { solid = true; aSolid = std::max<uint32_t>(aSolid, a); }
                else mid = true;
            }
            gateAlpha = !mid && clear && solid;
            if (amax > 0x80u) { aSolid = aSolid * 128u / 255u; aClear = aClear * 128u / 255u; }   // back to the PS2 range the image is stored in
        }
        std::vector<uint8_t> px; int w = 0, h = 0, fmt = 0;
        if (!ps2tex::loadReplacement(id, px, w, h, fmt) || w <= 0 || h <= 0 || px.empty()) { cache.emplace(name, ImageHandle{}); misses++; return {}; }
        // The gate BUILD itself (the alpha-only write) never reaches this hook: the fork keys those texture uses
        // separately and decodes the game's own texture for them, so the gate keeps the native alpha shape while
        // the colour fills drawn through it use the pack. Uncompressed gate-style replacements still get a binary
        // alpha so their own edge is hard; compressed ones are used as they are.
        if (gateAlpha)
        {   // pack colours, NATIVE alpha semantics: the pack stores "opaque" as 126..131 (DXT5 noise around 128) and the
            // game's later destination-alpha tests key on bit 7 -- BT3's ki gauge (DATM=1) leaked into the health bar
            // wherever a fill texel landed at >= 128. Rewrite every texel's alpha to the palette's own clear/solid value.
            if (fmt != 7)
            {
                std::vector<uint8_t> dec;
                if (!bcDecode(fmt, px, w, h, dec)) { cache.emplace(name, ImageHandle{}); misses++; return {}; }
                px.swap(dec); fmt = 7;
            }
            for (size_t i = 3; i < px.size(); i += 4) px[i] = px[i] >= 64u ? uint8_t(aSolid) : uint8_t(aClear);
            gateSkips++;   // stat: gate-style assets rewritten
            static unsigned s_gl = 0;
            if (s_gl < 6) { s_gl++; std::fprintf(stderr, "[pgs-pack] gate-alpha %s: alpha rewritten to native %u/%u (%dx%d)\n", name.c_str(), aSolid, aClear, w, h); }
        }
        // raylib PixelFormat values (raylib.h): 7 = R8G8B8A8, 14 = DXT1 RGB, 15 = DXT1 RGBA, 16 = DXT3, 17 = DXT5
        VkFormat vkfmt = VK_FORMAT_UNDEFINED;
        switch (fmt)
        {
        case 7: vkfmt = VK_FORMAT_R8G8B8A8_UNORM; break;
        case 14: case 15: vkfmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
        case 16: vkfmt = VK_FORMAT_BC2_UNORM_BLOCK; break;
        case 17: vkfmt = VK_FORMAT_BC3_UNORM_BLOCK; break;
        default: break;
        }
        if (vkfmt == VK_FORMAT_UNDEFINED) { cache.emplace(name, ImageHandle{}); misses++; return {}; }
        auto info = ImageCreateInfo::immutable_2d_image(uint32_t(w), uint32_t(h), vkfmt);
        info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;   // paraLLEl-GS's post-upload barrier moves GENERAL -> READ_ONLY
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ImageInitialData init = {}; init.data = px.data();
        ImageHandle img = device.create_image(info, &init);
        if (!img) { cache.emplace(name, ImageHandle{}); misses++; return {}; }
        cache.emplace(name, img); hits++;
        return img;
    }
};

#else
struct Replacer { GS *gs = nullptr; std::unordered_map<std::string, int> cache; uint64_t hits = 0, misses = 0, skipped = 0, rtstale = 0, gateSkips = 0; };   // upstream paraLLEl-GS without the hook: no pack path
#endif

struct State
{
    std::mutex mtx;
    bool inited = false, failed = false;
    Context ctx;
    Device device;
    GSInterface iface;
    Signals signals;
    Replacer replacer;   // [pgs-texreplace]
    Hacks hacks;
    // privileged register shadow, by hardware offset (0x0000.. and 0x1000..), 64-bit each
    uint64_t privLo[0x100] = {};
    uint64_t privHi[0x100] = {};
    // newest scanout
    std::vector<uint8_t> frame;
    uint32_t frameW = 0, frameH = 0; uint64_t frameGframe = 0;   // [presentlat]
    uint32_t ssaa = 1;                     // super-sampling factor the backend was created with (1/2/4/8/16)
    uint32_t baseW = 0, baseH = 0;         // [pgsfit] last scanout size in the 1x domain (image extent >> shift)
    uint32_t lastShift = 0;                // [pgsfit] scanout shift used for the last swap (0 = 1x, 1 = 2x, 2 = 4x)
    bool frameFresh = false;
    uint32_t field = 0;
    uint64_t swaps = 0, packets = 0, bytes = 0, noImage = 0;
    double xferMs = 0.0;   // CPU time inside gif_transfer (the packet parse on our GsThread)
    uint64_t fsPrims = 0, fsPasses = 0, fsCopies = 0, fsPal = 0;   // paraLLEl-GS flush stats per stats window
    struct SlowCall { double ms; uint32_t size, path, nloop, flg, nreg; uint64_t regs; uint32_t firstAD; };   // [pgs-slow] the 3 slowest gif_transfer calls per window
    SlowCall slowCalls[3] = {};
    uint32_t slowOver1ms = 0;
    uint64_t streamDispfb1 = 0; bool haveStreamFlip = false;   // the game's DISPFB1 flip, carried in stream order ([displatch] job)
    uint64_t streamLo[0x10] = {}; uint32_t streamHave = 0;    // [s1fence] the stream-ordered display block (slot = regOff >> 4), bit set once written
    uint64_t streamFlips = 0, flipMismatch = 0, lastFb1 = 0, lastLive1 = 0, lastLive2 = 0;   // [pgsflip] diagnostics
    uint32_t privHist[0x20] = {};   // privileged stores per 16-byte slot since the last stats line (bus + pseudo regs)
    uint64_t pseudoSeen = 0;        // in-stream pseudo A+D registers applied (exclusive mode)
    bool timestamps = false;
    std::chrono::steady_clock::time_point tStat = std::chrono::steady_clock::now();
    double readbackMs = 0.0, vsyncMs = 0.0;
};
State &st() { static State *s = new State; return *s; }   // leaked on purpose: never destroy the device behind a running thread

bool envOn(const char *name) { const char *v = std::getenv(name); return v && v[0] && v[0] != '0'; }
}
extern "C" bool ps2xStage1FenceC();   // [s1fence] ps2_memory.cpp (C linkage, declared inside ps2x_pgs on purpose)
namespace
{

void registerThread()
{   // Granite keys per-thread command pools by a registered index; unregistered threads log an error per call.
    // Every caller here is serialised by the state mutex, so they can all share index 0 (the replayer's main thread).
    static thread_local bool t_reg = false;
    if (!t_reg) { Util::register_thread_index(0); t_reg = true; }
}

bool initLocked(State &s)
{
    registerThread();
    if (s.inited) return true;
    if (s.failed) return false;
    s.failed = true;   // until proven otherwise
    auto fail = [](const char *why) { std::fprintf(stderr, "[pgs] %s -- backend unavailable, falling back to the OpenGL renderer\n", why); g_enabled.store(0, std::memory_order_relaxed); return false; };
    if (!Context::init_loader(nullptr)) return fail("Vulkan loader init failed");
    s.ctx.set_num_thread_indices(1);
    if (!s.ctx.init_instance_and_device(nullptr, 0, nullptr, 0,
                                        CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
                                        CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
                                        CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT))
        return fail("Vulkan instance/device init failed");
    s.device.set_context(s.ctx);
    s.device.init_frame_contexts(4);
    GSOptions opts = {};
    opts.vram_size = 4 * 1024 * 1024;
    {
        const char *v = std::getenv("PS2X_PGS_SSAA");
        const int ws = g_wantScale.load(std::memory_order_relaxed);
        const int r = ws > 0 ? int(scaleToSsaa(ws)) : (v && v[0] ? std::atoi(v) : 1);   // [pgslive] overlay scale > env > 1
        opts.super_sampling = r >= 16 ? SuperSampling::X16 : r >= 8 ? SuperSampling::X8 : r >= 4 ? SuperSampling::X4 : r >= 2 ? SuperSampling::X2 : SuperSampling::X1;
        opts.super_sampled_textures = [](){ const char *v = std::getenv("PS2X_PGS_SSTEX"); return !(v && v[0] == '0'); }();   // [pgsink] per-sample reads of render targets (the outline chain's silhouette buffer; needed for ink widths < 100%)
        s.ssaa = r >= 16 ? 16u : r >= 8 ? 8u : r >= 4 ? 4u : r >= 2 ? 2u : 1u;
    }
    if (!s.iface.init(&s.device, opts)) return fail("GSInterface init failed");
    s.iface.set_signal_interface(&s.signals);
    { Hacks hk; hk.force_bilinear = envOn("PS2X_PGS_FORCE_BILINEAR");
      hk.replaced_per_sample = [](){ const char *v = std::getenv("PS2X_PGS_REPLPERSAMPLE"); return !(v && v[0] == '0'); }();   // A/B: 0 = snapped evaluation for replaced sprites
      hk.skip_kick_mask = [](){ const char *v = std::getenv("PS2X_PGS_SKIPKICK"); return v && v[0] ? uint32_t(std::atoi(v)) : 0u; }();   // bisect: 1 = 16-bit frame kicks, 2 = 24/32-bit reads of block 10752, 4 = frame fbp 336
      s.hacks = hk; s.iface.set_hacks(hk); }
#if defined(PARALLEL_GS_TEXREPLACE)
    if (packMode()) s.iface.set_texture_replacement_interface(&s.replacer);   // [pgs-texreplace]
#else
    if (packMode()) std::fprintf(stderr, "[pgs] texture pack requested but this paraLLEl-GS checkout has no replacement hook (upstream); packs stay off\n");
#endif
    s.timestamps = envOn("PS2X_PGS_TIMESTAMPS");
    if (s.timestamps) { DebugMode dm = {}; dm.timestamps = true; s.iface.set_debug_mode(dm); }
    s.failed = false; s.inited = true;
    std::fprintf(stderr, "[pgs] paraLLEl-GS backend up: %s, ssaa=%u, force_bilinear=%d, replaced_per_sample=%d, skipkick=%u, %s\n",
                 s.device.get_gpu_properties().deviceName, unsigned(opts.super_sampling), s.hacks.force_bilinear ? 1 : 0, s.hacks.replaced_per_sample ? 1 : 0, unsigned(s.hacks.skip_kick_mask),
                 exclusive() ? "EXCLUSIVE (our GS parse skipped)" : packMode() ? "PACK mode (our GS parse state-only, replacements via the backend)" : "dual (our GL renderer keeps running)");
    return true;
}

void copyPrivLocked(State &s)
{
    auto &p = s.iface.get_priv_register_state();
    auto put = [](void *dst, uint64_t v) { std::memcpy(dst, &v, sizeof(v)); };
    if (const GSRegisters *r = s.signals.regs)
    {   // live block: bus stores, the sceGs stubs and our GS parse all land here
        put(&p.pmode, r->pmode);     put(&p.smode1, r->smode1 ? r->smode1 : 0x0000000740814504ULL);   put(&p.smode2, r->smode2);   // NTSC default when the CRTC was never programmed
        put(&p.srfsh, r->srfsh);     put(&p.synch1, r->synch1);   put(&p.synch2, r->synch2);
        static const bool s_useStream = !envOn("PS2X_PGS_LIVEFLIP");   // PS2X_PGS_LIVEFLIP=1: scan out whatever the bus says right now
        if (ps2xStage1FenceC())
        {   // [s1fence] the display block in STREAM order: whichever of the three writers (bus store, display-env
            // stub, parse pseudo-register) came last IN THE STREAM wins, not whichever thread ran last. Registers
            // never written on the stream fall back to the live block (boot, before the first frame).
            auto pick = [&](uint32_t slot, uint64_t live) { return (s.streamHave & (1u << slot)) ? s.streamLo[slot] : live; };
            const uint64_t pm = pick(0, r->pmode), sm2 = pick(2, r->smode2), fb1 = pick(7, r->dispfb1), d1 = pick(8, r->display1);
            const uint64_t fb2 = pick(9, r->dispfb2), d2 = pick(10, r->display2), bg = pick(14, r->bgcolor);
            put(&p.pmode, pm); put(&p.smode2, sm2);
            if (fb1 != r->dispfb1) s.flipMismatch++;
            s.lastFb1 = fb1; s.lastLive1 = r->dispfb1; s.lastLive2 = r->dispfb2;
            put(&p.syncv, r->syncv);     put(&p.dispfb1, fb1);        put(&p.display1, d1);
            put(&p.dispfb2, fb2);        put(&p.display2, d2);        put(&p.extbuf, r->extbuf);
            put(&p.extdata, r->extdata); put(&p.extwrite, r->extwrite); put(&p.bgcolor, bg);
            put(&p.csr, r->csr.load(std::memory_order_relaxed)); put(&p.imr, r->imr); put(&p.busdir, r->busdir);
            put(&p.siglblid, r->siglblid);
            s.privLo[0] = pm; s.privLo[2] = sm2; s.privLo[7] = fb1; s.privLo[8] = d1; s.privLo[9] = fb2; s.privLo[10] = d2;   // for the stats line
            return;
        }
        const uint64_t fb1 = (s_useStream && s.haveStreamFlip) ? s.streamDispfb1 : r->dispfb1;
        const uint64_t fb2 = (s_useStream && s.haveStreamFlip && r->dispfb2 == r->dispfb1) ? s.streamDispfb1 : r->dispfb2;
        if (fb1 != r->dispfb1) s.flipMismatch++;
        s.lastFb1 = fb1; s.lastLive1 = r->dispfb1; s.lastLive2 = r->dispfb2;
        put(&p.syncv, r->syncv);     put(&p.dispfb1, fb1);        put(&p.display1, r->display1);
        put(&p.dispfb2, fb2);        put(&p.display2, r->display2); put(&p.extbuf, r->extbuf);
        put(&p.extdata, r->extdata); put(&p.extwrite, r->extwrite); put(&p.bgcolor, r->bgcolor);
        put(&p.csr, r->csr.load(std::memory_order_relaxed)); put(&p.imr, r->imr); put(&p.busdir, r->busdir);
        put(&p.siglblid, r->siglblid);
        s.privLo[0] = r->pmode; s.privLo[2] = r->smode2; s.privLo[7] = r->dispfb1; s.privLo[8] = r->display1;
        s.privLo[9] = r->dispfb2; s.privLo[10] = r->display2;   // for the stats line
        return;
    }
    put(&p.pmode, s.privLo[0x00 >> 4]);   put(&p.smode1, s.privLo[0x10 >> 4]);  put(&p.smode2, s.privLo[0x20 >> 4]);
    put(&p.srfsh, s.privLo[0x30 >> 4]);   put(&p.synch1, s.privLo[0x40 >> 4]);  put(&p.synch2, s.privLo[0x50 >> 4]);
    put(&p.syncv, s.privLo[0x60 >> 4]);   put(&p.dispfb1, s.privLo[0x70 >> 4]); put(&p.display1, s.privLo[0x80 >> 4]);
    put(&p.dispfb2, s.privLo[0x90 >> 4]); put(&p.display2, s.privLo[0xA0 >> 4]); put(&p.extbuf, s.privLo[0xB0 >> 4]);
    put(&p.extdata, s.privLo[0xC0 >> 4]); put(&p.extwrite, s.privLo[0xD0 >> 4]); put(&p.bgcolor, s.privLo[0xE0 >> 4]);
    put(&p.csr, s.privHi[0x00 >> 4]);     put(&p.imr, s.privHi[0x10 >> 4]);     put(&p.busdir, s.privHi[0x40 >> 4]);
    put(&p.siglblid, s.privHi[0x80 >> 4]);
}

// [pgs-asyncrb] Asynchronous scanout readback: a three-deep ring of host buffers + fences. Each swap submits a copy of
// this frame's scanout and consumes the copy submitted two swaps ago IF its fence is already signalled (never waits).
// The first version waited for the GPU every frame (wait_idle); with the GPU idle at every frame start paraLLEl-GS
// took its CPU upload path for the frame's IMAGE transfers, a flat ~6 ms per frame ([pgs-slow] 2026-09-10).
struct RbSlot { BufferHandle buf; Fence fence; ImageHandle image; uint32_t w = 0, h = 0; bool pending = false; VkFormat fmt = VK_FORMAT_UNDEFINED; uint64_t gframe = 0; };   // [presentlat] gframe: the game frame this scanout belongs to
static RbSlot g_rb[3];
static uint32_t g_rbIdx = 0;
static void consumeSlotLocked(State &s, RbSlot &slot)
{
    const auto *src = static_cast<const uint32_t *>(s.device.map_host_buffer(*slot.buf, MEMORY_ACCESS_READ_BIT));
    const size_t n = size_t(slot.w) * slot.h;
    s.frame.resize(n * 4u);
    const bool bgra = slot.fmt == VK_FORMAT_B8G8R8A8_UNORM || slot.fmt == VK_FORMAT_B8G8R8A8_SRGB;
    if (!bgra)
    {
        std::memcpy(s.frame.data(), src, n * 4u);
        uint32_t *px = reinterpret_cast<uint32_t *>(s.frame.data());
        for (size_t i = 0; i < n; i++) px[i] |= 0xFF000000u;
    }
    else
        for (size_t i = 0; i < n; i++)
        {
            const uint32_t p = src[i];
            s.frame[i * 4 + 0] = (p >> 16) & 0xff; s.frame[i * 4 + 1] = (p >> 8) & 0xff; s.frame[i * 4 + 2] = p & 0xff; s.frame[i * 4 + 3] = 0xff;
        }
    s.device.unmap_host_buffer(*slot.buf, MEMORY_ACCESS_READ_BIT);
    s.frameW = slot.w; s.frameH = slot.h; s.frameFresh = true; s.frameGframe = slot.gframe;
    slot.pending = false; slot.image.reset(); slot.fence.reset();
}
void readbackLocked(State &s, const ScanoutResult &res)
{
    static const bool s_sync = envOn("PS2X_PGS_SYNCREADBACK");   // the old behaviour, for A/B
    // 1. consume the oldest pending slot without waiting (or with a wait in sync mode)
    for (int k = 1; k <= 2; k++)
    {
        RbSlot &old = g_rb[(g_rbIdx + k) % 3];
        if (old.pending && old.fence && (s_sync ? (old.fence->wait(), true) : old.fence->wait_timeout(0))) consumeSlotLocked(s, old);
    }
    // 2. submit this frame's copy into the current slot (if it is still in flight, drop this frame)
    RbSlot &slot = g_rb[g_rbIdx];
    if (slot.pending) { if (slot.fence && slot.fence->wait_timeout(0)) consumeSlotLocked(s, slot); else return; }
    const uint32_t w = res.image->get_width(), h = res.image->get_height();
    if (!slot.buf || slot.w != w || slot.h != h)
    {
        BufferCreateInfo bi = {};
        bi.size = VkDeviceSize(w) * h * 4u;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.domain = BufferDomain::CachedHost;
        slot.buf = s.device.create_buffer(bi);
        slot.w = w; slot.h = h;
    }
    slot.fmt = res.image->get_format();
    slot.gframe = g_bt3FrameCount.load(std::memory_order_relaxed);   // [presentlat]
    auto cmd = s.device.request_command_buffer();
    cmd->image_barrier(*res.image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    cmd->copy_image_to_buffer(*slot.buf, *res.image, 0, {}, { w, h, 1 }, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
    cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    Fence fence;
    s.device.submit(cmd, &fence);
    slot.fence = std::move(fence); slot.image = res.image; slot.pending = true;
    g_rbIdx = (g_rbIdx + 1u) % 3u;
    if (s_sync) { slot.fence->wait(); consumeSlotLocked(s, slot); }
}

// [pgs-pseudo] Our sceGs stubs deliver display-environment writes IN-STREAM as A+D writes to pseudo registers
// (Kernel/Stubs/GS.cpp: 0x41 PMODE, 0x42 SMODE2, 0x59 DISPFB1, 0x5a DISPLAY1, 0x5b DISPFB2, 0x5c DISPLAY2, 0x5f BGCOLOR).
// Our own GS parse applies them (ps2_gs_gpu.cpp); in exclusive mode that parse is skipped, so walk the packet's tags here.
// paraLLEl-GS itself treats those addresses as NOPs. Only PACKED tags carrying an A+D descriptor are walked.
// [pgswshud] the OpenGL path's layout map (ps2_gs_gpu_renderer.cpp wsMapX), W = frame width in game px
void applyPseudoRegsLocked(State &s, const uint8_t *data, size_t size)
{
    GSRegisters *r = s.signals.regs;
    if (!r) return;
    size_t off = 0;
    while (off + 16 <= size)
    {
        uint64_t lo, hi; std::memcpy(&lo, data + off, 8); std::memcpy(&hi, data + off + 8, 8);
        const uint32_t nloop = uint32_t(lo & 0x7FFFu), flg = uint32_t((lo >> 58) & 3u);
        uint32_t nreg = uint32_t((lo >> 60) & 0xFu); if (nreg == 0) nreg = 16;
        off += 16;
        if (nloop == 0) continue;
        if (flg == 0u)
        {   // PACKED: nloop * nreg qwords
            bool hasAD = false;
            for (uint32_t i = 0; i < nreg; i++) if (((hi >> (4 * i)) & 0xFu) == 0xEu) hasAD = true;
            const size_t bytes = size_t(nloop) * nreg * 16u;
            if (hasAD && off + bytes <= size)
            {
                for (uint32_t l = 0; l < nloop; l++)
                    for (uint32_t i = 0; i < nreg; i++)
                    {
                        if (((hi >> (4 * i)) & 0xFu) != 0xEu) continue;
                        const uint8_t *q = data + off + (size_t(l) * nreg + i) * 16u;
                        uint64_t v, a; std::memcpy(&v, q, 8); std::memcpy(&a, q + 8, 8);
                        switch (a & 0xFFu)
                        {
                        case 0x41:
                            // [pmodeguard] This path writes 0x1bf000001ff0000 about thirty times a
                            // second -- not a PMODE at all. PMODE has 16 meaningful bits (EN1, EN2,
                            // CRTMD, MMOD, AMOD, SLBG, ALP); that value is a DISPLAY-shaped one in
                            // the PMODE slot. It is harmless only because the game's own bus store
                            // (0x7f23, the value actually presented) lands after it once the kick
                            // queue is drained -- reorder anything on this path and the garbage
                            // reaches the scanout as black and squished frames. Drop it at source.
                            if (v <= 0xFFFFull) { r->pmode = v; s.streamLo[0] = v; s.streamHave |= 1u << 0; }
                            s.privHist[0x00 >> 4]++; s.pseudoSeen++; break;
                        case 0x42:
                            // [smode2guard] Same defect on SMODE2, which has four meaningful bits
                            // (INT, FFMD, DPMS). This path writes 0x44 -- bit 6 is undefined -- and
                            // it reaches the scanout as "640x224", i.e. half height. Same reason it
                            // normally stays invisible: the drain lets the real writer land last.
                            if (v <= 0xFull) { r->smode2 = v; s.streamLo[2] = v; s.streamHave |= 1u << 2; }
                            s.privHist[0x20 >> 4]++; s.pseudoSeen++; break;
                        // [s1fence] the parse runs on stage 2, so its writes are stream-ordered by construction: stamp the block too
                        case 0x59: r->dispfb1 = v;  s.streamLo[7] = v;  s.streamHave |= 1u << 7;  s.privHist[0x70 >> 4]++; s.pseudoSeen++; break;
                        case 0x5a: r->display1 = v; s.streamLo[8] = v;  s.streamHave |= 1u << 8;  s.privHist[0x80 >> 4]++; s.pseudoSeen++; break;
                        case 0x5b: r->dispfb2 = v;  s.streamLo[9] = v;  s.streamHave |= 1u << 9;  s.privHist[0x90 >> 4]++; s.pseudoSeen++; break;
                        case 0x5c: r->display2 = v; s.streamLo[10] = v; s.streamHave |= 1u << 10; s.privHist[0xA0 >> 4]++; s.pseudoSeen++; break;
                        case 0x5f: r->bgcolor = v;  s.streamLo[14] = v; s.streamHave |= 1u << 14; s.privHist[0xE0 >> 4]++; s.pseudoSeen++; break;
                        default: break;
                        }
                    }
            }
            off += bytes;
        }
        else if (flg == 1u) off += (size_t(nloop) * nreg + 1u) / 2u * 16u;   // REGLIST: 2 regs per qword
        else off += size_t(nloop) * 16u;                                       // IMAGE / disabled
    }
}
} // namespace

// [pgslive] re-create the backend at another super-sampling level, carrying VRAM (and, in pack mode, our parse's
// register file) across. Super-sampling is fixed at GSInterface creation; this makes the overlay's Internal
// Resolution live. Frame contexts and the Vulkan device stay; replacement images (owned by the hook's cache) survive.
static void drainReadbackLocked(State &s)
{
    for (RbSlot &slot : g_rb)
    {
        if (slot.pending && slot.fence) { slot.fence->wait(); consumeSlotLocked(s, slot); }
        slot = RbSlot{};
    }
    g_rbIdx = 0;
}
static bool reinitLocked(State &s, uint32_t ssaa)
{
    drainReadbackLocked(s);
    s.iface.flush();
    // the fork's own savestate mechanism: VRAM + the register file + the privileged registers, then clobber
    std::vector<uint8_t> vram(4u * 1024u * 1024u);
    if (const void *rd = s.iface.map_vram_read(0, vram.size())) std::memcpy(vram.data(), rd, vram.size());
    const RegisterState regs = s.iface.get_register_state();
    const PrivRegisterState priv = s.iface.get_priv_register_state();
    s.device.wait_idle();
    s.iface.~GSInterface();
    new (&s.iface) GSInterface();
    GSOptions opts = {};
    opts.vram_size = 4 * 1024 * 1024;
    opts.super_sampling = ssaa >= 16 ? SuperSampling::X16 : ssaa >= 8 ? SuperSampling::X8 : ssaa >= 4 ? SuperSampling::X4 : ssaa >= 2 ? SuperSampling::X2 : SuperSampling::X1;
    opts.super_sampled_textures = [](){ const char *v = std::getenv("PS2X_PGS_SSTEX"); return !(v && v[0] == '0'); }();
    if (!s.iface.init(&s.device, opts)) { std::fprintf(stderr, "[pgs] re-init at ssaa %u FAILED -- backend off\n", ssaa); s.failed = true; s.inited = false; g_enabled.store(0); return false; }
    s.iface.set_signal_interface(&s.signals);
    s.iface.set_hacks(s.hacks);
#if defined(PARALLEL_GS_TEXREPLACE)
    if (packMode()) s.iface.set_texture_replacement_interface(&s.replacer);
#endif
    if (s.timestamps) { DebugMode dm = {}; dm.timestamps = true; s.iface.set_debug_mode(dm); }
    if (void *w = s.iface.map_vram_write(0, vram.size())) { std::memcpy(w, vram.data(), vram.size()); s.iface.end_vram_write(0, vram.size()); }
    s.iface.get_register_state() = regs;
    s.iface.get_priv_register_state() = priv;
    s.iface.get_register_state().cached_cbp[0] = s.iface.get_register_state().cached_cbp[1] = UINT32_MAX;   // the CLUT cache is empty now
    s.iface.clobber_register_state();
    // the palette cache is gone with the old renderer: reload each context's CLUT from VRAM (BT3 keeps them there)
    for (int c = 0; c < 2; ++c)
    {
        const uint64_t tex0 = regs.ctx[c].tex0.bits;
        if (!tex0) continue;
        const uint32_t psm = uint32_t(tex0 >> 20) & 0x3f;
        if (!(psm == 0x13 || psm == 0x14 || psm == 0x1b || psm == 0x24 || psm == 0x2c)) continue;   // paletted only
        s.iface.write_register(c ? RegisterAddr::TEX0_2 : RegisterAddr::TEX0_1, (tex0 & ~(7ull << 61)) | (1ull << 61));
        s.iface.get_register_state().ctx[c].tex0.bits = tex0;
    }
    s.ssaa = ssaa; s.baseW = s.baseH = 0;
    std::fprintf(stderr, "[pgs] backend re-created live at ssaa=%u (VRAM + register file carried over)\n", ssaa);
    return true;
}
void setPackEnabled(bool on)
{
    const int prev = g_packOn.exchange(on ? 1 : 0);
    if (prev != (on ? 1 : 0)) g_packFlushReq.store(1);
}
void setRenderScale(int scale)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    g_wantScale.store(scale, std::memory_order_relaxed);
}
// [pgswin] No Windows hard-off: the D3D11 plan that retired paraLLEl-GS there is itself retired
// (1984cd1 brings paraLLEl-GS back as a renderer on every platform that builds it), but these two
// kept returning false on _WIN32, so selecting it silently ran nothing.
bool enabled()
{
    // NOTE: this used to hard-return false on Windows ("paraLLEl-GS retired on Windows"), left over
    // from the D3D11 era. The backend builds and runs on Windows again (the release bundles the Mesa
    // lavapipe ICD as the fallback driver), so the platform stub is gone: the PS2X_PGS env exported at
    // startup decides on every platform, and a failed init stores a definitive 0 via fail().
    int v = g_enabled.load(std::memory_order_relaxed);
    if (v < 0) { v = envOn("PS2X_PGS") ? 1 : 0; g_enabled.store(v, std::memory_order_relaxed); }
    return v != 0;
}
bool packMode()
{
    static const bool p = envOn("PS2X_PGS") && envOn("PS2X_PGS_PACK"); return p;
}
bool coalesce() { static const bool c = envOn("PS2X_PGS_COALESCE") && !packMode(); return c; }   // pack mode needs per-packet order
void setGs(GS *gs) { State &s = st(); std::lock_guard<std::mutex> lk(s.mtx); s.replacer.gs = gs; }
static thread_local bool t_suppressed = false;
void setSuppressed(bool on) { t_suppressed = on; }
bool exclusive() { static const bool ex = envOn("PS2X_PGS_EXCLUSIVE") && !packMode(); return ex; }   // pack mode keeps our (state-only) parse

// [postnative] ------------------------------------------------------------------------------------------------------
namespace
{
    // Byte address of pixel (x, y) in a 32-bit layout at a block-granular base (PSMCT32; PSMZ32 with z).
    inline uint32_t addrBlk32(uint32_t block, uint32_t width, uint32_t x, uint32_t y, bool z)
    {
        const uint32_t page = (block >> 5u) + (y >> 5u) * (width ? width : 1u) + (x >> 6u);
        uint32_t blockId = (block & 0x1Fu) + GSPSMCT32::blockTable32[(y >> 3u) & 3u][(x >> 3u) & 7u];
        const uint32_t pageOff = (blockId >> 5u) << 13u; blockId &= 0x1Fu;
        if (z) blockId ^= 0x18u;
        return ((page << 13u) + pageOff + blockId * 256u + GSPSMCT32::columnTable32[y & 7u][x & 7u] * 4u) & ((4u << 20) - 1u);
    }
    // One CT32 image upload (BITBLTBUF/TRXPOS/TRXREG/TRXDIR + IMAGE) through the backend, split so no IMAGE tag exceeds
    // 0x7fff qwords. dbp in blocks, dbw in 64-px pages, w*h pixels row-major.
    void uploadCt32(State &s, uint32_t dbp, uint32_t dbw, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, const uint32_t *px)
    {
        static std::vector<uint8_t> buf;
        const uint32_t rowsPer = std::max(1u, (0x7FFFu * 4u) / w);
        for (uint32_t ry = 0; ry < h; ry += rowsPer)
        {
            const uint32_t rows = std::min(rowsPer, h - ry), qw = (w * rows + 3u) / 4u;
            buf.resize(16u + 4u * 16u + 16u + qw * 16u);
            uint64_t *q = reinterpret_cast<uint64_t *>(buf.data());
            q[0] = 4ull | (0ull << 15) | (0ull << 58) | (1ull << 60); q[1] = 0xEull;   // A+D x4
            const uint64_t bitblt = (uint64_t)(dbp & 0x3FFFu) << 32 | (uint64_t)(dbw & 0x3Fu) << 48 | (uint64_t)0 << 56;
            const uint64_t trxpos = (uint64_t)(x0 & 0x7FFu) << 32 | (uint64_t)(y0 & 0x7FFu) << 48;
            const uint64_t trxreg = (uint64_t)(w & 0xFFFu) | (uint64_t)(rows & 0xFFFu) << 32;
            q[2] = bitblt; q[3] = 0x50; q[4] = trxpos; q[5] = 0x51; q[6] = trxreg; q[7] = 0x52; q[8] = 0; q[9] = 0x53;
            q[10] = (uint64_t)qw | (1ull << 15) | (2ull << 58); q[11] = 0;
            std::memcpy(buf.data() + 96, px + (size_t)ry * w, (size_t)w * rows * 4u);
            if ((w * rows) & 3u) std::memset(buf.data() + 96 + (size_t)w * rows * 4u, 0, (4u - ((w * rows) & 3u)) * 4u);
            s.iface.gif_transfer(2u, buf.data(), buf.size());   // PATH3
            y0 += rows;
        }
    }
    uint32_t csm1(uint32_t i) { return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 16u) >> 1); }
}
bool nativePostStep(int step)
{
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (!initLocked(s)) return false;
    const auto &r = s.iface.get_register_state();
    const uint32_t fbp = (uint32_t)(r.ctx[0].frame.bits & 0x1FFu) * 32u, fbw = (uint32_t)((r.ctx[0].frame.bits >> 16) & 0x3Fu);
    const uint32_t zbp = (uint32_t)(r.ctx[0].zbuf.bits & 0x1FFu) * 32u;
    const uint32_t W = 512u, H = 448u;
    if (fbw != 8u) return false;
    s.iface.flush();
    const uint8_t *v = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
    if (!v) return false;
    static std::vector<uint32_t> F, Zc, T; F.resize(W * H); Zc.resize(W * H);
    auto rd = [&](uint32_t a) { uint32_t w; std::memcpy(&w, v + a, 4); return w; };
    for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) F[y * W + x] = rd(addrBlk32(fbp, 8, x, y, false));
    switch (step)
    {
    case 0:   // depth mask: frame.A := Z24[15:8]
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) { const uint32_t z = rd(addrBlk32(zbp, 8, x, y, true)); F[y * W + x] = (F[y * W + x] & 0xFFFFFFu) | (((z >> 8) & 0xFFu) << 24); }
        uploadCt32(s, fbp, 8, 0, 0, W, H, F.data()); break;
    case 1:   // alpha clear
        for (uint32_t i = 0; i < W * H; ++i) F[i] &= 0xFFFFFFu;
        uploadCt32(s, fbp, 8, 0, 0, W, H, F.data()); break;
    case 2:   // Z top byte (CT32 layout of the Z buffer) := frame.A
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) Zc[y * W + x] = (rd(addrBlk32(zbp, 8, x, y, false)) & 0xFFFFFFu) | (F[y * W + x] & 0xFF000000u);
        uploadCt32(s, zbp, 8, 0, 0, W, H, Zc.data()); break;
    case 3:   // blur weight: frame.A := CLUT_0x3e84[Ztop].A
    {
        uint32_t clutA[256];
        for (uint32_t i = 0; i < 256; ++i) { const uint32_t j = csm1(i); clutA[i] = rd(addrBlk32(0x3e84u, 1, j & 15u, j >> 4, false)) & 0xFF000000u; }
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) { const uint32_t zt = rd(addrBlk32(zbp, 8, x, y, false)) >> 24; F[y * W + x] = (F[y * W + x] & 0xFFFFFFu) | clutA[zt]; }
        uploadCt32(s, fbp, 8, 0, 0, W, H, F.data()); break;
    }
    case 4:   // glow: 0x2a00 := 2:1 box of the scene (alpha 0x80); frame.rgb := floor(f + (round(U) - f) * A / 128), U bilinear at (x/2, y/2)
    {
        const uint32_t TW = 256u, TH = 224u; T.resize(TW * TH);
        for (uint32_t y = 0; y < TH; ++y) for (uint32_t x = 0; x < TW; ++x)
        {
            uint32_t o = 0x80000000u;
            for (int c = 0; c < 3; ++c)
            {
                const uint32_t sh = c * 8;
                const uint32_t sum = ((F[(2 * y) * W + 2 * x] >> sh) & 0xFF) + ((F[(2 * y) * W + 2 * x + 1] >> sh) & 0xFF) + ((F[(2 * y + 1) * W + 2 * x] >> sh) & 0xFF) + ((F[(2 * y + 1) * W + 2 * x + 1] >> sh) & 0xFF);
                o |= ((sum + 2u) / 4u) << sh;
            }
            T[y * TW + x] = o;
        }
        uploadCt32(s, 0x2a00u, 4, 0, 0, TW, TH, T.data());
        auto tex = [&](int x, int y, int c) { x = std::clamp(x, 0, (int)TW - 1); y = std::clamp(y, 0, (int)TH - 1); return (int)((T[y * TW + x] >> (c * 8)) & 0xFF); };
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x)
        {
            const uint32_t f = F[y * W + x]; const int a = (int)(f >> 24); if (!a) continue;
            uint32_t o = f & 0xFF000000u;
            const int x0 = (int)x / 2, y0 = (int)y / 2; const bool hx = x & 1u, hy = y & 1u;
            for (int c = 0; c < 3; ++c)
            {
                double u = tex(x0, y0, c);
                if (hx && hy) u = (tex(x0, y0, c) + tex(x0 + 1, y0, c) + tex(x0, y0 + 1, c) + tex(x0 + 1, y0 + 1, c)) / 4.0;
                else if (hx) u = (tex(x0, y0, c) + tex(x0 + 1, y0, c)) / 2.0;
                else if (hy) u = (tex(x0, y0, c) + tex(x0, y0 + 1, c)) / 2.0;
                const int ur = (int)std::floor(u + 0.5), fc = (int)((f >> (c * 8)) & 0xFF);
                int out = (int)std::floor(fc + (ur - fc) * (double)a / 128.0);
                out = std::clamp(out, 0, 255); o |= (uint32_t)out << (c * 8);
            }
            F[y * W + x] = o;
        }
        uploadCt32(s, fbp, 8, 0, 0, W, H, F.data()); break;
    }
    default: return false;
    }
    return true;
}
static bool dumpVramRawLocked(State &s, const char *binPath, const char *regsPath);
bool dumpVramRaw(const char *binPath, const char *regsPath)
{
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    return dumpVramRawLocked(s, binPath, regsPath);
}
bool dumpVramRawUnderLock(const char *binPath, const char *regsPath) { return dumpVramRawLocked(st(), binPath, regsPath); }   // caller holds the state lock (the swap)
static bool dumpVramRawLocked(State &s, const char *binPath, const char *regsPath)
{
    if (!initLocked(s)) return false;
    s.iface.flush();
    const uint8_t *v = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
    if (!v) return false;
    if (FILE *f = std::fopen(binPath, "wb")) { std::fwrite(v, 1, 4u * 1024u * 1024u, f); std::fclose(f); } else return false;
    if (FILE *f = std::fopen(regsPath, "w"))
    {
        const auto &r = s.iface.get_register_state();
        for (int c = 0; c < 2; ++c)
            std::fprintf(f, "ctx%d frame %016llx zbuf %016llx scissor %016llx xyoffset %016llx tex0 %016llx tex1 %016llx clamp %016llx alpha %016llx test %016llx fba %016llx\n", c + 1,
                         (unsigned long long)r.ctx[c].frame.bits, (unsigned long long)r.ctx[c].zbuf.bits, (unsigned long long)r.ctx[c].scissor.bits, (unsigned long long)r.ctx[c].xyoffset.bits,
                         (unsigned long long)r.ctx[c].tex0.bits, (unsigned long long)r.ctx[c].tex1.bits, (unsigned long long)r.ctx[c].clamp.bits, (unsigned long long)r.ctx[c].alpha.bits,
                         (unsigned long long)r.ctx[c].test.bits, (unsigned long long)r.ctx[c].fba.bits);
        std::fprintf(f, "prim %016llx prmodecont %016llx texa %016llx fogcol %016llx pabe %016llx colclamp %016llx texclut %016llx\n",
                     (unsigned long long)r.prim.bits, (unsigned long long)r.prmodecont.bits, (unsigned long long)r.texa.bits, (unsigned long long)r.fogcol.bits,
                     (unsigned long long)r.pabe.bits, (unsigned long long)r.colclamp.bits, (unsigned long long)r.texclut.bits);
        std::fclose(f);
    }
    return true;
}
static std::mutex g_tdMtx; static std::vector<TargetDumpReq> g_tdReqs;
void requestTargetDump(const TargetDumpReq &r) { std::lock_guard<std::mutex> lk(g_tdMtx); g_tdReqs.push_back(r); }
// [pgswshud] the HUD squeeze + [pgsink]/[pgsfx] packet edits (PS2X_PGS_WSHUD=0 disables): rewrite HUD vertex X before the
// backend (or the native front end) parses the packet. data/size move to the rebuilt packet when quads were split.
bool gifTransfer(uint8_t pathId, const uint8_t *data, size_t size)
{
    if (!data || size < 16 || pathId < 1 || pathId > 3 || t_suppressed) return false;
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (!initLocked(s)) return false;
    const auto t0 = std::chrono::steady_clock::now();
    if (g_packFlushReq.exchange(0)) { s.iface.invalidate_all_cached_textures(); std::fprintf(stderr, "[pgs] texture replacement %s: cached textures dropped\n", g_packOn.load() ? "ON" : "OFF"); }   // [pgslive]
    if (exclusive()) applyPseudoRegsLocked(s, data, size);
    if (!seamvk::on()) ps2x_wshud::apply(data, size);   // [pgswshud] (in seam mode the arbiter already ran it: ps2x_wshud::preprocess)
    s.iface.gif_transfer(pathId - 1u, data, size);
    {   // [targetdump] the oracle image of a target as the backend holds it now
        std::vector<TargetDumpReq> reqs; { std::lock_guard<std::mutex> lk(g_tdMtx); reqs.swap(g_tdReqs); }
        if (!reqs.empty())
        {
            s.iface.flush();
            const uint8_t *v = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
            for (const TargetDumpReq &r : reqs)
            {
                if (!v || !r.dir) continue;
                char path[512]; std::snprintf(path, sizeof(path), "%s/pgs_target_%04x_%ux%u.ppm", r.dir, r.fbp, r.w, r.h);
                char patha[512]; std::snprintf(patha, sizeof(patha), "%s/pgs_target_%04x_%ux%u.pgm", r.dir, r.fbp, r.w, r.h);
                FILE *fp = std::fopen(path, "wb"), *fa = std::fopen(patha, "wb");
                if (fp) std::fprintf(fp, "P6\n%u %u\n255\n", r.w, r.h);
                if (fa) std::fprintf(fa, "P5\n%u %u\n255\n", r.w, r.h);
                for (uint32_t y = 0; y < r.h; ++y) for (uint32_t x = 0; x < r.w; ++x)
                {
                    const uint32_t a = GSPSMCT32::addrPSMCT32(r.fbp, r.fbw, x, y) & ((4u << 20) - 1u);
                    uint32_t px; std::memcpy(&px, v + a, 4);
                    if (fp) { std::fputc(px & 0xFF, fp); std::fputc((px >> 8) & 0xFF, fp); std::fputc((px >> 16) & 0xFF, fp); }
                    if (fa) std::fputc((px >> 24) & 0xFF, fa);
                }
                if (fp) std::fclose(fp); if (fa) std::fclose(fa);
                std::fprintf(stderr, "[targetdump] %s\n", path);
            }
        }
    }
    {   // [regtrace] PS2X_PGS_REGTRACE=1: the backend's own register state after each PATH1 transfer that lands under a tight
        // SCISSOR_1 (the oracle for the native front-end's state at the seam's character packets)
        static const bool s_rt = envOn("PS2X_PGS_REGTRACE"); static unsigned s_rtN = 0;
        if (s_rt && pathId == 1u && s_rtN < 60u)
        {
            const auto &r = s.iface.get_register_state();
            const uint64_t sc1 = r.ctx[0].scissor.bits;
            if ((sc1 & 0x7ff0000ull) >> 16 < 100u)
            {
                ++s_rtN;
                std::fprintf(stderr, "[regtrace] path1 %zu bytes: prim %03llx prmodecont %u sc1 %016llx sc2 %016llx frame1 %016llx frame2 %016llx\n", size,
                             (unsigned long long)(r.prim.bits & 0x7ffu), (unsigned)(r.prmodecont.bits & 1u), (unsigned long long)sc1, (unsigned long long)r.ctx[1].scissor.bits,
                             (unsigned long long)r.ctx[0].frame.bits, (unsigned long long)r.ctx[1].frame.bits);
            }
        }
    }
    {   // [vramprobe] PS2X_PGS_VRAMPROBE=1: after the depth-mask pass, print the frame's alpha per column and the Z top bytes
        static const bool s_probe = envOn("PS2X_PGS_VRAMPROBE"); static unsigned s_n = 0;
        if (s_probe && s_n < 4 && g_pgsProbeReq.exchange(0) != 0)
        {
            s_n++;
            const uint32_t fbp = g_pgsProbeFbp.load(), zbp = g_pgsProbeZbp.load();
            s.iface.flush();
            const uint8_t *v = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
            if (v)
            {
                for (uint32_t y : { 60u, 200u, 300u })
                {
                    std::fprintf(stderr, "[vramprobe] #%u fbp %u y %u alpha:", s_n, fbp, y);
                    for (uint32_t x = 0; x < 24; x++) { uint32_t w; std::memcpy(&w, v + pgsAddr32(fbp, 8, x, y, false), 4); std::fprintf(stderr, " %02x", w >> 24); }
                    std::fprintf(stderr, " | rgb x0..7:");
                    for (uint32_t x = 0; x < 8; x++) { uint32_t w; std::memcpy(&w, v + pgsAddr32(fbp, 8, x, y, false), 4); std::fprintf(stderr, " %06x", w & 0xffffffu); }
                    std::fprintf(stderr, " | rgb x8..15:");
                    for (uint32_t x = 8; x < 16; x++) { uint32_t w; std::memcpy(&w, v + pgsAddr32(fbp, 8, x, y, false), 4); std::fprintf(stderr, " %06x", w & 0xffffffu); }
                    std::fprintf(stderr, "\n[vramprobe] #%u zbp %u y %u z words:", s_n, zbp, y);
                    for (uint32_t x = 0; x < 16; x++) { uint32_t w; std::memcpy(&w, v + pgsAddr32(zbp, 8, x, y, true), 4); std::fprintf(stderr, " %08x", w); }
                    std::fprintf(stderr, "\n");
                }
            }
        }
    }
    const double dtMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    s.xferMs += dtMs;
    if (dtMs > 0.5)
    {   // [pgs-slow] remember the slowest calls with a sketch of their first GIF tag
        if (dtMs > 1.0) s.slowOver1ms++;
        uint64_t lo = 0, hi = 0; std::memcpy(&lo, data, 8); std::memcpy(&hi, data + 8, 8);
        uint32_t firstAD = 0;
        if (((lo >> 58) & 3u) == 0u && (hi & 0xFu) == 0xEu && size >= 32) { uint64_t a; std::memcpy(&a, data + 24, 8); firstAD = uint32_t(a & 0xFFu); }
        State::SlowCall c{dtMs, uint32_t(size), pathId, uint32_t(lo & 0x7FFFu), uint32_t((lo >> 58) & 3u), uint32_t((lo >> 60) & 0xFu), hi, firstAD};
        int worst = 0; for (int i = 1; i < 3; i++) if (s.slowCalls[i].ms < s.slowCalls[worst].ms) worst = i;
        if (c.ms > s.slowCalls[worst].ms) s.slowCalls[worst] = c;
    }
    s.packets++; s.bytes += size;
    return true;
}

void streamFlip(uint64_t dispfb1)
{   // [displatch] job executed by stage 2 in stream order: the frame this DISPFB1 belongs to is complete here
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    s.streamDispfb1 = dispfb1; s.haveStreamFlip = true;
    s.streamFlips++;
}

void streamPriv(uint32_t regOff, uint64_t value)
{   // [s1fence] GsApply job on stage 2: a display register in stream order (see the header)
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    const uint32_t slot = regOff >> 4;
    if (slot >= 0x10u) return;
    s.streamLo[slot] = value; s.streamHave |= 1u << slot;
    if (slot == 7u) { s.streamDispfb1 = value; s.haveStreamFlip = true; }
}

void setInkColor(uint32_t rgb)
{   // [pgsink] the overlay's Ink Color (0xRRGGBB)
    ps2x_wshud::setInkColor(rgb);
}
void setInkWidthPct(int pct)
{   // [pgsink] the overlay's Ink Width (25..100 % of a texel)
    if (pct < 25) pct = 25; if (pct > 100) pct = 100;
    ps2x_wshud::setInkWidthPct(pct);
}
void setForceBilinear(bool on)
{   // overlay toggle (Force Filtering): applies to the next primitive
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    s.hacks.force_bilinear = on;
    if (s.inited) s.iface.set_hacks(s.hacks);
}

void setRegs(GSRegisters *regs)
{
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    s.signals.regs = regs;
}

void privWrite(uint32_t regOff, uint64_t value, GSRegisters *regs)
{
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    s.signals.regs = regs;
    if (regOff < 0x200u) s.privHist[regOff >> 4]++;
    if (regOff < 0x1000u) s.privLo[(regOff >> 4) & 0xFFu] = value;
    else s.privHi[((regOff - 0x1000u) >> 4) & 0xFFu] = value;
}

void onSwap()
{
    if (!enabled()) return;
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (!initLocked(s)) return;
    { const int ws = g_wantScale.load(std::memory_order_relaxed); if (ws > 0 && scaleToSsaa(ws) != s.ssaa && !reinitLocked(s, scaleToSsaa(ws))) return; }   // [pgslive]
    const auto t0 = std::chrono::steady_clock::now();
    static const bool s_noRef = !envOn("PS2X_SEAMVK_REF") && seamvk::on();   // [seamvk] reference backend idle: no flush, no scanout, no readback
    if (!s_noRef) s.iface.flush();
    if (!s_noRef) { const FlushStats fs = s.iface.consume_flush_stats(); s.fsPrims += fs.num_primitives; s.fsPasses += fs.num_render_passes; s.fsCopies += fs.num_copies; s.fsPal += fs.num_palette_updates; }
    copyPrivLocked(s);
    if (seamvk::on())
    {   // [seamvk] the native frame, composed from the same CRTC registers the backend scans out
        const auto &p = s.iface.get_priv_register_state();
        seamvk::PrivRegs pr;
        std::memcpy(&pr.pmode, &p.pmode, 8); std::memcpy(&pr.dispfb1, &p.dispfb1, 8); std::memcpy(&pr.display1, &p.display1, 8);
        std::memcpy(&pr.dispfb2, &p.dispfb2, 8); std::memcpy(&pr.display2, &p.display2, 8); std::memcpy(&pr.bgcolor, &p.bgcolor, 8);
        seamvk::renderFrame(s.device, pr);
        if (s_noRef) s.device.next_frame_context();   // the backend's frame-boundary flush would have advanced it (fences and command buffers recycle per context)
    }
    VSyncInfo info = {};
    info.phase = s.field ^= 1u;
    info.dst_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    info.dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    info.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    info.force_progressive = true;
    info.anti_blur = true;
    info.overscan = false;
    info.crtc_offsets = false;
    static const bool s_adapth = envOn("PS2X_PGS_ADAPTH");
    info.adapt_to_internal_horizontal_resolution = s_adapth;   // default off: scan out at the CRTC width (640) so the present keeps the GL path's aspect
    info.raw_circuit_scanout = true;
    {   // [pgsfit] scanout resolution: PS2X_PGS_HIRES=0|1|2 (1x / 2x / 4x) forces it; unset = AUTO, the smallest shift
        // whose scanout covers the on-screen size of the frame. The present downscales anything larger than the window
        // (2-tap bilinear drops columns at ratios above ~1.5, point sampling drops them at ANY ratio -- that was the
        // "rough edges around characters": a 2560x1792 scanout point-sampled onto 1920x1080). 2x needs SSAA >= 4, 4x needs 16.
        static const int s_hires = [](){ const char *v = std::getenv("PS2X_PGS_HIRES"); return v && v[0] ? std::atoi(v) : -1; }();
        const uint32_t maxShift = s.ssaa >= 16 ? 2u : s.ssaa >= 4 ? 1u : 0u;
        uint32_t shift = 0;
        if (s_hires >= 0) shift = std::min<uint32_t>(uint32_t(s_hires), maxShift);
        else
        {
            uint32_t dw = 0, dh = 0; ps2x_wshud::presentSize(dw, dh);
            const uint32_t bw = s.baseW ? s.baseW : 640u, bh = s.baseH ? s.baseH : 448u;
            while (shift < maxShift && ((bw << shift) < dw || (bh << shift) < dh)) shift++;
        }
        info.high_resolution_scanout = shift >= 1;
        info.high_resolution_scanout_shift = shift >= 2 ? 2u : 1u;
        s.lastShift = shift;
    }
    ScanoutResult res = {};
    if (!s_noRef) res = s.iface.vsync(info);
    if (res.image) { s.baseW = res.image->get_width() >> res.high_resolution_shift; s.baseH = res.image->get_height() >> res.high_resolution_shift; }
    const auto t1 = std::chrono::steady_clock::now();
    static const bool s_noReadback = envOn("PS2X_PGS_NOREADBACK");   // isolation: skip the sync scanout readback (nothing presented)
    if (res.image && !s_noReadback) readbackLocked(s, res); else if (!res.image) s.noImage++;
    const auto t2 = std::chrono::steady_clock::now();
    s.vsyncMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    s.readbackMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
    s.swaps++;
    ps2x_wshud::noteSwap(s.baseH);   // [wshud] the walker's per-frame reset
    const double dt = std::chrono::duration<double>(t2 - s.tStat).count();
    if (dt >= 5.0)
    {
        std::fprintf(stderr, "[pgs] %.1f swaps/s, %.0f packets/s, %.1f MB/s, scanout %ux%u shift %u (no image %llu), flush+vsync %.2f ms/swap, readback %.2f ms/swap | pmode=%llx smode2=%llx dispfb1=%llx display1=%llx dispfb2=%llx display2=%llx\n",
                     s.swaps / dt, s.packets / dt, s.bytes / dt / 1048576.0, s.frameW, s.frameH, s.lastShift, (unsigned long long)s.noImage,
                     s.swaps ? s.vsyncMs / s.swaps : 0.0, s.swaps ? s.readbackMs / s.swaps : 0.0,
                     (unsigned long long)s.privLo[0], (unsigned long long)s.privLo[2], (unsigned long long)s.privLo[7], (unsigned long long)s.privLo[8],
                     (unsigned long long)s.privLo[9], (unsigned long long)s.privLo[10]);
        std::fprintf(stderr, "[pgs] cpu gif_transfer %.1f ms/s (%.2f ms/swap) | priv writes/s:", s.xferMs / dt, s.swaps ? s.xferMs / s.swaps : 0.0);
        for (int k = 0; k < 0x20; k++) if (s.privHist[k]) { std::fprintf(stderr, " %02x=%.0f", k << 4, s.privHist[k] / dt); s.privHist[k] = 0; }
        std::fprintf(stderr, " pseudo=%.0f", s.pseudoSeen / dt); s.pseudoSeen = 0;
        ps2x_wshud::printStats(dt);
        if (packMode()) { std::fprintf(stderr, " | pack: hits %llu misses %llu skipped %llu rtstale %llu gate %llu cached %zu", (unsigned long long)s.replacer.hits, (unsigned long long)s.replacer.misses, (unsigned long long)s.replacer.skipped, (unsigned long long)s.replacer.rtstale, (unsigned long long)s.replacer.gateSkips, s.replacer.cache.size()); s.replacer.hits = s.replacer.misses = s.replacer.skipped = s.replacer.rtstale = s.replacer.gateSkips = 0; }
        {   // per swap: paraLLEl-GS render passes / copies / palette updates / primitives (consume_flush_stats)
            const double sw = s.swaps ? double(s.swaps) : 1.0;
            std::fprintf(stderr, " | per swap: passes %.1f copies %.1f pal %.1f prims %.0f", s.fsPasses / sw, s.fsCopies / sw, s.fsPal / sw, s.fsPrims / sw);
            s.fsPrims = s.fsPasses = s.fsCopies = s.fsPal = 0;
        }
        {   // [pgs-slow]
            std::fprintf(stderr, " | calls>1ms/s %.0f, slowest:", s.slowOver1ms / dt);
            for (int i = 0; i < 3; i++) if (s.slowCalls[i].ms > 0.0)
                std::fprintf(stderr, " [%.2fms path%u %uB nloop=%u flg=%u nreg=%u regs=%llx firstAD=0x%x]", s.slowCalls[i].ms, s.slowCalls[i].path, s.slowCalls[i].size,
                             s.slowCalls[i].nloop, s.slowCalls[i].flg, s.slowCalls[i].nreg, (unsigned long long)s.slowCalls[i].regs, s.slowCalls[i].firstAD);
            for (auto &c : s.slowCalls) c = State::SlowCall{}; s.slowOver1ms = 0;
        }
        std::fprintf(stderr, " | flip: stream calls/s %.0f, scanout fb1=%llx live1=%llx live2=%llx, stream!=live at %.0f%% of swaps",
                     s.streamFlips / dt, (unsigned long long)s.lastFb1, (unsigned long long)s.lastLive1, (unsigned long long)s.lastLive2,
                     s.swaps ? 100.0 * s.flipMismatch / s.swaps : 0.0);
        s.streamFlips = 0; s.flipMismatch = 0;
        if (s.timestamps)
        {
            static const char *const names[int(TimestampType::Count)] = { "SyncHostToVRAM", "CopyVRAM", "PaletteUpdate", "TextureUpload", "TriangleSetup", "Binning", "Shading", "Readback", "VSync" };
            static double last[int(TimestampType::Count)] = {};
            double total = 0.0;
            std::fprintf(stderr, " | gpu ms/s:");
            for (int t = 0; t < int(TimestampType::Count); t++)
            {
                const double acc = s.iface.get_accumulated_timestamps(TimestampType(t)) * 1e3;
                const double ms = (acc - last[t]) / dt; last[t] = acc; total += ms;
                std::fprintf(stderr, " %s=%.1f", names[t], ms);
            }
            std::fprintf(stderr, " total=%.1f (%.2f ms/swap)", total, s.swaps ? total * dt / s.swaps : 0.0);
        }
        std::fprintf(stderr, "\n");
        s.swaps = s.packets = s.bytes = 0; s.noImage = 0; s.vsyncMs = s.readbackMs = s.xferMs = 0.0; s.tStat = t2;
    }
}

void setPresentSize(uint32_t w, uint32_t h)
{   // [pgsfit]
    ps2x_wshud::setPresentSize(w, h);
}

// [seamvk] GS thread: the context registers as they stand at this point of the stream.

bool takeRefFrame(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h)
{
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (!s.frameFresh) return false;
    rgba = s.frame; w = s.frameW; h = s.frameH; s.frameFresh = false;
    return true;
}
uint64_t lastFrameGframe()
{   // [presentlat]
    if (seamvk::on()) return seamvk::lastFrameGframe();
    return st().frameGframe;
}

bool takeFrame(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h)
{
    if (seamvk::on()) return seamvk::takeFrame(rgba, w, h);   // [seamvk] the native view replaces the scanout
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (!s.frameFresh) return false;
    rgba.swap(s.frame); s.frame.clear();
    w = s.frameW; h = s.frameH; s.frameFresh = false;
    return true;
}

void shutdown()
{
    seamvk::shutdown();   // [earlyframe]
    State &s = st();
    std::lock_guard<std::mutex> lk(s.mtx);
    if (s.inited) s.device.wait_idle();
}
}

// [seampack] the BC decoder for the native renderer's texture-pack uploads (global scope: seamgs links to this name)
bool ps2xBcDecode(int fmt, const std::vector<uint8_t> &src, int w, int h, std::vector<uint8_t> &rgba) { return ps2x_pgs::bcDecode(fmt, src, w, h, rgba); }
