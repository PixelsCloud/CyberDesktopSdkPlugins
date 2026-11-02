#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "plugins/physics-toys/Model.h"

struct CdCreateContext;

namespace toys {
// UI-side model adapter. Static geometry stays in the plugin module and motion
// is copied only from a completed, leased POD frame. No method reads a World.
class Background {
public:
    Background(const CdCreateContext& context, Kind kind, bool compact, bool mechanical, ModelOptions options,
               const ClockLayout* clock = nullptr, const ChimeLayout* chime = nullptr);
    ~Background();
    void consume();
    void pause(bool paused);
    void reset(bool compact);
    void configure(const std::filesystem::path& assets);
    bool fit(Size size, cybercanvas::Rect limits, const std::function<Point(Point)>& map);
    void parameters(double wind, double speed);
    void setTime(int hour, int minute, int second, bool twelve, bool animate);
    void setWindSeed(uint32_t seed);
    void pluck();
    void beginDrag(size_t index, Point target);
    void drag(Point target);
    void cancelDrag();
    bool active() const;
    Size size() const;
    const std::vector<Piece>& pieces() const;
    physics::BodyState state(size_t index) const;
    Point anchor(const Piece& piece) const;
    Point attachment(const Piece& piece) const;
    physics::PhysicsStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace toys
