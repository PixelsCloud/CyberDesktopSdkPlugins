#include "tests/TextInkTest.h"

#include <CyberCanvas/Image.h>

#include <limits>

namespace canvas_testing {
void textInkTests(cybercanvas::Canvas& canvas, const Check& check) {
    using namespace cybercanvas;
    TextStyle style;
    style.size = 64;
    style.align = TextAlign::Center;
    const std::array<std::wstring, 10> references{L"00", L"11", L"22", L"33", L"44", L"55", L"66", L"77", L"88", L"99"};
    const Rect area{7, 9, 40, 30};
    const auto nearlyEqual = [](double a, double b) {
        return std::abs(a - b) < .001;
    };
    const auto large = canvas.fitTextInk(L"11", style, {0, 0, 400, 200}, references);
    const auto fitted = canvas.fitTextInk(L"11", style, area, references);
    check("ink fit shrinks without mutating the original cached font size",
          fitted.layout->GetFontSize() < large.layout->GetFontSize() && large.layout->GetFontSize() == 64);
    check("visible glyphs fit and center inside the destination",
          fitted.bounds.width <= area.width && fitted.bounds.height <= area.height &&
              nearlyEqual(fitted.bounds.x + fitted.bounds.width / 2, area.x + area.width / 2) &&
              nearlyEqual(fitted.bounds.y + fitted.bounds.height / 2, area.y + area.height / 2));
    const float fittedSize = fitted.layout->GetFontSize();
    for (const auto& value : references) {
        const auto digit = canvas.fitTextInk(value, style, area, references);
        check("reference digits keep one fitted font size", digit.layout->GetFontSize() == fittedSize);
    }
    const auto before = canvas.stats().textLayouts.count;
    canvas.fitTextInk(L"11", style, area, references);
    check("repeated ink fitting reuses cached layouts", canvas.stats().textLayouts.count == before);
    const auto wideLayout = canvas.nativeTextLayout(L"layout cache", style, {180, 40});
    const auto narrowLayout = canvas.nativeTextLayout(L"layout cache", style, {70, 25});
    check("different bounds do not mutate a previously cached text layout", wideLayout.Get() != narrowLayout.Get() &&
                                                                                wideLayout->GetMaxWidth() == 180 &&
                                                                                narrowLayout->GetMaxWidth() == 70);
    const auto layouts = canvas.stats().textLayouts.count;
    check("identical text bounds reuse immutable layouts",
          canvas.nativeTextLayout(L"layout cache", style, {180, 40}).Get() == wideLayout.Get() &&
              canvas.stats().textLayouts.count == layouts);
    style.align = TextAlign::End;
    style.vertical = VerticalAlign::Bottom;
    const auto trailing = canvas.fitTextInk(L"11", style, area, references);
    check("visible ink supports bottom and end alignment",
          nearlyEqual(trailing.bounds.x + trailing.bounds.width, area.x + area.width) &&
              nearlyEqual(trailing.bounds.y + trailing.bounds.height, area.y + area.height));
    style.align = TextAlign::Start;
    style.vertical = VerticalAlign::Top;
    const auto leading = canvas.fitTextInk(L"11", style, area, references);
    check("visible ink supports top and start alignment",
          nearlyEqual(leading.bounds.x, area.x) && nearlyEqual(leading.bounds.y, area.y));

    style.family = L"Microsoft YaHei";
    style.locale = L"zh-CN";
    style.bold = true;
    style.characterSpacing = .7f;
    const auto ink = canvas.measureTextInk(L"暴富", style, {200, 100});
    const auto native = canvas.nativeTextLayout(L"暴富", style, {200, 100});
    DWRITE_OVERHANG_METRICS bounds{};
    require(native->GetOverhangMetrics(&bounds));
    check("Chinese ink measurement agrees with DirectWrite overhangs",
          nearlyEqual(ink.x, -bounds.left) && nearlyEqual(ink.y, -bounds.top) &&
              nearlyEqual(ink.width, 200. + bounds.left + bounds.right) &&
              nearlyEqual(ink.height, 100. + bounds.top + bounds.bottom));
    check("ink measurement preserves bold Chinese font and spacing",
          native->GetFontWeight() == DWRITE_FONT_WEIGHT_BOLD);
    check("empty text can be fitted", canvas.fitTextInk(L"", style, area).layout != nullptr);
    check("zero fit area is rejected", rejected([&] {
              canvas.fitTextInk(L"text", style, {0, 0, 0, 20});
          }));
    check("non-finite fit area is rejected", rejected([&] {
              canvas.fitTextInk(L"text", style, {0, 0, std::numeric_limits<double>::infinity(), 20});
          }));
    check("invalid shrink margin is rejected", rejected([&] {
              canvas.fitTextInk(L"text", style, area, {}, 0);
          }));
    check("invalid ink font size is rejected", rejected([&] {
              auto invalid = style;
              invalid.size = -1;
              canvas.measureTextInk(L"text", invalid);
          }));
    check("reference set size is bounded", rejected([&] {
              std::vector<std::wstring> excessive(129, L"00");
              canvas.fitTextInk(L"text", style, area, excessive);
          }));
    const auto recovered = canvas.fitTextInk(L"福", style, area);
    check("ink fitting recovers after invalid requests", recovered.bounds.height > 0);
}
}  // namespace canvas_testing
