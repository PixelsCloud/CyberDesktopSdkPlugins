// Controller binding reused from plugins/vampire-alice/controllers/sign/Sign.h.
#pragma once

#include "plugins/deep-whale/Skin.h"
#include "plugins/mechanical-sign/Sign.h"

namespace deep_sign {
using namespace mechanicalsign;

Geometry deepGeometry();

class Renderer {
public:
    explicit Renderer(const CdCreateContext& context);
    bool poll();
    bool pending() const;
    void pause(bool paused);
    cdsdk::ImageSlotStats assets() const;
    void draw(cybercanvas::Canvas& canvas, const Model& model, bool background, double scale,
              Format format = Format::Standard, double aspect = 0);

    whaleskin::View view(CdRect safe, cybercanvas::Size world);

private:
    std::unique_ptr<whaleskin::Skin> skin_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);

private:
    bool enabled(const cdsdk::Settings& value) const;
    CdResult schedule(const Model& model, const cdsdk::Settings& value);
    void arrange();
    void trigger();
    void invalidate();
    static void CD_CALL wake(void* user, uint64_t now) noexcept;
    Model model_;
    cdsdk::Canvas canvas_;
    Renderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    cdsdk::Timer wake_;
    CdRect safe_{0, 0, 600, 200};
    cybercanvas::Point origin_{};
    double scale_ = .5;
    bool hidden_ = false, arranged_ = false;
    Format format_ = Format::Standard;
    bool layoutKnown_ = false;
};

}  // namespace deep_sign
