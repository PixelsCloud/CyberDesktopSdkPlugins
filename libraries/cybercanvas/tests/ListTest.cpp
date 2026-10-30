#include <CyberCanvas/List.h>

#include <iostream>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        ScrollView scroll;
        scroll.arrange({10, 20, 150, 200}, 2000, 1.5);
        check("scrollbar reserves a separate content gutter", scroll.contentArea().width < 150);
        scroll.scrollBy(120);
        scroll.advance(.06);
        check("wheel movement interpolates in time", scroll.offset() > 0 && scroll.offset() < 120 && scroll.active());
        scroll.scrollBy(120);
        scroll.advance(.3);
        check("rapid wheel events accumulate and settle", scroll.offset() == 240 && !scroll.active());
        scroll.scrollTo(1e8, false);
        check("offsets clamp to content boundaries", scroll.offset() == 1800);
        scroll.arrange({10, 20, 150, 200}, 50);
        check("shrinking content clears scroll and hides the bar", scroll.offset() == 0 && scroll.thumb().height == 0);

        Canvas canvas;
        ListView list;
        std::vector<ListItem> items;
        for (int i = 0; i < 100000; ++i) {
            items.push_back({std::to_string(i), 40, 0});
        }
        list.items(items);
        list.arrange({4, 6, 180, 200}, 2);
        size_t painted = 0;
        std::vector<uint8_t> pixels(400 * 440 * 4);
        FrameBuffer frame{400, 440, 1600, pixels.size(), pixels.data(), 2};
        const auto draw = [&] {
            painted = 0;
            canvas.render(frame, {}, [&](auto&, auto) {
                list.draw(canvas, WidgetTheme::dark(), [&](Canvas& c, const ListRow& row) {
                    ++painted;
                    c.measureText(std::wstring(row.id.begin(), row.id.end()), WidgetTheme::dark().font, {120, 30});
                    c.box(row.bounds.x, row.bounds.y, row.bounds.width, row.bounds.height, 0, 0x66AAEEFF);
                });
            });
        };
        draw();
        check("100000 items only paint five visible rows", painted == 5 && list.rows().size() == 5);
        check("list painting respects the viewport clip", pixels[3] == 0);
        const auto before = list.stats();
        int invalidations = 0;
        list.onInvalidate([&] {
            ++invalidations;
        });
        list.update("90000", 1);
        check("offscreen updates do not request a frame or rebuild offsets",
              !list.needsPaint() && invalidations == 0 && list.stats().prefixBuilds == before.prefixBuilds);
        list.update("1", 2);
        list.update("2", 3);
        check("visible updates coalesce and keep prefix geometry",
              invalidations == 1 && list.stats().prefixBuilds == before.prefixBuilds);
        list.scrollTo(205, false);
        check("partial rows are included and clipped",
              list.rows().front().id == "5" && list.rows().size() == 6 && list.rows().front().clip.y == 6);
        list.select("5");
        items.insert(items.begin(), {"new", 60, 0});
        list.items(items);
        check("inserting above keeps the visible ID and its intra-row offset",
              list.rows().front().id == "5" && list.offset() == 265 && list.selected() == "5");
        items.erase(items.begin() + 6);
        list.items(items);
        check("removing the anchor chooses the next surviving item",
              list.rows().front().id == "6" && list.selected().empty());
        const double anchored = list.offset();
        list.update("new", 4, 100);
        check("height changes above preserve the visible anchor",
              list.rows().front().id == "6" && list.offset() == anchored + 40);
        list.actions([](const ListRow& row) {
            return std::vector<ListAction>{{1, {row.bounds.x, row.bounds.y, 25, row.bounds.height}}};
        });
        auto row = list.rows().front();
        const Point point{row.bounds.x + 10, row.clip.y + 10};
        list.input({PointerKind::Press, point, true});
        auto event = list.input({PointerKind::Release, point});
        check("row actions return stable business IDs", event.clicked && event.id == row.id && event.action == 1);
        list.input({PointerKind::Press, point, true});
        list.wheel(50);
        event = list.input({PointerKind::Release, point});
        check("wheel input cancels a pending row click", !event.clicked);
        list.cancel();
        for (double dpr : {1., 1.25, 1.5, 2., 3.}) {
            list.arrange({4, 6, 180, 200}, dpr);
            const auto bar = list.thumb();
            Point grab{bar.x + bar.width * .5 + 7, bar.y + bar.height * .5 + 9};
            list.input({PointerKind::Press, grab, true}, {7, 9});
            check("scrollbar captures with translated DPI geometry", list.router().captured());
            list.input({PointerKind::Move, {grab.x, 1000}, true}, {7, 9});
            check("bar drag outside clamps to the last row", list.offset() == list.maximum());
            list.input({PointerKind::Release, {grab.x, 1000}}, {7, 9});
            check("outside release clears scrollbar capture", !list.router().captured());
        }
        list.cancel();
        draw();
        check("settled lists stop requesting frames", !list.active() && !list.advance(.02) && !list.needsPaint());
        bool rejected = false;
        try {
            list.items({{"x", 40}, {"x", 40}});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("duplicate IDs fail atomically", rejected && !list.rows().empty());
        list.items({});
        list.status({ListState::Error, L"Unable to load", L"Check the source", L"Retry", true});
        list.arrange({4, 6, 180, 200});
        draw();
        const Point retry{90, 180};
        list.input({PointerKind::Press, retry, true});
        event = list.input({PointerKind::Release, retry});
        check("error states expose a common retry action", event.retry && event.clicked);
        list.arrange({4, 6, 180, 50});
        draw();
        list.input({PointerKind::Press, {140, 30}, true});
        event = list.input({PointerKind::Release, {140, 30}});
        check("compact error states keep the retry button usable", event.retry && event.clicked);
        for (const auto state : {ListState::Loading, ListState::Empty}) {
            list.status({state, L"Status", L"Details", L"Retry", false});
            draw();
            check("empty and loading states draw without stale rows", painted == 0);
        }
        list.arrange({4, 6, 0, 0});
        draw();
        check("zero-sized status viewports remain safe", painted == 0);
        list.items({{"a", 40}, {"b", 40}});
        list.arrange({4, 6, 180, 80});
        list.input({PointerKind::Press, {30, 20}, true});
        list.items({{"new", 40}, {"a", 40}, {"b", 40}});
        event = list.input({PointerKind::Release, {30, 20}});
        check("data changes cancel the old row gesture", !event.clicked);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
