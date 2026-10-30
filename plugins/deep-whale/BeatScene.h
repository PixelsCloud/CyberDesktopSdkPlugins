#pragma once

#include <CyberCanvas/Physics2D.h>

#include <memory>
#include <vector>

namespace beatmachine {
struct Signal;
}

namespace deep_motion {
using namespace cybercanvas;
namespace ph = cybercanvas::physics;
inline constexpr double kUnits = 20;

// One Box2D scene is shared by the runtime and the motion regression verifier.
struct BeatScene {
    struct Piece {
        ph::Body body;
        double radius = 0, length = 0;
        Point anchor;
        Rect art;
        unsigned color = 0;
    };

    std::unique_ptr<ph::World> world;
    ph::Body ground, floor;
    std::vector<Piece> balls, pendulums;
    Rect field;
    double time = 0, pulse = 0, floorY = 0, strokeAge = 10, strokeStrength = 0;
    uint64_t beat = 0, accent = 0;

    void resetBeat(Rect area, unsigned count, bool compact);

    void stepBeat(double dt, const beatmachine::Signal& signal);
};
}  // namespace deep_motion
