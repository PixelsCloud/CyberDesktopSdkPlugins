#include "plugins/lunar-suite/MechanicalLunar.h"

namespace lunarplugin {
MechanicalLunarLayout mechanicalLunarLayout(Spec spec, double width, double height) {
    MechanicalLunarLayout result;
    if (spec.rows == 1) {
        result.source = {1774, 887};
        result.visible = {80, 120, 1614, 703};
    } else if (spec.rows == 2) {
        result.source = {1230, 1278};
        result.visible = {26, 52, 1175, 1171};
        result.previous = {181, 184, 133, 123};
        result.next = {914, 184, 135, 123};
        result.header = {397, 183, 430, 125};
    } else {
        result.source = {1222, 1287};
        result.visible = {44, 23, 1134, 1238};
        result.previous = {168, 136, 96, 101};
        result.next = {961, 136, 97, 101};
        result.header = {301, 129, 592, 122};
        result.weeks = {142, 315, 936, 62};
        result.dates = {142, 382, 936, 641};
        result.footer = {270, 1075, 680, 55};
    }
    const auto view = cybercanvas::sceneViewport({width * .055, height * .045, width * .89, height * .91},
                                                 {result.visible.width, result.visible.height});
    result.stage = {view.scale,
                    {view.origin.x - result.visible.x * view.scale, view.origin.y - result.visible.y * view.scale}};
    return result;
}

void lunarText(cybercanvas::Canvas& canvas, const std::wstring& value, cybercanvas::Rect bounds, double size,
               uint32_t ink, bool numeric) {
    if (value.empty()) {
        return;
    }
    cybercanvas::TextStyle style;
    style.family = numeric ? L"Bahnschrift" : L"Microsoft YaHei UI";
    style.locale = cdsdk::drawingLocale();
    style.size = size;
    style.weight = cybercanvas::FontWeight::SemiBold;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    style.tabularFigures = true;
    const auto fitted = canvas.fitTextInk(cdsdk::displayText(value), style, bounds);
    canvas.brush->SetColor(cybercanvas::color(ink));
    canvas.brush->SetOpacity(1);
    canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(),
                                  canvas.brush.Get());
}

std::wstring mechanicalLunarDate(const Lunar& value, bool english) {
    if (english) {
        return L"Lunar " + std::to_wstring(value.month) + L"/" + std::to_wstring(value.day) +
               (value.leap ? L" (leap)" : L"");
    }
    return value.monthName + value.dayName;
}

