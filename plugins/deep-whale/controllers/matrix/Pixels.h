// Controller binding reused from plugins/vampire-alice/controllers/matrix/Pixels.h.
#pragma once

#include <CyberCanvas/Sprites.h>

#include <memory>

#include "plugins/mechanical-pixels/Pixels.h"

namespace whaleskin {
class Skin;
}

namespace deep_matrix {
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
    std::unique_ptr<whaleskin::Skin> skin_;
    cybercanvas::SpriteAtlas beads_;
    cybercanvas::OffscreenSurface dimGrid_;
    cybercanvas::SpriteList dots_;
    std::string beadKey_;
    ID2D1RenderTarget* beadOwner_ = nullptr;
};

// Theme construction supplies only artwork and defaults. Subscription, clock,
// FFT, scheduling and pause/state behavior belong to the shared controller.
class Instance final : public mechanicalpixels::Instance {
public:
    explicit Instance(const CdCreateContext& context);
};

}  // namespace deep_matrix
