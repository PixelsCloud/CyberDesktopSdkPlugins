#pragma once

#include <CyberCanvas/Native.h>
#include <CyberCanvas/Physics2D.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Interaction.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "plugins/marble-clock/Model.h"

namespace marbleclock {
struct Appearance {
    bool light = false, background = true, english = false;
};

struct DriveGeometry {
    Point center, lever, crank;
    double angle = 0;
};

DriveGeometry driveGeometry(const Geometry& shape, double angle);

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& assets = {}, bool compact = false);
    bool pollAssets();
    bool pendingAssets() const;
    void pauseAssets(bool paused);
    cdsdk::ImageSlotStats assetStats() const;
    void draw(cybercanvas::Canvas& canvas, const Model& model, Appearance appearance, double scale);

private:
    void stationary(cybercanvas::Canvas& canvas, const Geometry& shape, Appearance appearance);
    cybercanvas::OffscreenSurface backdrop_;
    cdsdk::Images images_;
    std::filesystem::path assetRoot_;
    std::unique_ptr<cybercanvas::Path> gearOutline_;
    cybercanvas::ComPtr<ID2D1Factory> gearFactory_;
    uint64_t assetRevision_ = 0, cachedAssetRevision_ = 0;
    double scale_ = 0;
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
    bool frozen() const;
    bool synchronize(bool animate);
    void schedule();
    void cancel();
    void arrange();
    Model model_;
    cdsdk::Canvas canvas_;
    Renderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{0, 0, 600, 200};
    Point origin_{};
    double scale_ = 1;
    std::optional<uint64_t> lastSecond_;
    bool hidden_ = false, arranged_ = false;
};
}  // namespace marbleclock