std::wstring mechanicalWeekday(Date value, bool english) {
    constexpr const wchar_t* zh[]{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    constexpr const wchar_t* en[]{L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    return english ? en[weekday(value)] : zh[weekday(value)];
}

void lunarRollerText(cybercanvas::Canvas& canvas, const std::wstring& current, const std::wstring& previous,
                     cybercanvas::Rect bounds, double size, View view) {
    canvas.clipped(bounds, [&] {
        if (view.progress >= 1 || current == previous) {
            lunarText(canvas, current, bounds, size, mechanicalart::kIvory, true);
            return;
        }
        auto old = bounds, next = bounds;
        old.y -= view.progress * bounds.height * view.direction;
        next.y += (1 - view.progress) * bounds.height * view.direction;
        lunarText(canvas, previous, old, size, mechanicalart::kIvory, true);
        lunarText(canvas, current, next, size, mechanicalart::kIvory, true);
    });
}

void lunarTwoDigits(cybercanvas::Canvas& canvas, int number, int previous, View view, double first, double second,
                    double y, double width, double height, double size) {
    lunarRollerText(canvas, std::to_wstring(number / 10), std::to_wstring(previous / 10),
                    {first - width / 2, y, width, height}, size, view);
    lunarRollerText(canvas, std::to_wstring(number % 10), std::to_wstring(previous % 10),
                    {second - width / 2, y, width, height}, size, view);
}

void drawMechanicalLunar(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, cdsdk::Controls& controls,
                         Spec spec, const cdsdk::Settings& settings, View view, double width, double height) {
    const auto layout = mechanicalLunarLayout(spec, width, height);
    const auto body =
        artwork.material(canvas, "body", std::max(layout.source.width, layout.source.height) * layout.stage.scale);
    const auto lunar = convert(view.selected);
    const bool english = settings.at("language").text == "en";
    const auto special = !lunar.festival.empty() ? lunar.festival : lunar.term;
    const auto date = mechanicalLunarDate(lunar, english);
    constexpr uint32_t copper = 0xe7a175ff, muted = 0xc6c6b8ff;
    layout.stage.draw(canvas, [&] {
        canvas.clipped(layout.visible, [&] {
            if (body) {
                canvas.image(*body, {0, 0, layout.source.width, layout.source.height},
                             {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            } else {
                canvas.gradientBox(layout.visible, 30, 0x7d898aff, 0x373f43ff);
            }
            if (spec.rows == 1) {
                lunarTwoDigits(canvas, view.selected.month, view.previous.month, view, 428, 582, 283, 110, 100, 112);
                lunarTwoDigits(canvas, view.selected.day, view.previous.day, view, 387, 616, 449, 164, 170, 194);
                lunarText(canvas, date, {917, 322, 590, 145}, 116);
                lunarText(canvas, special.empty() ? mechanicalWeekday(view.selected, english) : special,
                          {950, 540, 535, 74}, 88, mechanicalart::kBrass);
                return;
            }
            for (unsigned id : {1u, 2u}) {
                const auto box = id == 1 ? layout.previous : layout.next;
                const auto state = controls.visual(id);
                if (state.hovered || state.pressed) {
                    canvas.box(box.x, box.y, box.width, box.height, 5, state.pressed ? 0x00000030 : 0xffffff12);
                }
                controls.icons.paint(
                    canvas, {box.x + box.width * .27, box.y + box.height * .27, box.width * .46, box.height * .46},
                    id == 1 ? "chevron-left" : "chevron-right", 0x24292aff);
            }
            if (spec.rows == 2) {
                // The two drums have the same visible height. Share a font size and
                // center visible glyph ink on each material window, not its highlight.
                constexpr double headerSize = 78;
                lunarRollerText(canvas, std::to_wstring(view.selected.year), std::to_wstring(view.previous.year),
                                {404, 192, 192, 112}, headerSize, view);
                const auto monthDigits = [](Date date) {
                    return (date.month < 10 ? L"0" : L"") + std::to_wstring(date.month);
                };
                lunarRollerText(canvas, monthDigits(view.selected), monthDigits(view.previous), {637, 192, 182, 112},
                                headerSize, view);
                lunarTwoDigits(canvas, view.selected.day, view.previous.day, view, 456, 773, 406, 220, 235, 294);
                lunarText(canvas, mechanicalWeekday(view.selected, english), {251, 734, 720, 51}, 61);
                lunarText(canvas, date, {254, 847, 714, 82}, 92);
                lunarText(canvas, special.empty() ? lunar.yearName : special, {355, 1002, 515, 59}, 66,
                          mechanicalart::kBrass);
                return;
            }
            // Move the complete month assembly, including its brass edges and drums.
            // Reuse the housing's blank graphite strip to clear its previous position.
            constexpr double monthOffset = 32;
            if (body) {
                const auto region = [&](cybercanvas::Rect source, cybercanvas::Point destination) {
                    canvas.clipped({destination.x, destination.y, source.width, source.height}, [&] {
                        canvas.image(*body,
                                     {destination.x - source.x, destination.y - source.y, layout.source.width,
                                      layout.source.height},
                                     {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                    });
                };
                region({866, 124, monthOffset, 130}, {670, 124});
                region({670, 124, 194, 130}, {670 + monthOffset, 124});
            }
            const std::wstring header = std::to_wstring(view.selected.year) + (view.selected.month < 10 ? L"0" : L"") +
                                        std::to_wstring(view.selected.month);
            const std::wstring previous = std::to_wstring(view.previous.year) +
                                          (view.previous.month < 10 ? L"0" : L"") +
                                          std::to_wstring(view.previous.month);
            // Digit drums are not evenly spaced: the brass dividers are wider.
            constexpr std::array<double, 6> centers{343.5, 417, 526.5, 630, 727 + monthOffset, 801 + monthOffset};
            for (size_t i = 0; i < centers.size(); ++i) {
                lunarRollerText(canvas, header.substr(i, 1), previous.substr(i, 1), {centers[i] - 26, 140, 52, 95}, 70,
                                view);
            }
            const bool monday = settings.at("week_start").text == "monday";
            const auto grid = monthGrid(view.selected, monday);
            const double cell = layout.dates.width / 7, row = layout.dates.height / grid.rows;
            constexpr const wchar_t* zh[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
            constexpr const wchar_t* en[]{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"};
            for (unsigned column = 0; column < 7; ++column) {
                const unsigned wd = (column + (monday ? 1 : 0)) % 7;
                const cybercanvas::Rect box{layout.weeks.x + column * cell + 3, layout.weeks.y, cell - 6,
                                            layout.weeks.height};
                canvas.gradientBox(box, 5, 0x25292bff, 0x14191cff);
                canvas.strokeBox(box, 5, 0x81858050, {.width = 1.});
                lunarText(canvas, english ? en[wd] : zh[wd], {box.x + 6, box.y + 8, box.width - 12, box.height - 16},
                          43, wd == 0 || wd == 6 ? copper : mechanicalart::kIvory);
            }
            for (int i = 0; i < grid.rows * 7; ++i) {
                const cybercanvas::Rect face{layout.dates.x + (i % 7) * cell + 3, layout.dates.y + (i / 7) * row + 3,
                                             cell - 6, row - 6};
                canvas.gradientBox(face, 6, 0x202629ee, 0x101619ee);
                canvas.strokeBox(face, 6, 0x767c7c50, {.width = 1.});
                const auto day = grid.dates[i];
                if (!valid(day) || (day.month != view.selected.month && !settings.at("adjacent_days").integer)) {
                    continue;
                }
                const auto value = convert(day);
                const bool selected = day == view.selected, now = day == view.current,
                           current = day.month == view.selected.month;
                const bool weekend = weekday(day) == 0 || weekday(day) == 6;
                uint32_t ink = weekend ? copper : mechanicalart::kIvory;
                if (!current) {
                    ink = (ink & 0xffffff00) | 0x60;
                }
                const cybercanvas::Point center{face.x + face.width / 2, face.y + face.height * .29};
                const double radius = std::min(face.width * .34, face.height * .27);
                if (selected) {
                    canvas.oval(center.x, center.y, radius + 2, radius + 2, 0x74542eff);
                    canvas.oval(center.x, center.y, radius, radius, mechanicalart::kBrass);
                } else if (now) {
                    canvas.strokeBox(face, 6, mechanicalart::kBrass, {.width = 2.});
                }
                lunarText(canvas, std::to_wstring(day.day),
                          selected ? cybercanvas::Rect{center.x - radius + 3, center.y - radius + 4, radius * 2 - 6,
                                                       radius * 2 - 8}
                                   : cybercanvas::Rect{face.x + 8, face.y + face.height * .055, face.width - 16,
                                                       face.height * .49},
                          64, selected ? 0x181c1eff : ink, true);
                const auto detail = english && value.festival.empty() && value.term.empty() ? std::to_wstring(value.day)
                                                                                            : value.label();
                lunarText(canvas, detail, {face.x + 7, face.y + face.height * .63, face.width - 14, face.height * .27},
                          std::min(40., face.height * .35),
                          !current                                         ? ink
                          : !value.festival.empty() || !value.term.empty() ? copper
                                                                           : muted);
            }
            lunarText(canvas, date + (special.empty() ? L"" : L" · " + special), layout.footer, 44,
                      mechanicalart::kBrass);
        });
    });
}
}  // namespace lunarplugin
