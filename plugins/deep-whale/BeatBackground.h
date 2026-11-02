#pragma once

#include <memory>

#include "plugins/deep-whale/BeatScene.h"

namespace deep_motion {
class BeatBackground {
public:
    explicit BeatBackground(const CdCreateContext& context);
    ~BeatBackground();
    void reset(Rect area, unsigned count, bool compact);
    void step(double dt, const beatmachine::Signal& signal);
    void consume(BeatScene& view);
    void pause(bool paused);
    bool active() const;
    ph::PhysicsStats stats() const;
    ph::BodyState state(ph::Body body) const;
    bool beginDrag(ph::Body body, Point target);
    void drag(Point target);
    void cancelDrag();
    void scatter(Point point);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace deep_motion
