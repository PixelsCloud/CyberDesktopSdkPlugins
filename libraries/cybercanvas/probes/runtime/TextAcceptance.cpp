#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Text.h>

#include <array>
#include <iostream>
#include <set>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
    } catch (const std::length_error&) {
        return true;
    }
    return false;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        TextSpan first;
        first.text = L"\u05d0";
        first.link = "aleph";
        first.style = {.size = 32, .family = L"Segoe UI"};
        TextSpan second = first;
        second.text = L"\u05d1";
        second.link = "bet";
        StyledText hebrew(canvas, std::array{first, second}, {.available = {300, 80}});
        const auto aleph = hebrew.linkBounds("aleph"), bet = hebrew.linkBounds("bet");
        require(aleph.size() == 1 && bet.size() == 1 && aleph[0].x > bet[0].x,
                "Hebrew visual order reverses logical span order");
        for (const auto* id : {"aleph", "bet"}) {
            const auto bounds = hebrew.linkBounds(id).front();
            const auto hit = hebrew.hit({bounds.x + bounds.width / 2, bounds.y + bounds.height / 2});
            require(hit && hit->link == id, "Hebrew link hit retains logical identity");
        }
        TextSpan clusters;
        clusters.text = L"e\u0301\U0001f600Z";
        clusters.link = "clusters";
        clusters.style = {.size = 32, .family = L"Segoe UI"};
        for (const double width : {300., 55., 30.}) {
            StyledText layout(canvas, std::array{clusters},
                              {.available = {width, 80}, .paragraph = {.wrap = false, .ellipsis = true}});
            std::set<uint32_t> positions;
            for (double x = 0; x < width; x += .25) {
                if (const auto hit = layout.hit({x, layout.metrics().height / 2})) {
                    positions.insert(hit->position);
                }
            }
            require(!positions.contains(1) && !positions.contains(3),
                    "Hit testing and ellipsis never split combining marks or UTF-16 surrogate pairs");
            if (width == 300) {
                require(positions == std::set<uint32_t>{0, 2, 4},
                        "All complete graphemes remain independently accessible");
            }
            const auto snapshot = layout.snapshot(1.75);
            require(snapshot && snapshot->width() > 0, "Complex script produces an actual-DPI snapshot");
        }
        require(rejects([&] {
                    StyledText tooMany(canvas, std::vector<TextSpan>(1025));
                }),
                "Span count is bounded before native layout creation");
        clusters.text.assign(65537, L'a');
        require(rejects([&] {
                    StyledText tooLong(canvas, std::array{clusters});
                }),
                "UTF-16 text capacity is bounded before native layout creation");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
