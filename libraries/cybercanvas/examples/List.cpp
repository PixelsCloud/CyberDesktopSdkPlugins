#include <CyberCanvas/List.h>

#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        ListView list;
        auto theme = WidgetTheme::dark();
        theme.font.size = 12;
        const std::string mode = argc > 2 ? argv[2] : "list";
        if (mode == "list") {
            std::vector<ListItem> items;
            for (int i = 0; i < 10000; ++i) {
                items.push_back({"item-" + std::to_string(i), 58});
            }
            list.items(std::move(items));
        } else {
            const auto state = mode == "error"     ? ListState::Error
                               : mode == "loading" ? ListState::Loading
                                                   : ListState::Empty;
            list.status(
                {state,
                 state == ListState::Error     ? L"Unable to load stories"
                 : state == ListState::Loading ? L"Loading stories…"
                                               : L"No saved stories",
                 state == ListState::Error ? L"Check your source and try again." : L"Your content will appear here.",
                 L"Retry", state == ListState::Error});
        }
        list.arrange({16, 62, 288, 300}, 2);
        list.scrollTo(127, false);
        std::vector<uint8_t> pixels(640 * 760 * 4);
        FrameBuffer frame{640, 760, 2560, pixels.size(), pixels.data(), 2};
        canvas.render(frame, {}, [&](auto& draw, auto area) {
            draw.box(0, 0, area.width, area.height, 12, 0x151C28FF);
            auto heading = theme.font;
            heading.size = 18;
            heading.bold = true;
            draw.text(L"CyberCanvas 0.8", heading, {16, 12, 288, 30}, theme.foreground);
            list.draw(draw, theme, [&](Canvas& c, const ListRow& row) {
                const auto r = inset(row.bounds, {8, 4, 8, 4});
                c.text(L"Story " + std::to_wstring(row.index + 1), theme.font, {r.x, r.y, r.width, 22},
                       theme.foreground);
                c.text(L"Only visible rows are drawn.", theme.font, {r.x, r.y + 23, r.width, 20}, theme.muted);
                c.line(r.x, row.bounds.y + row.bounds.height - 1, r.x + r.width, row.bounds.y + row.bounds.height - 1,
                       theme.border, .6);
            });
        });
        const std::string output = argc > 1 ? argv[1] : "list.png";
        if (!canvas.save(std::wstring(output.begin(), output.end()), frame)) {
            return 1;
        }
        std::cout << "PASS list example: " << list.stats().drawnRows << " visible rows\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
