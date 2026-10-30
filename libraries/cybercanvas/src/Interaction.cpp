#include "CyberCanvas/Interaction.h"

namespace cybercanvas {
Point logicalPointer(Point pixels, double dpr) {
    if (!std::isfinite(dpr) || dpr <= 0) {
        throw std::invalid_argument("Invalid pointer DPI");
    }
    return {pixels.x / dpr, pixels.y / dpr};
}

auto HitShape::rectangle(Rect bounds) -> HitShape {
    for (const auto value : {bounds.x, bounds.y, bounds.width, bounds.height}) {
        if (!std::isfinite(value) || std::abs(value) > 1e7) {
            throw std::invalid_argument("Invalid hit bounds");
        }
    }
    HitShape result;
    result.bounds_ = bounds;
    return result;
}

auto HitShape::ellipse(Rect bounds) -> HitShape {
    auto result = rectangle(bounds);
    result.ellipse_ = true;
    return result;
}

auto HitShape::path(Path& path, ID2D1Factory* factory) -> HitShape {
    HitShape result;
    result.geometry_ = path.geometry(factory);
    return result;
}

auto HitShape::transformed(const D2D1_MATRIX_3X2_F& matrix) const -> HitShape {
    for (const auto value : {matrix._11, matrix._12, matrix._21, matrix._22, matrix._31, matrix._32}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Invalid hit transform");
        }
    }
    auto result = *this;
    result.transform_ = D2D1::Matrix3x2F(matrix._11, matrix._12, matrix._21, matrix._22, matrix._31, matrix._32);
    return result;
}

auto HitShape::local(Point point) const -> std::optional<Point> {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        return {};
    }
    auto inverse = transform_;
    if (!inverse.Invert()) {
        return {};
    }
    const auto value = inverse.TransformPoint(D2D1::Point2F(float(point.x), float(point.y)));
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        return {};
    }
    return Point{value.x, value.y};
}

auto HitShape::contains(Point point) const -> bool {
    const auto value = local(point);
    if (!value) {
        return false;
    }
    if (geometry_) {
        BOOL inside = FALSE;
        require(geometry_->FillContainsPoint(D2D1::Point2F(float(value->x), float(value->y)), nullptr, .1f, &inside));
        return inside != FALSE;
    }
    const auto& r = bounds_;
    if (r.width <= 0 || r.height <= 0 || value->x < r.x || value->y < r.y || value->x >= r.x + r.width ||
        value->y >= r.y + r.height) {
        return false;
    }
    if (ellipse_) {
        const double x = (value->x - r.x) * 2 / r.width - 1, y = (value->y - r.y) * 2 / r.height - 1;
        return x * x + y * y <= 1;
    }
    return true;
}

auto InputRouter::regions(std::vector<HitRegion> values) -> PointerResult {
    std::set<uint32_t> ids;
    for (const auto& region : values) {
        if (!region.id || !ids.insert(region.id).second) {
            throw std::invalid_argument("Interaction targets need unique nonzero IDs");
        }
    }
    const auto oldHover = hovered_, oldPressed = pressed_;
    const auto* old = find(pressed_);
    const auto oldBehavior = old ? old->behavior : PointerBehavior::Button;
    regions_ = std::move(values);
    PointerResult result;
    const auto* current = find(pressed_);
    if (!current || current->behavior != oldBehavior) {
        result.target = pressed_;
        result.drag = captured_ ? DragPhase::Cancel : DragPhase::None;
        pressed_ = 0;
        captured_ = false;
    }
    if (!find(hovered_)) {
        hovered_ = 0;
    }
    result.changed = oldHover != hovered_ || oldPressed != pressed_;
    return result;
}

auto InputRouter::hit(Point point) const -> uint32_t {
    const HitRegion* best = nullptr;
    for (const auto& region : regions_) {
        if (!region.enabled || (best && region.priority < best->priority) || !region.shape.contains(point)) {
            continue;
        }
        if (std::all_of(region.clips.begin(), region.clips.end(), [&](const auto& clip) {
                return clip.contains(point);
            })) {
            best = &region;
        }
    }
    return best ? best->id : 0;
}

auto InputRouter::dispatch(PointerEvent event) -> PointerResult {
    const auto beforeHover = hovered_, beforePressed = pressed_;
    PointerResult result;
    const auto finish = [&] {
        result.changed = beforeHover != hovered_ || beforePressed != pressed_ || result.drag != DragPhase::None;
        return result;
    };
    if (event.kind == PointerKind::Cancel) {
        result.target = pressed_;
        result.drag = captured_ ? DragPhase::Cancel : DragPhase::None;
        hovered_ = pressed_ = 0;
        captured_ = false;
        return finish();
    }
    if (event.kind == PointerKind::Leave) {
        hovered_ = 0;
        if (!captured_) {
            pressed_ = 0;
        }
        return finish();
    }
    if (!std::isfinite(event.position.x) || !std::isfinite(event.position.y)) {
        return result;
    }
    hovered_ = hit(event.position);
    if (event.kind == PointerKind::Press && event.left && !pressed_) {
        pressed_ = hovered_;
        origin_ = event.position;
        const auto* region = find(pressed_);
        captured_ = region && region->behavior == PointerBehavior::Drag;
        result.drag = captured_ ? DragPhase::Begin : DragPhase::None;
    } else if (event.kind == PointerKind::Move && pressed_) {
        if (!event.left) {
            result.target = pressed_;
            result.drag = captured_ ? DragPhase::Cancel : DragPhase::None;
            pressed_ = 0;
            captured_ = false;
        } else if (captured_) {
            result.drag = DragPhase::Update;
        } else if (std::hypot(event.position.x - origin_.x, event.position.y - origin_.y) > 5) {
            pressed_ = 0;
        }
    }
    if (pressed_) {
        result.target = pressed_;
        const auto* region = find(pressed_);
        if (region) {
            result.local = region->shape.local(event.position).value_or(Point{});
        }
    }
    if (event.kind == PointerKind::Release && !event.left) {
        if (pressed_ && !captured_ && pressed_ == hovered_ &&
            std::hypot(event.position.x - origin_.x, event.position.y - origin_.y) <= 5) {
            result.clicked = pressed_;
        }
        result.drag = captured_ ? DragPhase::End : DragPhase::None;
        pressed_ = 0;
        captured_ = false;
    }
    return finish();
}

auto InputRouter::hovered() const -> uint32_t {
    return hovered_;
}

auto InputRouter::pressed() const -> uint32_t {
    return pressed_;
}

auto InputRouter::captured() const -> bool {
    return captured_;
}

auto InputRouter::find(uint32_t id) const -> const HitRegion* {
    const auto it = std::find_if(regions_.begin(), regions_.end(), [&](const auto& value) {
        return id && value.id == id && value.enabled;
    });
    return it == regions_.end() ? nullptr : &*it;
}

auto InteractionFeedback::target(double value) -> void {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("Invalid feedback target");
    }
    value = std::clamp(value, 0., 1.);
    if (value != to_) {
        from_ = value_;
        to_ = value;
        seconds_ = 0;
    }
}

auto InteractionFeedback::advance(double seconds) -> bool {
    if (!std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid feedback time");
    }
    const auto before = value_;
    seconds_ = std::min(.14, seconds_ + seconds);
    value_ = Tween<double>(from_, to_, {.14, Ease::OutCubic}).sample(seconds_).value;
    return before != value_;
}

auto InteractionFeedback::active() const -> bool {
    return value_ != to_;
}

auto InteractionFeedback::value() const -> double {
    return value_;
}
}  // namespace cybercanvas
