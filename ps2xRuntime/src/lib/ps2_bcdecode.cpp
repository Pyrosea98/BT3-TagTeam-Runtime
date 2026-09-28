// [gatealpha][seampack] BC1/BC2/BC3 (raylib PIXELFORMAT_COMPRESSED_DXT*: 14/15 = DXT1, 16 = DXT3, 17 = DXT5) -> RGBA8.
// Shared by the texture-pack paths (paraLLEl-GS's replacer, the native renderer's gate-alpha assets). Extracted from
// ps2_gs_pgs.cpp ([standalone] phase 3).
#include <cstdint>
#include <cstring>
#include <vector>
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
bool ps2xBcDecode(int fmt, const std::vector<uint8_t> &src, int w, int h, std::vector<uint8_t> &rgba)
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
