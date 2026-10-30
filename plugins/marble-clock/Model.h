#pragma once

#include <CyberCanvas/Physics2D.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace marbleclock {
namespace physics = cybercanvas::physics;
using cybercanvas::Point;
inline constexpr double kHeight = 360, kPixelsPerMeter = 500, kRadius = 8.5, kFloorY = 265;
inline constexpr size_t kMaximumMarbles = 72;
inline constexpr double kOpenAngle = -.10471975511965977;
inline constexpr double kClosedAngle = -.03490658503988659;
inline constexpr double kPixelRadius = 11.5;

struct Segment {
    Point first, second;
    double radius = 4.5;
};

enum class GeometryStyle { Classic, Illustrated, ExtendedCompact, Pixels };

struct Geometry {
    double width = 1080, left = 118, pivot = 588;
    double floorY = kFloorY, closedAngle = kClosedAngle;
    std::vector<Segment> track, enclosure, returning;
};

Geometry geometry(bool compact, GeometryStyle style = GeometryStyle::Classic);
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
    explicit Model(bool compact = false, GeometryStyle style = GeometryStyle::Classic);
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
    GeometryStyle style_;
    double radius_, closedAngle_, openAngle_;
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

}  // namespace marbleclock
