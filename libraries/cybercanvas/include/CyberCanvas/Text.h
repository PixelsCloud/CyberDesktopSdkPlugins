#pragma once

#include <optional>
#include <span>
#include <vector>

#include "CyberCanvas/Image.h"
#include "CyberCanvas/Types.h"

namespace cybercanvas {
class Canvas;

struct TextInk {
    uint32_t color = 0xffffffff;
    std::optional<uint32_t> bottomColor;
    double outlineWidth = 0;
    uint32_t outlineColor = 0x000000ff;
};

struct TextSpan {
    std::wstring text;
    TextStyle style;
    TextInk ink;
    std::string link;  // Opaque application identifier, never opened by the renderer.
    bool underline = false;
    std::shared_ptr<const ImageData> icon;
    Size iconSize{16, 16};
    double iconBaseline = 13;
};

struct StyledTextOptions {
    Size available{256, 128};
    TextStyle paragraph;
    uint32_t maximumLines = 0;  // Zero uses available height only.
};

struct TextHit {
    uint32_t position = 0;  // UTF-16 offset, including one U+FFFC per icon.
    bool trailing = false;
    std::string link;
};

// Immutable owner-thread layout. Recreate on content/style/width/locale/font
// collection changes. DirectWrite handles shaping, bidi and font fallback.
// COLR emoji retain their palette; unsupported bitmap/SVG color glyphs fall
// back to the system monochrome glyph, without outline or gradient promises.
class StyledText {
public:
    StyledText(Canvas& canvas, std::span<const TextSpan> spans, StyledTextOptions options = {});
    ~StyledText();
    StyledText(const StyledText&) = delete;
    StyledText& operator=(const StyledText&) = delete;
    TextMetrics metrics() const;
    std::wstring text() const;
    std::optional<TextHit> hit(Point local) const;
    std::vector<Rect> linkBounds(const std::string& link) const;
    void draw(Canvas& canvas, Point origin = {}) const;
    // Cached transparent BGRA, usable as EffectSurface input for soft shadows.
    // Padding is logical, must accommodate outline/overhang; default 4 DIP.
    std::shared_ptr<const ImageData> snapshot(double devicePixelRatio = 1, double padding = 4) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
