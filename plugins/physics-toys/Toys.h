#pragma once

#include <CyberCanvas/Noise.h>
#include <CyberCanvas/Physics2D.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Interaction.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "plugins/physics-toys/Model.h"

namespace toys {
struct Appearance {
    bool dark = true, background = true, twelve = false;
    std::string palette = "pastel";
};

void draw(cybercanvas::Canvas& canvas, const Model& model, const Appearance& appearance);

class MechanicalRenderer {
public:
    MechanicalRenderer();
    ~MechanicalRenderer();
    void bind(const CdCreateContext& context, std::function<void()> invalidate);
    void pause(bool paused);
    bool pending() const;
    void draw(cybercanvas::Canvas& canvas, const Model& model, const Appearance& appearance, double scale);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Instance {
public:
    Instance(const CdCreateContext& context, Kind kind, bool mechanical = false);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();

private:
    void synchronizeTime();
    void schedule();
    void cancel();
    bool frozen() const;
    Model model_;
    std::unique_ptr<MechanicalRenderer> mechanical_;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    Point origin_{};
    double scale_ = 1;
    bool hidden_ = false, compact_ = false, dark_ = true, laidOut_ = false;
};

template <Kind kind, bool mechanical = false>
class Plugin : public Instance {
public:
    explicit Plugin(const CdCreateContext& context) : Instance(context, kind, mechanical) {}
};
}  // namespace toys
