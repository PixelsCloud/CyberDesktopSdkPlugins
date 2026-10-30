#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include "plugins/pixel-desk/BasicClock.h"
#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelProductivity.h"

namespace pixeldesk {
namespace {
constexpr std::array<BasicSpec, 2> kCountdownCases{{{"com.cyberdesktop.pixeldesk.countdown.2x1",
                                                     "countdown-2x1",
                                                     BasicKind::Digital,
                                                     2,
                                                     1,
                                                     {334, 201},
                                                     {2, 2, 330, 197},
                                                     {},
                                                     0},
                                                    {"com.cyberdesktop.pixeldesk.countdown.4x1",
                                                     "countdown-4x1",
                                                     BasicKind::Digital,
                                                     4,
                                                     1,
                                                     {557, 198},
                                                     {2, 2, 553, 194},
                                                     {},
                                                     0}}};
}

struct CountdownFace::Impl {
    std::unique_ptr<BasicFace> face;
    std::unique_ptr<cdsdk::WindowsFonts> fonts;
    bool compact = false;
    cdsdk::RedrawRequests redraw;

    void label(cybercanvas::Canvas& layer, const std::wstring& text, cybercanvas::Rect area, double size,
               uint32_t color, bool wrap = false) {
        cybercanvas::TextStyle style;
        style.family = fonts->family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        style.wrap = wrap;
        style.ellipsis = true;
        layer.text(text, style, area, color);
    }

    void count(cybercanvas::Canvas& layer, const std::string& number, const std::wstring& suffix,
               cybercanvas::Rect area, double suffixWidth, double fontSize) {
        const double gap = 7, cells = double(number.size() * 6 - 1);
        const double unit = std::min(area.height / 7, (area.width - gap - suffixWidth) / cells);
        const double width = cells * unit, height = 7 * unit;
        const double x = area.x + (area.width - width - gap - suffixWidth) / 2, y = area.y + (area.height - height) / 2;
        pixelDigits(layer, number, {x, y, width, height}, kMint, true);
        cybercanvas::TextStyle style;
        style.family = fonts->family();
        style.locale = cdsdk::drawingLocale();
        style.size = fontSize;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Top;
        const auto fitted = layer.fitTextInk(suffix, style, {0, 0, suffixWidth, area.height});
        const auto layout = fitted.layout.Get();
        DWRITE_LINE_METRICS line{};
        UINT32 lines = 0;
        cybercanvas::require(layout->GetLineMetrics(&line, 1, &lines));
        layer.brush->SetColor(cybercanvas::color(kMint));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(
            D2D1::Point2F(float(x + width + gap + fitted.origin.x), float(y + height - line.baseline)), layout,
            layer.brush.Get());
    }

