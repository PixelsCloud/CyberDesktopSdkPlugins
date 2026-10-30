#include "plugins/mechanical-desk/PondHabitat.h"

#include <cmath>
#include <numbers>

namespace mechanicaldesk {
namespace {
using cybercanvas::Point;
using cybercanvas::Rect;

std::vector<Point> bank(std::string_view folder, Rect water) {
    std::vector<Point> normalized;
    if (folder == "koi-4x2") {
        for (unsigned i = 0; i < 64; ++i) {
            const double a = i * 2 * std::numbers::pi / 64;
            normalized.push_back({.5 + .495 * std::cos(a), .5 + .495 * std::sin(a)});
        }
    } else if (folder == "koi-4x4") {
        normalized = {{.28, .01}, {.55, .00}, {.76, .08}, {.83, .24}, {.85, .43}, {.98, .65}, {.98, .83}, {.82, .97},
                      {.52, 1},   {.2, .96},  {.04, .82}, {.00, .61}, {.10, .40}, {.06, .20}, {.14, .06}};
    } else {
        normalized = {{.01, .60}, {.05, .30}, {.18, .10}, {.30, .11}, {.44, .34}, {.52, .34},
                      {.64, .10}, {.80, .00}, {.94, .14}, {1, .40},   {.98, .73}, {.86, .96},
                      {.69, .99}, {.53, .78}, {.42, .81}, {.27, .99}, {.10, .97}, {.01, .82}};
    }
    for (auto& p : normalized) {
        p = {water.x + p.x * water.width, water.y + p.y * water.height};
    }
    return normalized;
}

}  // namespace

PondHabitat::PondHabitat(std::string_view folder, double width, double height)
    : outline_(bank(folder, {0, 0, width, height})) {
    cybercanvas::require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()));
    cybercanvas::Path path(factory_.Get());
    path.moveTo(outline_[0]);
    for (size_t i = 1; i < outline_.size(); ++i) {
        path.lineTo(outline_[i]);
    }
    path.close();
    geometry_ = path.geometry(factory_.Get());
}

bool PondHabitat::contains(koiplugin::Vec point, double clearance) const {
    const auto p = D2D1::Point2F(float(point.x), float(point.y));
    BOOL inside = FALSE, touches = FALSE;
    cybercanvas::require(geometry_->FillContainsPoint(p, nullptr, .05f, &inside));
    if (inside && clearance > 0) {
        cybercanvas::require(geometry_->StrokeContainsPoint(p, float(clearance * 2), nullptr, nullptr, .05f, &touches));
    }
    return inside && !touches;
}
}  // namespace mechanicaldesk

namespace mechanicaldesk {
auto PondHabitat::outline() const -> const std::vector<cybercanvas::Point>& {
    return outline_;
}
}  // namespace mechanicaldesk
