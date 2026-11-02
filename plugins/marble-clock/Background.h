#pragma once

#include <memory>

#include "plugins/marble-clock/Model.h"

namespace marbleclock {
class Background {
public:
    Background(const CdCreateContext& context, bool compact, GeometryStyle style);
    ~Background();
    void consume(Model& view);
    void pause(bool paused);
    void configure(const std::filesystem::path& assets);
    void setTime(int clock, bool animate, bool continuous);
    void quiet();
    bool release();
    void drain();
    bool beginDrag(uint32_t token, Point point);
    void drag(Point point);
    void cancelDrag();
    bool active() const;
    bool dragging() const;
    physics::BodyState state(const Marble& marble) const;
    double flapAngle() const;
    physics::PhysicsStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace marbleclock
