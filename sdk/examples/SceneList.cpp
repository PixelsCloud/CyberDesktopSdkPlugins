#include <CyberCanvas/DataWidgets.h>
#include <sdk/Canvas.h>
#include <sdk/SceneList.h>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace cybercanvas;
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "scene-list";
        std::filesystem::create_directories(output);
        cdsdk::Canvas canvas;
        cdsdk::SceneList card;
        View title;
        title.kind = ViewKind::Button;
        title.key = 1;
        title.text = L"Refresh / 刷新";
        title.layout.height = Length::points(32);
        title.layout.shrink = 0;
        const auto header = card.scene().add(0, title);
        card.scene().move(header, 0, card.body());
        card.list().items({{"cpu", 100}, {"memory", 100}, {"network", 100}});
        LineChart chart;
        chart.samples({3, 5, 4, 7, 8, 4, 5, 9, 7, 12});
        card.paint([&](auto& draw, const ListRow& row) {
            const auto theme = card.scene().resolvedTheme(card.body());
            const auto area = row.bounds;
            MetricReadout{
                {area.x, area.y, area.width * .48, area.height}, L"A long metric / 数据指标", L"42%", L"Updated"}
                .draw(draw, theme);
            chart.draw(draw, {area.x + area.width * .5, area.y + 25, area.width * .46, 55}, theme);
        });
        int count = 0;
        for (bool dark : {true, false}) {
            for (bool rtl : {false, true}) {
                for (double width : {180., 320.}) {
                    auto theme = dark ? WidgetTheme::dark() : WidgetTheme::light();
                    theme.font.size = 11;
                    theme.font.direction = rtl ? TextDirection::RightToLeft : TextDirection::LeftToRight;
                    card.scene().theme(theme);
                    card.arrange(canvas, {0, 0, width, 240}, 2, rtl);
                    std::vector<uint8_t> pixels(size_t(width * 2) * 480 * 4);
                    CdFrame frame{sizeof(frame), uint32_t(width * 2), 480, uint32_t(width * 8),
                                  pixels.size(), pixels.data(),       2};
                    canvas.render(frame, {}, [&](auto&, auto) {
                        canvas.box(0, 0, width, 240, 8, theme.surface);
                        card.draw(canvas);
                    });
                    if (!canvas.save((output / (std::to_string(count++) + ".png")).wstring(), frame)) {
                        return 1;
                    }
                }
            }
        }
        std::cout << "PASS eight theme/RTL/size data cards at DPI 2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