    void inlineCount(cybercanvas::Canvas& layer, const std::wstring& heading, const std::string& number, bool english,
                     cybercanvas::Rect area) {
        cybercanvas::TextStyle style;
        style.family = fonts->family();
        style.locale = cdsdk::drawingLocale();
        style.size = 26;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Start;
        style.vertical = cybercanvas::VerticalAlign::Top;
        const double cells = double(number.size() * 6 - 1), suffixWidth = english ? 57 : 30, gap = 18;
        const double numberWidth = std::min(cells * area.height / 7 + 7 + suffixWidth, area.width * .48);
        const double titleWidth = std::min(layer.measureText(heading, style).width, area.width - numberWidth - gap);
        const double left = area.x + (area.width - titleWidth - gap - numberWidth) / 2;
        const double digitHeight = std::min(area.height, (numberWidth - 7 - suffixWidth) / cells * 7);
        const double baseline = area.y + (area.height + digitHeight) / 2;
        style.ellipsis = true;
        const auto title = layer.nativeTextLayout(heading, style, {titleWidth, 64});
        DWRITE_LINE_METRICS line{};
        UINT32 lines = 0;
        cybercanvas::require(title->GetLineMetrics(&line, 1, &lines));
        layer.brush->SetColor(cybercanvas::color(kCream));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(D2D1::Point2F(float(left), float(baseline - line.baseline)), title.Get(),
                                     layer.brush.Get());
        count(layer, number, english ? L"days" : L"天", {left + titleWidth + gap, area.y, numberWidth, area.height},
              suffixWidth, 26);
    }
};

CountdownFace::CountdownFace() : impl_(std::make_unique<Impl>()) {}

CountdownFace::~CountdownFace() {
    if (impl_->face) {
        impl_->face->onChange({});
    }
}

void CountdownFace::bind(const CdCreateContext& context) {
    impl_->redraw.bind(context);
    impl_->compact = std::string_view(context.plugin_id).ends_with(".2x1");
    impl_->face = std::make_unique<BasicFace>(context, kCountdownCases[impl_->compact ? 0 : 1]);
    impl_->face->onChange([this] {
        impl_->redraw.invalidate();
    });
}

void CountdownFace::pause(bool paused) {
    impl_->face->pause(paused);
    impl_->redraw.pause(paused);
}

bool CountdownFace::ready() const {
    return impl_->face && impl_->face->ready();
}

uint64_t CountdownFace::cacheBuilds() const {
    return impl_->face ? impl_->face->cacheBuilds() : 0;
}

bool CountdownFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->face->save(path, frame);
}

bool CountdownFace::render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame,
                           CdRect safe) {
    impl_->redraw.beginFrame();
    const cdsdk::LanguageScope language(settings.at("language").text);
    const bool english = settings.at("language").text == "en";
    return impl_->face->renderContent(frame, safe, [&](cybercanvas::Canvas& layer) {
        if (!impl_->fonts) {
            impl_->fonts = std::make_unique<cdsdk::WindowsFonts>(layer.nativeTextFactory());
        }
        impl_->fonts->select(layer.nativeTextFactory(), settings.at("font_family").text);
        layer.fontCollection(impl_->fonts->collection());
        const auto title = productivity::wide(settings.at("title").text);
        const auto date = settings.at("date").text;
        const auto year = settings.at("annual").integer
                              ? (english ? std::wstring(L"Annual") : std::wstring(L"每年"))
                              : productivity::wide(date.size() == 10 ? date.substr(0, 4) : "--");
        const auto monthDay =
            productivity::wide(date.size() == 10 ? date.substr(5, 2) + "/" + date.substr(8, 2) : "--");
        const uint64_t magnitude = data.days < 0 ? uint64_t(-(data.days + 1)) + 1 : uint64_t(data.days);
        const auto number = std::to_string(magnitude);
        const auto status = data.days == 0  ? (english ? L"Today" : L"今天")
                            : data.days < 0 ? (english ? L"Elapsed" : L"已过")
                                            : (english ? L"Remaining" : L"剩余");
        if (impl_->compact) {
            impl_->label(layer, title, {65, 61, 77, 31}, 15, kInk, true);
            impl_->label(layer, year, {70, 95, 65, 15}, 12, kClay);
            impl_->label(layer, monthDay, {64, 113, 76, 22}, 19, kInk);
            const bool large = number.size() >= 5;
            impl_->label(layer, std::wstring(status) + (large ? (english ? L" days" : L" · 天") : L""),
                         {161, 86, 125, 25}, english ? 17 : 22, kCream);
            if (large) {
                pixelDigits(layer, number, {161, 114, 125, 32}, kMint, true);
            } else {
                impl_->count(layer, number, english ? L"d" : L"天", {161, 114, 125, 32}, 25, 22);
            }
        } else {
            impl_->label(layer, year, {47, 99, 75, 16}, 13, kClay);
            impl_->label(layer, monthDay, {40, 117, 88, 26}, 22, kInk);
            const auto heading = data.days <= 0 ? std::wstring(status) + L" · " + title : title;
            impl_->inlineCount(layer, heading, number, english, {169, 104, 294 + impl_->face->panelExtension(), 38});
        }
    });
}
}  // namespace pixeldesk
