#pragma once

#include <memory>

#include "plugins/brick-clock/Clock.h"

namespace brickclock {
class Background {
public:
    Background(const CdCreateContext& context, EnclosureGeometry enclosure, uint32_t seed);
    ~Background();
    void consume(Model& view);
    void pause(bool paused);
    void setTime(int clock, bool animate, bool continuous);
    void settle();
    void replay();
    void setChipCount(unsigned count);
    bool active() const;
    const std::vector<Chip>& chips() const;
    physics::BodyState state(const Chip& chip) const;
    physics::PhysicsStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace brickclock
