// [seamvk] GS front-end of the native renderer: see seamvk/seamgs_internal.h and ps2_seamvk.h.
//
// Runs on the GS thread, called by the GIF arbiter for every packet in stream order. Keeps the
// GS register state (both contexts), a 4 MB VRAM mirror for host->local uploads and local copies,
// decodes palettes and textures from the mirror into RGBA8 (cached by TEX0 + the write stamps of
// the VRAM pages they read), and assembles vertex kicks into a per-frame draw list the renderer
// consumes at the swap. Everything the GS does with VRAM that this mirror cannot see (pixels the
// game DRAWS and then reads back as a texture) is out of scope here and is recorded as such.
#include "seamvk/seamgs_internal.h"
#include "runtime/ps2_gs_psmct32.h"
#include "runtime/ps2_gs_psmct16.h"
#include "runtime/ps2_gs_psmt8.h"
#include "runtime/ps2_gs_psmt4.h"
#include "runtime/ps2_gs_gpu.h"          // [seampack] GSTex0Reg / GSTexaReg / GSTexClutReg
#include "runtime/ps2_gs_rasterizer.h"   // [seampack] GSRasterizer::fillClutForBackend (the pack hashes the CLUT in this layout)
#include "runtime/ps2_texreplace.h"      // [seampack] ps2tex::identify / loadReplacement
#include "runtime/ps2_gs_pgs.h"          // [seampack] ps2xBcDecode
#include <unordered_set>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <atomic>
extern std::atomic<uint64_t> g_bt3FrameCount;   // [xferlog] the game frame
#include "runtime/ps2_gs_gpu_renderer.h"   // [nativehud] GsGpuRenderer::outlineEnabled / inkStrengthPct / shadowsEnabled (the overlay's toggles)
extern std::atomic<int> g_wsHudLayout;                          // [nativehud] overlay: 0 centered, 1 edge-pinned, 2 custom (-1 = unset)
extern std::atomic<int> g_wsHudOffLQ, g_wsHudOffCQ, g_wsHudOffRQ;   // custom offsets x16
bool ps2FightUpdateRecent();                                    // ps2_stepcensus.cpp: the fight update ran within the last 2 render frames (menus: never)
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
namespace
{   // [crashbt] PS2X_CRASHBT=1: a glibc backtrace on SIGSEGV / SIGABRT / SIGBUS to stderr before the default action (addr2line -e ps2EntryRunner +off)
    void crashBt(int sig)
    {
        void *bt[64]; const int n = backtrace(bt, 64);
        const char *m = sig == SIGSEGV ? "[crashbt] SIGSEGV\n" : sig == SIGABRT ? "[crashbt] SIGABRT\n" : "[crashbt] signal\n";
        (void)!write(2, m, std::strlen(m));
        backtrace_symbols_fd(bt, n, 2);
        signal(sig, SIG_DFL); raise(sig);
    }
    const bool g_crashBtInstalled = [](){ const char *v = std::getenv("PS2X_CRASHBT"); if (!(v && v[0] && v[0] != '0')) return false; signal(SIGSEGV, crashBt); signal(SIGABRT, crashBt); signal(SIGBUS, crashBt); return true; }();
}
namespace seamgs
{
    namespace
    {
        // ---- VRAM mirror -------------------------------------------------------------------
        constexpr uint32_t kVramBytes = 4u * 1024u * 1024u;
        constexpr uint32_t kVramMask = kVramBytes - 1u;
        constexpr uint32_t kPages = 512;
        uint8_t g_vram[kVramBytes];
        uint32_t g_pageWrite[kPages];   // stamp of the last mirror write touching the page
        uint32_t g_pageUploaded[kPages];   // [gpudecode] write stamp of the page as last snapshotted for the GPU VRAM copy
        uint64_t g_pageHash[kPages];       // [gpudecode] content hash of the page as last uploaded (the game restreams identical texture bytes every frame)
        uint32_t g_pageDrawn[kPages];   // stamp at the last DRAW into the page (pixels the mirror never sees)
        uint32_t g_stamp = 1;           // bumped per transfer
        // PS2X_SEAMGS_WATCH=<lo>-<hi> (hex blocks): log the image transfers and scratch-raster writes that land in that block range
        struct Watch { uint32_t lo = 0, hi = 0; uint32_t logged = 0; };
        Watch g_watch = [](){ Watch w; const char *v = std::getenv("PS2X_SEAMGS_WATCH"); if (v && v[0]) { w.lo = (uint32_t)std::strtoul(v, nullptr, 16); if (const char *d = std::strchr(v, '-')) w.hi = (uint32_t)std::strtoul(d + 1, nullptr, 16); } return w; }();
        uint64_t g_frame = 0;

        enum : uint32_t { PSMCT32 = 0, PSMCT24 = 1, PSMCT16 = 2, PSMCT16S = 10, PSMT8 = 19, PSMT4 = 20, PSMT8H = 27, PSMT4HL = 36, PSMT4HH = 44,
                          PSMZ32 = 48, PSMZ24 = 49, PSMZ16 = 50, PSMZ16S = 58 };

