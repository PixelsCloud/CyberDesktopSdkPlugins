#pragma once

#include <memory>

#include "plugins/beat-machine/Model.h"

namespace beatmachine {
class Background {
public:
    Background(const CdCreateContext& context, unsigned count, bool compact, GeometryStyle style, double radius);
    ~Background();
    void consume(Model& view);
    void pause(bool paused);
    void configure(const std::filesystem::path& assets);
    void reset(unsigned count);
    void advance(double seconds, const Signal& signal);
    void strike(unsigned index, double strength);
    void nudge(unsigned index);
    void quiet();
    bool beginDrag(uint32_t token, Point point);
    void drag(Point point);
    void cancelDrag();
    bool active() const;
    bool dragging() const;
    physics::BodyState state(physics::Body body) const;
    physics::PhysicsStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace beatmachine
