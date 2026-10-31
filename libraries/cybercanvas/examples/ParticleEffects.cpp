#include <CyberCanvas/Fluid.h>
#include <CyberCanvas/PhysicsParticles.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>

// Native trajectories for offline playback; the browser contains no physics solver.
namespace {
using namespace cybercanvas;
using Json = nlohmann::json;
constexpr double kStep = 1. / 120;
constexpr unsigned kFrames = 240, kFps = 60;

struct Definition {
    const char *id, *name, *group, *kind, *description;
};

const Definition kScenes[]{
    {"sparks", "金属火花", "collision", "rigid", "火花击中斜板后反弹；重力、摩擦和寿命共同改变轨迹。"},
    {"rain", "屋檐落雨", "nature", "rigid", "快速雨滴受侧风影响，击打屋檐后飞溅，再落到地面。"},
    {"snow", "风中飘雪", "nature", "rigid", "连续风场推动雪花，落地后停留并按寿命消失。"},
    {"petals", "花瓣飘落", "nature", "rigid", "细长碰撞体在风力下飘落，花瓣方向跟随刚体旋转。"},
    {"debris", "旋转碎屑", "collision", "rigid", "方形碎屑撞击台阶、相互碰撞，最终堆积在底部。"},
    {"balls", "弹性碰撞", "collision", "rigid", "弹性小球交换动量，在封闭边界内反复弹跳。"},
    {"fireflies", "林间萤火", "atmosphere", "light", "轻量粒子受连续风场驱动，叠加柔光与明暗呼吸。"},
    {"smoke", "轻烟浮升", "atmosphere", "light", "上升加速度与侧风推动烟粒，粒子逐渐扩散、变淡。"},
    {"water", "清水倾落", "fluid", "fluid", "低黏性液体倾落后向两侧铺开，压力求解与实体边界限制运动。"},
    {"syrup", "黏液倾落", "fluid", "fluid", "与清水使用相同初态；提高黏性后，剪切运动衰减得更快。"},
    {"fountain", "碰撞喷泉", "fluid", "fluid", "持续注入的液柱撞击圆形障碍，分流、飞溅并回落成池。"},
    {"stirred", "机械搅拌", "fluid", "fluid", "Box2D 电机驱动搅拌叶；液体受挡板带动，反作用冲量回传刚体。"},
};

double rounded(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1000000) {
        throw std::runtime_error("Non-finite or unbounded example state");
    }
    return std::round(value * 1000) / 1000;
}

uint32_t opacity(uint32_t rgba, double amount) {
    return (rgba & 0xffffff00) | uint32_t(std::lround((rgba & 255) * std::clamp(amount, 0., 1.)));
}

struct Obstacle {
    int shape = 0;  // 0 box, 1 circle, 2 driven box; dimensions are half extents.
    double x, y, width, height, angle = 0;
};

class Example {
public:
    explicit Example(const Definition& definition) : definition_(definition) {
        if (std::string_view(definition_.kind) == "fluid") {
            initializeFluid();
        } else {
            initializeParticles();
        }
    }

    void advance() {
        if (fluid_) {
            if (is("fountain") && tick_ % 6 == 0) {
                const FluidSample jet[]{{{176, 192}, {-12, -300}}, {{182, 192}, {12, -300}}};
                fluid_->add(jet);
            }
            if (binding_) {
                binding_->synchronize();
            }
            fluid_->advance(kStep);
            if (binding_) {
                binding_->apply();
                world_->advance(kStep);
                obstacles_.back().angle = world_->state(rotor_).angle * 180 / std::numbers::pi;
            }
        } else {
            emitter().advance(kStep);
        }
        ++tick_;
    }

    Json sample() const {
        Json particles = Json::array();
        if (fluid_) {
            const auto view = fluid_->snapshot();
            uint64_t id = 0;
            for (const auto& p : view.particles) {
                particles.push_back({++id, rounded(p.position.x), rounded(p.position.y), view.radius, 0, 0x64cef0ffu,
                                     rounded(p.velocity.x), rounded(p.velocity.y)});
            }
        } else {
            for (const auto& p : emitter().particles()) {
                particles.push_back({p.id, rounded(p.position.x), rounded(p.position.y), rounded(p.size / 2),
                                     rounded(p.rotation), p.color, rounded(p.velocity.x), rounded(p.velocity.y)});
            }
        }
        return Json::array({std::move(particles), binding_ ? rounded(obstacles_.back().angle) : 0});
    }

