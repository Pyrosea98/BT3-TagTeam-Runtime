#pragma once
#include "ps2_ui_glyph_atlas.h"
#include "ps2_ui_strings.h"
#include <cmath>

namespace ps2x::ui {
using TextureHandle = uint64_t;
struct Rect { float x, y, width, height; };
struct TextSprite {
    TextureHandle indices, palette;
    float x0, y0, x1, y1, u0, v0, u1, v1;
};
// The renderer implements these operations on its owning thread. Uploads copy
// their input; texture/palette handles remain valid until release. begin/end
// save/restore graphics state, including scissor. No backend is selected here.
class TextSpriteSink {
public:
    virtual ~TextSpriteSink() = default;
    virtual TextureHandle uploadIndices(std::span<const uint8_t>, uint16_t width, uint16_t height) = 0;
    virtual TextureHandle uploadPalette(std::span<const uint8_t> straightRGBA) = 0;
    virtual void release(TextureHandle) = 0;
    virtual void begin(Rect scissor) = 0;
    virtual void sprite(const TextSprite&) = 0;
    virtual void end() = 0;
};
enum class TextAlign { Left, Centre, Right };
enum class Palette : uint8_t { Gold, White, Grey, Cyan, Yellow, Red, Green, Count };
struct DrawResult { bool success; size_t submitted; float advance; };
class NativeTextFont {
    GlyphAtlas metrics_;
    TextSpriteSink* sink_ = nullptr;
    std::vector<TextureHandle> pages_, palettes_;
public:
    NativeTextFont() = default;
    NativeTextFont(const NativeTextFont&) = delete;
    NativeTextFont& operator=(const NativeTextFont&) = delete;
    // Sink must outlive this font or reset must be called before sink teardown.
    ~NativeTextFont() { reset(); }
    void reset() {
        if (sink_) {
            for (auto h : pages_) sink_->release(h);
            for (auto h : palettes_) sink_->release(h);
        }
        pages_.clear(); palettes_.clear(); sink_ = nullptr;
    }
    bool load(TextSpriteSink& sink, std::span<const uint8_t> metrics,
              std::span<const std::span<const uint8_t>> pages,
              std::span<const std::span<const uint8_t>> palettes) {
        reset();
        if (!metrics_.load(metrics) || pages.size() != metrics_.pages() ||
            palettes.size() != size_t(Palette::Count)) return false;
        for (auto bytes : pages)
            if (bytes.size() != size_t(metrics_.width()) * metrics_.height()) return false;
        for (auto bytes : palettes) if (bytes.size() != 1024) return false;
        // Reserve before creating GPU resources: subsequent pushes cannot allocate.
        pages_.reserve(pages.size()); palettes_.reserve(palettes.size()); sink_ = &sink;
        for (auto bytes : pages) {
            auto handle = sink.uploadIndices(bytes, metrics_.width(), metrics_.height());
            if (!handle) { reset(); return false; }
            pages_.push_back(handle);
        }
        for (auto bytes : palettes) {
            auto handle = sink.uploadPalette(bytes);
            if (!handle) { reset(); return false; }
            palettes_.push_back(handle);
        }
        return true;
    }
    float measure(std::string_view text, float scale = 1) const { return metrics_.measure(text, scale); }
    DrawResult draw(std::string_view text, Palette palette, float x, float baselineY,
                    Rect viewport, std::span<GlyphQuad> scratch, TextAlign align = TextAlign::Left,
                    float scale = 1) const {
        if (!sink_ || size_t(palette) >= palettes_.size() ||
            !std::isfinite(x) || !std::isfinite(baselineY) || !std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
            !std::isfinite(viewport.width) || !std::isfinite(viewport.height) || viewport.width <= 0 || viewport.height <= 0 ||
            !std::isfinite(scale) || scale <= 0 || scale > 16) return {false, 0, 0};
        const auto result = metrics_.layout(text, 0, baselineY, scratch, scale);
        if (result.capacityExceeded) return {false, 0, result.advance}; // no partial label
        float offset = x;
        if (align == TextAlign::Centre) offset -= result.advance / 2;
        else if (align == TextAlign::Right) offset -= result.advance;
        // Fit the shared 640x448 logical canvas inside this particular viewport.
        const float fit = std::min(viewport.width / 640, viewport.height / 448);
        const float originX = viewport.x + (viewport.width - 640 * fit) / 2;
        const float originY = viewport.y + (viewport.height - 448 * fit) / 2;
        const float clipR = viewport.x + viewport.width, clipB = viewport.y + viewport.height;
        sink_->begin(viewport);
        size_t submitted = 0;
        for (size_t i = 0; i < result.written; ++i) {
            const auto& q = scratch[i];
            float x0 = originX + (q.x + offset) * fit, y0 = originY + q.y * fit;
            float x1 = x0 + q.width * q.scale * fit, y1 = y0 + q.height * q.scale * fit;
            float left = std::max(x0, viewport.x), top = std::max(y0, viewport.y);
            float right = std::min(x1, clipR), bottom = std::min(y1, clipB);
            if (left >= right || top >= bottom) continue;
            const float u0 = q.u / float(metrics_.width()), v0 = q.v / float(metrics_.height());
            const float du = q.width / float(metrics_.width()), dv = q.height / float(metrics_.height());
            sink_->sprite({pages_[q.page], palettes_[size_t(palette)], left, top, right, bottom,
                u0 + du * (left - x0) / (x1 - x0), v0 + dv * (top - y0) / (y1 - y0),
                u0 + du * (right - x0) / (x1 - x0), v0 + dv * (bottom - y0) / (y1 - y0)});
            ++submitted;
        }
        sink_->end();
        return {true, submitted, result.advance};
    }
    DrawResult draw(const Localizer& locale, TextId id, Palette palette, float x, float baselineY,
                    Rect viewport, std::span<GlyphQuad> scratch, TextAlign align = TextAlign::Left, float scale = 1) const {
        auto text = locale.text(id);
        if (text.empty()) return {false, 0, 0};
        return draw(text, palette, x, baselineY, viewport, scratch, align, scale);
    }
};
} // namespace ps2x::ui