        // [swztab] per-page swizzle offset tables: a GS address is page base + block-aligned base + one table lookup. Built from
        // the reference address functions and verified against them at start-up (fallback to them on any mismatch).
        struct Swz
        {
            uint16_t ct32[64 * 32], t8[128 * 64], t4[128 * 128], c16[64 * 64], c16s[64 * 64], z16[64 * 64], z16s[64 * 64]; bool ok = false, pair32 = false;
            Swz()
            {
                for (uint32_t y = 0; y < 32; ++y) for (uint32_t x = 0; x < 64; ++x) ct32[y * 64 + x] = (uint16_t)GSPSMCT32::addrPSMCT32(0, 1, x, y);
                for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 128; ++x) t8[y * 128 + x] = (uint16_t)GSPSMT8::addrPSMT8(0, 1, x, y);
                for (uint32_t y = 0; y < 128; ++y) for (uint32_t x = 0; x < 128; ++x) t4[y * 128 + x] = (uint16_t)GSPSMT4::addrPSMT4(0, 1, x, y);
                for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x)
                {
                    c16[y * 64 + x] = (uint16_t)GSPSMCT16::addrPSMCT16(0, 1, x, y); c16s[y * 64 + x] = (uint16_t)GSPSMCT16::addrPSMCT16S(0, 1, x, y);
                    z16[y * 64 + x] = (uint16_t)GSPSMCT16::addrPSMZ16(0, 1, x, y); z16s[y * 64 + x] = (uint16_t)GSPSMCT16::addrPSMZ16S(0, 1, x, y);
                }
                ok = true;
                static const uint32_t bws[] = { 1, 2, 4, 8, 10, 16 }, blocks[] = { 0, 1, 16, 31, 32, 0x2c10, 0x3e8c };
                for (uint32_t bw : bws) for (uint32_t b : blocks) for (uint32_t y = 0; y < 200 && ok; y += 3) for (uint32_t x = 0; x < 300 && ok; x += 7)
                {
                    if (a32(b, bw, x, y) != (GSPSMCT32::addrPSMCT32(b, bw, x, y) & kVramMask)) ok = false;
                    if (a8(b, bw, x, y) != (GSPSMT8::addrPSMT8(b, bw, x, y) & kVramMask)) ok = false;
                    if (a4(b, bw, x, y) != (GSPSMT4::addrPSMT4(b, bw, x, y) & (kVramMask * 2u + 1u))) ok = false;
                    if (a16(PSMCT16, b, bw, x, y) != (GSPSMCT16::addrPSMCT16(b, bw, x, y) & kVramMask)) ok = false;
                    if (a16(PSMCT16S, b, bw, x, y) != (GSPSMCT16::addrPSMCT16S(b, bw, x, y) & kVramMask)) ok = false;
                    if (a16(PSMZ16, b, bw, x, y) != (GSPSMCT16::addrPSMZ16(b, bw, x, y) & kVramMask)) ok = false;
                    if (a16(PSMZ16S, b, bw, x, y) != (GSPSMCT16::addrPSMZ16S(b, bw, x, y) & kVramMask)) ok = false;
                }
                if (!ok) std::fprintf(stderr, "[seamgs] swizzle tables disagree with the reference address functions: using the slow path\n");
                pair32 = ok;   // CT32: pixels (2k, 2k+1) of a row are adjacent words -> one 8-byte store per pair in imageData
                for (uint32_t y = 0; y < 32 && pair32; ++y) for (uint32_t x = 0; x < 64; x += 2) if (ct32[y * 64 + x + 1] != ct32[y * 64 + x] + 4u) { pair32 = false; break; }
            }
            inline uint32_t a32(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) const { const uint32_t ppr = bw ? bw : 1u; return ((((block >> 5) + (y >> 5) * ppr + (x >> 6)) << 13) + (block & 31u) * 256u + ct32[(y & 31u) * 64u + (x & 63u)]) & kVramMask; }
            inline uint32_t a8(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) const { const uint32_t ppr = (bw >> 1) ? (bw >> 1) : 1u; return ((((block >> 5) + (y >> 6) * ppr + (x >> 7)) << 13) + (block & 31u) * 256u + t8[(y & 63u) * 128u + (x & 127u)]) & kVramMask; }
            inline uint32_t a4(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) const { const uint32_t ppr = (bw >> 1) ? (bw >> 1) : 1u; return ((((block >> 5) + (y >> 7) * ppr + (x >> 7)) << 14) + (block & 31u) * 512u + t4[(y & 127u) * 128u + (x & 127u)]) & (kVramMask * 2u + 1u); }
            inline uint32_t a16(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y) const
            {
                const uint32_t ppr = bw ? bw : 1u; const uint16_t *t = psm == PSMCT16S ? c16s : psm == PSMZ16 ? z16 : psm == PSMZ16S ? z16s : c16;
                return ((((block >> 5) + (y >> 6) * ppr + (x >> 6)) << 13) + (block & 31u) * 256u + t[(y & 63u) * 64u + (x & 63u)]) & kVramMask;
            }
        };
        const Swz g_swz;
        inline uint32_t addr32(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) { return g_swz.ok ? g_swz.a32(block, bw, x, y) : (GSPSMCT32::addrPSMCT32(block, bw, x, y) & kVramMask); }
        inline uint32_t addr16(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y)
        {
            if (g_swz.ok) return g_swz.a16(psm, block, bw, x, y);
            switch (psm)
            {
            case PSMCT16S: return GSPSMCT16::addrPSMCT16S(block, bw, x, y) & kVramMask;
            case PSMZ16:   return GSPSMCT16::addrPSMZ16(block, bw, x, y) & kVramMask;
            case PSMZ16S:  return GSPSMCT16::addrPSMZ16S(block, bw, x, y) & kVramMask;
            default:       return GSPSMCT16::addrPSMCT16(block, bw, x, y) & kVramMask;
            }
        }
        inline uint32_t addr8(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) { return g_swz.ok ? g_swz.a8(block, bw, x, y) : (GSPSMT8::addrPSMT8(block, bw, x, y) & kVramMask); }
        inline uint32_t addr4(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) { return g_swz.ok ? g_swz.a4(block, bw, x, y) : (GSPSMT4::addrPSMT4(block, bw, x, y) & (kVramMask * 2u + 1u)); }   // nibble address

        // ---- [nativehud] The widescreen HUD layout (edge-pinned / centered / custom) and the overlay's outline, ink and shadow
        // toggles, applied to the front end's own 2D draws instead of paraLLEl-GS's packet rewrite (ps2_gs_pgs.cpp [pgswshud],
        // whose classification, cut points, scissor rule, scene gate and squeeze debounce are reproduced here). Sprites and
        // flat UV triangles in the top band of a scene buffer get their X mapped piecewise; a primitive crossing a cut point is
        // split there so its texture stays straight (the packet path subdivided the game's quads the same way).
        // PS2X_SEAMGS_NATIVEHUD=0 restores the packet walker.
        const bool g_nativeHud = [](){ const char *v = std::getenv("PS2X_SEAMGS_NATIVEHUD"); return !(v && v[0] == '0'); }();
        struct WsNative { bool active = false, frameHad3d = false, on = false; int no3dRun = 0; float inv = 1.0f, lastRawInv = 0.0f; int invRun = 0; bool invInit = false; uint32_t sceneFbw = 8; uint64_t mapped = 0, split = 0, scissors = 0, inkDropped = 0, inkScaled = 0, shadowDropped = 0; } g_ws;
        bool g_emitSprite = false;   // the triangle being emitted is half of a sprite
        float wsMapX(float x, float W, float inv)
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
        inline float wsW(uint32_t fbw) { return (fbw * 64u >= 320u && fbw * 64u <= 1024u) ? float(fbw * 64u) : 512.0f; }
        inline bool wsScene(uint32_t fbp, uint32_t fpsm) { return (fbp == 0u || fbp == 0xe00u) && (fpsm == 0u || fpsm == 1u); }
        void wsFrameEnd()
        {   // at the swap: the scene gate (5-frame hysteresis), the debounced squeeze, the fight gate
            if (g_ws.frameHad3d) { g_ws.active = true; g_ws.no3dRun = 0; } else if (++g_ws.no3dRun >= 5) g_ws.active = false;
            g_ws.frameHad3d = false;
            const float raw = ps2x_pgs::wsHudRawInv(g_ws.sceneFbw);
            if (raw < 0.999f)
            {   // [wsjit] commit a raw value only once it repeats (menus alternate 512/640-wide frames)
                if (!g_ws.invInit) { g_ws.inv = raw; g_ws.lastRawInv = raw; g_ws.invRun = 1; g_ws.invInit = true; }
                else if (raw == g_ws.lastRawInv) { if (++g_ws.invRun >= 2) g_ws.inv = raw; }
                else { g_ws.lastRawInv = raw; g_ws.invRun = 1; }
            }
            else g_ws.inv = 1.0f;
            g_ws.on = g_nativeHud && g_ws.active && g_ws.inv < 0.999f && ::ps2FightUpdateRecent();
        }
        inline uint32_t pageOf(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y);
        // ---- [lazyvram] Host->local transfers above a few KB are kept as their linear bytes (one memcpy) and swizzled into the
        // mirror only when a page of theirs is READ (a CPU decode, a palette, the scratch raster, the GPU page snapshot, a
        // dump, or a later write to the page). Textures identified by their upload content ([texcontent]) never need the
        // mirror at all, so the streamed HUD / glyph textures (2 MB a frame) cost a copy and a hash instead of a per-pixel
        // swizzle plus a page hash plus a GPU page upload plus a decode. PS2X_SEAMGS_LAZYVRAM=0 restores the eager write.
        struct XferRec { uint32_t dbp = 0, dbw = 0, dpsm = 0, dsax = 0, dsay = 0, rrw = 0, rrh = 0, stamp = 0, refs = 0; uint64_t hash = 1469598103934665603ull; std::vector<uint8_t> data; };
        std::unordered_map<uint64_t, XferRec> g_xlog; uint64_t g_xlogNext = 1;
        std::vector<uint32_t> g_pend[kPages];   // transfers pending on the page (log ids, stream order)
        int64_t g_pageCover[kPages] = {};       // [texcontent] the transfer whose bytes ARE the page (covered it fully, nothing touched it since), 0 = none
        uint64_t g_lazyApplied = 0, g_lazyCaptured = 0, g_lazyBytes = 0;
        const bool g_lazyVram = [](){ const char *v = std::getenv("PS2X_SEAMGS_LAZYVRAM"); return !(v && v[0] == '0'); }();
        inline XferRec *xrec(uint64_t idx) { auto it = g_xlog.find(idx); return it == g_xlog.end() ? nullptr : &it->second; }
        inline void xrecRelease(uint64_t idx) { auto it = g_xlog.find(idx); if (it == g_xlog.end()) return; if (it->second.refs) --it->second.refs; if (it->second.refs == 0u) g_xlog.erase(it); }
        inline void coverClear(uint32_t p) { if (g_pageCover[p]) { xrecRelease((uint64_t)g_pageCover[p]); g_pageCover[p] = 0; } }
        inline void pageDims(uint32_t psm, uint32_t &pw, uint32_t &ph)
        {
            switch (psm) { case PSMT8: pw = 128; ph = 64; break; case PSMT4: pw = 128; ph = 128; break; case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: pw = 64; ph = 64; break; default: pw = 64; ph = 32; break; }
        }
        // the page's origin in the buffer's pixel space (standard layout: pages left to right in rows of bw*64 pixels)
        inline bool pageOrigin(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t p, uint32_t &ox, uint32_t &oy)
        {
            uint32_t pw, ph; pageDims(psm, pw, ph);
            const uint32_t ppr = std::max(1u, bw * 64u / pw);
            if (p < (bp >> 5)) return false;
            const uint32_t rel = p - (bp >> 5); ox = (rel % ppr) * pw; oy = (rel / ppr) * ph; return true;
        }
        template <class F> void forRectPages(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, F &&f)
        {   // every page a rect spans (page corners + the rect's last row/column), each once
            if (w == 0u || h == 0u) return;
            uint32_t pw, ph; pageDims(psm, pw, ph);
            uint32_t seen[64]; uint32_t ns = 0;
            for (uint32_t y = y0;; y += ph)
            {
                const uint32_t yy = std::min(y, y0 + h - 1u);
                for (uint32_t x = x0;; x += pw)
                {
                    const uint32_t xx = std::min(x, x0 + w - 1u);
                    const uint32_t pg = pageOf(psm, bp, bw, xx & 0x7FFu, yy & 0x7FFu);
                    bool dup = false; for (uint32_t i = 0; i < ns; ++i) if (seen[i] == pg) { dup = true; break; }
                    if (!dup && pg < kPages) { if (ns < 64u) seen[ns++] = pg; f(pg); }
                    if (xx == x0 + w - 1u) break;
                }
                if (yy == y0 + h - 1u) break;
            }
        }
        inline void wr32raw(uint32_t a, uint32_t v) { std::memcpy(g_vram + a, &v, 4); }
        inline void wr16raw(uint32_t a, uint16_t v) { std::memcpy(g_vram + a, &v, 2); }
        inline void writePixelRaw(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y, uint32_t v)
        {   // the mirror write of a transfer being applied late: no stamp (stamped when captured), no pending check
            switch (psm)
            {
            case PSMCT32: case PSMZ32: wr32raw(addr32(block, bw, x, y), v); break;
            case PSMCT24: case PSMZ24: { const uint32_t a = addr32(block, bw, x, y); uint32_t o; std::memcpy(&o, g_vram + a, 4); wr32raw(a, (o & 0xFF000000u) | (v & 0xFFFFFFu)); break; }
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: wr16raw(addr16(psm, block, bw, x, y), (uint16_t)v); break;
            case PSMT8: g_vram[addr8(block, bw, x, y)] = (uint8_t)v; break;
            case PSMT4: { const uint32_t a = addr4(block, bw, x, y); const uint32_t sh = (a & 1u) * 4u; g_vram[a >> 1] = (uint8_t)((g_vram[a >> 1] & ~(0xFu << sh)) | ((v & 0xFu) << sh)); break; }
            case PSMT8H: { const uint32_t a = addr32(block, bw, x, y); uint32_t o; std::memcpy(&o, g_vram + a, 4); wr32raw(a, (o & 0x00FFFFFFu) | (v << 24)); break; }
            case PSMT4HL: { const uint32_t a = addr32(block, bw, x, y); uint32_t o; std::memcpy(&o, g_vram + a, 4); wr32raw(a, (o & 0xF0FFFFFFu) | ((v & 0xFu) << 24)); break; }
            case PSMT4HH: { const uint32_t a = addr32(block, bw, x, y); uint32_t o; std::memcpy(&o, g_vram + a, 4); wr32raw(a, (o & 0x0FFFFFFFu) | ((v & 0xFu) << 28)); break; }
            default: wr32raw(addr32(block, bw, x, y), v); break;
            }
        }
        void applyPage(const XferRec &r, uint32_t p)
        {   // the part of the transfer that lands in page p, from its linear bytes
            uint32_t ox, oy, pw, ph; if (!pageOrigin(r.dpsm, r.dbp, r.dbw, p, ox, oy)) return; pageDims(r.dpsm, pw, ph);
            const uint32_t x0 = std::max(ox, r.dsax), x1 = std::min(ox + pw, r.dsax + r.rrw), y0 = std::max(oy, r.dsay), y1 = std::min(oy + ph, r.dsay + r.rrh);
            if (x1 <= x0 || y1 <= y0) return;
            const uint8_t *d = r.data.data(); const size_t n = r.data.size();
            for (uint32_t y = y0; y < y1; ++y)
                for (uint32_t x = x0; x < x1; ++x)
                {
                    const size_t i = size_t(y - r.dsay) * r.rrw + (x - r.dsax); uint32_t v;
                    switch (r.dpsm)
                    {
                    case PSMCT24: case PSMZ24: if (i * 3u + 3u > n) continue; v = (uint32_t)d[i * 3u] | ((uint32_t)d[i * 3u + 1u] << 8) | ((uint32_t)d[i * 3u + 2u] << 16); break;
                    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: { if (i * 2u + 2u > n) continue; uint16_t t; std::memcpy(&t, d + i * 2u, 2); v = t; break; }
                    case PSMT8: case PSMT8H: if (i >= n) continue; v = d[i]; break;
                    case PSMT4: case PSMT4HL: case PSMT4HH: if ((i >> 1) >= n) continue; v = (d[i >> 1] >> ((i & 1u) * 4u)) & 0xFu; break;
                    default: if (i * 4u + 4u > n) continue; std::memcpy(&v, d + i * 4u, 4); break;
                    }
                    writePixelRaw(r.dpsm, r.dbp, r.dbw, x & 0x7FFu, y & 0x7FFu, v);
                }
        }
        void materialise(uint32_t p)
        {
            std::vector<uint32_t> lst; lst.swap(g_pend[p]);
            for (uint32_t idx : lst) { if (const XferRec *r = xrec(idx)) applyPage(*r, p); xrecRelease(idx); ++g_lazyApplied; }
        }
        inline void touch(uint32_t a) { const uint32_t p = a >> 13; if (p < kPages && !g_pend[p].empty()) materialise(p); }
        inline void touchPage(uint32_t p) { if (p < kPages && !g_pend[p].empty()) materialise(p); }
        inline void touchWrite(uint32_t a) { const uint32_t p = a >> 13; if (p < kPages) { if (!g_pend[p].empty()) materialise(p); coverClear(p); } }

        inline uint32_t rd32(uint32_t a) { touch(a); uint32_t v; std::memcpy(&v, g_vram + a, 4); return v; }
        inline void wr32(uint32_t a, uint32_t v) { touchWrite(a); std::memcpy(g_vram + a, &v, 4); g_pageWrite[a >> 13] = g_stamp; }
        inline uint16_t rd16(uint32_t a) { touch(a); uint16_t v; std::memcpy(&v, g_vram + a, 2); return v; }
        inline void wr16(uint32_t a, uint16_t v) { touchWrite(a); std::memcpy(g_vram + a, &v, 2); g_pageWrite[a >> 13] = g_stamp; }
        inline void wr8(uint32_t a, uint8_t v) { touchWrite(a); g_vram[a] = v; g_pageWrite[a >> 13] = g_stamp; }
        inline uint8_t rd8(uint32_t a) { touch(a); return g_vram[a]; }

        // Read one texel/pixel as a raw 32-bit value in the format's natural width.
        inline uint32_t readPixel(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y)
        {
            switch (psm)
            {
            case PSMCT32: case PSMZ32: return rd32(addr32(block, bw, x, y));
            case PSMCT24: case PSMZ24: return rd32(addr32(block, bw, x, y)) & 0xFFFFFFu;
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return rd16(addr16(psm, block, bw, x, y));
            case PSMT8: return rd8(addr8(block, bw, x, y));
            case PSMT4: { const uint32_t a = addr4(block, bw, x, y); return (rd8(a >> 1) >> ((a & 1u) * 4u)) & 0xFu; }
            case PSMT8H: return rd32(addr32(block, bw, x, y)) >> 24;
            case PSMT4HL: return (rd32(addr32(block, bw, x, y)) >> 24) & 0xFu;
            case PSMT4HH: return rd32(addr32(block, bw, x, y)) >> 28;
            default: return rd32(addr32(block, bw, x, y));
            }
        }
        inline void writePixel(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y, uint32_t v)
        {
            switch (psm)
            {
            case PSMCT32: case PSMZ32: wr32(addr32(block, bw, x, y), v); break;
            case PSMCT24: case PSMZ24: { const uint32_t a = addr32(block, bw, x, y); wr32(a, (rd32(a) & 0xFF000000u) | (v & 0xFFFFFFu)); break; }
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: wr16(addr16(psm, block, bw, x, y), (uint16_t)v); break;
            case PSMT8: wr8(addr8(block, bw, x, y), (uint8_t)v); break;
            case PSMT4: { const uint32_t a = addr4(block, bw, x, y); const uint32_t sh = (a & 1u) * 4u;
                          wr8(a >> 1, (uint8_t)((rd8(a >> 1) & ~(0xFu << sh)) | ((v & 0xFu) << sh))); break; }
            case PSMT8H: { const uint32_t a = addr32(block, bw, x, y); wr32(a, (rd32(a) & 0x00FFFFFFu) | (v << 24)); break; }
            case PSMT4HL: { const uint32_t a = addr32(block, bw, x, y); wr32(a, (rd32(a) & 0xF0FFFFFFu) | ((v & 0xFu) << 24)); break; }
            case PSMT4HH: { const uint32_t a = addr32(block, bw, x, y); wr32(a, (rd32(a) & 0x0FFFFFFFu) | ((v & 0xFu) << 28)); break; }
            default: wr32(addr32(block, bw, x, y), v); break;
            }
        }
        inline uint32_t pageOf(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y)
        {
            switch (psm)
            {
            case PSMT8: return addr8(block, bw, x, y) >> 13;
            case PSMT4: return addr4(block, bw, x, y) >> 14;
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return addr16(psm, block, bw, x, y) >> 13;
            default: return addr32(block, bw, x, y) >> 13;
            }
        }

        // ---- register state ----------------------------------------------------------------
        struct Ctx
        {
            uint64_t tex0 = 0, tex1 = 0, clamp = 0, xyoffset = 0, scissor = 0, alpha = 0, test = 0, frame = 0, zbuf = 0, fba = 0, miptbp1 = 0, miptbp2 = 0;
        };
        struct Regs
        {
            Ctx ctx[2];
            uint64_t prim = 0, prmode = 0, texa = 0, fogcol = 0, texclut = 0, pabe = 0, colclamp = 1, dthe = 0, dimx = 0;
            bool prmodecont = true;
            // vertex registers
            uint8_t r = 0x80, g = 0x80, b = 0x80, a = 0x80; float q = 1.0f, s = 0.0f, t = 0.0f; uint16_t u = 0, v = 0; uint8_t fog = 0;
            // transfer
            uint64_t bitbltbuf = 0, trxpos = 0, trxreg = 0; uint32_t trxdir = 3;
        };
        Regs g_r;

        struct PathParse { uint32_t nloop = 0, nreg = 1, ri = 0, flg = 0, total = 0; uint64_t regs = 0; bool eop = false; bool vtxOnly = false; };   // vtxOnly: [hostskip] a host packet's vertex-only tag, skipped as a whole
        PathParse g_path[4];

        struct Xfer
        {
            bool active = false;
            uint32_t dbp = 0, dbw = 0, dpsm = 0, dsax = 0, dsay = 0, rrw = 0, rrh = 0, x = 0, y = 0;
            uint8_t carry[4]; uint32_t carryN = 0;   // CT24: pixels straddle packet boundaries
                    uint32_t bytes = 0, repeatBytes = 0;   // [xferlog]
            bool lazy = false; uint64_t rec = 0;   // [lazyvram] captured, not written
        };
        Xfer g_xfer;

        // ---- vertex queue ------------------------------------------------------------------
        struct GsVert { uint32_t x, y, z; uint8_t r, g, b, a; float q, s, t; uint16_t u, v; uint8_t fog; };
        GsVert g_vq[3]; uint32_t g_vn = 0;

        // ---- frame list --------------------------------------------------------------------
        std::mutex g_mtx;
        FrameList g_list;
        bool g_haveHost = false; seamvk::DrawPacket g_host; std::vector<uint8_t> g_hostVerts;
        // The mesh is drawn once per DISTINCT draw state the seam's packets kick it with: the two-pass character program
        // kicks the same geometry twice (an outline pass in context 1, which the game hides under a 64x64 SCISSOR_1 while
        // its palette target is live, then the lit pass in context 2), while a stage chunk's clipper fans and strip all
        // share one state and must draw it once.
        bool g_hostDrawn = false; State g_hostLast; uint8_t g_hostPassN = 0;
        bool sameDrawState(const State &a, const State &b)
        {
            return a.fbp == b.fbp && a.fbw == b.fbw && a.fpsm == b.fpsm && a.fbmsk == b.fbmsk && a.zbp == b.zbp && a.zpsm == b.zpsm && a.zmsk == b.zmsk
                && a.ate == b.ate && a.atst == b.atst && a.aref == b.aref && a.afail == b.afail && a.date == b.date && a.datm == b.datm && a.zte == b.zte && a.ztst == b.ztst
                && a.abe == b.abe && a.aA == b.aA && a.aB == b.aB && a.aC == b.aC && a.aD == b.aD && a.fix == b.fix && a.pabe == b.pabe && a.fba == b.fba && a.colclamp == b.colclamp
                && a.prim == b.prim && a.iip == b.iip && a.tme == b.tme && a.fge == b.fge && a.fst == b.fst && a.ctxt == b.ctxt
                && a.tfx == b.tfx && a.tcc == b.tcc && a.wms == b.wms && a.wmt == b.wmt && a.tex0lo == b.tex0lo && a.tex0hi == b.tex0hi
                && a.scax0 == b.scax0 && a.scax1 == b.scax1 && a.scay0 == b.scay0 && a.scay1 == b.scay1 && a.ofx == b.ofx && a.ofy == b.ofy && a.fogcol == b.fogcol;
        }
        uint8_t g_curPath = 0;
        bool g_inHostGif = false;

        // [rtdecode] what the GPU targets hold: per FRAME base the row width / format / rows drawn and a draw stamp;
        // per ZBUF base the rows written. Textures whose pages the game drew are decoded from these on the GPU.
        struct TargetInfo { uint32_t fbw = 8, psm = 0, rows = 0, drawStamp = 0; };
        std::map<uint32_t, TargetInfo> g_targets, g_zbufs;
        uint32_t g_drawStamp = 1;

        // ---- texture cache -----------------------------------------------------------------
        struct TexEntry
        {
            uint64_t key = 0; uint32_t w = 0, h = 0; uint64_t lastUse = 0; bool used = false; uint32_t decodeId = 0;   // decodeId: serial of the decode that filled this entry
            std::vector<uint8_t> rgbaCopy;                      // kept for textures the CPU scratch raster may sample (<= 256x256)
            std::vector<std::pair<uint16_t, uint32_t>> pages;   // (page, write stamp seen)
            bool drawnPages = false;                            // texture pages the game had drawn into (stale mirror)
            bool clutDrawn = false;                             // palette pages the game had drawn into
            bool indexed = false; uint32_t clut[256] = {};      // the palette as decoded from the mirror
            uint16_t firstPage = 0xFFFFu;
            bool rtBased = false; uint32_t rtFbp = 0, rtStamp = 0;   // decoded from a target: valid until the target is drawn again
            bool gpuDecode = false;                              // decoded on the GPU from the VRAM copy (no CPU decode, no rgba)
        };
        std::vector<TexEntry> g_tex;
        std::vector<int32_t> g_texFree;
        std::unordered_map<uint64_t, int32_t> g_texByKey;
        bool g_forceCpuDecode = false;   // [gpudecode] cpuTargetOk: decode this one on the CPU regardless of the threshold
        bool g_texDirty = true; int32_t g_curTex = -1; uint32_t g_curTexW = 0, g_curTexH = 0;
        uint64_t g_kickCount = 0, g_hostSkipChecks = 0, g_hostSkipMismatch = 0;   // [hostskip] verify mode (PS2X_SEAMGS_HOSTSKIP=2)
        const int g_hostSkipMode = [](){ const char *v = std::getenv("PS2X_SEAMGS_HOSTSKIP"); return v && v[0] ? std::atoi(v) : 1; }();   // 0 off, 1 on, 2 verify (both paths, compare)
        // [fxpkt] PS2X_SEAMVK_PKTPROGS=<mask>: host programs drawn from their host-transformed packet vertices (the VU1 program's exact
        // output incl. its clipper's fans) instead of the GPU vertex program. Default: the effects program (bit 1) -- its shadow
        // patch (a 12-vertex ground quad crossing the near plane, DATE + generated texcoords) renders blocky through the vertex
        // program while the packet path is pixel-exact; effects are ~2 batches per frame, so nothing is lost.
        const uint32_t g_pktProgs = [](){ const char *v = std::getenv("PS2X_SEAMVK_PKTPROGS"); return v && v[0] ? (uint32_t)std::strtoul(v, nullptr, 0) : 0x2u; }();
        uint64_t g_streamGameFrame = 0, g_streamFightTick = 0;   // [fighttick] the last flip stamp seen in stream order
        int g_nativeParse = -1;   // [clutpass] the generic native step whose packet is being parsed (its kicks are neutralised: recorded, not drawn)
        uint32_t g_lastSceneFbp = 0;   // [ztopnative] the scene buffer (0x0 / 0xe00) the game last set as FRAME: the native step 2's source
        uint64_t g_texLookups = 0, g_texDecodes = 0, g_texStale = 0, g_cpuSprites = 0, g_cpuTris = 0, g_rtDecodes = 0, g_gpuDecodes = 0;
        uint64_t g_pagesSame = 0;
        uint64_t g_swapHash = 1469598103934665603ull, g_lastSwapHash = 0, g_swapBytes = 0, g_sameSwaps = 0, g_swapsSeen = 0;   // [swaphash] is a swap's packet stream identical to the previous one?
        double g_msParse = 0, g_msDecode = 0, g_msRaster = 0, g_msHost = 0;
        double g_msState = 0, g_msTri = 0, g_msImage = 0, g_msVramDec = 0, g_msReg = 0;   // [parseprof] sub-timers of the parse
        bool profOn() { static const bool s = [](){ const char *v = std::getenv("PS2X_SEAMGS_PROF"); return v && v[0] && v[0] != '0'; }(); return s; }   // the sub-timers cost ~10% of the parse thread in clock reads
        struct ScopeMs { double &acc; bool on = profOn(); std::chrono::steady_clock::time_point t = on ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point(); ~ScopeMs() { if (on) acc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); } };
        uint32_t g_busyFrames = 0;   // frames so far with > 1500 draws (a fight): PS2X_SEAMVK_TEXDUMP_FROM=1 starts there
        std::vector<int32_t> g_retired;   // slots freed by the renderer after this frame; reusable from the next

        void retireSlot(int32_t slot)
        {
            TexEntry &e = g_tex[slot];
            e.used = false; e.pages.clear(); e.key = 0;
            g_list.texFrees.push_back(slot);
            g_retired.push_back(slot);
        }

        uint64_t fnv(const void *p, size_t n, uint64_t h = 1469598103934665603ull)
        {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
            return h;
        }

        // CSM1 palette: 256 RGBA entries (raw alpha bytes), TEXA applied to 16-bit palettes.
        void readClut(uint32_t cbp, uint32_t cpsm, uint32_t csa, uint64_t texa, uint32_t *out, uint32_t *pagesBits)
        {
            const uint32_t ta0 = (uint32_t)(texa & 0xFFu), aem = (uint32_t)((texa >> 15) & 1u), ta1 = (uint32_t)((texa >> 32) & 0xFFu);
            (void)csa;
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t x, y, v;
                if (cpsm == PSMCT32 || cpsm == PSMCT24)
                {
                    x = (i & 7u) | ((i & 0x10u) >> 1); y = ((i & 8u) >> 3) | ((i & 0xE0u) >> 4);
                    const uint32_t a = addr32(cbp, 1, x, y); v = rd32(a); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u);
                    if (cpsm == PSMCT24) { v &= 0xFFFFFFu; v |= ((aem && v == 0u) ? 0u : ta0) << 24; }
                }
                else
                {
                    x = i & 15u; y = i >> 4;
                    const uint32_t a = addr16(cpsm == PSMCT16S ? PSMCT16S : PSMCT16, cbp, 1, x, y); const uint32_t c = rd16(a); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u);
                    const uint32_t r = (c & 0x1Fu) << 3, g = ((c >> 5) & 0x1Fu) << 3, b = ((c >> 10) & 0x1Fu) << 3;
                    uint32_t al = (c & 0x8000u) ? ta1 : ((aem && (c & 0x7FFFu) == 0u) ? 0u : ta0);
                    v = r | (g << 8) | (b << 16) | (al << 24);
                }
                out[i] = v;
            }
        }

        // Decode the texture TEX0 describes into RGBA8 (raw alpha bytes: 0x80 = 1.0).
        bool decodeTextureImpl(uint64_t tex0, uint64_t texa, TexEntry &e, std::vector<uint8_t> &rgba);
        bool decodeTexture(uint64_t tex0, uint64_t texa, TexEntry &e, std::vector<uint8_t> &rgba)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = decodeTextureImpl(tex0, texa, e, rgba);
            g_msDecode += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return ok;
        }
        bool decodeTextureImpl(uint64_t tex0, uint64_t texa, TexEntry &e, std::vector<uint8_t> &rgba)
        {
            const uint32_t tbp = (uint32_t)(tex0 & 0x3FFFu), tbw = (uint32_t)((tex0 >> 14) & 0x3Fu), psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            const uint32_t tw = (uint32_t)((tex0 >> 26) & 0xFu), th = (uint32_t)((tex0 >> 30) & 0xFu);
            const uint32_t cbp = (uint32_t)((tex0 >> 37) & 0x3FFFu), cpsm = (uint32_t)((tex0 >> 51) & 0xFu), csa = (uint32_t)((tex0 >> 56) & 0x1Fu);
            const uint32_t w = 1u << std::min(tw, 10u), h = 1u << std::min(th, 10u);
            const uint32_t ta0 = (uint32_t)(texa & 0xFFu), aem = (uint32_t)((texa >> 15) & 1u), ta1 = (uint32_t)((texa >> 32) & 0xFFu);
            e.w = w; e.h = h;
            // Pages the game drew into: decoded from the target on the GPU instead; only the page sets and the
            // palette are needed here (a 1024x1024 CPU decode per frame is what made the fight crawl).
            bool anyDrawn = false;
            {
                const uint32_t p0 = pageOf(psm, tbp, tbw, 0, 0), p1 = pageOf(psm, tbp, tbw, w - 1u, h - 1u);
                for (uint32_t p = std::min(p0, p1); p <= std::max(p0, p1) && p < kPages; ++p) if (g_pageDrawn[p] >= g_pageWrite[p] && g_pageDrawn[p] != 0u) { anyDrawn = true; break; }
            }
            // [gpudecode] PS2X_SEAMVK_GPUDECODE_MIN=<texels> (default 65536; 0 = every texture, -1 = none): textures at least
            // this big are not decoded here; the renderer decodes them from its GPU copy of VRAM (rtdecode.frag FROM_VRAM),
            // this side only lists the pages (snapshotted when their write stamp moved) and reads the palette.
            static const long long s_gpuMin = [](){ const char *v = std::getenv("PS2X_SEAMVK_GPUDECODE_MIN"); return v && v[0] ? std::atoll(v) : 0ll; }();   // default 0 (every texture on the GPU) since 2026-09-27: the page listing is block-exact and hoisted decodes own their images
            const bool gpu = !anyDrawn && !g_forceCpuDecode && s_gpuMin >= 0 && (long long)w * h >= s_gpuMin;
            rgba.resize((anyDrawn || gpu) ? 0u : size_t(w) * h * 4u);
            uint32_t *dst = (anyDrawn || gpu) ? nullptr : reinterpret_cast<uint32_t *>(rgba.data());
            uint32_t pagesBits[16] = {}, clutBits[16] = {};
            const bool indexed = psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH;
            uint32_t clut[256];
            if (indexed) readClut(cbp, cpsm, csa, texa, clut, clutBits);
            e.indexed = indexed;
            if (indexed) std::memcpy(e.clut, clut, sizeof(clut));
            e.gpuDecode = gpu;
            if (gpu)
            {   // the pages the texture rect spans, sampled per block (block size by format: 8x8, 16x8, 16x16, 32x16): exact
                // even for a block-aligned base whose blocks wrap into the next page (white boots) -- and no page the
                // texture does not touch (page-corner sampling + "next page" listed a drawn HUD page for the opponent's
                // portrait, which then went through the feedback skip and vanished; 2026-09-27)
                const uint32_t bw_ = (psm == PSMT8) ? 16u : (psm == PSMT4) ? 32u : (psm == PSMCT16 || psm == PSMCT16S || psm == PSMZ16 || psm == PSMZ16S) ? 16u : 8u;
                const uint32_t bh_ = (psm == PSMT8 || psm == PSMT4) ? 16u : 8u;
                for (uint32_t y = 0; y < h; y += bh_)
                    for (uint32_t x = 0; x < w; x += bw_)
                    {
                        const uint32_t p = pageOf(psm, tbp, tbw, x, y);
                        if (p < kPages) pagesBits[p >> 5] |= 1u << (p & 31u);
                    }
            }
            const uint32_t csaOff = (psm == PSMT4 || psm == PSMT4HL || psm == PSMT4HH) ? (csa & 15u) * 16u : 0u;
            const uint32_t step = anyDrawn ? 8u : 1u;   // page bookkeeping only: pages are >= 32 px, a stride of 8 touches them all
            for (uint32_t y = 0; y < (gpu ? 0u : h); y += step)   // (GPU-decoded textures: pages listed above, nothing to read here)
            {
                for (uint32_t x = 0; x < w; x += step)
                {
                    uint32_t v;
                    switch (psm)
                    {
                    case PSMT8: { const uint32_t a = addr8(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[rd8(a)]; break; }
                    case PSMT4: { const uint32_t a = addr4(tbp, tbw, x, y); const uint32_t pg = a >> 14; pagesBits[pg >> 5] |= 1u << (pg & 31u);
                                  v = clut[(csaOff + ((rd8(a >> 1) >> ((a & 1u) * 4u)) & 0xFu)) & 255u]; break; }
                    case PSMT8H: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[rd32(a) >> 24]; break; }
                    case PSMT4HL: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[(csaOff + ((rd32(a) >> 24) & 0xFu)) & 255u]; break; }
                    case PSMT4HH: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[(csaOff + (rd32(a) >> 28)) & 255u]; break; }
                    case PSMCT24: case PSMZ24: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); const uint32_t c = rd32(a) & 0xFFFFFFu; v = c | (((aem && c == 0u) ? 0u : ta0) << 24); break; }
                    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S:
                    {
                        const uint32_t a = addr16(psm, tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u);
                        const uint32_t c = rd16(a);
                        const uint32_t r = (c & 0x1Fu) << 3, g = ((c >> 5) & 0x1Fu) << 3, b = ((c >> 10) & 0x1Fu) << 3;
                        const uint32_t al = (c & 0x8000u) ? ta1 : ((aem && (c & 0x7FFFu) == 0u) ? 0u : ta0);
                        v = r | (g << 8) | (b << 16) | (al << 24); break;
                    }
                    default: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = rd32(a); break; }
                    }
                    if (dst) dst[size_t(y) * w + x] = v;
                }
            }
            {   // [clut3c00] stage palettes: what the mirror holds for CLUTs in the rendered-palette area
                static int s_dbg = 0;
                if (indexed && cbp >= 0x3c00u && cbp < 0x3e00u && s_dbg < 8)
                {
                    ++s_dbg;
                    const uint32_t pg = addr32(cbp, 1, 0, 0) >> 13;
                    std::fprintf(stderr, "[clut3c00] frame %llu tex psm %u %ux%u tbp 0x%x cbp 0x%x: clut page 0x%x write %u drawn %u stamp %u; entries %08x %08x %08x %08x\n",
                                 (unsigned long long)g_frame, psm, w, h, tbp, cbp, pg, g_pageWrite[pg], g_pageDrawn[pg], g_stamp, clut[0], clut[1], clut[2], clut[3]);
                }
            }
            e.pages.clear(); e.drawnPages = false; e.clutDrawn = false; e.firstPage = 0xFFFFu;
            for (uint32_t p = 0; p < kPages; ++p)
                if (clutBits[p >> 5] & (1u << (p & 31u)))
                {
                    e.pages.emplace_back((uint16_t)p, g_pageWrite[p]);
                    if (g_pageDrawn[p] >= g_pageWrite[p] && g_pageDrawn[p] != 0u) e.clutDrawn = true;
                }
            for (uint32_t p = 0; p < kPages; ++p)
                if (pagesBits[p >> 5] & (1u << (p & 31u)))
                {
                    if (e.firstPage == 0xFFFFu) e.firstPage = (uint16_t)p;
                    e.pages.emplace_back((uint16_t)p, g_pageWrite[p]);
                    if (g_pageDrawn[p] >= g_pageWrite[p] && g_pageDrawn[p] != 0u)
                    {
                        static int s_dbg = 0;
                        if (!e.drawnPages && s_dbg < 10) { ++s_dbg; std::fprintf(stderr, "[drawnpage] frame %llu tex psm %u %ux%u tbp 0x%x cbp 0x%x: page 0x%x drawn %u write %u\n", (unsigned long long)g_frame, psm, w, h, tbp, cbp, p, g_pageDrawn[p], g_pageWrite[p]); }
                        e.drawnPages = true;
                    }
                }
            return true;
        }

        // The target whose drawn region holds page p (the most recently drawn one when several do).
        const std::map<uint32_t, TargetInfo>::value_type *targetForPage(const std::map<uint32_t, TargetInfo> &m, uint32_t p)
        {
            const std::map<uint32_t, TargetInfo>::value_type *best = nullptr;
            for (const auto &kv : m)
            {
                const uint32_t base = kv.first / 32u;
                if (p < base || kv.second.rows == 0u) continue;
                const uint32_t rel = p - base;
                if (rel >= kv.second.rows * kv.second.fbw) continue;
                if (!best || kv.second.drawStamp > best->second.drawStamp) best = &kv;
            }
            return best;
        }
        // [gpudecode] decode on the GPU from its VRAM copy: snapshot the texture's pages whose bytes changed since the copy
        // last saw them (in stream order, so a page rewritten between two decodes gets both versions), pass the palette.
        void vramDecodeFor(int32_t slot, TexEntry &e, uint64_t tex0)
        {
            ScopeMs _sm{g_msVramDec};
            RtDecode r;
            r.slot = slot; r.w = e.w; r.h = e.h; r.fromVram = 1;
            r.tbp = (uint32_t)(tex0 & 0x3FFFu); r.tbw = (uint32_t)((tex0 >> 14) & 0x3Fu); r.psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            r.cbp = (uint32_t)((tex0 >> 37) & 0x3FFFu); r.cpsm = (uint32_t)((tex0 >> 51) & 0xFu); r.csa = (uint32_t)((tex0 >> 56) & 0x1Fu);
            r.texa = g_r.texa;
            if (e.indexed) std::memcpy(r.clut, e.clut, sizeof(r.clut));
            r.upFirst = (uint32_t)g_list.vramPages.size();
            for (const auto &pg : e.pages)
            {
                const uint32_t p = pg.first;
                if (g_pageUploaded[p] == g_pageWrite[p] && g_pageWrite[p] != 0u) continue;
                g_pageUploaded[p] = g_pageWrite[p] ? g_pageWrite[p] : 1u;
                touchPage(p);   // [lazyvram]
                static const bool s_hash = [](){ const char *v = std::getenv("PS2X_SEAMGS_PAGEHASH"); return !(v && v[0] == '0'); }();   // =0: upload every changed page without hashing it first
                if (s_hash)
                {   // same bytes as the GPU copy already holds (restreamed texture): nothing to upload
                    const uint64_t *q = reinterpret_cast<const uint64_t *>(g_vram + size_t(p) * 8192u);
                    uint64_t h0 = 1469598103934665603ull, h1 = 0x9E3779B97F4A7C15ull, h2 = 0xC2B2AE3D27D4EB4Full, h3 = 0x165667B19E3779F9ull;   // 4 lanes: no serial multiply chain
                    for (uint32_t i = 0; i < 1024u; i += 4) { h0 = (h0 ^ q[i]) * 1099511628211ull; h1 = (h1 ^ q[i + 1]) * 1099511628211ull; h2 = (h2 ^ q[i + 2]) * 1099511628211ull; h3 = (h3 ^ q[i + 3]) * 1099511628211ull; }
                    const uint64_t h = ((h0 ^ (h1 >> 7)) * 1099511628211ull) ^ ((h2 ^ (h3 << 9)) * 0x9E3779B97F4A7C15ull);
                    if (h == g_pageHash[p]) { ++g_pagesSame; continue; }
                    g_pageHash[p] = h;
                }
                g_list.vramPages.push_back((uint16_t)p);
                const size_t off = g_list.vramBytes.size(); g_list.vramBytes.resize(off + 8192u);
                std::memcpy(g_list.vramBytes.data() + off, g_vram + size_t(p) * 8192u, 8192u);
            }
            r.upCount = (uint32_t)g_list.vramPages.size() - r.upFirst;
            Draw d; d.kind = 2; d.rt = (int32_t)g_list.rtDecodes.size(); d.st.tex = slot;
            g_list.rtDecodes.push_back(r);
            g_list.draws.push_back(d);
            ++g_gpuDecodes;
        }
        bool rtDecodeFor(int32_t slot, TexEntry &e, uint64_t tex0)
        {
            static const bool s_noRt = [](){ const char *v = std::getenv("PS2X_SEAMVK_NORT"); return v && v[0] && v[0] != '0'; }();
            if (s_noRt) return false;
            const uint32_t psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            const auto *ct = targetForPage(g_targets, e.firstPage);
            const auto *zt = targetForPage(g_zbufs, e.firstPage);
            if (!ct && !zt) return false;
            RtDecode r;
            r.slot = slot; r.w = e.w; r.h = e.h;
            r.tbp = (uint32_t)(tex0 & 0x3FFFu); r.tbw = (uint32_t)((tex0 >> 14) & 0x3Fu); r.psm = psm;
            r.cbp = (uint32_t)((tex0 >> 37) & 0x3FFFu); r.cpsm = (uint32_t)((tex0 >> 51) & 0xFu); r.csa = (uint32_t)((tex0 >> 56) & 0x1Fu);
            r.texa = g_r.texa;
            const bool zRead = psm >= 48u;   // PSMZ*: the Z bits come from the depth image
            if (ct && (!zt || (!zRead && ct->second.drawStamp >= zt->second.drawStamp) || (zRead && zt->first != ct->first)))
            {
                r.srcFbp = ct->first; r.srcFbw = ct->second.fbw; r.srcRows = ct->second.rows;
                if (zt && zt->first == ct->first) r.depthSrc = 1;   // colour target over the Z buffer: byte 3 from it, Z24 from depth
                e.rtFbp = ct->first; e.rtStamp = ct->second.drawStamp;
            }
            else
            {
                r.srcFbp = zt->first; r.srcFbw = zt->second.fbw; r.srcRows = zt->second.rows; r.depthSrc = 1;
                e.rtFbp = zt->first; e.rtStamp = zt->second.drawStamp;
                if (!g_targets.count(zt->first)) return false;   // needs the colour view of that base for the top byte
            }
            if (e.indexed)
            {
                std::memcpy(r.clut, e.clut, sizeof(r.clut));
                if (e.clutDrawn)
                {
                    const uint32_t cpage = addr32(r.cbp, 1, 0, 0) >> 13;
                    const auto *cct = targetForPage(g_targets, cpage);
                    if (cct && cct->first == r.srcFbp && r.cpsm == PSMCT32) r.clutFromTarget = 1;
                }
            }
            e.rtBased = true;
            Draw d; d.kind = 2; d.rt = (int32_t)g_list.rtDecodes.size(); d.st.tex = slot;
            g_list.rtDecodes.push_back(r);
            g_list.draws.push_back(d);
            ++g_rtDecodes;
            return true;
        }

        struct ScratchMemo { uint64_t hash = 0; uint32_t p0 = 0, p1 = 0; }; ScratchMemo g_scratchMemo[128]; uint64_t g_scratchSkips = 0; uint32_t g_scratchSeqIdx = 0; bool g_scratchSeqOk = true;
        uint32_t g_pageScratch[kPages] = {};   // [scratchrepeat] write stamp of the last scratch raster into each page
        uint64_t g_memoHits = 0, g_stateBuilds = 0, g_imgBytes = 0, g_imgRepeat = 0, g_imgByPsm[64] = {}; std::unordered_map<uint64_t, uint64_t> g_imgHash;
        int32_t resolveTextureImpl(const Ctx &c);
        // [texcontent] textures identified by the bytes the game uploaded: a texture whose pages are each exactly one pending
        // transfer's bytes (page fully covered, untouched since) gets a content key from those transfers' hashes, its palette
        // and its TEX0 format; the second time a key is seen the texture is decoded once on the CPU and its image shared for
        // the session, so every later re-stream of the same glyph / HUD piece binds the image and touches neither the mirror
        // nor the GPU VRAM copy. PS2X_SEAMGS_TEXCONTENT=0 disables.
        const bool g_texContent = [](){ const char *v = std::getenv("PS2X_SEAMGS_TEXCONTENT"); return !(v && v[0] == '0'); }();
        struct ContentImg { uint32_t w, h; uint64_t share; uint64_t lastUse; };
        std::unordered_map<uint64_t, ContentImg> g_contentImgs; std::unordered_set<uint64_t> g_contentSeen;
        uint64_t g_contentHits = 0, g_contentRegs = 0, g_contentMiss = 0, g_contentNoKey = 0;
        bool contentKeyFor(uint64_t tex0, uint64_t texa, bool needTexa, uint64_t &key, std::vector<std::pair<uint16_t, uint32_t>> &pages, uint32_t *clut, bool &indexed)
        {
            const uint32_t tbp = (uint32_t)(tex0 & 0x3FFFu), tbw = (uint32_t)((tex0 >> 14) & 0x3Fu), psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            const uint32_t tw = (uint32_t)((tex0 >> 26) & 0xFu), th = (uint32_t)((tex0 >> 30) & 0xFu);
            const uint32_t w = 1u << std::min(tw, 10u), h = 1u << std::min(th, 10u);
            uint64_t k = fnv(&tex0, 8);
            if (needTexa) k = fnv(&texa, 8, k);
            bool ok = true; pages.clear();
            forRectPages(psm, tbp, tbw, 0u, 0u, w, h, [&](uint32_t p) {
                if (!ok) return;
                if (g_pageDrawn[p] != 0u && g_pageDrawn[p] >= g_pageWrite[p]) { ok = false; return; }
                pages.emplace_back((uint16_t)p, g_pageWrite[p]);
                const int64_t cv = g_pageCover[p]; const XferRec *r = cv ? xrec((uint64_t)cv) : nullptr;
                if (!r) { ok = false; return; }
                uint32_t ox, oy; if (!pageOrigin(r->dpsm, r->dbp, r->dbw, p, ox, oy)) { ok = false; return; }
                const uint32_t mix[4] = { ox - r->dsax, oy - r->dsay, r->rrw, r->dpsm };
                k = fnv(&r->hash, 8, k); k = fnv(mix, sizeof(mix), k);
            });
            if (!ok || pages.empty()) return false;
            indexed = psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH;
            if (indexed)
            {
                uint32_t bits[16] = {};
                readClut((uint32_t)((tex0 >> 37) & 0x3FFFu), (uint32_t)((tex0 >> 51) & 0xFu), (uint32_t)((tex0 >> 56) & 0x1Fu), texa, clut, bits);
                for (uint32_t i = 0; i < 16; ++i) if (bits[i]) for (uint32_t b = 0; b < 32; ++b) if (bits[i] & (1u << b)) { const uint32_t p = i * 32u + b; if (p < kPages && g_pageDrawn[p] != 0u && g_pageDrawn[p] >= g_pageWrite[p]) return false; }   // a rendered palette is not upload content
                k = fnv(clut, 1024, k);
            }
            key = k | 1ull;
            return true;
        }
        struct ResolveMemo { uint64_t tex0 = 0, texa = 0, frame = ~0ull; uint32_t stamp = 0; int32_t slot = -1; };
        ResolveMemo g_resolveMemo[256];   // [resolvememo] direct-mapped by TEX0: a repeat lookup with no VRAM write since (same stamp, same frame) is O(1)
        bool seamPackReplace(int32_t slot, TexEntry &e, uint64_t tex0, uint64_t texa);   // [seampack] below
        int32_t resolveTexture(const Ctx &c)
        {
            ++g_texLookups;
            const uint64_t tex0 = c.tex0;
            ResolveMemo &memo = g_resolveMemo[(uint32_t)((tex0 * 0x9E3779B97F4A7C15ull) >> 56)];
            if (memo.slot >= 0 && memo.tex0 == tex0 && memo.texa == g_r.texa && memo.stamp == g_stamp && memo.frame == g_frame && (size_t)memo.slot < g_tex.size() && g_tex[memo.slot].used && !g_tex[memo.slot].rtBased)
            {
                ++g_memoHits; TexEntry &e = g_tex[memo.slot]; e.lastUse = g_frame; g_curTexW = e.w; g_curTexH = e.h; return memo.slot;
            }
            const int32_t slotOut = resolveTextureImpl(c);
            memo.tex0 = tex0; memo.texa = g_r.texa; memo.stamp = g_stamp; memo.frame = g_frame; memo.slot = slotOut;
            return slotOut;
        }
        int32_t resolveTextureImpl(const Ctx &c)
        {
            const uint64_t tex0 = c.tex0;
            const uint32_t psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            const bool needTexa = psm == PSMCT16 || psm == PSMCT16S || psm == PSMCT24 || psm == PSMZ16 || psm == PSMZ16S || psm == PSMZ24
                               || ((psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH) && (((tex0 >> 51) & 0xFu) != PSMCT32));
            uint64_t key = fnv(&tex0, 8);
            if (needTexa) key = fnv(&g_r.texa, 8, key);
            auto it = g_texByKey.find(key);
            // PS2X_SEAMGS_TEXTRACE=<cbp hex>: every lookup of a texture with that palette base, with the cache decision
            static const uint32_t s_traceCbp = [](){ const char *v = std::getenv("PS2X_SEAMGS_TEXTRACE"); return v && v[0] ? (uint32_t)std::strtoul(v, nullptr, 16) : 0xFFFFFFFFu; }();
            const bool trace = ((uint32_t)((tex0 >> 37) & 0x3FFFu) == s_traceCbp) && g_watch.logged < 400u;
            if (it != g_texByKey.end())
            {
                TexEntry &e = g_tex[it->second];
                bool valid = true;
                for (const auto &pg : e.pages) if (g_pageWrite[pg.first] != pg.second) { valid = false; break; }
                if (valid && e.rtBased) { auto ti = g_targets.find(e.rtFbp); if (ti == g_targets.end() || ti->second.drawStamp != e.rtStamp) valid = false; }
                if (trace)
                {
                    ++g_watch.logged;
                    std::fprintf(stderr, "[textrace] frame %llu draw %zu tex0 %016llx: cached slot %d %s, %zu pages:", (unsigned long long)g_frame, g_list.draws.size(), (unsigned long long)tex0, it->second, valid ? "VALID" : "stale", e.pages.size());
                    for (const auto &pg : e.pages) std::fprintf(stderr, " p%x(%u/%u)", pg.first, pg.second, g_pageWrite[pg.first]);
                    const uint32_t a0 = addr32((uint32_t)((tex0 >> 37) & 0x3FFFu), 1, 0, 0); std::fprintf(stderr, " | mirror clut[0..1] %08x %08x\n", rd32(a0), rd32(a0 + 4));
                }
                if (valid) { e.lastUse = g_frame; g_curTexW = e.w; g_curTexH = e.h; return it->second; }
                // Stale (the game uploaded over it): retire this slot -- draws already recorded this frame keep
                // it, the renderer frees it after the frame -- and decode into a fresh one.
                retireSlot(it->second);
                g_texByKey.erase(it);
            }
            int32_t slot;
            if (!g_texFree.empty()) { slot = g_texFree.back(); g_texFree.pop_back(); }
            else { slot = (int32_t)g_tex.size(); g_tex.emplace_back(); }
            TexEntry &e = g_tex[slot];
            e = TexEntry(); e.key = key; e.used = true; e.lastUse = g_frame;
            std::vector<uint8_t> rgba;
            uint64_t ckey = 0; bool haveKey = false, contentSeen = false;
            if (g_texContent && g_lazyVram)
            {   // [texcontent]
                std::vector<std::pair<uint16_t, uint32_t>> ckPages; uint32_t ckClut[256]; bool ckIndexed = false;
                haveKey = contentKeyFor(tex0, g_r.texa, needTexa, ckey, ckPages, ckClut, ckIndexed);
                if (!haveKey) ++g_contentNoKey;
                else
                {
                    auto ci = g_contentImgs.find(ckey);
                    if (ci != g_contentImgs.end())
                    {   // the image is on the GPU already: bind it, nothing decoded, no mirror, no page upload
                        ci->second.lastUse = g_frame;
                        e.w = ci->second.w; e.h = ci->second.h; e.pages = std::move(ckPages); e.indexed = ckIndexed; if (ckIndexed) std::memcpy(e.clut, ckClut, sizeof(e.clut));
                        e.gpuDecode = false; e.rgbaCopy.clear();
                        ++g_texDecodes; e.decodeId = (uint32_t)g_texDecodes + (uint32_t)(g_frame << 12);
                        g_list.texUploads.push_back(TexUpload{ slot, e.w, e.h, {}, ci->second.share });
                        g_texByKey[key] = slot;
                        ++g_contentHits;
                        g_curTexW = e.w; g_curTexH = e.h;
                        return slot;
                    }
                    contentSeen = g_contentSeen.count(ckey) != 0u;
                    if (contentSeen) g_forceCpuDecode = true;   // recurring content: decode once on the CPU, share for the session
                    else { g_contentSeen.insert(ckey); ++g_contentMiss; }
                }
            }
            decodeTexture(tex0, g_r.texa, e, rgba);
            g_forceCpuDecode = false;
            ++g_texDecodes; e.decodeId = (uint32_t)g_texDecodes + (uint32_t)(g_frame << 12);
            if (e.drawnPages) ++g_texStale;
            if (e.w * e.h <= 256u * 256u && !rgba.empty()) e.rgbaCopy = rgba; else e.rgbaCopy.clear();
            {   // PS2X_SEAMVK_TEXDUMP=<dir>: every first decode as a PPM (rgb) + PGM (alpha), named by its TEX0 fields
                static const char *s_dir = std::getenv("PS2X_SEAMVK_TEXDUMP"); static int s_n = 0;
                static const uint64_t s_from = [](){ const char *v = std::getenv("PS2X_SEAMVK_TEXDUMP_FROM"); return v && v[0] ? (uint64_t)std::atoll(v) : 0ull; }();
                if (s_dir && s_dir[0] && s_n < 400 && !rgba.empty() && (s_from == 1u ? g_busyFrames > 0u : g_frame >= s_from))   // (GPU-decoded textures have no CPU pixels)
                {
                    char path[512];
                    std::snprintf(path, sizeof(path), "%s/t%03d_f%llu_psm%u_%ux%u_tbp%x_tbw%u_cbp%x_cpsm%u_csa%u%s.ppm", s_dir, s_n, (unsigned long long)g_frame, psm, e.w, e.h,
                                  (unsigned)(tex0 & 0x3FFFu), (unsigned)((tex0 >> 14) & 0x3Fu), (unsigned)((tex0 >> 37) & 0x3FFFu), (unsigned)((tex0 >> 51) & 0xFu), (unsigned)((tex0 >> 56) & 0x1Fu), e.drawnPages ? "_DRAWN" : "");
                    if (FILE *f = std::fopen(path, "wb"))
                    {
                        std::fprintf(f, "P6\n%u %u\n255\n", e.w, e.h);
                        for (size_t i = 0; i < size_t(e.w) * e.h; ++i) std::fwrite(rgba.data() + i * 4u, 1, 3, f);
                        std::fclose(f);
                    }
                    std::snprintf(path + std::strlen(path) - 4, 5, ".pgm");
                    if (FILE *f = std::fopen(path, "wb"))
                    {
                        std::fprintf(f, "P5\n%u %u\n255\n", e.w, e.h);
                        for (size_t i = 0; i < size_t(e.w) * e.h; ++i) std::fputc(rgba[i * 4u + 3u], f);
                        std::fclose(f);
                    }
                    ++s_n;
                }
            }
            g_texByKey[key] = slot;
            if (trace)
            {
                ++g_watch.logged; std::fprintf(stderr, "[textrace] frame %llu draw %zu tex0 %016llx: DECODED into slot %d, %zu pages, drawn %u clutDrawn %u; clut[0..1] %08x %08x\n", (unsigned long long)g_frame, g_list.draws.size(), (unsigned long long)tex0, slot, e.pages.size(), e.drawnPages ? 1 : 0, e.clutDrawn ? 1 : 0, e.clut[0], e.clut[1]);
                static int s_n = 0; const char *dir = std::getenv("PS2X_SEAMVK_TEXDUMP");
                if (dir && s_n < 12 && !rgba.empty())
                {   // the decoded texels of a traced texture, rgb + alpha
                    ++s_n; char path[512];
                    std::snprintf(path, sizeof(path), "%s/trace%02d_f%llu_%016llx.ppm", dir, s_n, (unsigned long long)g_frame, (unsigned long long)tex0);
                    if (FILE *fp = std::fopen(path, "wb")) { std::fprintf(fp, "P6\n%u %u\n255\n", e.w, e.h); for (size_t i = 0; i < size_t(e.w) * e.h; ++i) std::fwrite(rgba.data() + i * 4u, 1, 3, fp); std::fclose(fp); }
                    std::snprintf(path, sizeof(path), "%s/trace%02d_f%llu_%016llx_a.pgm", dir, s_n, (unsigned long long)g_frame, (unsigned long long)tex0);
                    if (FILE *fp = std::fopen(path, "wb")) { std::fprintf(fp, "P5\n%u %u\n255\n", e.w, e.h); for (size_t i = 0; i < size_t(e.w) * e.h; ++i) std::fputc(rgba[i * 4u + 3u], fp); std::fclose(fp); }
                }
            }
            if (seamPackReplace(slot, e, tex0, g_r.texa))
            {   // [seampack] the pack's image is on its way to the slot
            }
            else if (e.drawnPages && rtDecodeFor(slot, e, tex0))
            {   // decoded from the target on the GPU, in stream order: no mirror upload
                e.drawnPages = false;
            }
            else if (e.gpuDecode) vramDecodeFor(slot, e, tex0);   // [gpudecode] decoded on the GPU from the VRAM copy, in stream order
            else if (!rgba.empty() && haveKey && contentSeen && !e.drawnPages && !e.clutDrawn)
            {   // [texcontent] second sighting of this content: the image is shared for the session under its content key
                const uint64_t share = ckey;
                g_contentImgs[ckey] = ContentImg{ e.w, e.h, share, g_frame };
                ++g_contentRegs;
                if (g_contentImgs.size() > 3000u)
                {   // evict the least recently bound quarter (the renderer drops its images next frame)
                    std::vector<std::pair<uint64_t, uint64_t>> byUse; byUse.reserve(g_contentImgs.size());
                    for (const auto &kv : g_contentImgs) byUse.emplace_back(kv.second.lastUse, kv.first);
                    std::sort(byUse.begin(), byUse.end());
                    for (size_t i = 0; i < byUse.size() / 4u; ++i) { g_list.sharedDrops.push_back(g_contentImgs[byUse[i].second].share); g_contentImgs.erase(byUse[i].second); }
                }
                g_list.texUploads.push_back(TexUpload{ slot, e.w, e.h, std::move(rgba), share });
            }
            else if (!rgba.empty()) g_list.texUploads.push_back(TexUpload{ slot, e.w, e.h, std::move(rgba) });
            g_curTexW = e.w; g_curTexH = e.h;
            return slot;
        }

        // [seampack] PCSX2-format texture packs on the native renderer: at a texture's first resolve, hash it exactly as the
        // paraLLEl-GS / OpenGL paths do (TEX0Hash over the swizzled VRAM blocks, CLUTHash over the CLUT in the backend layout)
        // and, when the pack has it, upload the replacement image to the slot instead of decoding the game's texels. The
        // slot image is then a multiple of the GS texture size and the draw shader samples it through [rtnative]'s factor.
        // Paletted formats only (what identify() hashes); textures on drawn pages / rendered palettes are never replaced.
        // PS2X_SEAMVK_PACK=0 disables. Same [gatealpha] rule as the other paths: a binary-alpha palette is a destination-
        // alpha gate asset -- an uncompressed replacement gets its alpha snapped, a compressed one keeps the game's texels.
        bool seamPackOn() { static const bool s = [](){ const char *v = std::getenv("PS2X_SEAMVK_PACK"); return !(v && v[0] == '0'); }(); return s; }
        std::unordered_set<std::string> g_packMiss; uint64_t g_packHits = 0, g_packMisses = 0, g_packSkipped = 0, g_packLoads = 0;
        struct PackLoaded { uint32_t w, h; uint64_t share; }; std::unordered_map<std::string, PackLoaded> g_packLoaded;   // decoded once per session: later slots share the renderer's image
        bool seamPackReplace(int32_t slot, TexEntry &e, uint64_t tex0, uint64_t texa)
        {
            if (!seamPackOn() || !ps2tex::replacementsEnabled()) return false;
            const uint32_t psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            if (psm != PSMT8 && psm != PSMT4) return false;
            if (e.drawnPages || e.clutDrawn || !e.indexed) { ++g_packSkipped; return false; }
            GSTex0Reg t{}; t.tbp0 = (uint32_t)(tex0 & 0x3FFFu); t.tbw = (uint8_t)((tex0 >> 14) & 0x3Fu); t.psm = (uint8_t)psm;
            t.tw = (uint8_t)((tex0 >> 26) & 0xFu); t.th = (uint8_t)((tex0 >> 30) & 0xFu); t.tcc = (uint8_t)((tex0 >> 34) & 1u); t.tfx = (uint8_t)((tex0 >> 35) & 3u);
            t.cbp = (uint32_t)((tex0 >> 37) & 0x3FFFu); t.cpsm = (uint8_t)((tex0 >> 51) & 0xFu); t.csm = (uint8_t)((tex0 >> 55) & 1u); t.csa = (uint8_t)((tex0 >> 56) & 0x1Fu); t.cld = (uint8_t)((tex0 >> 61) & 7u);
            GSTexaReg ta{ (uint8_t)(texa & 0xFFu), ((texa >> 15) & 1u) != 0u, (uint8_t)((texa >> 32) & 0xFFu) };
            GSTexClutReg tc{ (uint8_t)(g_r.texclut & 0x3Fu), (uint8_t)((g_r.texclut >> 6) & 0x3Fu), (uint16_t)((g_r.texclut >> 12) & 0x3FFu) };
            uint32_t clut[256] = {};
            for (const auto &pg : e.pages) touchPage(pg.first);   // [lazyvram] identify() reads the mirror directly
            { const uint32_t ca = addr32((uint32_t)((tex0 >> 37) & 0x3FFFu), 1u, 0u, 0u); touch(ca); touch(ca + 1023u); }
            const int n = GSRasterizer::fillClutForBackend(clut, g_vram, ta, tc, t);
            ps2tex::TexIdent id;
            if (n <= 0 || !ps2tex::identify(g_vram, t.tbp0, t.tbw, t.psm, t.tw, t.th, clut, ta.ta0, ta.aem, ta.ta1, id)) { ++g_packSkipped; return false; }
            const std::string name = id.name();
            {   // PS2X_SEAMPACK_LOG=<n>: the first n identifications (compare with PS2X_PGS_PACKLOG on the reference path)
                static const int s_log = [](){ const char *v = std::getenv("PS2X_SEAMPACK_LOG"); return v && v[0] ? std::atoi(v) : 0; }(); static int s_n = 0;
                if (s_n < s_log && !g_packLoaded.count(name) && !g_packMiss.count(name)) { ++s_n; std::fprintf(stderr, "[seampack-id] #%d %s psm=%u tbp0=%u tbw=%u tw=%u th=%u cbp=%u csa=%u cpsm=%u n=%d clut0=%08x texa=%llx\n", s_n, name.c_str(), psm, t.tbp0, t.tbw, t.tw, t.th, t.cbp, t.csa, t.cpsm, n, clut[0], (unsigned long long)texa); }
            }
            if (g_packMiss.count(name)) { ++g_packMisses; return false; }
            if (auto li = g_packLoaded.find(name); li != g_packLoaded.end())
            {   // already on the GPU: point the slot at the shared image (no pixels travel)
                g_list.texUploads.push_back(TexUpload{ slot, li->second.w, li->second.h, {}, li->second.share });
                e.gpuDecode = false; ++g_packHits; return true;
            }
            std::vector<uint8_t> px; int w = 0, h = 0, fmt = 0;
            ++g_packLoads;
            static const int s_logOut = [](){ const char *v = std::getenv("PS2X_SEAMPACK_LOG"); return v && v[0] ? std::atoi(v) : 0; }(); static int s_outN = 0;
            auto outcome = [&](const char *what) { if (s_outN < s_logOut) { ++s_outN; std::fprintf(stderr, "[seampack-out] %s psm=%u tbp0=%u tw=%u th=%u: %s (%dx%d fmt %d, GS %ux%u)\n", name.c_str(), psm, t.tbp0, t.tw, t.th, what, w, h, fmt, e.w, e.h); } };
            if (!ps2tex::loadReplacement(id, px, w, h, fmt) || w <= 0 || h <= 0 || px.empty()) { outcome("no file / decode failed"); g_packMiss.insert(name); ++g_packMisses; return false; }
            bool gateAlpha = false; uint32_t aSolid = 0, aClear = 255;
            {
                bool clear = false, solid = false, mid = false; uint32_t amax = 0;
                for (int i = 0; i < n; i++) amax = std::max(amax, clut[i] >> 24);
                const uint32_t loT = amax > 0x80u ? 4u : 2u, hiT = amax > 0x80u ? 251u : 0x7eu;
                for (int i = 0; i < n && !mid; i++) { const uint32_t a = clut[i] >> 24; if (a <= loT) { clear = true; aClear = std::min<uint32_t>(aClear, a); } else if (a >= hiT) { solid = true; aSolid = std::max<uint32_t>(aSolid, a); } else mid = true; }
                gateAlpha = !mid && clear && solid;
                if (amax > 0x80u) { aSolid = aSolid * 128u / 255u; aClear = aClear * 128u / 255u; }
            }
            if (fmt != 7)
            {   // BC-compressed DDS: the native renderer samples RGBA8 slots; a gate asset keeps the game's own texels (as in OpenGL)
                if (gateAlpha) { outcome("gate asset, compressed: keep the game's texels"); g_packMiss.insert(name); ++g_packMisses; return false; }
                std::vector<uint8_t> dec; if (!ps2xBcDecode(fmt, px, w, h, dec)) { outcome("BC decode failed"); g_packMiss.insert(name); ++g_packMisses; return false; }
                px.swap(dec);
            }
            else if (gateAlpha)
                for (size_t i = 3; i < px.size(); i += 4) px[i] = (uint8_t)(px[i] >= 64u ? aSolid : aClear);
            if ((uint32_t)w % e.w != 0u || (uint32_t)h % e.h != 0u || (uint32_t)w / e.w != (uint32_t)h / e.h) { outcome("not an integer multiple of the GS size"); g_packMiss.insert(name); ++g_packMisses; return false; }   // the draw needs one integer factor
            outcome("REPLACED");
            const uint64_t share = fnv(name.data(), name.size()) | 1ull;
            g_packLoaded[name] = PackLoaded{ (uint32_t)w, (uint32_t)h, share };
            g_list.texUploads.push_back(TexUpload{ slot, (uint32_t)w, (uint32_t)h, std::move(px), share });
            e.gpuDecode = false;
            ++g_packHits;
            return true;
        }

        void evictTextures()
        {
            for (size_t i = 0; i < g_tex.size(); ++i)
            {
                TexEntry &e = g_tex[i];
                if (!e.used) continue;
                if (g_frame - e.lastUse > 180u || g_tex.size() > 4096u) { g_texByKey.erase(e.key); retireSlot((int32_t)i); }
            }
        }

        // ---- draw state --------------------------------------------------------------------
        bool g_stateDirty = true; State g_stateCache[2]; bool g_stateValid[2] = { false, false };   // [statecache] currentState(true/false) memo, invalidated by writeReg
        State buildState(bool tex);
        const State &currentState(bool tex)
        {   // a reference into the memo: the kick path runs per primitive and must not copy the struct; each variant built on demand
            if (g_stateDirty) { g_stateValid[0] = g_stateValid[1] = false; g_stateDirty = false; }
            if (g_texDirty) g_stateValid[1] = false;   // (buildState(true) resolves the texture and clears g_texDirty)
            const int i = tex ? 1 : 0;
            if (!g_stateValid[i]) { ScopeMs _sm{g_msState}; g_stateCache[i] = buildState(tex); g_stateValid[i] = true; ++g_stateBuilds; }
            return g_stateCache[i];
        }
        State buildState(bool tex)
        {
            State s;
            const uint64_t pr = g_r.prmodecont ? g_r.prim : ((g_r.prim & 7u) | (g_r.prmode & ~7ull));
            s.prim = (uint8_t)(pr & 7u); s.iip = (pr >> 3) & 1u; s.tme = (pr >> 4) & 1u; s.fge = (pr >> 5) & 1u;
            s.abe = (pr >> 6) & 1u; s.aa1 = (pr >> 7) & 1u; s.fst = (pr >> 8) & 1u; s.ctxt = (pr >> 9) & 1u;
            const Ctx &c = g_r.ctx[s.ctxt];
            s.dbgPrim = g_r.prim; s.dbgPrmode = g_r.prmode; s.dbgPrmodecont = g_r.prmodecont ? 1 : 0; s.dbgSc[0] = g_r.ctx[0].scissor; s.dbgSc[1] = g_r.ctx[1].scissor;
            s.fbp = (uint32_t)(c.frame & 0x1FFu) * 32u; s.fbw = (uint32_t)((c.frame >> 16) & 0x3Fu); s.fpsm = (uint32_t)((c.frame >> 24) & 0x3Fu); s.fbmsk = (uint32_t)(c.frame >> 32);
            s.zbp = (uint32_t)(c.zbuf & 0x1FFu) * 32u; s.zpsm = (uint32_t)((c.zbuf >> 24) & 0xFu); s.zmsk = (uint8_t)((c.zbuf >> 32) & 1u);
            s.ate = (uint8_t)(c.test & 1u); s.atst = (uint8_t)((c.test >> 1) & 7u); s.aref = (uint8_t)((c.test >> 4) & 0xFFu); s.afail = (uint8_t)((c.test >> 12) & 3u);
            s.date = (uint8_t)((c.test >> 14) & 1u); s.datm = (uint8_t)((c.test >> 15) & 1u); s.zte = (uint8_t)((c.test >> 16) & 1u); s.ztst = (uint8_t)((c.test >> 17) & 3u);
            s.aA = (uint8_t)(c.alpha & 3u); s.aB = (uint8_t)((c.alpha >> 2) & 3u); s.aC = (uint8_t)((c.alpha >> 4) & 3u); s.aD = (uint8_t)((c.alpha >> 6) & 3u); s.fix = (uint8_t)((c.alpha >> 32) & 0xFFu);
            s.pabe = (uint8_t)(g_r.pabe & 1u); s.fba = (uint8_t)(c.fba & 1u); s.colclamp = (uint8_t)(g_r.colclamp & 1u);
            s.scax0 = (uint16_t)(c.scissor & 0x7FFu); s.scax1 = (uint16_t)((c.scissor >> 16) & 0x7FFu); s.scay0 = (uint16_t)((c.scissor >> 32) & 0x7FFu); s.scay1 = (uint16_t)((c.scissor >> 48) & 0x7FFu);
            s.ofx = (uint16_t)(c.xyoffset & 0xFFFFu); s.ofy = (uint16_t)((c.xyoffset >> 32) & 0xFFFFu);
            s.fogcol = (uint32_t)(g_r.fogcol & 0xFFFFFFu);
            if (s.tme && tex)
            {
                s.tfx = (uint8_t)((c.tex0 >> 35) & 3u); s.tcc = (uint8_t)((c.tex0 >> 34) & 1u);
                s.wms = (uint8_t)(c.clamp & 3u); s.wmt = (uint8_t)((c.clamp >> 2) & 3u);
                s.minu = (uint16_t)((c.clamp >> 4) & 0x3FFu); s.maxu = (uint16_t)((c.clamp >> 14) & 0x3FFu);
                s.minv = (uint16_t)((c.clamp >> 24) & 0x3FFu); s.maxv = (uint16_t)((c.clamp >> 34) & 0x3FFu);
                s.mmag = (uint8_t)((c.tex1 >> 5) & 1u); s.mmin = (uint8_t)((c.tex1 >> 6) & 7u);
                if (g_texDirty) { g_curTex = resolveTexture(c); g_texDirty = false; }
                s.tex = g_curTex; s.texW = g_curTexW; s.texH = g_curTexH;
                s.texFromDrawn = (g_curTex >= 0 && g_tex[g_curTex].drawnPages) ? 1u : 0u;
                s.tex0lo = (uint32_t)c.tex0; s.tex0hi = (uint32_t)(c.tex0 >> 32);
            }
            return s;
        }

        inline bool sameState(const State &a, const State &b) { return std::memcmp(&a, &b, sizeof(State)) == 0; }

        void noteDrawPages(const State &s)
        {   // the pages a draw into this target can touch: the scissor's rows of pages (page = 64x32 px at 32 bpp, 64x64 at 16)
            {   // [stampmemo] a run of draws with the same target/scissor/Z state and no transfer in between stamps the same pages
                // with the same stamp: only the targets' draw order needs bumping (per triangle otherwise: 4 % of the process)
                static struct { uint32_t fbp, fbw, fpsm, zbp, zpsm, stamp; uint16_t sx0, sx1, sy0, sy1; uint8_t zw; TargetInfo *t, *z; } m = {};
                static const bool s_memo = [](){ const char *v = std::getenv("PS2X_SEAMGS_STAMPMEMO"); return !(v && v[0] == '0'); }();
                const uint8_t zw = (s.zte && !s.zmsk) ? 1u : 0u;
                if (s_memo && m.t && m.fbp == s.fbp && m.fbw == s.fbw && m.fpsm == s.fpsm && m.stamp == g_stamp && m.sx0 == s.scax0 && m.sx1 == s.scax1 && m.sy0 == s.scay0 && m.sy1 == s.scay1
                    && m.zw == zw && (!zw || (m.zbp == s.zbp && m.zpsm == s.zpsm)))
                {
                    m.t->drawStamp = ++g_drawStamp; if (m.z) m.z->drawStamp = g_drawStamp;
                    return;
                }
                m.fbp = s.fbp; m.fbw = s.fbw; m.fpsm = s.fpsm; m.zbp = s.zbp; m.zpsm = s.zpsm; m.stamp = g_stamp; m.sx0 = s.scax0; m.sx1 = s.scax1; m.sy0 = s.scay0; m.sy1 = s.scay1; m.zw = zw;
                m.t = &g_targets[s.fbp]; m.z = zw ? &g_zbufs[s.zbp] : nullptr;
            }
            const uint32_t pagesPerRow = s.fbw ? s.fbw : 1u;
            const uint32_t pageH = (s.fpsm == PSMCT16 || s.fpsm == PSMCT16S) ? 64u : 32u;
            const uint32_t row0 = s.scay0 / pageH, row1 = s.scay1 / pageH;
            {
                TargetInfo &t = g_targets[s.fbp];
                {   // A target keeps the row width of its own (32-bit) layout; a 16-bit FRAME view of the same pages (64-pixel
                    // page rows, possibly a different FBW) only extends the page span. Taking the view's width would make
                    // the read-back decode the native image with the wrong width (the outline mask came back empty).
                    const bool f16 = s.fpsm == 2u || s.fpsm == 10u;
                    const uint32_t pages = ((s.scay1 / (f16 ? 64u : 32u)) + 1u) * pagesPerRow;
                    if (t.rows == 0u || (!f16 && (t.psm == 2u || t.psm == 10u))) { t.fbw = pagesPerRow; t.psm = s.fpsm; }
                    const uint32_t fbwE = t.fbw != 0u ? t.fbw : 1u;
                    t.rows = std::max(t.rows, (pages + fbwE - 1u) / fbwE); t.drawStamp = ++g_drawStamp;
                }
                if (s.zte && !s.zmsk) { TargetInfo &z = g_zbufs[s.zbp]; z.fbw = pagesPerRow; z.psm = s.zpsm; z.rows = std::max(z.rows, (s.scay1 / 32u) + 1u); z.drawStamp = g_drawStamp; }
            }
            const uint32_t col0 = s.scax0 / 64u, col1 = std::min<uint32_t>(s.scax1 / 64u, pagesPerRow - 1u);
            const uint32_t p0 = s.fbp / 32u;
            for (uint32_t r = row0; r <= row1; ++r) for (uint32_t c = col0; c <= col1; ++c)
            {
                const uint32_t p = p0 + r * pagesPerRow + c;
                if (p >= 0x1a0u && p < 0x1c0u) { static int s_dbg = 0; if (s_dbg < 6) { ++s_dbg; std::fprintf(stderr, "[stamp] frame %llu page 0x%x by fbp 0x%x fbw %u psm %u sc %u..%u %u..%u prim %u tme %u\n", (unsigned long long)g_frame, p, s.fbp, s.fbw, s.fpsm, s.scax0, s.scax1, s.scay0, s.scay1, s.prim, s.tme); } }
                if (p < kPages) g_pageDrawn[p] = g_stamp;
            }
            if (s.zte && !s.zmsk)
            {
                const uint32_t z0 = s.zbp / 32u, zr0 = s.scay0 / 32u, zr1 = s.scay1 / 32u;
                for (uint32_t r = zr0; r <= zr1; ++r) for (uint32_t c = col0; c <= col1; ++c) { const uint32_t p = z0 + r * pagesPerRow + c; if (p < kPages) g_pageDrawn[p] = g_stamp; }
            }
        }

        float zNorm(uint32_t z, uint32_t zpsm)
        {
            switch (zpsm & 0xFu)
            {
            case 0: return float(double(z) / 4294967296.0);
            case 1: return float(z & 0xFFFFFFu) / 16777216.0f;
            default: return float(z & 0xFFFFu) / 65536.0f;
            }
        }

        // [targethist] PS2X_SEAMVK_TARGETHIST=1: draws per (kind, prog, FRAME base, fbw, psm, zte/zmsk, ctxt), printed every 300 frames
        std::map<uint64_t, uint64_t> g_targetHist;
        void noteTargetHist(int kind, int prog, const State &s)
        {
            static const bool s_on = [](){ const char *v = std::getenv("PS2X_SEAMVK_TARGETHIST"); return v && v[0] && v[0] != '0'; }();
            if (!s_on) return;
            const uint64_t k = (uint64_t)kind | ((uint64_t)prog << 1) | ((uint64_t)s.fbp << 4) | ((uint64_t)s.fbw << 20) | ((uint64_t)s.fpsm << 26) | ((uint64_t)s.zte << 32) | ((uint64_t)s.zmsk << 33) | ((uint64_t)s.ctxt << 34) | ((uint64_t)s.tme << 35) | ((uint64_t)s.texFromDrawn << 36);
            ++g_targetHist[k];
        }
        void printTargetHist()
        {
            if (g_targetHist.empty()) return;
            std::fprintf(stderr, "[targethist] frame %llu\n", (unsigned long long)g_frame);
            for (const auto &kv : g_targetHist)
            {
                const uint64_t k = kv.first;
                std::fprintf(stderr, "[targethist]  %8llu  kind %llu prog %llu fbp 0x%llx fbw %llu psm %llu zte %llu zmsk %llu ctxt %llu tme %llu drawnTex %llu\n", (unsigned long long)kv.second,
                             (unsigned long long)(k & 1), (unsigned long long)((k >> 1) & 7), (unsigned long long)((k >> 4) & 0xFFFF), (unsigned long long)((k >> 20) & 0x3F), (unsigned long long)((k >> 26) & 0x3F),
                             (unsigned long long)((k >> 32) & 1), (unsigned long long)((k >> 33) & 1), (unsigned long long)((k >> 34) & 1), (unsigned long long)((k >> 35) & 1), (unsigned long long)((k >> 36) & 1));
            }
            g_targetHist.clear();
        }

        // [spriterect] the sprite being expanded, as its texel rect packed for the vertex's spare words (0 = not a sprite):
        // UV mode: 1/16-texel units; STQ mode: s/q and t/q in 1/4096 (the shader scales by the texture size). Bilinear
        // sampling is clamped to this rect: at render scale N the sub-pixel samples of a sprite's edge rows reach up to
        // half a texel OUTSIDE its rect (a 1x GS never does -- its samples sit on texel centres) and pull in the atlas
        // row/column next to it: a 1-px light line under the ki gauge (user, 2026-09-28).
        uint32_t g_rectPad[2] = { 0u, 0u };
        void emitTriangle(const GsVert *v0, const GsVert *v1, const GsVert *v2, const State &s)
        {
            ScopeMs _sm{g_msTri};
            const GsVert *vs[3] = { v0, v1, v2 };
            Vtx out[3];
            {   // PS2X_SEAMGS_PIXLOG=x,y (dev): every triangle (sprites included, expanded) whose bounding box covers that GS pixel
                static const int s_px = [](){ const char *v = std::getenv("PS2X_SEAMGS_PIXLOG"); return v && v[0] ? std::atoi(v) : -1; }();
                static const int s_py = [](){ const char *v = std::getenv("PS2X_SEAMGS_PIXLOG"); if (v) if (const char *c = std::strchr(v, ',')) return std::atoi(c + 1); return -1; }();
                if (s_px >= 0)
                {
                    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                    for (int i = 0; i < 3; ++i) { const float x = (float(int32_t(vs[i]->x)) - float(s.ofx)) / 16.0f, y = (float(int32_t(vs[i]->y)) - float(s.ofy)) / 16.0f; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
                    const float px = s_px + 0.5f, py = s_py + 0.5f;
                    if (px >= x0 - 1.0f && px < x1 + 1.0f && py >= y0 - 1.0f && py < y1 + 1.0f)
                        std::fprintf(stderr, "[pixlog] frame %llu draw %zu tri (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) uv (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) fst %u tex0 %08x%08x tme %u abe %u (%u,%u,%u,%u) fix %u ate %u atst %u aref %u fbp %x msk %08x rgba %02x%02x%02x%02x mmag %u prim %u iip %u\n",
                                     (unsigned long long)g_frame, g_list.draws.size(), (float(int32_t(v0->x)) - s.ofx) / 16.0f, (float(int32_t(v0->y)) - s.ofy) / 16.0f, (float(int32_t(v1->x)) - s.ofx) / 16.0f, (float(int32_t(v1->y)) - s.ofy) / 16.0f, (float(int32_t(v2->x)) - s.ofx) / 16.0f, (float(int32_t(v2->y)) - s.ofy) / 16.0f,
                                     v0->u / 16.0f, v0->v / 16.0f, v1->u / 16.0f, v1->v / 16.0f, v2->u / 16.0f, v2->v / 16.0f, s.fst, s.tex0hi, s.tex0lo, s.tme, s.abe, s.aA, s.aB, s.aC, s.aD, s.fix, s.ate, s.atst, s.aref, s.fbp, s.fbmsk, v2->r, v2->g, v2->b, v2->a, s.mmag, s.prim, s.iip);
                }
            }
            uint32_t rectPad[2] = { g_rectPad[0], g_rectPad[1] };
            if (s.tme && s.fst && rectPad[0] == 0u && rectPad[1] == 0u)
            {   // [spriterect] UV-mode triangles too (the HUD's plates are tri-strip quads): the triangle's own texel rect
                uint32_t u0 = 0xFFFFu, v0 = 0xFFFFu, u1 = 0u, v1 = 0u;
                for (int i = 0; i < 3; ++i) { u0 = std::min<uint32_t>(u0, vs[i]->u); v0 = std::min<uint32_t>(v0, vs[i]->v); u1 = std::max<uint32_t>(u1, vs[i]->u); v1 = std::max<uint32_t>(v1, vs[i]->v); }
                rectPad[0] = u0 | (v0 << 16); rectPad[1] = u1 | (v1 << 16);
                if (rectPad[0] == 0u && rectPad[1] == 0u) rectPad[1] = 1u;
            }
            for (int i = 0; i < 3; ++i)
            {
                const GsVert &g = *vs[i];
                Vtx &o = out[i];
                o.x = (float(int32_t(g.x)) - float(s.ofx)) / 16.0f;
                o.y = (float(int32_t(g.y)) - float(s.ofy)) / 16.0f;
                o.z = zNorm(g.z, s.zpsm);
                o.q = g.q; o.s = g.s; o.t = g.t; o.u = float(g.u) / 16.0f; o.v = float(g.v) / 16.0f;
                const GsVert &cv = s.iip ? g : *v2;   // flat: the last vertex's colour
                o.rgba = (uint32_t)cv.r | ((uint32_t)cv.g << 8) | ((uint32_t)cv.b << 16) | ((uint32_t)cv.a << 24);
                o.fog = float(g.fog) / 255.0f; o.pad[0] = rectPad[0]; o.pad[1] = rectPad[1];   // [spriterect]
            }
            noteTargetHist(0, 0, s);
            auto pushTri = [&](const Vtx tri[3], const State &st) {
                if (!g_list.draws.empty() && g_list.draws.back().kind == 0 && sameState(g_list.draws.back().st, st) && !st.date)   // [datesplit] a DATE triangle reads the destination in-pass: overlapping triangles of one draw would not see each other's writes (the shadow patch's clipped fan darkened twice along the shared edge)
                    g_list.draws.back().count += 3;
                else
                {
                    Draw d; d.kind = 0; d.st = st; d.vertOff = (uint32_t)g_list.verts.size(); d.count = 3; d.stride = sizeof(Vtx);
                    g_list.draws.push_back(d);
                    if (!st.cpuRastered) noteDrawPages(st);
                }
                const uint8_t *b = reinterpret_cast<const uint8_t *>(tri);
                g_list.verts.insert(g_list.verts.end(), b, b + 3u * sizeof(Vtx));
            };
            if (g_nativeHud)
            {   // [nativehud] the overlay's toggles and the widescreen HUD layout, on this triangle
                const bool isTri = !g_emitSprite;
                const bool scene = wsScene(s.fbp, s.fpsm);
                if (!g_emitSprite && s.zte && s.ztst >= 2u) g_ws.frameHad3d = true;
                if (!s.tme && s.abe && s.aA == 2u && s.aB == 0u && s.aC == 1u && s.aD == 1u && scene)
                {   // the cel-outline darkener (Cd - Cs * Ad): Cel Outline OFF drops it; ink strength / colour scale its colour
                    if (!GsGpuRenderer::outlineEnabled()) { ++g_ws.inkDropped; return; }
                    const int pct = GsGpuRenderer::inkStrengthPct(); const uint32_t col = ps2x_pgs::inkColor();
                    if (pct != 199 || col != 0u)
                    {
                        const float k = float(pct) / 199.0f;
                        const float kc[3] = { k * float(255u - ((col >> 16) & 0xFFu)) / 255.0f, k * float(255u - ((col >> 8) & 0xFFu)) / 255.0f, k * float(255u - (col & 0xFFu)) / 255.0f };
                        for (int i = 0; i < 3; ++i) { uint32_t c = out[i].rgba, o = c & 0xFF000000u; for (int ch = 0; ch < 3; ++ch) { const float v = float((c >> (8 * ch)) & 0xFFu) * kc[ch]; o |= (uint32_t)(v > 255.f ? 255u : (uint32_t)(v + 0.5f)) << (8 * ch); } out[i].rgba = o; }
                        ++g_ws.inkScaled;
                    }
                }
                if (isTri && s.tme && scene && (s.tex0lo & 0x3FFFu) == 0x2a00u && ((s.tex0lo >> 20) & 0x3Fu) == 1u && ((s.tex0lo >> 26) & 0xFu) == 8u && (((s.tex0lo >> 30) | (s.tex0hi << 2)) & 0xFu) == 8u && !s.date)
                {   // the shadow decal tiles (CT24 256x256 read of the silhouette buffer, no DATE): Character Shadows OFF drops them
                    if (!GsGpuRenderer::shadowsEnabled()) { ++g_ws.shadowDropped; return; }
                }
                if (g_ws.on && scene)
                {
                    const float W = wsW(s.fbw), inv = g_ws.inv;
                    State ts = s;
                    {   // scissor: bar scissors in the top band map with the layout (origin-anchored / tiny rects are render-to-texture)
                        const uint32_t x0 = s.scax0, x1 = s.scax1, y1 = s.scay1;
                        if (!(x0 < 8u || x1 < x0 + 16u) && !(x1 < x0 || y1 >= 96u || float(x1 - x0 + 1u) >= 0.8f * W))
                        {
                            float mx0 = std::floor(wsMapX(float(x0), W, inv)), mx1 = std::ceil(wsMapX(float(x1 + 1u), W, inv)) - 1.0f;
                            if (mx0 < 0.f) mx0 = 0.f; if (mx1 > 2047.f) mx1 = 2047.f; if (mx1 < mx0) mx1 = mx0;
                            ts.scax0 = (uint16_t)mx0; ts.scax1 = (uint16_t)mx1; ++g_ws.scissors;
                        }
                    }
                    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; bool zAllZero = true;
                    for (int i = 0; i < 3; ++i) { x0 = std::min(x0, out[i].x); x1 = std::max(x1, out[i].x); y0 = std::min(y0, out[i].y); y1 = std::max(y1, out[i].y); if (vs[i]->z != 0u) zAllZero = false; }
                    const float w = x1 - x0, hh = y1 - y0;
                    bool hud;
                    if (g_emitSprite) hud = (w > 0.f && w < 0.8f * W && hh > 0.f && hh < 300.f && y1 < 96.f && (!s.zte || s.ztst == 1u));
                    else hud = (s.fst && zAllZero && hh < 300.f && y1 < 96.f && (w < 0.8f * W || x0 > 8.f));
                    if (hud)
                    {
                        // split at the layout's cut points (the map is piecewise linear between them), then map every vertex
                        std::vector<Vtx> poly(out, out + 3), tmp;
                        int layout = g_wsHudLayout.load(std::memory_order_relaxed);
                        if (layout > 0)
                        {
                            const float k = W / 512.0f; const float cuts[4] = { 124.f * k, 216.f * k, 296.f * k, 388.f * k };
                            auto lerpV = [](const Vtx &a, const Vtx &b, float t) { Vtx r = a; r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t; r.z = a.z + (b.z - a.z) * t; r.q = a.q + (b.q - a.q) * t; r.s = a.s + (b.s - a.s) * t; r.t = a.t + (b.t - a.t) * t; r.u = a.u + (b.u - a.u) * t; r.v = a.v + (b.v - a.v) * t; r.fog = a.fog + (b.fog - a.fog) * t;
                                uint32_t c = 0; for (int ch = 0; ch < 4; ++ch) { const float va = float((a.rgba >> (8 * ch)) & 0xFFu), vb = float((b.rgba >> (8 * ch)) & 0xFFu); c |= (uint32_t)(va + (vb - va) * t + 0.5f) << (8 * ch); } r.rgba = c; return r; };
                            std::vector<std::vector<Vtx>> pieces; pieces.push_back(poly);
                            for (float cx : cuts)
                            {
                                if (!(cx > x0 + 1.f && cx < x1 - 1.f)) continue;
                                std::vector<std::vector<Vtx>> next;
                                for (const auto &pg : pieces)
                                {   // Sutherland-Hodgman against x <= cx and x >= cx
                                    for (int side = 0; side < 2; ++side)
                                    {
                                        std::vector<Vtx> o;
                                        for (size_t i = 0; i < pg.size(); ++i)
                                        {
                                            const Vtx &a = pg[i], &b = pg[(i + 1) % pg.size()];
                                            const bool ina = side == 0 ? a.x <= cx : a.x >= cx, inb = side == 0 ? b.x <= cx : b.x >= cx;
                                            if (ina) o.push_back(a);
                                            if (ina != inb) o.push_back(lerpV(a, b, (cx - a.x) / (b.x - a.x)));
                                        }
                                        if (o.size() >= 3) next.push_back(std::move(o));
                                    }
                                }
                                pieces.swap(next);
                            }
                            if (pieces.size() > 1u) ++g_ws.split;
                            for (auto &pg : pieces)
                            {
                                for (Vtx &v : pg) v.x = wsMapX(v.x, W, inv);
                                for (size_t i = 1; i + 1 < pg.size(); ++i) { Vtx tri[3] = { pg[0], pg[i], pg[i + 1] }; pushTri(tri, ts); }
                            }
                            g_ws.mapped += 3; return;
                        }
                        for (int i = 0; i < 3; ++i) out[i].x = wsMapX(out[i].x, W, inv);
                        ++g_ws.mapped;
                    }
                    pushTri(out, ts);
                    return;
                }
            }
            pushTri(out, s);
        }

        uint64_t g_hostIn[8] = {}, g_hostOut[8] = {}, g_hostDropped[8] = {};

        void emitHostDraw(const State &s)
        {
            ++g_hostOut[g_host.prog & 7u];
            noteTargetHist(1, g_host.prog, s);
            g_ws.frameHad3d = true;   // [nativehud]
            Draw d; d.kind = 1; d.prog = g_host.prog; d.hostPass = g_hostPassN++; d.st = s; d.c = g_host.c;
            d.vertOff = (uint32_t)g_list.verts.size(); d.count = g_host.count; d.stride = g_host.stride;
            g_list.verts.insert(g_list.verts.end(), g_hostVerts.begin(), g_hostVerts.end());
            g_list.draws.push_back(d);
            noteDrawPages(s);
            g_hostDrawn = true; g_hostLast = s;
        }

        // [scratchraster] Sprites into small scratch targets (rendered palettes, lighting ramps) are rasterised into the
        // VRAM mirror on the CPU, exactly, so textures and CLUTs that read them decode like uploads. Only when the source
        // texture itself is mirror-backed. The GPU still draws them too (harmless: nothing displays those targets).
        bool cpuTargetOk(const State &s, const TexEntry *&te)
        {
            te = nullptr;
            if (g_watch.hi && s.fbp * 32u >= g_watch.lo && s.fbp * 32u < g_watch.hi && g_watch.logged < 400u)
            { ++g_watch.logged; std::fprintf(stderr, "[seamgs-watch] frame %llu draw into fbp 0x%x (block 0x%x) fbw %u psm %u msk %08x sc %u..%u %u..%u\n", (unsigned long long)g_frame, s.fbp, s.fbp * 32u, s.fbw, s.fpsm, s.fbmsk, s.scax0, s.scax1, s.scay0, s.scay1); }
            static const bool s_scratch = [](){ const char *v = std::getenv("PS2X_SEAMGS_SCRATCH"); return !(v && v[0] == '0'); }();
            if (!s_scratch) return false;   // PS2X_SEAMGS_SCRATCH=0: every target through the GPU (rt decodes) -- saves 2 ms/frame but the HUD's rendered palettes then decode stale from the mirror (gray flicker, 2026-09-27)
            if (s.fbp == 0u || s.fbp == 0xe00u || s.fbw > 4u) return false;
            if (s.fpsm != PSMCT32 && s.fpsm != PSMCT24 && s.fpsm != PSMCT16 && s.fpsm != PSMCT16S) return false;
            if (s.tme)
            {
                if (s.tex < 0 || (size_t)s.tex >= g_tex.size()) return false;
                TexEntry &ent = g_tex[s.tex];
                if (!ent.drawnPages && ent.rgbaCopy.empty() && ent.gpuDecode && ent.w * ent.h <= 256u * 256u)
                {   // [gpudecode] a GPU-decoded texture the scratch raster samples: decode it once here too (the HUD's portraits)
                    std::vector<uint8_t> rgba; const uint64_t tex0 = (uint64_t)s.tex0lo | ((uint64_t)s.tex0hi << 32);
                    g_forceCpuDecode = true; decodeTextureImpl(tex0, g_r.texa, ent, rgba); g_forceCpuDecode = false;
                    if (!rgba.empty()) ent.rgbaCopy = rgba;
                    ent.gpuDecode = true;   // the GPU copy stays the renderer's source for draws
                }
                te = &ent;
                if (te->drawnPages || te->rgbaCopy.empty()) return false;
            }
            return true;
        }
        // One pixel of the GS pipeline into the mirror: texture (nearest), TFX, alpha test, blend, FBA, FBMSK.
        void cpuShadePixel(const State &s, const TexEntry *te, int x, int y, uint32_t cr, uint32_t cg, uint32_t cb, uint32_t ca, float fu, float fv)
        {
            uint32_t r = cr, g = cg, bl = cb, al = ca;
            if (te)
            {
                int u = (int)std::floor(fu), v = (int)std::floor(fv);
                auto wrap = [](int t, int size, uint32_t mode, int mn, int mx) {
                    if (mode == 0u) return t & (size - 1);
                    if (mode == 1u) return std::min(std::max(t, 0), size - 1);
                    if (mode == 2u) return std::min(std::max(t, mn), mx);
                    return (t & mn) | mx;
                };
                u = wrap(u, (int)te->w, s.wms, s.minu, s.maxu); v = wrap(v, (int)te->h, s.wmt, s.minv, s.maxv);
                u = std::min(std::max(u, 0), (int)te->w - 1); v = std::min(std::max(v, 0), (int)te->h - 1);
                const uint8_t *t = te->rgbaCopy.data() + (size_t(v) * te->w + u) * 4u;
                const uint32_t tr = t[0], tg = t[1], tb = t[2], ta = t[3];
                switch (s.tfx)
                {
                case 0: r = std::min(255u, (tr * cr) >> 7); g = std::min(255u, (tg * cg) >> 7); bl = std::min(255u, (tb * cb) >> 7); al = s.tcc ? std::min(255u, (ta * ca) >> 7) : ca; break;
                case 1: r = tr; g = tg; bl = tb; al = s.tcc ? ta : ca; break;
                case 2: r = std::min(255u, ((tr * cr) >> 7) + ca); g = std::min(255u, ((tg * cg) >> 7) + ca); bl = std::min(255u, ((tb * cb) >> 7) + ca); al = s.tcc ? std::min(255u, ta + ca) : ca; break;
                default: r = std::min(255u, ((tr * cr) >> 7) + ca); g = std::min(255u, ((tg * cg) >> 7) + ca); bl = std::min(255u, ((tb * cb) >> 7) + ca); al = s.tcc ? ta : ca; break;
                }
            }
            if (s.ate)
            {
                bool pass = true;
                switch (s.atst) { case 0: pass = false; break; case 2: pass = al < s.aref; break; case 3: pass = al <= s.aref; break; case 4: pass = al == s.aref; break; case 5: pass = al >= s.aref; break; case 6: pass = al > s.aref; break; case 7: pass = al != s.aref; break; default: break; }
                if (!pass) return;   // KEEP (other AFAIL modes not needed here)
            }
            const bool c16 = s.fpsm == PSMCT16 || s.fpsm == PSMCT16S;
            const uint32_t pa = c16 ? addr16(s.fpsm, s.fbp, s.fbw, (uint32_t)x, (uint32_t)y) : addr32(s.fbp, s.fbw, (uint32_t)x, (uint32_t)y);   // one swizzle for the read and the write
            const uint32_t dst = c16 ? (uint32_t)rd16(pa) : rd32(pa);
            uint32_t dr, dg, db, da;
            if (c16) { dr = (dst & 0x1Fu) << 3; dg = ((dst >> 5) & 0x1Fu) << 3; db = ((dst >> 10) & 0x1Fu) << 3; da = (dst & 0x8000u) ? 0x80u : 0u; }
            else { dr = dst & 0xFFu; dg = (dst >> 8) & 0xFFu; db = (dst >> 16) & 0xFFu; da = (dst >> 24) & 0xFFu; }
            if (s.abe)
            {
                auto pick = [&](uint32_t sel, uint32_t sv, uint32_t dv) { return sel == 0u ? (int)sv : sel == 1u ? (int)dv : 0; };
                const int cc = s.aC == 0u ? (int)al : s.aC == 1u ? (int)da : (int)s.fix;
                auto blend = [&](uint32_t sv, uint32_t dv) {
                    int v = (((pick(s.aA, sv, dv) - pick(s.aB, sv, dv)) * cc) >> 7) + pick(s.aD, sv, dv);
                    return (uint32_t)(s.colclamp ? std::min(std::max(v, 0), 255) : (v & 0xFF));
                };
                r = blend(r, dr); g = blend(g, dg); bl = blend(bl, db);
            }
            if (s.fba) al |= 0x80u;
            uint32_t out;
            if (c16) out = (r >> 3) | ((g >> 3) << 5) | ((bl >> 3) << 10) | ((al & 0x80u) ? 0x8000u : 0u);
            else out = r | (g << 8) | (bl << 16) | (al << 24);
            if (s.fbmsk) out = (out & ~s.fbmsk) | (dst & s.fbmsk);
            if (c16) wr16(pa, (uint16_t)out);
            else if (s.fpsm == PSMCT24) wr32(pa, (dst & 0xFF000000u) | (out & 0xFFFFFFu));
            else wr32(pa, out);
        }

        // [scratchraster] Sprites into small scratch targets (rendered palettes, lighting ramps) are rasterised into the
        // VRAM mirror on the CPU, exactly, so textures and CLUTs that read them decode like uploads. Only when the source
        // texture itself is mirror-backed. The GPU still draws them too (harmless: nothing displays those targets).
        bool cpuSpriteRasterImpl(const State &s, const GsVert &a, const GsVert &b);
        bool cpuSpriteRaster(const State &s, const GsVert &a, const GsVert &b)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = cpuSpriteRasterImpl(s, a, b);
            g_msRaster += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return ok;
        }
        bool cpuSpriteRasterImpl(const State &s, const GsVert &a, const GsVert &b)
        {
            const TexEntry *te;
            static int s_dbg = 0;
            {   // PS2X_SEAMGS_SCRATCHLOG=<game frame>: every scratch-raster candidate of 4 frames (what the CPU rasterises and why)
                static const uint64_t s_at = [](){ const char *v = std::getenv("PS2X_SEAMGS_SCRATCHLOG"); return v && v[0] ? (uint64_t)std::atoll(v) : 0ull; }();
                const uint64_t gf = g_bt3FrameCount.load(std::memory_order_relaxed);
                if (s_at && gf >= s_at && gf < s_at + 4u)
                    std::fprintf(stderr, "[scratchlog] frame %llu sprite fbp 0x%x fbw %u psm %u tme %u tex %d %ux%u abe %u (%u,%u,%u,%u) sc %u..%u %u..%u xy %d,%d -> %d,%d rgba %02x%02x%02x%02x\n", (unsigned long long)gf, s.fbp, s.fbw, s.fpsm, s.tme, s.tex, s.texW, s.texH, s.abe, s.aA, s.aB, s.aC, s.aD, s.scax0, s.scax1, s.scay0, s.scay1,
                                 (int32_t(a.x) - s.ofx) / 16, (int32_t(a.y) - s.ofy) / 16, (int32_t(b.x) - s.ofx) / 16, (int32_t(b.y) - s.ofy) / 16, b.r, b.g, b.b, b.a);
            }
            if (s.fbp >= 0x3c00u && s.fbp < 0x3e00u && s_dbg < 12)
            {
                ++s_dbg;
                std::fprintf(stderr, "[spr3c00] frame %llu fbp 0x%x fbw %u psm %u tme %u tex %d%s abe %u (%u,%u,%u,%u) sc %u..%u %u..%u of %u,%u xy %u,%u %u,%u rgba %02x%02x%02x%02x\n",
                             (unsigned long long)g_frame, s.fbp, s.fbw, s.fpsm, s.tme, s.tex, s.texFromDrawn ? "(DRAWN)" : "", s.abe, s.aA, s.aB, s.aC, s.aD, s.scax0, s.scax1, s.scay0, s.scay1, s.ofx, s.ofy, a.x, a.y, b.x, b.y, b.r, b.g, b.b, b.a);
            }
            if (!cpuTargetOk(s, te)) return false;
            int x0 = (int)std::floor((float(int32_t(a.x)) - float(s.ofx)) / 16.0f + 0.5f), y0 = (int)std::floor((float(int32_t(a.y)) - float(s.ofy)) / 16.0f + 0.5f);
            int x1 = (int)std::floor((float(int32_t(b.x)) - float(s.ofx)) / 16.0f + 0.5f), y1 = (int)std::floor((float(int32_t(b.y)) - float(s.ofy)) / 16.0f + 0.5f);
            if (x1 < x0) std::swap(x0, x1);
            if (y1 < y0) std::swap(y0, y1);
            const int cx0 = std::max(x0, (int)s.scax0), cx1 = std::min(x1, (int)s.scax1 + 1), cy0 = std::max(y0, (int)s.scay0), cy1 = std::min(y1, (int)s.scay1 + 1);
            if (cx1 <= cx0 || cy1 <= cy0 || (cx1 - cx0) * (cy1 - cy0) > 256 * 256) return false;
            // [scratchmax] PS2X_SEAMGS_SCRATCHMAX=<px> (default 1024): only sprites up to this many pixels are rasterised on the CPU --
            // the rendered palettes (16x16, read back as CLUTs from the mirror); bigger scratch draws (the 64x64 blur ping-pong
            // buffers, 12 a frame, 1.1 ms) go through the GPU like every other target and decode as targets (bit-identical, fight frame 798)
            static const int s_max = [](){ const char *v = std::getenv("PS2X_SEAMGS_SCRATCHMAX"); return v && v[0] ? std::atoi(v) : 1024; }();
            if ((cx1 - cx0) * (cy1 - cy0) > s_max) return false;
            // [scratchrepeat] the HUD rasterises the same palettes every frame: while this frame's sequence of scratch sprites
            // (state, vertices, texture decode) matches last frame's from the first one on, and every target page's last writer
            // was a scratch raster, the mirror already holds the result
            uint64_t sh = 1469598103934665603ull;
            auto mixb = [&](const void *d, size_t n) { const uint8_t *q = static_cast<const uint8_t *>(d); for (size_t i = 0; i < n; ++i) { sh ^= q[i]; sh *= 1099511628211ull; } };
            mixb(&s, sizeof(State)); mixb(&a, sizeof(GsVert)); mixb(&b, sizeof(GsVert)); if (te) { mixb(&te->key, sizeof(te->key)); mixb(&te->decodeId, sizeof(te->decodeId)); }
            uint32_t p0 = pageOf(s.fpsm, s.fbp, s.fbw, (uint32_t)cx0, (uint32_t)cy0), p1 = pageOf(s.fpsm, s.fbp, s.fbw, (uint32_t)cx1 - 1u, (uint32_t)cy1 - 1u);
            if (p1 < p0) std::swap(p0, p1);
            const uint32_t idx = g_scratchSeqIdx++;
            if (idx < 128u)
            {
                ScratchMemo &sm = g_scratchMemo[idx];
                bool same = g_scratchSeqOk && sm.hash == sh && sm.p0 == p0 && sm.p1 == p1;
                for (uint32_t p = p0; p <= p1 && same && p < kPages; ++p) if (g_pageWrite[p] != g_pageScratch[p]) same = false;
                if (same) { ++g_scratchSkips; return true; }
                sm.hash = sh; sm.p0 = p0; sm.p1 = p1;
            }
            g_scratchSeqOk = false;
            ++g_stamp;
            struct MarkPages { uint32_t p0, p1; ~MarkPages() { for (uint32_t p = p0; p <= p1 && p < kPages; ++p) g_pageScratch[p] = g_pageWrite[p]; } } markPages{p0, p1};
            const float du = (x1 > x0) ? (float(b.u) - float(a.u)) / 16.0f / float(x1 - x0) : 0.0f;
            const float dv = (y1 > y0) ? (float(b.v) - float(a.v)) / 16.0f / float(y1 - y0) : 0.0f;
            for (int y = cy0; y < cy1; ++y)
                for (int x = cx0; x < cx1; ++x)
                    cpuShadePixel(s, te, x, y, b.r, b.g, b.b, b.a, float(a.u) / 16.0f + du * (float(x) - float(x0) + 0.5f), float(a.v) / 16.0f + dv * (float(y) - float(y0) + 0.5f));
            g_texDirty = true;
            ++g_cpuSprites;
            return true;
        }

        // Triangles into small scratch targets (the lighting ramps are gouraud strips): edge functions at pixel centres,
        // colours and texcoords interpolated linearly in screen space (the GS's own rule for these flat 2D strips).
        bool cpuTriRaster(const State &s, const GsVert &v0, const GsVert &v1, const GsVert &v2)
        {
            const TexEntry *te;
            static int s_dbg = 0;
            if (s.fbp >= 0x3c00u && s_dbg < 12)
            {
                ++s_dbg;
                std::fprintf(stderr, "[tri3c00] fbp 0x%x fbw %u psm %u tme %u tex %d iip %u sc %u..%u %u..%u of %u,%u xy %u,%u %u,%u %u,%u rgba %02x%02x%02x%02x %02x%02x%02x%02x\n",
                             s.fbp, s.fbw, s.fpsm, s.tme, s.tex, s.iip, s.scax0, s.scax1, s.scay0, s.scay1, s.ofx, s.ofy, v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, v0.r, v0.g, v0.b, v0.a, v2.r, v2.g, v2.b, v2.a);
            }
            if (!cpuTargetOk(s, te)) return false;
            float px[3], py[3];
            const GsVert *vs[3] = { &v0, &v1, &v2 };
            for (int i = 0; i < 3; ++i) { px[i] = (float(int32_t(vs[i]->x)) - float(s.ofx)) / 16.0f; py[i] = (float(int32_t(vs[i]->y)) - float(s.ofy)) / 16.0f; }
            const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
            if (std::fabs(area) < 1e-6f) return true;
            int bx0 = std::max((int)std::floor(std::min({px[0], px[1], px[2]})), (int)s.scax0), bx1 = std::min((int)std::ceil(std::max({px[0], px[1], px[2]})), (int)s.scax1 + 1);
            int by0 = std::max((int)std::floor(std::min({py[0], py[1], py[2]})), (int)s.scay0), by1 = std::min((int)std::ceil(std::max({py[0], py[1], py[2]})), (int)s.scay1 + 1);
            if (bx1 <= bx0 || by1 <= by0 || (bx1 - bx0) * (by1 - by0) > 256 * 256) return false;
            ++g_stamp;
            const float inv = 1.0f / area;
            for (int y = by0; y < by1; ++y)
            {
                const float cy = float(y) + 0.5f;
                for (int x = bx0; x < bx1; ++x)
                {
                    const float cx = float(x) + 0.5f;
                    float w0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) * inv;
                    float w1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) * inv;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;   // (top-left rule approximated: shared edges draw twice, harmless without blending)
                    uint32_t cr, cg, cb, ca;
                    if (s.iip)
                    {
                        cr = (uint32_t)std::min(255.0f, std::max(0.0f, w0 * v0.r + w1 * v1.r + w2 * v2.r + 0.5f));
                        cg = (uint32_t)std::min(255.0f, std::max(0.0f, w0 * v0.g + w1 * v1.g + w2 * v2.g + 0.5f));
                        cb = (uint32_t)std::min(255.0f, std::max(0.0f, w0 * v0.b + w1 * v1.b + w2 * v2.b + 0.5f));
                        ca = (uint32_t)std::min(255.0f, std::max(0.0f, w0 * v0.a + w1 * v1.a + w2 * v2.a + 0.5f));
                    }
                    else { cr = v2.r; cg = v2.g; cb = v2.b; ca = v2.a; }
                    float fu = 0.0f, fv = 0.0f;
                    if (te)
                    {
                        if (s.fst) { fu = (w0 * v0.u + w1 * v1.u + w2 * v2.u) / 16.0f; fv = (w0 * v0.v + w1 * v1.v + w2 * v2.v) / 16.0f; }
                        else
                        {
                            const float q = w0 * v0.q + w1 * v1.q + w2 * v2.q;
                            const float st = w0 * v0.s + w1 * v1.s + w2 * v2.s, tt = w0 * v0.t + w1 * v1.t + w2 * v2.t;
                            fu = (q != 0.0f ? st / q : st) * float(te->w); fv = (q != 0.0f ? tt / q : tt) * float(te->h);
                        }
                    }
                    cpuShadePixel(s, te, x, y, cr, cg, cb, ca, fu, fv);
                }
            }
            g_texDirty = true;
            ++g_cpuTris;
            return true;
        }

        // [clutpass] A neutralised kick of a generic native step: the exact register state (+ the palette, as the mirror holds it
        // NOW) becomes a kind-3 pass at this stream position; consecutive kicks with the same state (the 16 strips of one
        // full-screen sprite pass) fold into one pass whose rect is their union.
        void captureNativeKick(uint32_t prim)
        {
            State s = currentState(false);
            const Ctx &c = g_r.ctx[s.ctxt];
            if (s.tme) { s.tfx = (uint8_t)((c.tex0 >> 35) & 3u); s.tcc = (uint8_t)((c.tex0 >> 34) & 1u); s.tex0lo = (uint32_t)c.tex0; s.tex0hi = (uint32_t)(c.tex0 >> 32); s.mmag = (uint8_t)((c.tex1 >> 5) & 1u); }
            const uint32_t nv = prim == 6u ? 2u : (prim == 0u ? 1u : (prim <= 2u ? 2u : 3u));
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (uint32_t i = 0; i < nv; ++i) { const float x = (float(int32_t(g_vq[i].x)) - float(s.ofx)) / 16.0f, y = (float(int32_t(g_vq[i].y)) - float(s.ofy)) / 16.0f; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
            const GsVert &cv = g_vq[nv - 1u];   // flat colour: the last vertex's
            uint32_t vcol = (uint32_t)cv.r | ((uint32_t)cv.g << 8) | ((uint32_t)cv.b << 16) | ((uint32_t)cv.a << 24);
            if (g_nativeHud && !s.tme && s.abe && s.aA == 2u && s.aB == 0u && s.aC == 1u && s.aD == 1u && wsScene(s.fbp, s.fpsm))
            {   // [nativehud] the outline ink draw as a native pass: Cel Outline OFF drops it, ink strength / colour scale it
                if (!GsGpuRenderer::outlineEnabled()) { ++g_ws.inkDropped; return; }
                const int pct = GsGpuRenderer::inkStrengthPct(); const uint32_t col = ps2x_pgs::inkColor();
                if (pct != 199 || col != 0u)
                {
                    const float k = float(pct) / 199.0f;
                    const float kc[3] = { k * float(255u - ((col >> 16) & 0xFFu)) / 255.0f, k * float(255u - ((col >> 8) & 0xFFu)) / 255.0f, k * float(255u - (col & 0xFFu)) / 255.0f };
                    uint32_t o = vcol & 0xFF000000u; for (int ch = 0; ch < 3; ++ch) { const float v = float((vcol >> (8 * ch)) & 0xFFu) * kc[ch]; o |= (uint32_t)(v > 255.f ? 255u : (uint32_t)(v + 0.5f)) << (8 * ch); } vcol = o;
                    ++g_ws.inkScaled;
                }
            }
            if (s.tme && s.fst)
            {   // the passes map texels 1:1 onto pixels (u = x + 0.5): anything else is not this pass family
                static int s_warn = 0;
                const float du = float(g_vq[0].u) / 16.0f - x0, dv = float(g_vq[0].v) / 16.0f - y0;
                if ((du < 0.0f || du > 1.0f || dv < 0.0f || dv > 1.0f) && s_warn++ < 4) std::fprintf(stderr, "[clutpass] step %d: sprite uv not 1:1 (du %.2f dv %.2f) fbp 0x%x tex0 %08x%08x\n", g_nativeParse, du, dv, s.fbp, s.tex0hi, s.tex0lo);
            }
            const uint16_t rx0 = (uint16_t)std::max(0.0f, x0), ry0 = (uint16_t)std::max(0.0f, y0), rx1 = (uint16_t)std::max(0.0f, std::ceil(x1) - 1.0f), ry1 = (uint16_t)std::max(0.0f, std::ceil(y1) - 1.0f);
            Draw *last = g_list.draws.empty() ? nullptr : &g_list.draws.back();
            auto samePass = [](const State &a, const State &b) {
                return a.fbp == b.fbp && a.fbw == b.fbw && a.fpsm == b.fpsm && a.fbmsk == b.fbmsk && a.zbp == b.zbp && a.tme == b.tme && a.tex0lo == b.tex0lo && a.tex0hi == b.tex0hi
                    && a.tfx == b.tfx && a.tcc == b.tcc && a.abe == b.abe && a.aA == b.aA && a.aB == b.aB && a.aC == b.aC && a.aD == b.aD && a.fix == b.fix
                    && a.ate == b.ate && a.atst == b.atst && a.aref == b.aref && a.afail == b.afail && a.colclamp == b.colclamp && a.ctxt == b.ctxt; };
            if (last && last->kind == 3 && last->hostPass == 1 && last->prog == (uint8_t)g_nativeParse && last->vcol == vcol && samePass(last->st, s))
            {
                last->st.scax0 = std::min(last->st.scax0, rx0); last->st.scay0 = std::min(last->st.scay0, ry0);
                last->st.scax1 = std::max(last->st.scax1, rx1); last->st.scay1 = std::max(last->st.scay1, ry1);
                return;
            }
            Draw d; d.kind = 3; d.prog = (uint8_t)g_nativeParse; d.hostPass = 1; d.vcol = vcol;
            // the pass rect: the sprites' union clipped by the scissor
            s.scax0 = std::max<uint16_t>(s.scax0, rx0); s.scay0 = std::max<uint16_t>(s.scay0, ry0); s.scax1 = std::min<uint16_t>(s.scax1, rx1); s.scay1 = std::min<uint16_t>(s.scay1, ry1);
            d.st = s;
            if (s.tme)
            {
                const uint32_t psm = (uint32_t)((c.tex0 >> 20) & 0x3Fu);
                if (psm == 0x13u || psm == 0x14u || psm == 0x1Bu || psm == 0x24u || psm == 0x2Cu)
                {
                    std::array<uint32_t, 256> cl{}; uint32_t bits[16] = {};
                    readClut((uint32_t)((c.tex0 >> 37) & 0x3FFFu), (uint32_t)((c.tex0 >> 51) & 0xFu), (uint32_t)((c.tex0 >> 56) & 0x1Fu), g_r.texa, cl.data(), bits);
                    d.rt = (int32_t)g_list.stepCluts.size(); g_list.stepCluts.push_back(cl);
                }
            }
            noteDrawPages(d.st);
            g_list.draws.push_back(d);
        }
        void kick(bool draw)
        {
            const uint64_t pr = g_r.prmodecont ? g_r.prim : ((g_r.prim & 7u) | (g_r.prmode & ~7ull));
            const uint32_t prim = (uint32_t)(pr & 7u);
            const uint32_t need = prim == 0 ? 1u : (prim == 1 || prim == 2 || prim == 6) ? 2u : 3u;
            if (g_vn < need) return;
            if (draw) ++g_kickCount;   // [hostskip] verify mode
            if (!draw && g_nativeParse >= 0 && !g_inHostGif) captureNativeKick(prim);   // [clutpass]
            if (draw)
            {
                if (g_inHostGif && !(g_haveHost && ((g_pktProgs >> (g_host.prog & 7u)) & 1u)))
                {   // the seam's packet: its kicks draw the pending host mesh, once per distinct state; its own vertices are not drawn
                    // (PS2X_SEAMVK_PKTPROGS=<mask>: programs in the mask draw the packet's own host-transformed vertices instead)
                    if (g_haveHost)
                    {
                        const State &s = currentState(true);
                        if (!g_hostDrawn || !sameDrawState(s, g_hostLast)) emitHostDraw(s);
                    }
                }
                else
                {
                    const State &s = currentState(true);
                    switch (prim)
                    {
                    case 3: case 4: case 5:
                    {
                        State ts = s;
                        if (cpuTriRaster(s, g_vq[0], g_vq[1], g_vq[2])) ts.cpuRastered = 1;
                        emitTriangle(&g_vq[0], &g_vq[1], &g_vq[2], ts);
                        break;
                    }
                    case 6:
                    {   // sprite: v0 top-left, v1 bottom-right; colour, z and fog from v1; texcoords per axis
                        GsVert a = g_vq[0], b = g_vq[1];
                        const bool cpu = cpuSpriteRaster(s, a, b);
                        GsVert tr = b, bl = b;
                        tr.x = b.x; tr.y = a.y; tr.u = b.u; tr.v = a.v; tr.s = b.s; tr.t = a.t;
                        bl.x = a.x; bl.y = b.y; bl.u = a.u; bl.v = b.v; bl.s = a.s; bl.t = b.t;
                        GsVert tl = b; tl.x = a.x; tl.y = a.y; tl.u = a.u; tl.v = a.v; tl.s = a.s; tl.t = a.t; tl.q = a.q;
                        State fs = s; fs.iip = 1;   // colours already flattened to v1's
                        if (cpu) fs.cpuRastered = 1;
                        if (s.tme)
                        {   // [spriterect]
                            auto pk = [](uint32_t lo, uint32_t hi) { return (std::min(lo, 65535u)) | (std::min(hi, 65535u) << 16); };
                            if (s.fst)
                                { g_rectPad[0] = pk(std::min(a.u, b.u), std::min(a.v, b.v)); g_rectPad[1] = pk(std::max(a.u, b.u), std::max(a.v, b.v)); }
                            else
                            {
                                auto fx = [](float v) { const float c = v * 4096.0f; return (uint32_t)std::max(0.0f, std::min(65535.0f, c)); };
                                const float sa = a.q != 0.0f ? a.s / a.q : 0.0f, ta = a.q != 0.0f ? a.t / a.q : 0.0f, sb = b.q != 0.0f ? b.s / b.q : 0.0f, tb = b.q != 0.0f ? b.t / b.q : 0.0f;
                                g_rectPad[0] = pk(fx(std::min(sa, sb)), fx(std::min(ta, tb))); g_rectPad[1] = pk(fx(std::max(sa, sb)), fx(std::max(ta, tb)));
                            }
                            if (g_rectPad[0] == 0u && g_rectPad[1] == 0u) g_rectPad[1] = 1u;   // a rect at the origin is still a rect
                        }
                        g_emitSprite = true;
                        emitTriangle(&tl, &tr, &bl, fs);
                        emitTriangle(&tr, &b, &bl, fs);
                        g_emitSprite = false;
                        g_rectPad[0] = g_rectPad[1] = 0u;
                        break;
                    }
                    default: break;   // points and lines: not drawn (none in BT3's streams)
                    }
                }
            }
            switch (prim)
            {
            case 0: case 1: case 3: case 6: g_vn = 0; break;
            case 2: g_vq[0] = g_vq[1]; g_vn = 1; break;
            case 4: g_vq[0] = g_vq[1]; g_vq[1] = g_vq[2]; g_vn = 2; break;
            case 5: g_vq[1] = g_vq[2]; g_vn = 2; break;
            default: g_vn = 0; break;
            }
        }

        void pushVertex(uint32_t x, uint32_t y, uint32_t z, bool adc)
        {
            GsVert &v = g_vq[g_vn < 3u ? g_vn : 2u];
            v.x = x; v.y = y; v.z = z; v.r = g_r.r; v.g = g_r.g; v.b = g_r.b; v.a = g_r.a; v.q = g_r.q; v.s = g_r.s; v.t = g_r.t; v.u = g_r.u; v.v = g_r.v; v.fog = g_r.fog;
            if (g_vn < 3u) ++g_vn;
            kick(!adc);
        }

        // ---- transfers ---------------------------------------------------------------------
        void startTransfer()
        {
            const uint64_t bb = g_r.bitbltbuf, tp = g_r.trxpos, tr = g_r.trxreg;
            const uint32_t dir = g_r.trxdir & 3u;
            if (dir == 0)
            {
                Xfer &x = g_xfer;
                x.active = true;
                {   // PS2X_SEAMGS_XFERLOG=<game frame>: every host->local transfer of that frame (destination, format, rect)
                    static const uint64_t s_at = [](){ const char *v = std::getenv("PS2X_SEAMGS_XFERLOG"); return v && v[0] ? (uint64_t)std::atoll(v) : 0ull; }();
                    const uint64_t gf = g_bt3FrameCount.load(std::memory_order_relaxed);
                    if (s_at && gf >= s_at && gf < s_at + 4u)
                        std::fprintf(stderr, "[xferlog] frame %llu dbp 0x%x dbw %u dpsm 0x%x at %u,%u size %ux%u\n", (unsigned long long)gf, (unsigned)((bb >> 32) & 0x3FFFu), (unsigned)((bb >> 48) & 0x3Fu), (unsigned)((bb >> 56) & 0x3Fu), (unsigned)((tp >> 32) & 0x7FFu), (unsigned)((tp >> 48) & 0x7FFu), (unsigned)(tr & 0xFFFu), (unsigned)((tr >> 32) & 0xFFFu));
                }
                x.dbp = (uint32_t)((bb >> 32) & 0x3FFFu); x.dbw = (uint32_t)((bb >> 48) & 0x3Fu); x.dpsm = (uint32_t)((bb >> 56) & 0x3Fu);
                x.dsax = (uint32_t)((tp >> 32) & 0x7FFu); x.dsay = (uint32_t)((tp >> 48) & 0x7FFu);
                x.rrw = (uint32_t)(tr & 0xFFFu); x.rrh = (uint32_t)((tr >> 32) & 0xFFFu);
                x.x = 0; x.y = 0; x.carryN = 0; x.bytes = 0; x.repeatBytes = 0;
                ++g_stamp;
                x.lazy = false; x.rec = 0;
                {   // [lazyvram] keep the bytes, stamp the pages now, write later (small transfers -- palettes, border pieces -- stay eager)
                    const uint32_t bpp8 = (x.dpsm == PSMT8 || x.dpsm == PSMT8H) ? 8u : (x.dpsm == PSMT4 || x.dpsm == PSMT4HL || x.dpsm == PSMT4HH) ? 4u : (x.dpsm == PSMCT16 || x.dpsm == PSMCT16S || x.dpsm == PSMZ16 || x.dpsm == PSMZ16S) ? 16u : (x.dpsm == PSMCT24 || x.dpsm == PSMZ24) ? 24u : 32u;
                    const uint64_t total = (uint64_t)x.rrw * x.rrh * bpp8 / 8u;
                    if (g_lazyVram && total > 4096u && x.dbw != 0u && x.rrw <= 2048u && x.rrh <= 2048u)
                    {
                        x.lazy = true; x.rec = g_xlogNext++;
                        XferRec &r = g_xlog[x.rec];
                        r.dbp = x.dbp; r.dbw = x.dbw; r.dpsm = x.dpsm; r.dsax = x.dsax; r.dsay = x.dsay; r.rrw = x.rrw; r.rrh = x.rrh; r.stamp = g_stamp; r.data.reserve((size_t)total);
                        uint32_t pw, ph; pageDims(x.dpsm, pw, ph);
                        forRectPages(x.dpsm, x.dbp, x.dbw, x.dsax, x.dsay, x.rrw, x.rrh, [&](uint32_t p) {
                            g_pageWrite[p] = g_stamp;
                            uint32_t ox, oy; const bool covered = pageOrigin(x.dpsm, x.dbp, x.dbw, p, ox, oy) && ox >= x.dsax && oy >= x.dsay && ox + pw <= x.dsax + x.rrw && oy + ph <= x.dsay + x.rrh;
                            if (covered)
                            {   // earlier pending transfers on this page are overwritten entirely: drop them; this transfer's bytes ARE the page
                                for (uint32_t idx : g_pend[p]) xrecRelease(idx);
                                g_pend[p].clear();
                                coverClear(p); g_pageCover[p] = (int64_t)x.rec; ++r.refs;
                            }
                            else coverClear(p);
                            g_pend[p].push_back((uint32_t)x.rec); ++r.refs;
                        });
                        ++g_lazyCaptured; g_lazyBytes += total;
                        g_texDirty = true;
                    }
                    else if (g_lazyVram && x.rrw != 0u && x.rrh != 0u)
                    {   // eager: pending transfers on its pages land first (stream order), and the pages are no longer one upload's bytes
                        forRectPages(x.dpsm, x.dbp, x.dbw, x.dsax, x.dsay, x.rrw, x.rrh, [&](uint32_t p) { touchPage(p); coverClear(p); });
                    }
                }
            }
            else if (dir == 2)
            {   // local -> local
                const uint32_t sbp = (uint32_t)(bb & 0x3FFFu), sbw = (uint32_t)((bb >> 16) & 0x3Fu), spsm = (uint32_t)((bb >> 24) & 0x3Fu);
                const uint32_t dbp = (uint32_t)((bb >> 32) & 0x3FFFu), dbw = (uint32_t)((bb >> 48) & 0x3Fu), dpsm = (uint32_t)((bb >> 56) & 0x3Fu);
                const uint32_t ssax = (uint32_t)(tp & 0x7FFu), ssay = (uint32_t)((tp >> 16) & 0x7FFu);
                const uint32_t dsax = (uint32_t)((tp >> 32) & 0x7FFu), dsay = (uint32_t)((tp >> 48) & 0x7FFu);
                const uint32_t w = (uint32_t)(tr & 0xFFFu), h = (uint32_t)((tr >> 32) & 0xFFFu);
                ++g_stamp;
                std::vector<uint32_t> tmp(size_t(w) * h);
                for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) tmp[size_t(y) * w + x] = readPixel(spsm, sbp, sbw, (ssax + x) & 0x7FFu, (ssay + y) & 0x7FFu);
                for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) writePixel(dpsm, dbp, dbw, (dsax + x) & 0x7FFu, (dsay + y) & 0x7FFu, tmp[size_t(y) * w + x]);
                g_texDirty = true;
                g_xfer.active = false;
            }
            else g_xfer.active = false;
        }

        void imageData(const uint8_t *d, uint32_t n)
        {
            ScopeMs _sm{g_msImage};
            Xfer &x = g_xfer;
            if (!x.active || x.rrw == 0u) return;
            g_imgBytes += n; g_imgByPsm[x.dpsm & 63u] += n;
            {   // [uploadstat] how much of the stream is byte-identical to the previous upload of the same destination chunk
                uint64_t h = 1469598103934665603ull; for (uint32_t i = 0; i + 8 <= n; i += 8) { uint64_t w; std::memcpy(&w, d + i, 8); h ^= w; h *= 1099511628211ull; }
                const uint64_t key = ((uint64_t)x.dbp << 40) | ((uint64_t)(x.dsay + x.y) << 24) | ((uint64_t)(x.dsax + x.x) << 8) | (n & 0xFFu);
                uint64_t &slot = g_imgHash[key];
                if (slot == h) { g_imgRepeat += n; x.repeatBytes += n; } slot = h;
                x.bytes += n;
                if (x.lazy)
                {   // [lazyvram]
                    if (XferRec *r = xrec(x.rec)) { r->data.insert(r->data.end(), d, d + n); r->hash = (r->hash ^ h) * 1099511628211ull ^ (uint64_t)n; }
                    const uint32_t bpp8 = (x.dpsm == PSMT8 || x.dpsm == PSMT8H) ? 8u : (x.dpsm == PSMT4 || x.dpsm == PSMT4HL || x.dpsm == PSMT4HH) ? 4u : (x.dpsm == PSMCT16 || x.dpsm == PSMCT16S || x.dpsm == PSMZ16 || x.dpsm == PSMZ16S) ? 16u : (x.dpsm == PSMCT24 || x.dpsm == PSMZ24) ? 24u : 32u;
                    const uint64_t px = (uint64_t)x.bytes * 8u / bpp8;
                    x.y = (uint32_t)(px / x.rrw); x.x = (uint32_t)(px % x.rrw);
                    g_texDirty = true;
                    if (x.y >= x.rrh)
                    {
                        x.active = false;
                        static const uint64_t s_at = [](){ const char *v = std::getenv("PS2X_SEAMGS_XFERLOG"); return v && v[0] ? (uint64_t)std::atoll(v) : 0ull; }();
                        const uint64_t gf = g_bt3FrameCount.load(std::memory_order_relaxed);
                        if (s_at && gf >= s_at && gf < s_at + 4u)
                            std::fprintf(stderr, "[xferend] frame %llu dbp 0x%x dbw %u dpsm 0x%x size %ux%u bytes %u repeat %u lazy\n", (unsigned long long)gf, x.dbp, x.dbw, x.dpsm, x.rrw, x.rrh, x.bytes, x.repeatBytes == x.bytes ? 1u : 0u);
                    }
                    return;
                }
            }
            if (g_watch.hi && x.x == 0u && x.y == 0u && x.dbp >= g_watch.lo && x.dbp < g_watch.hi && g_watch.logged < 400u)
            { ++g_watch.logged; std::fprintf(stderr, "[seamgs-watch] frame %llu draw %zu path %u xfer -> dbp 0x%x dbw %u dpsm %u %ux%u at (%u,%u), %u bytes in this packet\n", (unsigned long long)g_frame, g_list.draws.size(), g_curPath, x.dbp, x.dbw, x.dpsm, x.rrw, x.rrh, x.dsax, x.dsay, n); }
            g_texDirty = true;
            auto put = [&](uint32_t v) {
                if (x.y >= x.rrh) return;
                writePixel(x.dpsm, x.dbp, x.dbw, (x.dsax + x.x) & 0x7FFu, (x.dsay + x.y) & 0x7FFu, v);
                if (++x.x >= x.rrw) { x.x = 0; ++x.y; }
            };
            uint32_t i = 0;
            switch (x.dpsm)
            {
            case PSMCT32: case PSMZ32:
                if (g_swz.pair32)
                {   // [swztab] pairs of pixels at even x are adjacent: one address, one 8-byte store, one page stamp
                    for (; i + 4 <= n;)
                    {
                        if (x.y >= x.rrh) { i = n; break; }
                        const uint32_t px = (x.dsax + x.x) & 0x7FFu, py = (x.dsay + x.y) & 0x7FFu;
                        if (i + 8 <= n && (px & 1u) == 0u && x.x + 1u < x.rrw && (px & 63u) != 63u)
                        {
                            const uint32_t a = addr32(x.dbp, x.dbw, px, py);
                            std::memcpy(g_vram + a, d + i, 8); g_pageWrite[a >> 13] = g_stamp;
                            i += 8; x.x += 2u; if (x.x >= x.rrw) { x.x = 0; ++x.y; }
                        }
                        else { uint32_t v; std::memcpy(&v, d + i, 4); put(v); i += 4; }
                    }
                }
                else for (; i + 4 <= n; i += 4) { uint32_t v; std::memcpy(&v, d + i, 4); put(v); }
                break;
            case PSMCT24: case PSMZ24:
            {
                while (i < n)
                {
                    x.carry[x.carryN++] = d[i++];
                    if (x.carryN == 3u) { put((uint32_t)x.carry[0] | ((uint32_t)x.carry[1] << 8) | ((uint32_t)x.carry[2] << 16)); x.carryN = 0; }
                }
                break;
            }
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: for (; i + 2 <= n; i += 2) { uint16_t v; std::memcpy(&v, d + i, 2); put(v); } break;
            case PSMT8: case PSMT8H: for (; i < n; ++i) put(d[i]); break;
            case PSMT4: case PSMT4HL: case PSMT4HH: for (; i < n; ++i) { put(d[i] & 0xFu); put(d[i] >> 4); } break;
            default: for (; i + 4 <= n; i += 4) { uint32_t v; std::memcpy(&v, d + i, 4); put(v); } break;
            }
            if (x.y >= x.rrh)
            {
                x.active = false;
                {   // [xferlog] end of the transfer: bytes and whether every chunk repeated the previous upload of that destination
                    static const uint64_t s_at = [](){ const char *v = std::getenv("PS2X_SEAMGS_XFERLOG"); return v && v[0] ? (uint64_t)std::atoll(v) : 0ull; }();
                    const uint64_t gf = g_bt3FrameCount.load(std::memory_order_relaxed);
                    if (s_at && gf >= s_at && gf < s_at + 4u)
                        std::fprintf(stderr, "[xferend] frame %llu dbp 0x%x dbw %u dpsm 0x%x size %ux%u bytes %u repeat %u\n", (unsigned long long)gf, x.dbp, x.dbw, x.dpsm, x.rrw, x.rrh, x.bytes, x.repeatBytes == x.bytes ? 1u : 0u);
                }
            }
        }

        // ---- registers ---------------------------------------------------------------------
        void writeReg(uint32_t addr, uint64_t v)
        {
            ScopeMs _sm{g_msReg};
            {   // [statedirty] only a state register whose value CHANGES invalidates the draw state: vertex data (RGBAQ, ST, UV, XYZ*,
                // FOG) never does, and the seam rewrites TEST/ALPHA/FRAME... per chunk with the same values (10k rebuilds a frame)
                const uint32_t a = addr & 0xFFu;
                const bool vertexReg = a == 0x01u || a == 0x02u || a == 0x03u || a == 0x04u || a == 0x05u || a == 0x0Au || a == 0x0Cu || a == 0x0Du;
                if (!vertexReg)
                {
                    uint64_t cur; bool known = true;
                    switch (a)
                    {
                    case 0x00: cur = g_r.prim; break;
                    case 0x06: cur = g_r.ctx[0].tex0; break; case 0x07: cur = g_r.ctx[1].tex0; break;
                    case 0x08: cur = g_r.ctx[0].clamp; break; case 0x09: cur = g_r.ctx[1].clamp; break;
                    case 0x14: cur = g_r.ctx[0].tex1; break; case 0x15: cur = g_r.ctx[1].tex1; break;
                    case 0x18: cur = g_r.ctx[0].xyoffset; break; case 0x19: cur = g_r.ctx[1].xyoffset; break;
                    case 0x1A: cur = g_r.prmodecont ? 1u : 0u; v &= 1u; break;
                    case 0x1B: cur = g_r.prmode; break;
                    case 0x3B: cur = g_r.texa; break; case 0x3D: cur = g_r.fogcol; break;
                    case 0x40: cur = g_r.ctx[0].scissor; break; case 0x41: cur = g_r.ctx[1].scissor; break;
                    case 0x42: cur = g_r.ctx[0].alpha; break; case 0x43: cur = g_r.ctx[1].alpha; break;
                    case 0x46: cur = g_r.colclamp; break;
                    case 0x47: cur = g_r.ctx[0].test; break; case 0x48: cur = g_r.ctx[1].test; break;
                    case 0x49: cur = g_r.pabe; break;
                    case 0x4A: cur = g_r.ctx[0].fba; break; case 0x4B: cur = g_r.ctx[1].fba; break;
                    case 0x4C: cur = g_r.ctx[0].frame; break; case 0x4D: cur = g_r.ctx[1].frame; break;
                    case 0x4E: cur = g_r.ctx[0].zbuf; break; case 0x4F: cur = g_r.ctx[1].zbuf; break;
                    default: known = false; cur = 0; break;
                    }
                    if (!known || cur != v) g_stateDirty = true;
                }
            }
            if ((addr >= 0x40u && addr <= 0x41u) || (addr >= 0x4cu && addr <= 0x4du) || addr == 0x3bu)
                if (g_list.regEvents.size() < 4096u) g_list.regEvents.push_back(FrameList::RegEvent{(uint32_t)g_list.draws.size(), g_curPath, (uint8_t)(g_inHostGif ? 1 : 0), (uint8_t)addr, v});
            switch (addr & 0xFFu)
            {
            case 0x00: g_r.prim = v; g_vn = 0; break;
            case 0x01: g_r.r = (uint8_t)v; g_r.g = (uint8_t)(v >> 8); g_r.b = (uint8_t)(v >> 16); g_r.a = (uint8_t)(v >> 24); { uint32_t q = (uint32_t)(v >> 32); std::memcpy(&g_r.q, &q, 4); } break;
            case 0x02: { uint32_t s = (uint32_t)v, t = (uint32_t)(v >> 32); std::memcpy(&g_r.s, &s, 4); std::memcpy(&g_r.t, &t, 4); } break;
            case 0x03: g_r.u = (uint16_t)(v & 0x3FFFu); g_r.v = (uint16_t)((v >> 16) & 0x3FFFu); break;
            case 0x04: g_r.fog = (uint8_t)(v >> 56); pushVertex((uint32_t)(v & 0xFFFFu), (uint32_t)((v >> 16) & 0xFFFFu), (uint32_t)((v >> 32) & 0xFFFFFFu), false); break;
            case 0x05: pushVertex((uint32_t)(v & 0xFFFFu), (uint32_t)((v >> 16) & 0xFFFFu), (uint32_t)(v >> 32), false); break;
            case 0x06: g_r.ctx[0].tex0 = v; g_texDirty = true; break;
            case 0x07: g_r.ctx[1].tex0 = v; g_texDirty = true; break;
            case 0x08: g_r.ctx[0].clamp = v; break;
            case 0x09: g_r.ctx[1].clamp = v; break;
            case 0x0A: g_r.fog = (uint8_t)(v >> 56); break;
            case 0x0C: g_r.fog = (uint8_t)(v >> 56); pushVertex((uint32_t)(v & 0xFFFFu), (uint32_t)((v >> 16) & 0xFFFFu), (uint32_t)((v >> 32) & 0xFFFFFFu), true); break;
            case 0x0D: pushVertex((uint32_t)(v & 0xFFFFu), (uint32_t)((v >> 16) & 0xFFFFu), (uint32_t)(v >> 32), true); break;
            case 0x14: g_r.ctx[0].tex1 = v; break;
            case 0x15: g_r.ctx[1].tex1 = v; break;
            case 0x16: case 0x17:
            {   // TEX2: PSM, CBP, CPSM, CSM, CSA, CLD into TEX0
                Ctx &c = g_r.ctx[addr - 0x16];
                const uint64_t mask = (0x3Full << 20) | (0x3FFFull << 37) | (0xFull << 51) | (1ull << 55) | (0x1Full << 56) | (7ull << 61);
                c.tex0 = (c.tex0 & ~mask) | (v & mask); g_texDirty = true; break;
            }
            case 0x18: g_r.ctx[0].xyoffset = v; break;
            case 0x19: g_r.ctx[1].xyoffset = v; break;
            case 0x1A: g_r.prmodecont = (v & 1u) != 0u; break;
            case 0x1B: g_r.prmode = v; break;
            case 0x1C: g_r.texclut = v; break;
            case 0x3B: g_r.texa = v; g_texDirty = true; break;
            case 0x3D: g_r.fogcol = v; break;
            case 0x3F: break;   // TEXFLUSH
            case 0x40: g_r.ctx[0].scissor = v; break;
            case 0x41: g_r.ctx[1].scissor = v; break;
            case 0x42: g_r.ctx[0].alpha = v; break;
            case 0x43: g_r.ctx[1].alpha = v; break;
            case 0x44: g_r.dimx = v; break;
            case 0x45: g_r.dthe = v; break;
            case 0x46: g_r.colclamp = v; break;
            case 0x47: g_r.ctx[0].test = v; break;
            case 0x48: g_r.ctx[1].test = v; break;
            case 0x49: g_r.pabe = v; break;
            case 0x4A: g_r.ctx[0].fba = v; break;
            case 0x4B: g_r.ctx[1].fba = v; break;
            case 0x4C: g_r.ctx[0].frame = v; { const uint32_t fb = (uint32_t)(v & 0x1FFu) << 5; if (fb == 0u || fb == 0xe00u) { g_lastSceneFbp = fb; g_ws.sceneFbw = (uint32_t)((v >> 16) & 0x3Fu); } } break;
            case 0x4D: g_r.ctx[1].frame = v; break;
            case 0x4E: g_r.ctx[0].zbuf = v; break;
            case 0x4F: g_r.ctx[1].zbuf = v; break;
            case 0x50: g_r.bitbltbuf = v; break;
            case 0x51: g_r.trxpos = v; break;
            case 0x52: g_r.trxreg = v; break;
            case 0x53: g_r.trxdir = (uint32_t)(v & 3u); startTransfer(); break;
            case 0x54: { uint8_t b[8]; std::memcpy(b, &v, 8); imageData(b, 8); break; }   // HWREG
            default: break;
            }
        }

        void packedReg(uint32_t desc, uint64_t lo, uint64_t hi)
        {
            switch (desc)
            {
            case 0x0: g_r.prim = lo & 0x7FFu; g_vn = 0; break;
            case 0x1: g_r.r = (uint8_t)lo; g_r.g = (uint8_t)(lo >> 32); g_r.b = (uint8_t)hi; g_r.a = (uint8_t)(hi >> 32); break;
            case 0x2: { uint32_t s = (uint32_t)lo, t = (uint32_t)(lo >> 32), q = (uint32_t)hi; std::memcpy(&g_r.s, &s, 4); std::memcpy(&g_r.t, &t, 4); std::memcpy(&g_r.q, &q, 4); } break;
            case 0x3: g_r.u = (uint16_t)(lo & 0x3FFFu); g_r.v = (uint16_t)((lo >> 32) & 0x3FFFu); break;
            case 0x4: g_r.fog = (uint8_t)((hi >> 36) & 0xFFu); pushVertex((uint32_t)(lo & 0xFFFFu), (uint32_t)((lo >> 32) & 0xFFFFu), (uint32_t)((hi >> 4) & 0xFFFFFFu), ((hi >> 47) & 1u) != 0u); break;
            case 0x5: pushVertex((uint32_t)(lo & 0xFFFFu), (uint32_t)((lo >> 32) & 0xFFFFu), (uint32_t)hi, ((hi >> 47) & 1u) != 0u); break;
            case 0xA: g_r.fog = (uint8_t)((hi >> 36) & 0xFFu); break;
            case 0xC: g_r.fog = (uint8_t)((hi >> 36) & 0xFFu); pushVertex((uint32_t)(lo & 0xFFFFu), (uint32_t)((lo >> 32) & 0xFFFFu), (uint32_t)((hi >> 4) & 0xFFFFFFu), true); break;
            case 0xD: pushVertex((uint32_t)(lo & 0xFFFFu), (uint32_t)((lo >> 32) & 0xFFFFu), (uint32_t)hi, true); break;
            case 0xE: writeReg((uint32_t)(hi & 0xFFu), lo); break;
            case 0xF: break;
            default: writeReg(desc, lo); break;
            }
        }

        void parse(uint8_t path, const uint8_t *d, uint32_t n)
        {
            PathParse &p = g_path[path & 3u];
            g_curPath = path;
            uint32_t off = 0;
            while (off < n)
            {
                if (p.nloop == 0u)
                {
                    if (off + 16u > n) break;
                    uint64_t lo, hi; std::memcpy(&lo, d + off, 8); std::memcpy(&hi, d + off + 8, 8); off += 16;
                    p.nloop = (uint32_t)(lo & 0x7FFFu); p.eop = ((lo >> 15) & 1u) != 0u; p.flg = (uint32_t)((lo >> 58) & 3u);
                    p.nreg = (uint32_t)((lo >> 60) & 0xFu); if (p.nreg == 0u) p.nreg = 16u; p.regs = hi; p.ri = 0; p.total = p.nloop * p.nreg;
                    if (p.flg == 3u) p.flg = 2u;
                    if (((lo >> 46) & 1u) && p.nloop) { g_r.prim = (lo >> 47) & 0x7FFu; g_vn = 0; }
                    p.vtxOnly = false;
                    if (g_hostSkipMode && g_inHostGif && p.flg == 0u && p.nloop && !(g_haveHost && ((g_pktProgs >> (g_host.prog & 7u)) & 1u)))
                    {   // [hostskip] the seam's own packet: its vertices are not drawn (the host mesh is), only its kicks matter, and a
                        // vertex-only PACKED tag (RGBAQ/ST/UV/XYZ/FOG descriptors) cannot change the draw state between its
                        // kicks -> skip the payload and kick once at its end (100k vertices/frame otherwise walked one by one)
                        bool ok = true, kick = false;
                        for (uint32_t i = 0; i < p.nreg; ++i) { const uint32_t nib = (uint32_t)((hi >> (4u * i)) & 0xFu); if (nib == 4u || nib == 5u) kick = true; else if (!(nib == 1u || nib == 2u || nib == 3u || nib == 0xAu)) ok = false; }
                        p.vtxOnly = ok && kick;
                    }
                    continue;
                }
                if (p.flg == 0u && p.vtxOnly)
                {   // [hostskip] walk the XYZ qwords' ADC bits only, keeping the vertex queue count exactly as pushVertex/kick would
                    // (strip restarts are ADC vertices that never kick); one real kick per tag that kicked at least once
                    const uint32_t remain = p.nloop * p.nreg - p.ri, avail = (n - off) / 16u, take = std::min(remain, avail);
                    const uint64_t pr = g_r.prmodecont ? g_r.prim : ((g_r.prim & 7u) | (g_r.prmode & ~7ull));
                    const uint32_t prim = (uint32_t)(pr & 7u);
                    const uint32_t need = prim == 0 ? 1u : (prim == 1 || prim == 2 || prim == 6) ? 2u : 3u;
                    bool kicked = false; uint32_t ri = p.ri, vn = g_vn;
                    for (uint32_t j = 0; j < take; ++j)
                    {
                        const uint32_t desc = (uint32_t)((p.regs >> (4u * ri)) & 0xFu);
                        if (desc == 4u || desc == 5u)
                        {
                            const bool adc = (d[off + j * 16u + 13u] & 0x80u) != 0u;   // bit 111 of the qword
                            if (vn < 3u) ++vn;
                            if (vn >= need)
                            {
                                if (!adc) kicked = true;
                                switch (prim) { case 2: vn = 1; break; case 4: case 5: vn = 2; break; default: vn = 0; break; }
                            }
                        }
                        if (++ri == p.nreg) ri = 0;
                    }
                    if (g_hostSkipMode == 2)
                    {   // verify: the real per-vertex path over the same qwords must kick iff the simulation did, and leave the same queue count
                        const uint64_t k0 = g_kickCount;
                        for (uint32_t j = 0; j < take; ++j)
                        {
                            uint64_t lo, hi; std::memcpy(&lo, d + off + j * 16u, 8); std::memcpy(&hi, d + off + j * 16u + 8, 8);
                            packedReg((uint32_t)((p.regs >> (4u * p.ri)) & 0xFu), lo, hi);
                            if (++p.ri == p.nreg) { p.ri = 0; --p.nloop; }
                        }
                        off += take * 16u;
                        ++g_hostSkipChecks;
                        if (((g_kickCount != k0) != kicked) || g_vn != vn) { if (g_hostSkipMismatch++ < 8) std::fprintf(stderr, "[hostskip] MISMATCH frame %llu: real kicks %llu sim %d, vn real %u sim %u (prim %u nreg %u take %u)\n", (unsigned long long)g_frame, (unsigned long long)(g_kickCount - k0), (int)kicked, g_vn, vn, prim, p.nreg, take); }
                        if (p.nloop == 0u) { p.ri = 0; p.vtxOnly = false; }
                        if (take < remain) break;
                        continue;
                    }
                    off += take * 16u;
                    const uint32_t idx = p.ri + take; p.nloop -= idx / p.nreg; p.ri = idx % p.nreg;
                    if (kicked) { g_vn = need; kick(true); }
                    g_vn = vn;
                    if (p.nloop == 0u) { p.ri = 0; p.vtxOnly = false; }
                    if (take < remain) break;   // the rest of the tag is in the next packet
                    continue;
                }
                if (p.flg == 2u)
                {
                    const uint32_t take = std::min(p.nloop * 16u, n - off);
                    imageData(d + off, take); off += take;
                    p.nloop -= take / 16u;
                    if (take % 16u) p.nloop = 0;
                    continue;
                }
                if (p.flg == 1u)
                {   // REGLIST: 64-bit registers, nreg per loop; a trailing odd register is padded to the qword
                    if (off + 8u > n) break;
                    uint64_t v; std::memcpy(&v, d + off, 8); off += 8;
                    const uint32_t desc = (uint32_t)((p.regs >> (4u * p.ri)) & 0xFu);
                    if (desc < 0xEu) writeReg(desc, v);
                    if (++p.ri == p.nreg) { p.ri = 0; --p.nloop; }
                    if (p.nloop == 0u && (p.total & 1u)) off += 8;   // an odd register count pads to the qword
                    continue;
                }
                if (off + 16u > n) break;
                uint64_t lo, hi; std::memcpy(&lo, d + off, 8); std::memcpy(&hi, d + off + 8, 8); off += 16;
                packedReg((uint32_t)((p.regs >> (4u * p.ri)) & 0xFu), lo, hi);
                if (++p.ri == p.nreg) { p.ri = 0; --p.nloop; }
            }
        }

        void report()
        {
            static uint64_t s_last = 0;
            if (g_frame - s_last < 300u) return;
            s_last = g_frame;
            std::fprintf(stderr, "[seamgs] per frame: parse %.2f ms (decode %.2f, scratch raster %.2f, host draws %.2f | state %.2f, triangles %.2f, image data %.2f, vram decode lists %.2f, reg writes %.2f)\n", g_msParse / 300.0, g_msDecode / 300.0, g_msRaster / 300.0, g_msHost / 300.0, g_msState / 300.0, g_msTri / 300.0, g_msImage / 300.0, g_msVramDec / 300.0, g_msReg / 300.0);
            extern uint64_t g_packHits, g_packMisses, g_packSkipped, g_packLoads; extern std::unordered_map<std::string, PackLoaded> g_packLoaded;
            if (ps2tex::replacementsEnabled()) std::fprintf(stderr, "[seampack] frame %llu: pack hits %llu (file loads %llu, %zu images) misses %llu skipped %llu\n", (unsigned long long)g_frame, (unsigned long long)g_packHits, (unsigned long long)g_packLoads, g_packLoaded.size(), (unsigned long long)g_packMisses, (unsigned long long)g_packSkipped);
            g_msParse = g_msDecode = g_msRaster = g_msHost = 0; g_msState = g_msTri = g_msImage = g_msVramDec = g_msReg = 0;
            std::fprintf(stderr, "[seamgs] frame %llu: tex lookups %llu (memo hits %llu, state builds %llu, upload KB %llu, repeat KB %llu, ct32 %llu t8 %llu t4 %llu c16 %llu other %llu) decodes %llu (from drawn pages %llu), cache %zu slots, cpu scratch sprites %llu (skipped %llu) tris %llu, rt decodes %llu, gpu decodes %llu (pages unchanged %llu); identical swaps %llu/%llu\n",
                         (unsigned long long)g_frame, (unsigned long long)g_texLookups, (unsigned long long)g_memoHits, (unsigned long long)g_stateBuilds, (unsigned long long)(g_imgBytes >> 10), (unsigned long long)(g_imgRepeat >> 10), (unsigned long long)(g_imgByPsm[0] >> 10), (unsigned long long)(g_imgByPsm[19] >> 10), (unsigned long long)(g_imgByPsm[20] >> 10), (unsigned long long)((g_imgByPsm[2] + g_imgByPsm[10]) >> 10), (unsigned long long)((g_imgBytes - g_imgByPsm[0] - g_imgByPsm[19] - g_imgByPsm[20] - g_imgByPsm[2] - g_imgByPsm[10]) >> 10), (unsigned long long)g_texDecodes, (unsigned long long)g_texStale, g_tex.size(), (unsigned long long)g_cpuSprites, (unsigned long long)g_scratchSkips, (unsigned long long)g_cpuTris, (unsigned long long)g_rtDecodes, (unsigned long long)g_gpuDecodes, (unsigned long long)g_pagesSame, (unsigned long long)g_sameSwaps, (unsigned long long)g_swapsSeen);
            std::memset(g_imgByPsm, 0, sizeof(g_imgByPsm)); g_texLookups = g_memoHits = g_stateBuilds = g_imgBytes = g_imgRepeat = g_texDecodes = g_texStale = g_cpuSprites = g_scratchSkips = g_cpuTris = g_rtDecodes = g_gpuDecodes = g_pagesSame = 0; g_sameSwaps = g_swapsSeen = 0;
            printTargetHist();
            if (g_hostSkipChecks) std::fprintf(stderr, "[hostskip] verify: %llu tags checked, %llu mismatches\n", (unsigned long long)g_hostSkipChecks, (unsigned long long)g_hostSkipMismatch);
            std::fprintf(stderr, "[nativehud] per frame: on %d inv %.3f active %d | mapped %.1f split %.1f scissors %.1f ink dropped %.1f scaled %.1f shadows dropped %.1f\n", g_ws.on ? 1 : 0, g_ws.inv, g_ws.active ? 1 : 0, g_ws.mapped / 300.0, g_ws.split / 300.0, g_ws.scissors / 300.0, g_ws.inkDropped / 300.0, g_ws.inkScaled / 300.0, g_ws.shadowDropped / 300.0);
            g_ws.mapped = g_ws.split = g_ws.scissors = g_ws.inkDropped = g_ws.inkScaled = g_ws.shadowDropped = 0;
            std::fprintf(stderr, "[lazyvram] per frame: %.1f transfers captured (%.0f KB), %.1f pages applied late, %zu pending records | [texcontent] hits %.2f regs %.2f first-sightings %.2f no-key %.2f per frame, %zu images shared\n",
                         g_lazyCaptured / 300.0, g_lazyBytes / 300.0 / 1024.0, g_lazyApplied / 300.0, g_xlog.size(), g_contentHits / 300.0, g_contentRegs / 300.0, g_contentMiss / 300.0, g_contentNoKey / 300.0, g_contentImgs.size());
            g_lazyCaptured = g_lazyBytes = g_lazyApplied = 0; g_contentHits = g_contentRegs = g_contentMiss = g_contentNoKey = 0;
            std::fprintf(stderr, "[seamgs] host draws in/out/dropped per prog:");
            for (int i = 0; i < 5; ++i) std::fprintf(stderr, " %d:%llu/%llu/%llu", i, (unsigned long long)g_hostIn[i], (unsigned long long)g_hostOut[i], (unsigned long long)g_hostDropped[i]);
            std::fprintf(stderr, "\n");
            for (int i = 0; i < 8; ++i) g_hostIn[i] = g_hostOut[i] = g_hostDropped[i] = 0;
        }
    }

    // [dispvram] Movies (and any frame the game builds by DMA image transfers alone) reach the display buffer without a
    // single draw: the IPU output is uploaded straight into the frame buffer and DISPFB shows it. The renderer composes
    // from its render targets, which such uploads never touch (black). At the swap: if every page of a circuit's display
    // buffer was written by an upload more recently than any draw touched it, queue a decode of the buffer from the GPU
    // copy of VRAM (the [gpudecode] path) into a per-circuit slot; the circuit blit samples that slot instead.
    static int32_t g_dispSlot[2] = { -1, -1 };
    static void displayDecodeLocked(int i, uint64_t dispfb)
    {
        const uint32_t fbp = (uint32_t)(dispfb & 0x1FFu) << 5, fbw = (uint32_t)((dispfb >> 9) & 0x3Fu), psm = (uint32_t)((dispfb >> 15) & 0x1Fu);
        if (fbw == 0u || fbw > 16u) return;
        const uint32_t w = fbw * 64u, h = 448u;
        bool uploaded = false, drawnAfter = false;
        std::vector<std::pair<uint16_t, uint32_t>> pages;
        for (uint32_t y = 0; y < h; y += 32u)
            for (uint32_t x = 0; x < w; x += 64u)
            {
                const uint32_t p = pageOf(psm, fbp, fbw, x, y);
                if (p >= kPages) continue;
                if (g_pageWrite[p] != 0u && g_pageWrite[p] > g_pageDrawn[p]) uploaded = true;
                if (g_pageDrawn[p] != 0u && g_pageDrawn[p] >= g_pageWrite[p]) drawnAfter = true;
                bool dup = false; for (const auto &q : pages) if (q.first == p) { dup = true; break; }
                if (!dup) pages.emplace_back((uint16_t)p, g_pageWrite[p]);
            }
        if (!uploaded || drawnAfter) return;   // the targets hold the frame (the normal case)
        if (g_dispSlot[i] < 0) { g_dispSlot[i] = (int32_t)g_tex.size(); g_tex.emplace_back(); }   // outside the texture cache: never evicted
        uint32_t tw = 6; while ((1u << tw) < w && tw < 10u) ++tw;
        TexEntry e; e.w = 1u << tw; e.h = 512u; e.pages = std::move(pages); e.gpuDecode = true;
        const uint64_t tex0 = (uint64_t)fbp | ((uint64_t)fbw << 14) | ((uint64_t)psm << 20) | ((uint64_t)tw << 26) | (9ull << 30);
        vramDecodeFor(g_dispSlot[i], e, tex0);
        g_list.dispSlot[i] = g_dispSlot[i]; g_list.dispFbp[i] = fbp;
    }

    void takeFrame(FrameList &out, const uint64_t dispfb[2], uint32_t enMask)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (int i = 0; i < 2; ++i) if (enMask & (1u << i)) displayDecodeLocked(i, dispfb[i]);
        ++g_frame;
        wsFrameEnd();   // [nativehud]
        ++g_swapsSeen; if (g_swapHash == g_lastSwapHash) ++g_sameSwaps; g_lastSwapHash = g_swapHash; g_swapHash = 1469598103934665603ull; g_swapBytes = 0;
        {   // a fight frame: > 1500 GS draws (the kernel + packet path) or > 200 host meshes (skip mode)
            size_t host = 0; for (const Draw &d : g_list.draws) if (d.kind == 1) ++host;
            if (g_list.draws.size() > 1500u || host > 200u) ++g_busyFrames;
        }
        out.busy = g_busyFrames;
        out.gameFrame = g_streamGameFrame; out.fightTick = g_streamFightTick;   // [fighttick]
        evictTextures();
        g_list.frame = g_frame;
        out.draws.swap(g_list.draws); out.verts.swap(g_list.verts); out.texUploads.swap(g_list.texUploads); out.texFrees.swap(g_list.texFrees); out.rtDecodes.swap(g_list.rtDecodes); out.sharedDrops.swap(g_list.sharedDrops); out.vramPages.swap(g_list.vramPages); out.vramBytes.swap(g_list.vramBytes); out.stepCluts.swap(g_list.stepCluts); out.regEvents.swap(g_list.regEvents);
        out.frame = g_frame;
        for (int i = 0; i < 2; ++i) { out.dispSlot[i] = g_list.dispSlot[i]; out.dispFbp[i] = g_list.dispFbp[i]; g_list.dispSlot[i] = -1; g_list.dispFbp[i] = ~0u; }   // [dispvram]
        g_list.draws.clear(); g_list.verts.clear(); g_list.texUploads.clear(); g_list.texFrees.clear(); g_list.rtDecodes.clear(); g_list.sharedDrops.clear(); g_list.vramPages.clear(); g_list.vramBytes.clear(); g_list.stepCluts.clear(); g_list.regEvents.clear();
        g_texFree.insert(g_texFree.end(), g_retired.begin(), g_retired.end()); g_retired.clear();
        g_texDirty = true;   // a new frame: re-resolve (the renderer may have dropped slots)
        seamgs::g_scratchSeqIdx = 0; seamgs::g_scratchSeqOk = true;   // [scratchrepeat] a new sequence
        report();
    }

    void dropAllTextures()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_tex.clear(); g_texFree.clear(); g_texByKey.clear(); g_texDirty = true; g_curTex = -1;
    }
}

