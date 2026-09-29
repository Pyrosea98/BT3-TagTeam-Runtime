// [netmenu] Force-decode the QRS textures of a (decompressed) BT3 file through the RUNTIME's own
// PSMT8 reader -- GSMem::ReadRowP8, the exact swizzle the game's decodes use -- plus the 16-bit
// CLUT, and write the result as PNGs. No guessing at the swizzle: this reuses the proven path.
//
// Trigger: PS2X_NETMENU_TEXDUMP=<file>:<outDir> (the file is a decompressed buffer such as
// dumps\netmenu\decompressed_net_460.bin). Runs once, from the net-menu tick.
#include "gfx/image_io.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace GSMem
{
    void ReadRowP8(const unsigned char *data, unsigned bp, unsigned bw, unsigned x0, unsigned x1,
                   unsigned y, unsigned char *dst);
}

namespace
{
    bool s_ran = false;

    inline uint32_t rd32(const std::vector<uint8_t> &b, size_t o)
    {
        return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
    }

    bool isQrs(const std::vector<uint8_t> &b, size_t o)
    {
        return o + 64 <= b.size() && rd32(b, o + 8) == 81 && rd32(b, o + 24) == 82 && rd32(b, o + 40) == 83;
    }

    uint8_t expand5(uint16_t v) { return (uint8_t)((v << 3) | (v >> 2)); }

    // CSM1 palette order: swap the middle 8 of every 32 entries.
    void csm1(const uint16_t *in, uint16_t *out)
    {
        for (int i = 0, j = 0; i < 256; i += 32, j += 32)
        {
            for (int k = 0; k < 8; ++k) out[i + k] = in[j + k];
            for (int k = 0; k < 8; ++k) out[i + 16 + k] = in[j + 8 + k];
            for (int k = 0; k < 8; ++k) out[i + 8 + k] = in[j + 16 + k];
            for (int k = 0; k < 8; ++k) out[i + 24 + k] = in[j + 24 + k];
        }
    }
}

// Returns the number of textures dumped, or -1 on error.
extern "C" int ps2xNetTexDump(const char *path, const char *outDir)
{
    if (!path || !outDir) return -1;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { std::fprintf(stderr, "[texdump] cannot open %s\n", path); return -1; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    std::fprintf(stderr, "[texdump] %s: %zu bytes\n", path, buf.size());
    int dumped = 0, found = 0;
    for (size_t o = 0; o + 64 <= buf.size(); o += 4)
    {
        if (!isQrs(buf, o)) continue;
        const uint32_t w = rd32(buf, o + 16) * 2u, h = rd32(buf, o + 20) * 2u;
        if (w == 0 || h == 0 || w > 1024 || h > 1024) continue;
        const size_t gfx = o + 64;
        const size_t pal = gfx + (size_t)w * h;
        if (pal + 512 > buf.size()) continue;
        ++found;

        // The runtime reader wants the texture's own base as bp=0, so give it a private copy.
        std::vector<uint8_t> tex(buf.begin() + gfx, buf.begin() + gfx + (size_t)w * h);
        std::vector<uint8_t> indices((size_t)w * h);
        for (uint32_t y = 0; y < h; ++y)
            GSMem::ReadRowP8(tex.data(), 0u, w / 64u, 0u, w, y, &indices[(size_t)y * w]);

        uint16_t rawPal[256], orderedPal[256];
        for (int i = 0; i < 256; ++i)
            rawPal[i] = (uint16_t)(buf[pal + (size_t)i * 2] | (buf[pal + (size_t)i * 2 + 1] << 8));
        csm1(rawPal, orderedPal);

        // Two palette orders x the 16-bit expand; the correct one becomes obvious visually.
        for (int variant = 0; variant < 2; ++variant)
        {
            const uint16_t *p = variant ? orderedPal : rawPal;
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            for (size_t i = 0; i < (size_t)w * h; ++i)
            {
                const uint16_t c = p[indices[i]];
                uint8_t *px = &rgba[i * 4];
                px[0] = expand5(c & 0x1f);
                px[1] = expand5((c >> 5) & 0x1f);
                px[2] = expand5((c >> 10) & 0x1f);
                px[3] = ((c >> 15) & 1u) ? 255 : 0;
            }
            char out[1024];
            std::snprintf(out, sizeof out, "%s/qrs_%03d_%ux%u_%08zX_%s.png", outDir, found - 1, w, h, o,
                          variant ? "csm1" : "lin");
            if (ps2x::gfx::GsWritePngRGBA8(out, rgba.data(), (int)w, (int)h)) ++dumped;
        }
        o += (size_t)w * h + 512 - 4;   // skip past this record's data + palette
    }
    std::fprintf(stderr, "[texdump] done: %d QRS found, %d PNGs written\n", found, dumped);
    return dumped;
}

extern "C" void ps2xNetTexDumpOnce(void)
{
    if (s_ran) return;
    const char *file = std::getenv("PS2X_NETMENU_TEXDUMP");
    if (!file || !file[0]) return;
    s_ran = true;
    const char *dir = std::getenv("PS2X_NETMENU_TEXDUMP_DIR");
    ps2xNetTexDump(file, (dir && dir[0]) ? dir : "dumps/texdump");
}
