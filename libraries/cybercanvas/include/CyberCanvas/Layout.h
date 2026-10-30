#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
namespace detail {
void layoutRect(Rect area);

void layoutExtent(double value);
}  // namespace detail

Rect inset(Rect area, Insets padding);

Rect aligned(Rect area, Size desired, Alignment horizontal = Alignment::Center, Alignment vertical = Alignment::Center);

// A small immediate layout cursor, not a flexbox or a widget tree. Fixed slots
// consume the available rectangle; oversized slots clamp without escaping it.
class Stack {
public:
    Stack(Rect area, Axis axis, double gap = 0);

    Rect take(double extent);

    Rect takeEnd(double extent);

    Rect remaining() const;

private:
    Rect consume(double extent, bool end);

    Rect remaining_;
    Axis axis_;
    double gap_;
};

std::vector<Rect> divide(Rect area, Axis axis, size_t count, double gap = 0);
}  // namespace cybercanvas