namespace seamvk
{
    void onGifPacket(uint8_t path, const uint8_t *data, uint32_t size, bool hostGif)
    {
        {   // [swaphash] cheap stream identity: fnv over path, size and a 64-byte stride of the payload
            uint64_t h = seamgs::g_swapHash; h ^= path; h *= 1099511628211ull; h ^= size; h *= 1099511628211ull;
            for (uint32_t o = 0; o + 8 <= size; o += 64) { uint64_t v; std::memcpy(&v, data + o, 8); h ^= v; h *= 1099511628211ull; }
            seamgs::g_swapHash = h; seamgs::g_swapBytes += size;
        }
        if (!on() || !data || size == 0u) return;
        std::lock_guard<std::mutex> lk(seamgs::g_mtx);
        const bool prof = seamgs::profOn();   // [timers] a clock read per packet costs 2 % of the process: only when profiling
        const auto t0 = prof ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        seamgs::g_inHostGif = hostGif;
        seamgs::parse(path, data, size);
        seamgs::g_inHostGif = false;
        if (prof) seamgs::g_msParse += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        // The seam submits 'SVKD' and its 'SVKG' packets back to back from the kick thread, but the game thread's PATH3
        // uploads can land between them in the arbiter queue: they do not end the pairing. Only a new 'SVKD' does.
    }

