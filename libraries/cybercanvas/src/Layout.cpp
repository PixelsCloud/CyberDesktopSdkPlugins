#include "CyberCanvas/Layout.h"

namespace cybercanvas::detail {
void layoutRect(Rect area) {
    if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.width) || !std::isfinite(area.height) ||
        area.width < 0 || area.height < 0) {
        throw std::invalid_argument("Invalid layout rectangle");
    }
}

void layoutExtent(double value) {
    if (!std::isfinite(value) || value < 0) {
        throw std::invalid_argument("Invalid layout extent");
    }
}
}  // namespace cybercanvas::detail

namespace cybercanvas {
Rect inset(Rect area, Insets padding) {
    detail::layoutRect(area);
    for (double value : {padding.left, padding.top, padding.right, padding.bottom}) {
        detail::layoutExtent(value);
    }
    const double left = std::min(area.width, padding.left), top = std::min(area.height, padding.top);
    return {area.x + left, area.y + top, std::max(0., area.width - left - padding.right),
            std::max(0., area.height - top - padding.bottom)};
}

Rect aligned(Rect area, Size desired, Alignment horizontal, Alignment vertical) {
    detail::layoutRect(area);
    detail::layoutExtent(desired.width);
    detail::layoutExtent(desired.height);
    const double width = std::min(area.width, desired.width), height = std::min(area.height, desired.height);
    const auto offset = [](double remaining, Alignment alignment) {
        return alignment == Alignment::Center ? remaining / 2 : alignment == Alignment::End ? remaining : 0.;
    };
    return {area.x + offset(area.width - width, horizontal), area.y + offset(area.height - height, vertical), width,
            height};
}

Stack::Stack(Rect area, Axis axis, double gap) : remaining_(area), axis_(axis), gap_(gap) {
    detail::layoutRect(area);
    detail::layoutExtent(gap);
}

auto Stack::take(double extent) -> Rect {
    return consume(extent, false);
}

auto Stack::takeEnd(double extent) -> Rect {
    return consume(extent, true);
}

auto Stack::remaining() const -> Rect {
    return remaining_;
}

auto Stack::consume(double extent, bool end) -> Rect {
    detail::layoutExtent(extent);
    double& origin = axis_ == Axis::Horizontal ? remaining_.x : remaining_.y;
    double& available = axis_ == Axis::Horizontal ? remaining_.width : remaining_.height;
    extent = std::min(extent, available);
    Rect result = remaining_;
    double& resultOrigin = axis_ == Axis::Horizontal ? result.x : result.y;
    (axis_ == Axis::Horizontal ? result.width : result.height) = extent;
    if (end) {
        resultOrigin += available - extent;
    }
    const double used = std::min(available, extent + gap_);
    if (!end) {
        origin += used;
    }
    available -= used;
    return result;
}

std::vector<Rect> divide(Rect area, Axis axis, size_t count, double gap) {
    detail::layoutRect(area);
    detail::layoutExtent(gap);
    if (!count || count > 4096) {
        throw std::invalid_argument("Invalid layout slot count");
    }
    const double length = axis == Axis::Horizontal ? area.width : area.height;
    const double spacing = count > 1 ? std::min(gap, length / (count - 1)) : 0;
    const double extent = std::max(0., (length - spacing * (count - 1)) / count);
    std::vector<Rect> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto slot = area;
        if (axis == Axis::Horizontal) {
            slot.x += i * (extent + spacing);
            slot.width = extent;
        } else {
            slot.y += i * (extent + spacing);
            slot.height = extent;
        }
        result.push_back(slot);
    }
    return result;
}
}  // namespace cybercanvas
