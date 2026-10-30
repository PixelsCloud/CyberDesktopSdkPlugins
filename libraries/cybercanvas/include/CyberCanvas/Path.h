#pragma once
#include <memory>
#include <utility>

#include "CyberCanvas/Native.h"
#include "CyberCanvas/Types.h"

namespace cybercanvas {
// A retained Direct2D geometry. Finish explicitly, or let the first draw seal it.
// Paths belong to their creating Canvas factory and survive target/DPI changes.
class Path {
public:
    explicit Path(ID2D1Factory* factory, FillRule rule = FillRule::Alternate);

    ~Path();

    Path(const Path&) = delete;

    Path& operator=(const Path&) = delete;

    Path(Path&&) noexcept;

    Path& operator=(Path&&) noexcept;

    Path& moveTo(Point value, bool filled = true);

    Path& lineTo(Point value);

    Path& quadraticTo(Point control, Point end);

    Path& cubicTo(Point first, Point second, Point end);

    Path& arcTo(Point end, Size radii, double rotation = 0, ArcSweep sweep = ArcSweep::Clockwise,
                ArcSize size = ArcSize::Small);

    Path& close();

    Path& finish();

    ID2D1PathGeometry* geometry(ID2D1Factory* owner);

private:
    struct Impl;
    Impl& implementation();
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
