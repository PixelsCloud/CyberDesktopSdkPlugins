#pragma once

#include <memory>

#include "plugins/streetlink-desk/PhysicsModel.h"

namespace streetlink {
class PhysicsBackground {
public:
    explicit PhysicsBackground(const CdCreateContext& context);
    ~PhysicsBackground();
    void reset(const PhysicsModel& view, unsigned count);
    void consume(PhysicsModel& view);
    void pause(bool paused);
    void spawn(unsigned index, bool preload);
    void advance(double dt, const beatmachine::Signal& signal, bool audioActive, double phase);
    void kick(double strength, double phase);
    bool beginDrag(size_t index, Point point);
    void drag(Point point);
    void cancelDrag();
    bool active() const;
    ph::BodyState state(ph::Body body) const;
    ph::PhysicsStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace streetlink
