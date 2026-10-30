#pragma once

#include "plugins/brick-clock/Clock.h"
#include "plugins/mechanical-desk/BasicClock.h"

namespace mechanicaldesk {
extern const BasicSpec kMechanicalBrick;

bool drawMechanicalBrick(BasicFace& face, const brickclock::Model& model, const cdsdk::Settings& settings,
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
}  // namespace mechanicaldesk
