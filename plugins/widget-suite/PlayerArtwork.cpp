#include "plugins/widget-suite/PlayerArtwork.h"

namespace widgetplugin {
mechanicalart::Stage playerArtworkStage(Spec spec, double width, double height) {
    if (spec.rows == 2 && spec.columns == 4) {
        return mechanicalart::arrange(mechanicalart::Kind::Player, width, height);
    }
    const cybercanvas::Rect artwork{0, 0, 1000, spec.rows == 1 ? 1000. / 3 : 1250.};
    const double margin = std::min(width, height) * .045;
    const auto viewport = cybercanvas::sceneViewport(
        {margin, margin, std::max(1., width - margin * 2), std::max(1., height - margin * 2)},
        {artwork.width, artwork.height});
    return {viewport.scale, viewport.origin};
}

mechanicalart::Stage playerTonearmStage(const PlayerLayout& layout) {
    const double scale = layout.cover.width / 402;
    return {scale, {layout.tonearmPivot.x - 553 * scale, layout.tonearmPivot.y - 130 * scale}};
}

cybercanvas::Canvas::InkText playerTitleInk(cybercanvas::Canvas& canvas, const std::wstring& title,
                                            const PlayerLayout& layout) {
    cybercanvas::TextStyle style;
    style.family = L"Microsoft YaHei UI";
    style.size = layout.titleSize;
    style.weight = cybercanvas::FontWeight::SemiBold;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Top;
    style.wrap = false;
    style.ellipsis = true;
    const cybercanvas::Size available{layout.title.width, std::max(layout.title.height, double(style.size) * 3)};
    auto ink = canvas.measureTextInk(title, style, available);
    if (ink.height > layout.title.height) {
        style.size = float(style.size * layout.title.height / ink.height * .98);
        ink = canvas.measureTextInk(title, style, available);
    }
    const cybercanvas::Point origin{layout.title.x, layout.title.y + (layout.title.height - ink.height) * .5 - ink.y};
    return {canvas.nativeTextLayout(title, style, available),
            origin,
            {origin.x + ink.x, origin.y + ink.y, ink.width, ink.height}};
}
}  // namespace widgetplugin