    Json obstacles() const {
        Json result = Json::array();
        for (const auto& p : obstacles_) {
            result.push_back({p.shape, p.x, p.y, p.width, p.height, p.angle});
        }
        return result;
    }

    void draw(Canvas& canvas, const Json& frame, double time) const {
        canvas.box(0, 0, 360, 220, 0, 0x101b28ff);
        canvas.clipped({6, 6, 348, 208}, [&] {
            for (const auto& p : frame[0]) {
                const double x = p[1], y = p[2], radius = p[3], angle = p[4], vx = p[6], vy = p[7];
                const auto color = p[5].get<uint32_t>();
                canvas.saved([&] {
                    if (is("rain")) {
                        canvas.line(x, y, x - vx * .025, y - vy * .025, color, 1.35);
                    } else if (is("sparks")) {
                        canvas.line(x, y, x - vx * .028, y - vy * .028, opacity(color, .35), 1.4);
                        canvas.oval(x, y, radius, radius, color);
                    } else if (is("petals") || is("debris")) {
                        canvas.translate(x, y);
                        canvas.rotate(angle);
                        if (is("petals")) {
                            canvas.oval(0, 0, 5.5, 2.5, color);
                            canvas.line(-3, 0, 3, 0, 0xffe4ed99, .7);
                        } else {
                            canvas.box(-4, -3, 8, 6, 1, color);
                        }
                    } else if (is("smoke") || is("fireflies")) {
                        double size = radius;
                        uint32_t ink = color;
                        if (is("fireflies")) {
                            size *= 1.8;
                            ink = opacity(ink, .55 + .45 * std::sin(time * 2.5 + p[0].get<double>()));
                        }
                        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(ink)},
                                                         {1, cybercanvas::color(ink & 0xffffff00)}};
                        const auto brush = canvas.radialGradient({x, y}, {size, size}, stops);
                        canvas.target->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(float(x), float(y)), float(size), float(size)), brush.Get());
                        if (is("fireflies")) {
                            canvas.oval(x, y, radius * .45, radius * .45, ink);
                        }
                    } else if (is("snow")) {
                        const double r = 1.2 + p[0].get<unsigned>() % 3;
                        canvas.oval(x, y, r, r, color);
                    } else if (fluid_) {
                        canvas.oval(x, y, radius, radius, is("syrup") ? 0xf0b66aff : 0x64cef0ff);
                    } else {
                        canvas.oval(x, y, radius, radius, color);
                        if (is("balls")) {
                            canvas.oval(x - 1.3, y - 1.5, 1.5, 1.5, 0xffffff88);
                        }
                    }
                });
            }
            for (const auto& obstacle : obstacles_) {
                canvas.saved([&] {
                    canvas.translate(obstacle.x, obstacle.y);
                    canvas.rotate(obstacle.shape == 2 ? frame[1].get<double>() : obstacle.angle);
                    if (obstacle.shape == 1) {
                        canvas.oval(0, 0, obstacle.width, obstacle.width, 0x233b4cff);
                        canvas.oval(0, 0, obstacle.width, obstacle.width, 0x7299afff, 1);
                    } else {
                        canvas.box(-obstacle.width, -obstacle.height, obstacle.width * 2, obstacle.height * 2, 2,
                                   0x5b768eff);
                    }
                });
            }
        });
    }

