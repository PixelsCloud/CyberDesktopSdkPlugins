#pragma once

#include <CyberCanvas/Physics2D.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Interaction.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "plugins/beat-machine/Model.h"

namespace beatmachine {
struct Appearance {
    bool light = false, background = true, english = false;
    uint32_t status = CD_DATA_READY;
};

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& assets = {}, bool compact = false);
    void bind(const CdCreateContext& context);
    bool pollAssets();
    bool pendingAssets() const;
    cdsdk::ImageSlotStats assetStats() const;
    void pauseAssets(bool paused);
    void draw(cybercanvas::Canvas& canvas, const Model& model, const Signal& signal, Appearance appearance,
              double scale);

private:
    void stationary(cybercanvas::Canvas& canvas, const Model& model, Appearance appearance);
    cdsdk::Images images_;
    std::filesystem::path assetRoot_;
    cybercanvas::OffscreenSurface backdrop_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> owner_;
    uint32_t width_ = 0, height_ = 0;
    double dpi_ = 0, scale_ = 0;
    uint64_t assetRevision_ = 0, cachedAssetRevision_ = 0;
    Appearance appearance_;
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

    std::array<uint64_t, 4> physicsCounters() const;

private:
    void subscribe();
    void cancel();
    void schedule();
    void arrange();
    bool frozen() const;
    CdCreateContext context_;
    Model model_;
    AudioMotion audio_;
    Renderer renderer_;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    cdsdk::AudioWatch watch_;
    cdsdk::AudioStatusWatch statusWatch_;
    CdRect safe_{0, 0, 600, 200};
    Point origin_{};
    double scale_ = 1;
    uint64_t lastAudio_ = 0;
    uint32_t status_ = CD_DATA_LOADING;
    bool hidden_ = false, arranged_ = false;
};
}  // namespace beatmachine
