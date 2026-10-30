#pragma once

#include "plugins/marble-clock/Clock.h"
#include "plugins/songyun-desk/Skin.h"

namespace songyun_marble {
namespace physics = cybercanvas::physics;
using cybercanvas::Point;
inline constexpr double kHeight = 360, kPixelsPerMeter = 500, kRadius = 10.5, kFloorY = 265;
inline constexpr size_t kMaximumMarbles = 72;
inline constexpr double kOpenAngle = -.10471975511965977;
inline constexpr double kClosedAngle = 0;

struct Segment {
    Point first, second;
    double radius = 4.5;
};

struct Geometry {
    double width = 1080, left = 118, pivot = 588;
    double floorY = kFloorY;
    std::vector<Segment> track, enclosure, returning;
};

Geometry geometry(bool compact);
double floorHeight(const Geometry& shape, double x);
enum class Phase { Collecting, Opening, Draining, Closing };

struct Marble {
    physics::Body body;
    uint32_t token = 0;
    uint64_t generation = 0;
    double born = 0;
};

class Model {
public:
    explicit Model(bool compact = false);
    void configure(const std::filesystem::path& assets);
    bool setTime(int hour, int minute, int second, bool animate = true);
    bool advance(double seconds);
    bool release();
    void drain();
    void quiet();
    bool beginDrag(uint32_t token, Point logical);
    void drag(Point logical);
    void cancelDrag();
    bool dragging() const;
    bool active() const;
    const Geometry& shape() const;
    const std::vector<Marble>& marbles() const;
    physics::BodyState state(const Marble& marble) const;
    double flapAngle() const;
    double motionTime() const;
    double feederAngle() const;
    double digitProgress() const;
    std::wstring previousTimeText() const;
    Phase phase() const;
    unsigned pending() const;
    unsigned oldMarbles() const;
    unsigned second() const;
    std::wstring timeText() const;
    physics::PhysicsStats stats() const;
    uint64_t contacts() const;
    uint64_t recycled() const;
    uint64_t recoveries() const;
    uint64_t releases() const;

private:
    void reset();
    bool spawn();
    void add(Point logical, bool launched);
    void remove(size_t index, bool recovered = false);
    void phaseStep(double delta);
    Geometry geometry_;
    std::unique_ptr<physics::World> world_;
    physics::Body ground_, flap_, dragged_;
    physics::Joint hinge_, mouse_;
    std::vector<Marble> marbles_;
    Phase phase_ = Phase::Collecting;
    int clock_ = -1;
    double time_ = 0, accumulator_ = 0, drainTime_ = 0, phaseTime_ = 0, releaseDelay_ = 0;
    double lastRelease_ = -1;
    double clockChanged_ = -1;
    unsigned pending_ = 0;
    uint32_t nextToken_ = 0;
    uint64_t generation_ = 0, contacts_ = 0, recycled_ = 0, recoveries_ = 0, releases_ = 0;
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

    songyun::View view(CdRect safe, cybercanvas::Size world);
    cybercanvas::Rect control(std::string_view name, cybercanvas::Size world) const;

private:
    std::unique_ptr<songyun::Skin> skin_;
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

}  // namespace songyun_marble
