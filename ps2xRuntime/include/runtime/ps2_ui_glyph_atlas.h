#pragma once
// Portable native glyph metrics/layout. Texture upload and rendering are separate.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ps2x::ui {
struct Glyph {
    uint32_t codepoint;
    uint16_t page, u, v, width, height;
    int16_t bearingX, bearingY;
    int32_t advance64;
};
struct GlyphQuad {
    uint16_t page, u, v, width, height;
    float x, y, scale;
};
struct TextLayoutResult { size_t written; float advance; bool capacityExceeded; };
class GlyphAtlas {
    struct Kern { uint32_t left, right; int32_t adjustment64; };
    std::vector<Glyph> glyphs_;
    std::vector<Kern> kerning_;
    uint16_t pages_ = 0, width_ = 0, height_ = 0;
    int32_t line64_ = 0, baseline64_ = 0;
    static uint16_t u16(std::span<const uint8_t> b, size_t p) {
        return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
    }
    static uint32_t u32(std::span<const uint8_t> b, size_t p) {
        return uint32_t(u16(b, p)) | (uint32_t(u16(b, p + 2)) << 16);
    }
    int32_t kern(uint32_t a, uint32_t b) const {
        for (const auto& k : kerning_) if (k.left == a && k.right == b) return k.adjustment64;
        return 0;
    }
    const Glyph* find(uint32_t c) const {
        auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), c,
                                  [](const Glyph& g, uint32_t value) { return g.codepoint < value; });
        return it != glyphs_.end() && it->codepoint == c ? &*it : nullptr;
    }
    static uint32_t next(std::string_view text, size_t& pos) {
        const auto first = uint8_t(text[pos++]);
        if (first < 128) return first;
        unsigned count; uint32_t c, minimum;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; c = first & 31; minimum = 128; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; c = first & 15; minimum = 2048; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; c = first & 7; minimum = 65536; }
        else return 0xfffd;
        if (text.size() - pos < count) return 0xfffd;
        for (unsigned i = 0; i < count; ++i) {
            auto byte = uint8_t(text[pos + i]);
            if ((byte & 0xc0) != 0x80) return 0xfffd;
            c = (c << 6) | (byte & 63);
        }
        pos += count;
        return c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ? 0xfffd : c;
    }
public:
    // Allocations happen only while loading. Invalid input clears prior metrics.
    bool load(std::span<const uint8_t> b) {
        glyphs_.clear(); kerning_.clear(); pages_ = width_ = height_ = 0; line64_ = baseline64_ = 0;
        if (b.size() < 28 || b[0] != 'G' || b[1] != 'A' || b[2] != 'T' || b[3] != 'L' || u16(b, 4) != 1) return false;
        auto pages = u16(b, 6), w = u16(b, 8), h = u16(b, 10);
        auto line = std::bit_cast<int32_t>(u32(b, 12)), baseline = std::bit_cast<int32_t>(u32(b, 16));
        auto ng = u32(b, 20), nk = u32(b, 24);
        if (!pages || pages > 64 || !w || !h || w > 512 || h > 256 ||
            (w & (w - 1)) || (h & (h - 1)) || line <= 0 || baseline < 0 || baseline > line ||
            !ng || ng > 4096 || nk > 4096 || b.size() != 28ull + 22ull * ng + 12ull * nk) return false;
        std::vector<Glyph> glyphs; std::vector<Kern> kerning;
        glyphs.reserve(ng); kerning.reserve(nk);
        for (size_t i = 0, p = 28; i < ng; ++i, p += 22) {
            Glyph g{u32(b, p), u16(b, p+4), u16(b, p+6), u16(b, p+8), u16(b, p+10), u16(b, p+12),
                    std::bit_cast<int16_t>(u16(b, p+14)), std::bit_cast<int16_t>(u16(b, p+16)),
                    std::bit_cast<int32_t>(u32(b, p+18))};
            if (g.codepoint > 0x10ffff || (g.codepoint >= 0xd800 && g.codepoint <= 0xdfff) ||
                g.page >= pages || !g.width || !g.height || uint32_t(g.u)+g.width > w ||
                uint32_t(g.v)+g.height > h || g.advance64 < 0 ||
                (!glyphs.empty() && glyphs.back().codepoint >= g.codepoint)) return false;
            glyphs.push_back(g);
        }
        if (std::none_of(glyphs.begin(), glyphs.end(), [](const Glyph& g) { return g.codepoint == '?'; })) return false;
        for (size_t i = 0, p = 28 + 22 * ng; i < nk; ++i, p += 12)
            kerning.push_back({u32(b, p), u32(b, p+4), std::bit_cast<int32_t>(u32(b, p+8))});
        glyphs_ = std::move(glyphs); kerning_ = std::move(kerning);
        pages_ = pages; width_ = w; height_ = h; line64_ = line; baseline64_ = baseline;
        return true;
    }
    uint16_t pages() const { return pages_; }
    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    float lineHeight() const { return line64_ / 64.0f; }
    float baseline() const { return baseline64_ / 64.0f; }
    // x/y is baseline origin. Caller owns output; no allocations per layout.
    // Unknown/malformed codepoints use the baked boxed-question replacement glyph.
    TextLayoutResult layout(std::string_view text, float x, float y,
                            std::span<GlyphQuad> output, float scale = 1.0f) const {
        if (glyphs_.empty() || !(scale > 0.0f) || scale > 16.0f) return {0, 0, false};
        size_t pos = 0, written = 0; double advance = 0; uint32_t previous = 0; bool exceeded = false;
        while (pos < text.size()) {
            uint32_t c = next(text, pos);
            const Glyph* g = find(c);
            if (!g) { g = find(0xfffd); if (!g) g = find('?'); }
            advance += kern(previous, g->codepoint);
            if (written < output.size()) {
                output[written++] = {g->page, g->u, g->v, g->width, g->height,
                    x + float(advance / 64.0 + g->bearingX) * scale, y + g->bearingY * scale, scale};
            } else exceeded = true;
            advance += g->advance64; previous = g->codepoint;
        }
        return {written, float(advance / 64.0) * scale, exceeded};
    }
    float measure(std::string_view text, float scale = 1.0f) const {
        return layout(text, 0, 0, {}, scale).advance;
    }
};
} // namespace ps2x::ui
