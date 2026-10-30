#include "plugins/mechanical-sign/Sign.h"

namespace mechanicalsign {
cybercanvas::Rect Geometry::slot(size_t index) const {
    return {130 + index * 160., 220, 145, 280};
}

double Geometry::mechanismY() const {
    return 506;
}

double Geometry::mechanismScale() const {
    return 1;
}

Geometry geometry(Format, double) {
    // Retain the production instance's visible-body registration and layout IDs.
    return {2172, 724, true};
}

Format formatFor(const CdLayout&) {
    return Format::Compact;
}
}  // namespace mechanicalsign
