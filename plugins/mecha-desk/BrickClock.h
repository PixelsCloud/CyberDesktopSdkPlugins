#pragma once
#include "plugins/brick-clock/Clock.h"
#include "plugins/mecha-desk/Skin.h"

namespace aeris_brick {
using namespace brickclock;

class Renderer {
public:
    explicit Renderer(const CdCreateContext&);
    void pause(bool);
    aerisskin::View view(CdRect);
    void draw(cybercanvas::Canvas&, const Model&, const std::string&, double);

private:
    aerisskin::Skin skin_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    void input(const CdInput& input);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();

private:
    bool synchronize(bool animate);
    bool frozen() const;
    void schedule();
    Model model_;
    cdsdk::Canvas canvas_;
    Renderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    std::optional<uint64_t> lastSecond_;
    bool hidden_ = false;
};
}  // namespace aeris_brick
