#include <CyberCanvas/Native.h>
#include <CyberCanvas/Path.h>
#include <CyberCanvas/Types.h>

#include <cmath>

namespace cybercanvas {
class Path::Impl {
public:
    explicit Impl(ID2D1Factory* factory, FillRule rule = FillRule::Alternate) {
        if (!factory) {
            throw std::invalid_argument("Path requires a factory");
        }
        require(factory->CreatePathGeometry(geometry_.GetAddressOf()));
        require(geometry_->Open(sink_.GetAddressOf()));
        sink_->SetFillMode(rule == FillRule::Winding ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) noexcept = default;
    Impl& operator=(Impl&&) noexcept = default;

    Impl& moveTo(Point value, bool filled = true) {
        writable();
        const auto point = native(value);
        if (figure_) {
            sink_->EndFigure(D2D1_FIGURE_END_OPEN);
        }
        sink_->BeginFigure(point, filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        figure_ = true;
        return *this;
    }

    Impl& lineTo(Point value) {
        active();
        sink_->AddLine(native(value));
        return *this;
    }

    Impl& quadraticTo(Point control, Point end) {
        active();
        sink_->AddQuadraticBezier(D2D1::QuadraticBezierSegment(native(control), native(end)));
        return *this;
    }

    Impl& cubicTo(Point first, Point second, Point end) {
        active();
        sink_->AddBezier(D2D1::BezierSegment(native(first), native(second), native(end)));
        return *this;
    }

    // Endpoint-form elliptical arc, using Direct2D's native arc implementation.
    // Rotation is in degrees. Radii must be positive; use lineTo for a straight edge.
    Impl& arcTo(Point end, Size radii, double rotation = 0, ArcSweep sweep = ArcSweep::Clockwise,
                ArcSize size = ArcSize::Small) {
        active();
        const auto endpoint = native(end);
        if (!std::isfinite(radii.width) || !std::isfinite(radii.height) || radii.width <= 0 || radii.height <= 0 ||
            radii.width > 1e7 || radii.height > 1e7 || !std::isfinite(rotation) || std::abs(rotation) > 1e7 ||
            (sweep != ArcSweep::Clockwise && sweep != ArcSweep::CounterClockwise) ||
            (size != ArcSize::Small && size != ArcSize::Large)) {
            throw std::invalid_argument("Invalid Path arc");
        }
        sink_->AddArc(D2D1::ArcSegment(
            endpoint, D2D1::SizeF(float(radii.width), float(radii.height)), float(rotation),
            sweep == ArcSweep::Clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            size == ArcSize::Small ? D2D1_ARC_SIZE_SMALL : D2D1_ARC_SIZE_LARGE));
        return *this;
    }

    Impl& close() {
        active();
        sink_->EndFigure(D2D1_FIGURE_END_CLOSED);
        figure_ = false;
        return *this;
    }

    Impl& finish() {
        if (sink_) {
            if (figure_) {
                sink_->EndFigure(D2D1_FIGURE_END_OPEN);
                figure_ = false;
            }
            require(sink_->Close());
            sink_.Reset();
        }
        return *this;
    }

    ID2D1PathGeometry* geometry(ID2D1Factory* owner) {
        if (!geometry_) {
            throw std::logic_error("Path has been moved");
        }
        ComPtr<ID2D1Factory> factory;
        geometry_->GetFactory(factory.GetAddressOf());
        if (factory.Get() != owner) {
            throw std::invalid_argument("Path belongs to a different Canvas");
        }
        finish();
        return geometry_.Get();
    }

private:
    static D2D1_POINT_2F native(Point point) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 1e7 || std::abs(point.y) > 1e7) {
            throw std::invalid_argument("Invalid Path point");
        }
        return D2D1::Point2F(float(point.x), float(point.y));
    }

    void writable() const {
        if (!sink_) {
            throw std::logic_error("Path is already finished");
        }
    }

    void active() const {
        writable();
        if (!figure_) {
            throw std::logic_error("Path needs moveTo before a segment");
        }
    }

    ComPtr<ID2D1PathGeometry> geometry_;
    ComPtr<ID2D1GeometrySink> sink_;
    bool figure_ = false;
};

Path::Path(ID2D1Factory* factory, FillRule rule) : impl_(std::make_unique<Impl>(factory, rule)) {}

Path::Path(Path&&) noexcept = default;

Path& Path::operator=(Path&&) noexcept = default;

Path& Path::moveTo(Point value, bool filled) {
    implementation().moveTo(value, filled);
    return *this;
}

Path& Path::lineTo(Point value) {
    implementation().lineTo(value);
    return *this;
}

Path& Path::quadraticTo(Point control, Point end) {
    implementation().quadraticTo(control, end);
    return *this;
}

Path& Path::cubicTo(Point first, Point second, Point end) {
    implementation().cubicTo(first, second, end);
    return *this;
}

Path& Path::arcTo(Point end, Size radii, double rotation, ArcSweep sweep, ArcSize size) {
    implementation().arcTo(end, radii, rotation, sweep, size);
    return *this;
}

Path& Path::close() {
    implementation().close();
    return *this;
}

Path& Path::finish() {
    implementation().finish();
    return *this;
}

ID2D1PathGeometry* Path::geometry(ID2D1Factory* owner) {
    return implementation().geometry(owner);
}

Path::~Path() = default;

Path::Impl& Path::implementation() {
    if (!impl_) {
        throw std::logic_error("Path has been moved");
    }
    return *impl_;
}
}  // namespace cybercanvas