private:
    bool is(std::string_view id) const {
        return definition_.id == id;
    }

    ParticleEmitter& emitter() {
        return rigid_ ? rigid_->emitter() : *light_;
    }

    const ParticleEmitter& emitter() const {
        return rigid_ ? rigid_->emitter() : *light_;
    }

    void box(double x, double y, double halfWidth, double halfHeight, double angle = 0) {
        obstacles_.push_back({0, x, y, halfWidth, halfHeight, angle});
        if (rigid_) {
            auto& world = rigid_->world();
            const auto body = world.createBody({.type = physics::BodyType::Static,
                                                .position = {x / 100, y / 100},
                                                .angle = angle * std::numbers::pi / 180});
            world.box(body, {halfWidth / 100, halfHeight / 100}, {}, 0, {.friction = .45, .restitution = .55});
        }
    }

    void initializeParticles() {
        PhysicsParticleSettings options;
        auto& s = options.particles;
        s.maximum = 160;
        s.seed = 42;
        s.rate = 30;
        s.minimumLife = s.maximumLife = 5;
        s.minimumSpeed = 80;
        s.maximumSpeed = 170;
        s.direction = -90;
        s.spread = 110;
        s.gravity = {0, 300};
        s.size = Keyframes<double>{{{0, 5}, {1, 1}}};
        s.colors = {{0, 0xffde91ff}, {1, 0xfa794bff}};
        options.restitution = .8;
        options.collideWithParticles = false;
        Point origin{120, 88};
        unsigned burst = 24, warmup = 0;
        if (is("rain")) {
            s.rate = 95;
            s.minimumSpeed = 250;
            s.maximumSpeed = 340;
            s.minimumLife = s.maximumLife = 1.3;
            s.direction = 78;
            s.spread = 10;
            s.gravity = {0, 380};
            s.shape = EmissionShape::Line;
            s.extent = {340, 0};
            s.size = Keyframes<double>{{{0, 2}}};
            s.opacity = Keyframes<double>{{{0, .9}, {1, .15}}};
            s.colors = {{0, 0x9ed8ffff}};
            options.radius = 1;
            options.restitution = .4;
            origin = {150, -10};
            warmup = 100;
        } else if (is("snow") || is("petals")) {
            const bool snow = is("snow");
            s.rate = snow ? 25 : 15;
            s.minimumSpeed = snow ? 18 : 32;
            s.maximumSpeed = snow ? 32 : 55;
            s.minimumLife = s.maximumLife = 8;
            s.direction = 90;
            s.spread = 45;
            s.gravity = {0, snow ? 9. : 20.};
            s.shape = EmissionShape::Line;
            s.extent = {340, 0};
            s.size = Keyframes<double>{{{0, snow ? 5. : 10.}}};
            s.opacity = Keyframes<double>{{{0, .95}, {.85, .95}, {1, 0}}};
            s.colors = {{0, snow ? 0xd9efffff : 0xffa9c6ff}};
            options.radius = 1.7;
            options.restitution = .06;
            options.damping = .2;
            options.angularVelocity = 65;
            if (!snow) {
                options.collider = ParticleCollider::Box;
                options.halfExtent = {4, 1.5};
                options.sleepingAllowed = false;
            }
            origin = {165, -12};
            warmup = snow ? 450 : 300;
        } else if (is("debris")) {
            s.rate = 0;
            s.maximum = 72;
            s.spread = 135;
            s.minimumSpeed = 70;
            s.maximumSpeed = 160;
            s.minimumLife = s.maximumLife = 10;
            s.shape = EmissionShape::Rectangle;
            s.extent = {85, 18};
            s.gravity = {0, 330};
            s.size = Keyframes<double>{{{0, 8}}};
            s.opacity = Keyframes<double>{{{0, 1}}};
            s.colors = {{0, 0xf4b778ff}};
            options.collider = ParticleCollider::Box;
            options.halfExtent = {4, 3};
            options.collideWithParticles = true;
            options.angularVelocity = 190;
            options.restitution = .35;
            origin = {170, 76};
            burst = 54;
        } else if (is("balls")) {
            s.rate = 0;
            s.maximum = 24;
            s.minimumSpeed = s.maximumSpeed = 0;
            s.minimumLife = s.maximumLife = 20;
            s.shape = EmissionShape::Rectangle;
            s.extent = {245, 95};
            s.gravity = {0, 300};
            s.size = Keyframes<double>{{{0, 11}}};
            s.opacity = Keyframes<double>{{{0, 1}}};
            s.colors = {{0, 0xa9b7ffff}};
            options.radius = 5.5;
            options.restitution = .98;
            options.damping = 0;
            options.collideWithParticles = true;
            origin = {180, 74};
            burst = 22;
        } else if (is("fireflies") || is("smoke")) {
            const bool smoke = is("smoke");
            s.maximum = 160;
            s.rate = smoke ? 30 : 5;
            s.minimumSpeed = smoke ? 8 : 3;
            s.maximumSpeed = smoke ? 20 : 10;
            s.minimumLife = smoke ? 3 : 7;
            s.maximumLife = smoke ? 4 : 9;
            s.gravity = {0, smoke ? -11. : 0.};
            s.shape = smoke ? EmissionShape::Line : EmissionShape::Rectangle;
            s.extent = smoke ? Size{24, 0} : Size{300, 145};
            s.size = smoke ? Keyframes<double>{{{0, 12}, {1, 100}}} : Keyframes<double>{{{0, 5}, {.5, 7}, {1, 4}}};
            s.opacity = smoke ? Keyframes<double>{{{0, 0}, {.15, .17}, {.8, .07}, {1, 0}}}
                              : Keyframes<double>{{{0, 0}, {.15, 1}, {.8, .9}, {1, 0}}};
            s.colors = {{0, smoke ? 0xb9c9daff : 0xcff28bff}};
            s.space = ParticleSpace::World;
            light_ = std::make_unique<ParticleEmitter>(s);
            origin = smoke ? Point{180, 206} : Point{180, 105};
            burst = smoke ? 0 : 50;
            warmup = smoke ? 160 : 100;
        }
        if (!light_) {
            rigid_ = std::make_unique<PhysicsParticleEmitter>(options);
            box(180, 211, 174, 3);
            if (is("balls") || is("debris")) {
                box(9, 110, 3, 104);
                box(351, 110, 3, 104);
            }
            if (is("sparks")) {
                box(205, 135, 103, 3.5, 12);
            } else if (is("rain")) {
                box(204, 128, 98, 3.5, -10);
                box(278, 164, 3, 34);
            } else if (is("debris")) {
                box(95, 160, 64, 4, 10);
                box(270, 178, 61, 4, -10);
            }
        }
        emitter().origin(origin);
        emitter().burst(burst);
        if (is("snow") || is("petals") || is("fireflies") || is("smoke")) {
            const double strength = is("snow") ? 16 : is("petals") ? 22 : 7;
            emitter().wind([strength](Point p, double time) {
                return Point{strength * std::sin(time * 1.1 + p.y * .017),
                             strength * .25 * std::cos(time + p.x * .023)};
            });
        }
        for (unsigned step = 0; step < warmup; ++step) {
            emitter().advance(kStep);
        }
    }

    void initializeFluid() {
        fluid_ = std::make_unique<FluidSystem>(FluidSettings{.maximum = 768,
                                                             .radius = 3,
                                                             .viscosity = is("syrup") ? .3 : .015,
                                                             .surfaceTension = .01,
                                                             .gravity = {0, 300}});
        std::vector<FluidSample> walls, drops;
        for (int layer = 0; layer < 3; ++layer) {
            for (int x = 6; x <= 354; x += 6) {
                walls.push_back({{double(x), 208. + layer * 6}, {}});
            }
            for (int y = 4; y < 208; y += 6) {
                walls.push_back({{12. - layer * 6, double(y)}, {}});
                walls.push_back({{348. + layer * 6, double(y)}, {}});
            }
        }
        fluid_->boundary(walls);
        obstacles_ = {{0, 180, 210, 174, 4}, {0, 9, 110, 3, 104}, {0, 351, 110, 3, 104}};
        if (is("fountain")) {
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 44; ++x) {
                    drops.push_back({{45. + x * 6, 190. + y * 6}, {}});
                }
            }
            std::vector<FluidSample> circle;
            for (int i = 0; i < 32; ++i) {
                const auto a = i * 2 * std::numbers::pi / 32;
                circle.push_back({{180 + 24 * std::cos(a), 118 + 24 * std::sin(a)}, {}});
            }
            fluid_->boundary(circle);
            obstacles_.push_back({1, 180, 118, 24, 24});
            obstacles_.push_back({0, 179, 204, 10, 4});
        } else {
            for (int y = 0; y < 10; ++y) {
                for (int x = 0; x < 20; ++x) {
                    drops.push_back(
                        {{is("stirred") ? 80. + x * 9 : 65. + x * 6, is("stirred") ? 130. + y * 6 : 26. + y * 6},
                         {is("stirred") ? 0. : (x % 2 ? 30. : -30.), 0}});
                }
            }
        }
        fluid_->add(drops);
        if (is("stirred")) {
            world_ = std::make_unique<physics::World>(
                physics::WorldOptions{.gravity = {}, .fixedStep = kStep, .maximumSteps = 1, .collectEvents = false});
            const auto ground = world_->createBody({.type = physics::BodyType::Static});
            rotor_ = world_->createBody({.position = {1.8, 1.42}, .sleepingAllowed = false});
            world_->box(rotor_, {.48, .025}, {}, 0, {.density = 10000});
            world_->revolute(ground, rotor_, {1.8, 1.42}, {}, {.motor = true, .motorSpeed = 1.1, .maximumTorque = 600});
            std::vector<Point> samples;
            for (int y : {-3, 3}) {
                for (int x = -48; x <= 48; x += 6) {
                    samples.push_back({double(x), double(y)});
                }
            }
            binding_ = std::make_unique<FluidBodyBinding>(*fluid_, *world_, rotor_, samples);
            obstacles_.push_back({2, 180, 142, 48, 3});
        }
    }

    const Definition& definition_;
    unsigned tick_ = 0;
    std::unique_ptr<PhysicsParticleEmitter> rigid_;
    std::unique_ptr<ParticleEmitter> light_;
    std::unique_ptr<physics::World> world_;
    std::unique_ptr<FluidSystem> fluid_;
    physics::Body rotor_;
    std::unique_ptr<FluidBodyBinding> binding_;
    std::vector<Obstacle> obstacles_;
};

