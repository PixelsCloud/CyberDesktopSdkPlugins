#pragma once

#include <array>
#include <cstdint>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"
#include "plugins/songyun-desk/Skin.h"

namespace songyun_hourglass {
using namespace mechanicalhourglass;

class Renderer {
public:
    explicit Renderer(const CdCreateContext& context);
    ~Renderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(bool timer, const Model& model, const PhysicalSand* sand, const cdsdk::Settings& settings,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    std::unique_ptr<songyun::Skin> skin_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    const Model& model() const;

    std::array<uint64_t, 4> physicsCounters() const;

private:
    const Spec& spec_;
    CdHostApi host_{};
    void* context_ = nullptr;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    Renderer renderer_;
    Model model_;
    PhysicalSand sand_;
    CdRect safe_{};
    uint64_t lastTick_ = 0;
    bool paused_ = false, pressed_ = false;
    double pressX_ = 0, pressY_ = 0;
    void tick(uint64_t now);
    CdResult subscribe(const cdsdk::Settings& settings, bool restarting, cdsdk::Timer& timer);
    void restart();
};

}  // namespace songyun_hourglass
