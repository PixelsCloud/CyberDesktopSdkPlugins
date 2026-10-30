#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include "CyberCanvas/Animation.h"
#include "CyberCanvas/Path.h"

namespace cybercanvas {
// Logical coordinates throughout; the host converts physical pixels once.
Point logicalPointer(Point pixels, double dpr);

class HitShape {
public:
    static HitShape rectangle(Rect bounds);

    static HitShape ellipse(Rect bounds);

    static HitShape path(Path& path, ID2D1Factory* factory);

    HitShape transformed(const D2D1_MATRIX_3X2_F& matrix) const;

    std::optional<Point> local(Point point) const;

    bool contains(Point point) const;

private:
    Rect bounds_{};
    bool ellipse_ = false;
    ComPtr<ID2D1Geometry> geometry_;
    D2D1::Matrix3x2F transform_ = D2D1::Matrix3x2F::Identity();
};

enum class PointerKind { Move, Press, Release, Leave, Cancel };
enum class PointerBehavior { Button, Drag };
enum class DragPhase { None, Begin, Update, End, Cancel };

struct PointerEvent {
    PointerKind kind = PointerKind::Move;
    Point position{};
    bool left = false;
};

struct HitRegion {
    uint32_t id = 0;
    HitShape shape;
    PointerBehavior behavior = PointerBehavior::Button;
    int priority = 0;
    bool enabled = true;
    // Each clip is in the same logical/world coordinates as the pointer.
    std::vector<HitShape> clips;
};

struct PointerResult {
    uint32_t target = 0, clicked = 0;
    DragPhase drag = DragPhase::None;
    Point local{};
    bool changed = false;
};

// A small retained target list, with no widget tree, timers, or platform window.
// Higher priority wins; equal priorities use the last registered target.
class InputRouter {
public:
    PointerResult regions(std::vector<HitRegion> values);

    uint32_t hit(Point point) const;

    PointerResult dispatch(PointerEvent event);

    uint32_t hovered() const;

    uint32_t pressed() const;

    bool captured() const;

private:
    const HitRegion* find(uint32_t id) const;

    std::vector<HitRegion> regions_;
    uint32_t hovered_ = 0, pressed_ = 0;
    Point origin_{};
    bool captured_ = false;
};

// Retargetable, time-based feedback; interruptions start at the visible value.
class InteractionFeedback {
public:
    void target(double value);

    bool advance(double seconds);

    bool active() const;

    double value() const;

private:
    double from_ = 0, to_ = 0, value_ = 0, seconds_ = .14;
};
}  // namespace cybercanvas