std::filesystem::path templatePath() {
#ifdef CYBERCANVAS_PARTICLE_TEMPLATE
    return CYBERCANVAS_PARTICLE_TEMPLATE;
#else
    return std::filesystem::path(__FILE__).parent_path() / "ParticleEffects.html";
#endif
}

std::filesystem::path iconPath() {
#ifdef CYBERCANVAS_PARTICLE_ICONS
    return CYBERCANVAS_PARTICLE_ICONS;
#else
    return std::filesystem::path(__FILE__).parent_path() / "particle-effects-icons";
#endif
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : ".";
        std::filesystem::create_directories(output);
        std::filesystem::copy_file(templatePath(), output / "index.html",
                                   std::filesystem::copy_options::overwrite_existing);
        Json icons = Json::object();
        for (const auto* icon : {"play", "pause", "rotate-ccw", "maximize", "x"}) {
            std::ifstream source(iconPath() / (std::string(icon) + ".svg"), std::ios::binary);
            if (!source) {
                throw std::runtime_error("Cannot read Lucide icon");
            }
            icons[icon] = std::string(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>());
        }
        std::filesystem::copy_file(iconPath() / "LICENSE.txt", output / "lucide-LICENSE.txt",
                                   std::filesystem::copy_options::overwrite_existing);
        std::ofstream stream(output / "effects-data.js", std::ios::binary);
        stream << "window.PARTICLE_EFFECTS={\"version\":2,\"fps\":" << kFps
               << ",\"width\":360,\"height\":220,\"icons\":" << icons.dump() << ",\"scenes\":[\n";
        Json report = {{"passed", true}, {"fps", kFps}, {"frameCount", kFrames}, {"scenes", Json::array()}};
        Canvas canvas;
        bool first = true;
        for (const auto& definition : kScenes) {
            std::cout << "Rendering " << definition.id << std::endl;
            Example scene(definition);
            Json frames = Json::array();
            size_t maximum = 0;
            for (unsigned frame = 0; frame < kFrames; ++frame) {
                if (frame) {
                    scene.advance();
                    scene.advance();
                }
                frames.push_back(scene.sample());
                maximum = std::max(maximum, frames.back()[0].size());
                if (frame == 90) {
                    for (double dpi : {1., 1.5, 2.}) {
                        const auto width = static_cast<uint32_t>(360 * dpi), height = static_cast<uint32_t>(220 * dpi);
                        std::vector<uint8_t> pixels(size_t(width) * height * 4);
                        FrameBuffer buffer{width, height, width * 4, pixels.size(), pixels.data(), dpi};
                        canvas.render(buffer, {}, [&](Canvas& draw, Rect) {
                            scene.draw(draw, frames.back(), double(frame) / kFps);
                        });
                        const auto name = std::string(definition.id) + "-" + std::to_string(int(dpi * 100)) + ".png";
                        if (!canvas.save((output / name).wstring(), buffer)) {
                            throw std::runtime_error("Cannot save native particle poster");
                        }
                    }
                }
            }
            const Json metadata{{"id", definition.id},
                                {"name", definition.name},
                                {"group", definition.group},
                                {"kind", definition.kind},
                                {"description", definition.description},
                                {"obstacles", scene.obstacles()},
                                {"frames", std::move(frames)}};
            if (!first) {
                stream << ",\n";
            }
            first = false;
            stream << metadata.dump();
            report["scenes"].push_back({{"id", definition.id},
                                        {"name", definition.name},
                                        {"maximumParticles", maximum},
                                        {"frames", kFrames},
                                        {"finite", true},
                                        {"nativeDpi", {1, 1.5, 2}}});
        }
        stream << "\n]};\n";
        if (!stream) {
            throw std::runtime_error("Cannot write particle replay data");
        }
        std::ofstream summary(output / "gallery-report.json");
        summary << report.dump(2);
        if (!summary) {
            throw std::runtime_error("Cannot write particle gallery report");
        }
        std::cout << "PASS 12 native effects, 240 frames each at 60 FPS, 36 DPI posters\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL particle gallery: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
