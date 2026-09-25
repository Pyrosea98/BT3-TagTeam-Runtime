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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

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
        uint32_t g_pageDrawn[kPages];   // stamp at the last DRAW into the page (pixels the mirror never sees)
        uint32_t g_stamp = 1;           // bumped per transfer
        uint64_t g_frame = 0;

        enum : uint32_t { PSMCT32 = 0, PSMCT24 = 1, PSMCT16 = 2, PSMCT16S = 10, PSMT8 = 19, PSMT4 = 20, PSMT8H = 27, PSMT4HL = 36, PSMT4HH = 44,
                          PSMZ32 = 48, PSMZ24 = 49, PSMZ16 = 50, PSMZ16S = 58 };

        inline uint32_t addr32(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) { return GSPSMCT32::addrPSMCT32(block, bw, x, y) & kVramMask; }
        inline uint32_t addr16(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y)
        {
            switch (psm)
            {
            case PSMCT16S: return GSPSMCT16::addrPSMCT16S(block, bw, x, y) & kVramMask;
            case PSMZ16:   return GSPSMCT16::addrPSMZ16(block, bw, x, y) & kVramMask;
            case PSMZ16S:  return GSPSMCT16::addrPSMZ16S(block, bw, x, y) & kVramMask;
            default:       return GSPSMCT16::addrPSMCT16(block, bw, x, y) & kVramMask;
            }
        }
        inline uint32_t addr8(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) { return GSPSMT8::addrPSMT8(block, bw, x, y) & kVramMask; }
        inline uint32_t addr4(uint32_t block, uint32_t bw, uint32_t x, uint32_t y) { return GSPSMT4::addrPSMT4(block, bw, x, y) & (kVramMask * 2u + 1u); }   // nibble address

        inline uint32_t rd32(uint32_t a) { uint32_t v; std::memcpy(&v, g_vram + a, 4); return v; }
        inline void wr32(uint32_t a, uint32_t v) { std::memcpy(g_vram + a, &v, 4); g_pageWrite[a >> 13] = g_stamp; }
        inline uint16_t rd16(uint32_t a) { uint16_t v; std::memcpy(&v, g_vram + a, 2); return v; }
        inline void wr16(uint32_t a, uint16_t v) { std::memcpy(g_vram + a, &v, 2); g_pageWrite[a >> 13] = g_stamp; }
        inline void wr8(uint32_t a, uint8_t v) { g_vram[a] = v; g_pageWrite[a >> 13] = g_stamp; }

        // Read one texel/pixel as a raw 32-bit value in the format's natural width.
        inline uint32_t readPixel(uint32_t psm, uint32_t block, uint32_t bw, uint32_t x, uint32_t y)
        {
            switch (psm)
            {
            case PSMCT32: case PSMZ32: return rd32(addr32(block, bw, x, y));
            case PSMCT24: case PSMZ24: return rd32(addr32(block, bw, x, y)) & 0xFFFFFFu;
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return rd16(addr16(psm, block, bw, x, y));
            case PSMT8: return g_vram[addr8(block, bw, x, y)];
            case PSMT4: { const uint32_t a = addr4(block, bw, x, y); return (g_vram[a >> 1] >> ((a & 1u) * 4u)) & 0xFu; }
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
                          wr8(a >> 1, (uint8_t)((g_vram[a >> 1] & ~(0xFu << sh)) | ((v & 0xFu) << sh))); break; }
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

        struct PathParse { uint32_t nloop = 0, nreg = 1, ri = 0, flg = 0, total = 0; uint64_t regs = 0; bool eop = false; };
        PathParse g_path[4];

        struct Xfer
        {
            bool active = false;
            uint32_t dbp = 0, dbw = 0, dpsm = 0, dsax = 0, dsay = 0, rrw = 0, rrh = 0, x = 0, y = 0;
            uint8_t carry[4]; uint32_t carryN = 0;   // CT24: pixels straddle packet boundaries
        };
        Xfer g_xfer;

        // ---- vertex queue ------------------------------------------------------------------
        struct GsVert { uint32_t x, y, z; uint8_t r, g, b, a; float q, s, t; uint16_t u, v; uint8_t fog; };
        GsVert g_vq[3]; uint32_t g_vn = 0;

        // ---- frame list --------------------------------------------------------------------
        std::mutex g_mtx;
        FrameList g_list;
        bool g_haveHost = false; seamvk::DrawPacket g_host; std::vector<uint8_t> g_hostVerts;
        bool g_inHostGif = false;

        // ---- texture cache -----------------------------------------------------------------
        struct TexEntry
        {
            uint64_t key = 0; uint32_t w = 0, h = 0; uint64_t lastUse = 0; bool used = false;
            std::vector<std::pair<uint16_t, uint32_t>> pages;   // (page, write stamp seen)
            bool drawnPages = false;                            // read pages the game had drawn into (stale mirror)
        };
        std::vector<TexEntry> g_tex;
        std::vector<int32_t> g_texFree;
        std::unordered_map<uint64_t, int32_t> g_texByKey;
        bool g_texDirty = true; int32_t g_curTex = -1; uint32_t g_curTexW = 0, g_curTexH = 0;
        uint64_t g_texLookups = 0, g_texDecodes = 0, g_texStale = 0;
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
                    if (cpsm == PSMCT24) v = (v & 0xFFFFFFu) | (ta0 << 24);
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
        bool decodeTexture(uint64_t tex0, uint64_t texa, TexEntry &e, std::vector<uint8_t> &rgba)
        {
            const uint32_t tbp = (uint32_t)(tex0 & 0x3FFFu), tbw = (uint32_t)((tex0 >> 14) & 0x3Fu), psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            const uint32_t tw = (uint32_t)((tex0 >> 26) & 0xFu), th = (uint32_t)((tex0 >> 30) & 0xFu);
            const uint32_t cbp = (uint32_t)((tex0 >> 37) & 0x3FFFu), cpsm = (uint32_t)((tex0 >> 51) & 0xFu), csa = (uint32_t)((tex0 >> 56) & 0x1Fu);
            const uint32_t w = 1u << std::min(tw, 10u), h = 1u << std::min(th, 10u);
            const uint32_t ta0 = (uint32_t)(texa & 0xFFu), aem = (uint32_t)((texa >> 15) & 1u), ta1 = (uint32_t)((texa >> 32) & 0xFFu);
            e.w = w; e.h = h;
            rgba.resize(size_t(w) * h * 4u);
            uint32_t *dst = reinterpret_cast<uint32_t *>(rgba.data());
            uint32_t pagesBits[16] = {};
            const bool indexed = psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH;
            uint32_t clut[256];
            if (indexed) readClut(cbp, cpsm, csa, texa, clut, pagesBits);
            const uint32_t csaOff = (psm == PSMT4 || psm == PSMT4HL || psm == PSMT4HH) ? (csa & 15u) * 16u : 0u;
            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    uint32_t v;
                    switch (psm)
                    {
                    case PSMT8: { const uint32_t a = addr8(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[g_vram[a]]; break; }
                    case PSMT4: { const uint32_t a = addr4(tbp, tbw, x, y); const uint32_t pg = a >> 14; pagesBits[pg >> 5] |= 1u << (pg & 31u);
                                  v = clut[(csaOff + ((g_vram[a >> 1] >> ((a & 1u) * 4u)) & 0xFu)) & 255u]; break; }
                    case PSMT8H: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[rd32(a) >> 24]; break; }
                    case PSMT4HL: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[(csaOff + ((rd32(a) >> 24) & 0xFu)) & 255u]; break; }
                    case PSMT4HH: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = clut[(csaOff + (rd32(a) >> 28)) & 255u]; break; }
                    case PSMCT24: case PSMZ24: { const uint32_t a = addr32(tbp, tbw, x, y); pagesBits[(a >> 13) >> 5] |= 1u << ((a >> 13) & 31u); v = (rd32(a) & 0xFFFFFFu) | (ta0 << 24); break; }
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
                    dst[size_t(y) * w + x] = v;
                }
            }
            e.pages.clear(); e.drawnPages = false;
            for (uint32_t p = 0; p < kPages; ++p)
                if (pagesBits[p >> 5] & (1u << (p & 31u)))
                {
                    e.pages.emplace_back((uint16_t)p, g_pageWrite[p]);
                    if (g_pageDrawn[p] >= g_pageWrite[p] && g_pageDrawn[p] != 0u) e.drawnPages = true;
                }
            return true;
        }

        int32_t resolveTexture(const Ctx &c)
        {
            ++g_texLookups;
            const uint64_t tex0 = c.tex0;
            const uint32_t psm = (uint32_t)((tex0 >> 20) & 0x3Fu);
            const bool needTexa = psm == PSMCT16 || psm == PSMCT16S || psm == PSMCT24 || psm == PSMZ16 || psm == PSMZ16S || psm == PSMZ24
                               || ((psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH) && (((tex0 >> 51) & 0xFu) != PSMCT32));
            uint64_t key = fnv(&tex0, 8);
            if (needTexa) key = fnv(&g_r.texa, 8, key);
            auto it = g_texByKey.find(key);
            if (it != g_texByKey.end())
            {
                TexEntry &e = g_tex[it->second];
                bool valid = true;
                for (const auto &pg : e.pages) if (g_pageWrite[pg.first] != pg.second) { valid = false; break; }
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
            decodeTexture(tex0, g_r.texa, e, rgba);
            ++g_texDecodes;
            if (e.drawnPages) ++g_texStale;
            {   // PS2X_SEAMVK_TEXDUMP=<dir>: every first decode as a PPM (rgb) + PGM (alpha), named by its TEX0 fields
                static const char *s_dir = std::getenv("PS2X_SEAMVK_TEXDUMP"); static int s_n = 0;
                if (s_dir && s_dir[0] && s_n < 400)
                {
                    char path[512];
                    std::snprintf(path, sizeof(path), "%s/t%03d_f%llu_psm%u_%ux%u_tbp%x_tbw%u_cbp%x_cpsm%u_csa%u.ppm", s_dir, s_n, (unsigned long long)g_frame, psm, e.w, e.h,
                                  (unsigned)(tex0 & 0x3FFFu), (unsigned)((tex0 >> 14) & 0x3Fu), (unsigned)((tex0 >> 37) & 0x3FFFu), (unsigned)((tex0 >> 51) & 0xFu), (unsigned)((tex0 >> 56) & 0x1Fu));
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
            g_list.texUploads.push_back(TexUpload{ slot, e.w, e.h, std::move(rgba) });
            g_curTexW = e.w; g_curTexH = e.h;
            return slot;
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
        State currentState(bool tex)
        {
            State s;
            const uint64_t pr = g_r.prmodecont ? g_r.prim : ((g_r.prim & 7u) | (g_r.prmode & ~7ull));
            s.prim = (uint8_t)(pr & 7u); s.iip = (pr >> 3) & 1u; s.tme = (pr >> 4) & 1u; s.fge = (pr >> 5) & 1u;
            s.abe = (pr >> 6) & 1u; s.aa1 = (pr >> 7) & 1u; s.fst = (pr >> 8) & 1u; s.ctxt = (pr >> 9) & 1u;
            const Ctx &c = g_r.ctx[s.ctxt];
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
            }
            return s;
        }

        inline bool sameState(const State &a, const State &b) { return std::memcmp(&a, &b, sizeof(State)) == 0; }

        void noteDrawPages(const State &s)
        {   // the pages a draw into this target can touch (whole rows of pages: cheap and conservative)
            const uint32_t pagesPerRow = s.fbw ? s.fbw : 1u;
            const uint32_t rows = (s.fpsm == PSMCT16 || s.fpsm == PSMCT16S) ? 7u : 14u;   // 448 lines / (64 or 32) rows per page
            const uint32_t p0 = s.fbp / 32u;
            for (uint32_t p = p0; p < p0 + pagesPerRow * rows && p < kPages; ++p) g_pageDrawn[p] = g_stamp;
            if (s.zte && !s.zmsk) { const uint32_t z0 = s.zbp / 32u; for (uint32_t p = z0; p < z0 + pagesPerRow * 14u && p < kPages; ++p) g_pageDrawn[p] = g_stamp; }
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

        void emitTriangle(const GsVert *v0, const GsVert *v1, const GsVert *v2, const State &s)
        {
            const GsVert *vs[3] = { v0, v1, v2 };
            Vtx out[3];
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
                o.fog = float(g.fog) / 255.0f; o.pad[0] = o.pad[1] = 0;
            }
            if (!g_list.draws.empty() && g_list.draws.back().kind == 0 && sameState(g_list.draws.back().st, s))
                g_list.draws.back().count += 3;
            else
            {
                Draw d; d.kind = 0; d.st = s; d.vertOff = (uint32_t)g_list.verts.size(); d.count = 3; d.stride = sizeof(Vtx);
                g_list.draws.push_back(d);
                noteDrawPages(s);
            }
            const uint8_t *b = reinterpret_cast<const uint8_t *>(out);
            g_list.verts.insert(g_list.verts.end(), b, b + sizeof(out));
        }

        void emitHostDraw(const State &s)
        {
            Draw d; d.kind = 1; d.prog = g_host.prog; d.st = s; d.c = g_host.c;
            d.vertOff = (uint32_t)g_list.verts.size(); d.count = g_host.count; d.stride = g_host.stride;
            g_list.verts.insert(g_list.verts.end(), g_hostVerts.begin(), g_hostVerts.end());
            g_list.draws.push_back(d);
            noteDrawPages(s);
            g_haveHost = false;
        }

        void kick(bool draw)
        {
            const uint64_t pr = g_r.prmodecont ? g_r.prim : ((g_r.prim & 7u) | (g_r.prmode & ~7ull));
            const uint32_t prim = (uint32_t)(pr & 7u);
            const uint32_t need = prim == 0 ? 1u : (prim == 1 || prim == 2 || prim == 6) ? 2u : 3u;
            if (g_vn < need) return;
            if (draw)
            {
                if (g_inHostGif)
                {   // the seam's packet: its first kick draws the pending host mesh with this state; its own vertices are not drawn
                    if (g_haveHost) emitHostDraw(currentState(true));
                }
                else
                {
                    const State s = currentState(true);
                    switch (prim)
                    {
                    case 3: case 4: case 5: emitTriangle(&g_vq[0], &g_vq[1], &g_vq[2], s); break;
                    case 6:
                    {   // sprite: v0 top-left, v1 bottom-right; colour, z and fog from v1; texcoords per axis
                        GsVert a = g_vq[0], b = g_vq[1];
                        GsVert tr = b, bl = b;
                        tr.x = b.x; tr.y = a.y; tr.u = b.u; tr.v = a.v; tr.s = b.s; tr.t = a.t;
                        bl.x = a.x; bl.y = b.y; bl.u = a.u; bl.v = b.v; bl.s = a.s; bl.t = b.t;
                        GsVert tl = b; tl.x = a.x; tl.y = a.y; tl.u = a.u; tl.v = a.v; tl.s = a.s; tl.t = a.t; tl.q = a.q;
                        State fs = s; fs.iip = 1;   // colours already flattened to v1's
                        emitTriangle(&tl, &tr, &bl, fs);
                        emitTriangle(&tr, &b, &bl, fs);
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
                x.dbp = (uint32_t)((bb >> 32) & 0x3FFFu); x.dbw = (uint32_t)((bb >> 48) & 0x3Fu); x.dpsm = (uint32_t)((bb >> 56) & 0x3Fu);
                x.dsax = (uint32_t)((tp >> 32) & 0x7FFu); x.dsay = (uint32_t)((tp >> 48) & 0x7FFu);
                x.rrw = (uint32_t)(tr & 0xFFFu); x.rrh = (uint32_t)((tr >> 32) & 0xFFFu);
                x.x = 0; x.y = 0; x.carryN = 0;
                ++g_stamp;
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
            Xfer &x = g_xfer;
            if (!x.active || x.rrw == 0u) return;
            g_texDirty = true;
            auto put = [&](uint32_t v) {
                if (x.y >= x.rrh) return;
                writePixel(x.dpsm, x.dbp, x.dbw, (x.dsax + x.x) & 0x7FFu, (x.dsay + x.y) & 0x7FFu, v);
                if (++x.x >= x.rrw) { x.x = 0; ++x.y; }
            };
            uint32_t i = 0;
            switch (x.dpsm)
            {
            case PSMCT32: case PSMZ32: for (; i + 4 <= n; i += 4) { uint32_t v; std::memcpy(&v, d + i, 4); put(v); } break;
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
            if (x.y >= x.rrh) x.active = false;
        }

        // ---- registers ---------------------------------------------------------------------
        void writeReg(uint32_t addr, uint64_t v)
        {
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
            case 0x4C: g_r.ctx[0].frame = v; break;
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
            std::fprintf(stderr, "[seamgs] frame %llu: tex lookups %llu decodes %llu (from drawn pages %llu), cache %zu slots\n",
                         (unsigned long long)g_frame, (unsigned long long)g_texLookups, (unsigned long long)g_texDecodes, (unsigned long long)g_texStale, g_tex.size());
            g_texLookups = g_texDecodes = g_texStale = 0;
        }
    }

    void takeFrame(FrameList &out)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        ++g_frame;
        evictTextures();
        g_list.frame = g_frame;
        out.draws.swap(g_list.draws); out.verts.swap(g_list.verts); out.texUploads.swap(g_list.texUploads); out.texFrees.swap(g_list.texFrees);
        out.frame = g_frame;
        g_list.draws.clear(); g_list.verts.clear(); g_list.texUploads.clear(); g_list.texFrees.clear();
        g_texFree.insert(g_texFree.end(), g_retired.begin(), g_retired.end()); g_retired.clear();
        g_texDirty = true;   // a new frame: re-resolve (the renderer may have dropped slots)
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
        if (!on() || !data || size == 0u) return;
        std::lock_guard<std::mutex> lk(seamgs::g_mtx);
        seamgs::g_inHostGif = hostGif;
        seamgs::parse(path, data, size);
        seamgs::g_inHostGif = false;
        if (hostGif) seamgs::g_haveHost = false;   // a host packet without a kick draws nothing
    }

    void onHostDraw(const uint8_t *data, uint32_t size)
    {
        if (!on() || size < sizeof(DrawPacket)) return;
        DrawPacket k; std::memcpy(&k, data, sizeof(k));
        if (k.magic != kDrawMagic || k.count < 3u || size < sizeof(DrawPacket) + size_t(k.count) * k.stride) return;
        std::lock_guard<std::mutex> lk(seamgs::g_mtx);
        seamgs::g_host = k; seamgs::g_haveHost = true;
        seamgs::g_hostVerts.assign(data + sizeof(DrawPacket), data + sizeof(DrawPacket) + size_t(k.count) * k.stride);
    }
}
