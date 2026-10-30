#include "plugins/mechanical-sign/Sign.h"

namespace mechanicalsign {
cybercanvas::Rect Geometry::slot(size_t index) const {
    return {241 + index * 148., 236, 130, 230};
}

double Geometry::mechanismY() const {
    return 519;
}

double Geometry::mechanismScale() const {
    return .65;
}

Geometry geometry(Format format, double aspect) {
    // The immutable plugin identity and legacy format values stay readable.
    // Only the approved 4x1 device is offered; photographic geometry stays rigid.
    (void)format;
    (void)aspect;
    return {2172, 724, true};
}

Format formatFor(const CdLayout& layout) {
    (void)layout;
    return Format::Compact;
}
}  // namespace mechanicalsign
