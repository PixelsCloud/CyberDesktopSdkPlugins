#pragma once

#include <list>
#include <tuple>

#include "CyberCanvas/Canvas.h"
#include "CyberCanvas/Effects.h"

namespace cybercanvas {
// Small reusable ellipse shadows (records, pots, floating content). Position
// changes reuse pixels; eight shape/style/DPI variants share one effect device.
class ShadowPainter {
public:
    explicit ShadowPainter(std::shared_ptr<MemoryBudget> budget = std::make_shared<MemoryBudget>());

    void ellipse(Canvas& canvas, Rect bounds, double sigma, Point offset, uint32_t color);

    void reset();

    EffectStats stats() const;

private:
    using Key = std::tuple<double, double, double, double, double, double, uint32_t>;

    struct Entry {
        Key key;
        EffectResult result;
    };

    std::shared_ptr<MemoryBudget> budget_;
    EffectSurface effects_;
    std::list<Entry> cache_;
};
}  // namespace cybercanvas
