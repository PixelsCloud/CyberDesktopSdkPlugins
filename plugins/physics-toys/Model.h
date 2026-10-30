#pragma once

#include <CyberCanvas/Noise.h>
#include <CyberCanvas/Physics2D.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace toys {
namespace physics = cybercanvas::physics;
using cybercanvas::Point;
using cybercanvas::Size;
enum class Kind { Chime, Clock };

struct Piece {
    physics::Body body, parent;
    Point anchor, attachment;
    Size half;
    unsigned art = 0;
};

// Theme-authored clock geometry in the same meter/world units as Piece.
struct ClockLayout {
    Size size{360, 112};
    double top = .27, length = .18, halfWidth = .20, halfHeight = .31;
    std::array<double, 6> centers{.40, .89, 1.52, 2.01, 2.64, 3.13};
};

struct ChimePieceLayout {
    Point center, anchor, attachment;
    Size half;
    int parent = -1;
    unsigned art = 0;
};

struct ChimeLayout {
    Size size;
    std::array<ChimePieceLayout, 6> pieces;
};

struct ModelOptions {
    bool illustrated = false;
    bool pixels = false;
    bool separateEnclosure = false;
    double wallInset = -.05;
    Point dragMargin{.1, .1};
};

class Model {
public:
    explicit Model(Kind kind, bool compact = false, bool mechanical = false, ModelOptions options = {});
    void configure(const std::filesystem::path& assets);
    bool fit(Size size, cybercanvas::Rect limits, std::function<Point(Point)> map);
    Model(Kind kind, bool compact, bool mechanical, const ClockLayout& layout);
    explicit Model(const ChimeLayout& layout);
    void reset(bool compact);
    bool advance(double seconds, double wind, double speed);
    bool setTime(int hour, int minute, int second, bool twelve, bool animate = true);
    void pluck();
    void beginDrag(size_t index, Point target);
    void drag(Point target);
    void cancelDrag();
    bool active() const;
    Size size() const;
    Point anchor(const Piece& piece) const;
    Point attachment(const Piece& piece) const;
    const std::vector<Piece>& pieces() const;
    physics::BodyState state(size_t index) const;
    physics::PhysicsStats stats() const;
    const std::wstring& digits() const;
    bool afternoon() const;
    Kind kind() const;
    bool mechanical() const;
    void setWindSeed(uint32_t seed);

private:
    Kind kind_;
    ModelOptions options_;
    std::optional<ClockLayout> clockLayout_;
    std::optional<ChimeLayout> chimeLayout_;
    bool compact_ = false, afternoon_ = false, mechanical_ = false;
    double time_ = 0, accumulator_ = 0;
    std::wstring digits_;
    std::unique_ptr<physics::World> world_;
    physics::Body ground_;
    physics::Joint drag_;
    std::optional<size_t> draggedPiece_;
    std::vector<Piece> pieces_;
    cybercanvas::NoiseField breeze_{{.seed = 721, .frequency = .6}};
    Size registeredSize_{};
    Size fittedSize_{};
    cybercanvas::Rect limits_{};
    std::function<Point(Point)> layoutMap_;
    std::vector<Point> registeredAnchors_;
    std::vector<cybercanvas::Rect> registeredBodies_, registeredTails_;
};

}  // namespace toys
