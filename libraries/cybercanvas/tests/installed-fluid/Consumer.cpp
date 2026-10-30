#include <CyberCanvas/Fluid.h>
#include <CyberCanvas/PhysicsParticles.h>

#include <iostream>

int main() {
    using namespace cybercanvas;
    FluidSystem fluid({.maximum = 8});
    const FluidSample sample{{20, 20}, {}};
    fluid.add(std::span(&sample, 1));
    fluid.advance(1. / 120);
    PhysicsParticleEmitter rigid;
    rigid.emitter().burst(1);
    rigid.emitter().advance(1. / 120);
    const auto offset = rigid.world().createBody({.position = {1, 1}});
    rigid.world().box(offset, {.1, .1}, {.5, 0});
    const auto center = rigid.world().centerOfMass(offset);
    if (fluid.stats().steps != 1 || fluid.snapshot().particles[0].velocity.y <= 0 || rigid.world().stats().steps != 1 ||
        std::abs(center.x - 1.5) > .001) {
        return 1;
    }
    std::cout << "PASS installed optional Fluid/PhysicsParticles targets, units and static backend\n";
    return 0;
}