    bool nativeStepGeneric(int step) { return step == 1 || step == 3 || step == 6 || step == 7; }   // [clutpass] ink + Kaioken mask, haze + blur weight, alpha-keyed 0x3e80, far tint 0x3e94
    void nativeParse(int step) { seamgs::g_nativeParse = step; }
    bool nativeHudOn() { return seamgs::g_nativeHud; }
    void onNativeStep(int step)
    {
        if (!on() || nativeStepGeneric(step)) return;
        std::lock_guard<std::mutex> lk(seamgs::g_mtx);
        static uint64_t s_lastFrame[8] = {};   // once per frame: the step's packets interleave with other owners' (uploads, decodes), so the arbiter sees several runs
        // Step 5 (outline) runs at EVERY run of its packets: the game's alpha clear (FUN_00106ba8) sits between the mask
        // write and the read-backs, so only a pass placed at the second run survives to the ink draw.
        if (step != 5 && step >= 0 && step < 8 && s_lastFrame[step] == seamgs::g_frame + 1u) return;
        if (step >= 0 && step < 8) s_lastFrame[step] = seamgs::g_frame + 1u;
        seamgs::Draw d; d.kind = 3; d.prog = (uint8_t)step;
        d.st.fbp = (uint32_t)(seamgs::g_r.ctx[0].frame & 0x1FFu) << 5; d.st.fbw = (uint32_t)((seamgs::g_r.ctx[0].frame >> 16) & 0x3Fu);
        d.st.tex0lo = seamgs::g_lastSceneFbp;   // [ztopnative] step 2 reads the scene buffer's alpha (its TEX0 may be in the not-yet-parsed packet)
        d.st.zbp = (uint32_t)(seamgs::g_r.ctx[0].zbuf & 0x1FFu) << 5;
        if (step == 5)
        {   // the palette the step reads (0x3e8c depth ramp for the outline) as it is NOW: the GPU thread runs the pass later,
            // when the mirror may hold another frame's palette
            std::array<uint32_t, 256> cl{}; uint32_t bits[16] = {};
            seamgs::readClut(0x3e8cu, 0u, 0u, seamgs::g_r.texa, cl.data(), bits);
            d.rt = (int32_t)seamgs::g_list.stepCluts.size(); seamgs::g_list.stepCluts.push_back(cl);
            {   // PS2X_SEAMGS_STEPCLUTLOG=1: what the step's palette holds at the marker (stale if the packet itself uploads it)
                static const bool s_log = [](){ const char *v = std::getenv("PS2X_SEAMGS_STEPCLUTLOG"); return v && v[0] && v[0] != '0'; }(); static uint64_t s_lastF = ~0ull;
                if (s_log && step == 3 && (seamgs::g_frame % 300u) == 0u && s_lastF != seamgs::g_frame)
                {
                    s_lastF = seamgs::g_frame; unsigned nz = 0; for (uint32_t i = 0; i < 256; ++i) if (cl[i] >> 24) ++nz;
                    std::fprintf(stderr, "[stepclut] frame %llu step 3 CLUT 0x3e84: %u entries with alpha; a[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x a[15..25]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x stamp %u\n",
                                 (unsigned long long)seamgs::g_frame, nz, cl[0]>>24, cl[1]>>24, cl[2]>>24, cl[3]>>24, cl[4]>>24, cl[5]>>24, cl[6]>>24, cl[7]>>24, cl[15]>>24, cl[16]>>24, cl[17]>>24, cl[18]>>24, cl[19]>>24, cl[20]>>24, cl[21]>>24, cl[22]>>24, cl[23]>>24, cl[24]>>24, cl[25]>>24, seamgs::g_pageWrite[seamgs::addr32(0x3e84u, 1, 0, 0) >> 13]);
                }
            }
        }
        if (step == 4)
        {   // [postnative] the native pass draws the 2:1 buffer at 0x2a00 (fbw 4, 256x224): mark it drawn so the blur chain's reads decode from the target
            seamgs::State gs; gs.fbp = 0x2a00u; gs.fbw = 4u; gs.scax0 = 0; gs.scax1 = 255; gs.scay0 = 0; gs.scay1 = 223;
            seamgs::noteDrawPages(gs);
        }
        seamgs::g_list.draws.push_back(d);
    }
    void onHostDraw(const uint8_t *data, uint32_t size)
    {
        if (!on() || size < sizeof(DrawPacket)) return;
        DrawPacket k; std::memcpy(&k, data, sizeof(k));
        if (k.magic != kDrawMagic || k.count < 3u || size < sizeof(DrawPacket) + size_t(k.count) * k.stride) return;
        std::lock_guard<std::mutex> lk(seamgs::g_mtx);
        const bool prof = seamgs::profOn();
        const auto t0 = prof ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        struct T { std::chrono::steady_clock::time_point t; bool on; ~T() { if (on) seamgs::g_msHost += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); } } tt{t0, prof};
        if (seamgs::g_haveHost && !seamgs::g_hostDrawn) ++seamgs::g_hostDropped[seamgs::g_host.prog & 7u];
        ++seamgs::g_hostIn[k.prog & 7u];
        seamgs::g_host = k; seamgs::g_haveHost = true; seamgs::g_hostDrawn = false; seamgs::g_hostPassN = 0;
        seamgs::g_hostVerts.assign(data + sizeof(DrawPacket), data + sizeof(DrawPacket) + size_t(k.count) * k.stride);
    }
}

