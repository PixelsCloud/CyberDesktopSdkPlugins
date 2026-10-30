#pragma once

#include "plugins/marble-clock/Clock.h"
#include "plugins/redline-desk/Skin.h"

namespace redline_marble {
namespace physics = cybercanvas::physics;
using cybercanvas::Point;
inline constexpr double kHeight = 360, kPixelsPerMeter = 500, kRadius = 10.5, kFloorY = 265;
inline constexpr size_t kMaximumMarbles = 72;
inline constexpr double kOpenAngle = -.10471975511965977;
inline constexpr double kClosedAngle = 0;

using marbleclock::Geometry;
using marbleclock::Marble;
using marbleclock::Phase;
using marbleclock::Segment;
Geometry geometry(bool compact);
using marbleclock::floorHeight;

class Model final : public marbleclock::Model {
public:
    explicit Model(bool compact = false);
};

using marbleclock::Appearance;

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& assets = {}, bool compact = false);
    bool pollAssets();
    bool pendingAssets() const;
    void pauseAssets(bool paused);
    cdsdk::ImageSlotStats assetStats() const;
    void draw(cybercanvas::Canvas& canvas, const Model& model, Appearance appearance, double scale);

    redline::View view(CdRect safe, cybercanvas::Size world);
    cybercanvas::Rect control(std::string_view name, cybercanvas::Size world) const;

private:
    std::unique_ptr<redline::Skin> skin_;
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

}  // namespace redline_marble
