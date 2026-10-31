#pragma once

#include <array>
#include <cstdint>

#include "plugins/brick-clock/Clock.h"
#include "plugins/pixel-desk/BasicClock.h"

namespace pixeldesk {
inline constexpr BasicSpec kPixelBrick{"com.cyberdesktop.pixeldesk.clock.brick.4x1",
                                       "clock-brick-4x1",
                                       BasicKind::Digital,
                                       4,
                                       1,
                                       {2170, 725},
                                       {15, 31, 2140, 665},
                                       {},
                                       0};

bool drawPixelBrick(BasicFace& face, const brickclock::Model& model, const cdsdk::Settings& settings,
                    const CdFrame& frame, CdRect safe);

class BrickInstance {
public:
    explicit BrickInstance(const CdCreateContext& context);
    ~BrickInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();

    std::array<uint64_t, 4> physicsCounters() const;

private:
    bool synchronize(bool animate);
    bool frozen() const;
    void schedule();
    brickclock::Model model_;
    BasicFace renderer_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    std::optional<uint64_t> lastSecond_;
    bool hidden_ = false;
};
}  // namespace pixeldesk
