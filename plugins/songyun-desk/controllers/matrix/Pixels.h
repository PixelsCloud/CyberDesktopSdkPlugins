#pragma once
#include <CyberCanvas/Sprites.h>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/songyun-desk/Skin.h"

namespace songyun_matrix {
using namespace mechanicalpixels;

class Renderer final : public mechanicalpixels::Face {
public:
    explicit Renderer(const CdCreateContext& context);
    ~Renderer() override;
    void onChange(std::function<void()> callback) override;
    void pause(bool paused) override;
    bool ready() const;
    bool render(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colonVisible,
                const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) override;
    uint64_t cacheBuilds() const;
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    std::unique_ptr<songyun::Skin> skin_;
    cybercanvas::PrimitiveSprites sprites_;
};

// Theme construction supplies only artwork and defaults. Subscription, clock,
// FFT, scheduling and pause/state behavior belong to the shared controller.
class Instance final : public mechanicalpixels::Instance {
public:
    explicit Instance(const CdCreateContext& context);
};

}  // namespace songyun_matrix