namespace seamgs
{
    bool peekTexel(int32_t slot, uint32_t x, uint32_t y, uint32_t &rgba)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (slot < 0 || (size_t)slot >= g_tex.size()) return false;
        const TexEntry &e = g_tex[slot]; if (e.rgbaCopy.empty() || x >= e.w || y >= e.h) return false;
        std::memcpy(&rgba, e.rgbaCopy.data() + (size_t(y) * e.w + x) * 4u, 4); return true;
    }
    bool dumpMirror(const char *path)
    {   // diagnostics: the whole VRAM mirror, same layout as the backend's VRAM (compare with ps2x_pgs::dumpVramRaw)
        if (!seamvk::on()) return false;
        std::lock_guard<std::mutex> lk(g_mtx);
        FILE *f = std::fopen(path, "wb"); if (!f) return false;
        for (uint32_t p = 0; p < kPages; ++p) touchPage(p);   // [lazyvram]
        std::fwrite(g_vram, 1, kVramBytes, f); std::fclose(f); return true;
    }
    bool peekClut(uint32_t cbp, uint32_t cpsm, uint32_t *out256)
    {
        if (!seamvk::on()) return false;
        std::lock_guard<std::mutex> lk(g_mtx);
        uint32_t bits[16] = {};
        readClut(cbp, cpsm, 0, g_r.texa, out256, bits);
        return true;
    }
}
extern "C" void ps2xSeamFlipStamp(unsigned long long gameFrame, unsigned long long)
{   // [fighttick] from the stream-ordered flip job (GS thread): the game frame (jitters by one: the flip is a vblank handler)
    seamgs::g_streamGameFrame = gameFrame;
}
extern "C" void ps2xSeamTickMark(unsigned long long tick)
{   // [fighttick] the fight update's own stream marker: deterministic
    seamgs::g_streamFightTick = tick;
}
