#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <memory>

namespace cybercanvas::physics {
struct PhysicsStats;
}

namespace streetlink {
// Lifecycle and input adapter for the authored channel / magnetic-drum scenes.
// Box2D and the shared audio onset detector own the actual motion.
class Physics {
public:
    explicit Physics(const CdCreateContext& context);
    ~Physics();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    std::array<uint64_t, 4> physicsCounters() const;
    cybercanvas::physics::PhysicsStats physicsStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace streetlink
