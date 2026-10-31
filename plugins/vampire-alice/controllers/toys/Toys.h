#pragma once

#include <array>
#include <cstdint>

#include "plugins/physics-toys/Toys.h"
#include "plugins/vampire-alice/Skin.h"

namespace alice_toys {
using namespace toys;

class Model final : public toys::Model {
public:
    explicit Model(Kind kind, bool compact = false, bool mechanical = false);
};

class MechanicalRenderer {
public:
    MechanicalRenderer();
    ~MechanicalRenderer();
    void bind(const CdCreateContext& context, std::function<void()> invalidate);
    void pause(bool paused);
    bool pending() const;
    void draw(cybercanvas::Canvas& canvas, const Model& model, const Appearance& appearance, double scale);

    alice::View view(CdRect safe, cybercanvas::Size world);

private:
    std::unique_ptr<alice::Skin> skin_;
    std::array<cybercanvas::OffscreenSurface, 6> cards_;
    std::array<wchar_t, 6> glyphs_{};
    double density_ = 0;
    uint64_t artworkRevision_ = 0;
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

    std::array<uint64_t, 4> physicsCounters() const;

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

template <Kind kind, bool mechanical = true>
class Plugin : public Instance {
public:
    explicit Plugin(const CdCreateContext& context) : Instance(context, kind, mechanical) {}
};

}  // namespace alice_toys
