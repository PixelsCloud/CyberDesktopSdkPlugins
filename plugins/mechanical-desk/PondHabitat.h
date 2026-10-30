#pragma once

#include <CyberCanvas/Path.h>

#include "plugins/koi-suite/Pond.h"

namespace mechanicaldesk {
// One authored contour drives the water mask, pointer hit testing and swimming.
// Native Direct2D geometry handles both concavity and clearance from the bank.
class PondHabitat {
public:
    PondHabitat(std::string_view folder, double width, double height);
    bool contains(koiplugin::Vec point, double clearance = 0) const;

    const std::vector<cybercanvas::Point>& outline() const;

private:
    std::vector<cybercanvas::Point> outline_;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry_;
};
}  // namespace mechanicaldesk
