#pragma once

#include <CyberCanvas/Physics2D.h>
#include <sdk/cpp/Services.h>

#include <array>
#include <memory>

#include "plugins/redline-desk/Skin.h"

namespace redline {
namespace physics = cybercanvas::physics;

struct Striker {
    physics::Body body, target;
    physics::Joint joint;
    Point rest, contact;
    double radius = 0, phaseTime = 0, flash = 0;
    unsigned phase = 0, hits = 0;
    bool hat = false;
};

class KineticModel {
public:
    KineticModel(int kind, Size size);
    bool advance(double seconds);
    void strike(unsigned channel, double strength = 1);
    void beginDrag(Point position);
    void drag(Point position);
    void cancelDrag();
    void quiet();
    bool active() const;
    void draw(Canvas& canvas, const std::function<void(std::string_view, Rect)>& sprite = {});
    physics::PhysicsStats stats() const;
    unsigned hits() const;
    std::vector<physics::DebugBody> debugBodies() const;
    const std::vector<Striker>& strikers() const;

private:
    void step();
    int kind_;
    Size size_;
    physics::World world_;
    physics::Body ground_;
    std::vector<physics::Body> rotors_;
    std::vector<physics::Joint> rotorJoints_;
    std::vector<Point> pivots_;
    std::vector<Striker> strikers_;
    physics::Joint drag_;
    double accumulator_ = 0;
    unsigned alternate_ = 0;
    unsigned escapementSteps_ = 0;
    double previousAngle_ = 0;
};

CdResult createKinetic(int kind, const CdCreateContext* context, CdInstanceApi* output, uint32_t size);
}  // namespace redline
