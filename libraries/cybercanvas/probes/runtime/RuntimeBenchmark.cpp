#include <CyberCanvas/Effects.h>
#include <CyberCanvas/Particles.h>
#include <CyberCanvas/Physics2D.h>
#include <CyberCanvas/Scene.h>
#include <CyberCanvas/Vector.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {
using namespace cybercanvas;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

struct Run {
    std::filesystem::path output;
    double warmup = 10, duration = 60;
    unsigned repetitions = 3;
    Json results = Json::array();
    bool passed = true;
    std::string selectedCase;

    template <class Work>
    void measure(const std::string& name, double budget, Work work) {
        if (!selectedCase.empty() && name != selectedCase) {
            return;
        }
        for (unsigned repeat = 0; repeat < repetitions; ++repeat) {
            std::cout << name << " repetition " << repeat + 1 << std::endl;
            const auto start = Clock::now();
            auto next = start;
            std::vector<double> samples;
            std::ofstream raw(output / (name + "-" + std::to_string(repeat + 1) + ".csv"));
            raw << "elapsed_s,duration_ms\n";
            uint64_t frame = 0;
            while (true) {
                const auto before = Clock::now();
                const auto elapsed = std::chrono::duration<double>(before - start).count();
                if (elapsed >= warmup + duration) {
                    break;
                }
                work(frame++);
                const auto milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
                if (elapsed >= warmup) {
                    samples.push_back(milliseconds);
                    raw << elapsed << ',' << milliseconds << '\n';
                }
                next += std::chrono::microseconds(16667);
                if (next < Clock::now()) {
                    next = Clock::now();
                }
                std::this_thread::sleep_until(next);
            }
            if (samples.empty()) {
                throw std::runtime_error("No performance samples");
            }
            std::sort(samples.begin(), samples.end());
            const auto quantile = [&](double value) {
                return samples[size_t((samples.size() - 1) * value)];
            };
            const bool accepted = quantile(.95) <= budget;
            passed &= accepted;
            results.push_back({{"name", name},
                               {"repeat", repeat + 1},
                               {"samples", samples.size()},
                               {"p50_ms", quantile(.5)},
                               {"p95_ms", quantile(.95)},
                               {"p99_ms", quantile(.99)},
                               {"max_ms", samples.back()},
                               {"p95_budget_ms", budget},
                               {"passed", accepted}});
            std::ofstream(output / "report.json") << Json{
                {"passed", passed},
                {"formal", duration >= 60 && warmup >= 10 && repetitions >= 3},
                {"warmup_seconds", warmup},
                {"sample_seconds", duration},
                {"repetitions", repetitions},
                {"cadence_hz", 60},
                {"selected_case", selectedCase},
                {"results", results}}.dump(2);
        }
    }

    int result() const {
        if (results.empty()) {
            throw std::invalid_argument("Unknown benchmark case");
        }
        return passed ? 0 : 1;
    }
};

void physical(Run& run, int count, bool composite = false) {
    physics::World world({.gravity = {}});
    const auto walls = world.createBody({.type = physics::BodyType::Static});
    world.box(walls, {5, .1}, {5, 0});
    world.box(walls, {5, .1}, {5, 10});
    world.box(walls, {.1, 5}, {0, 5});
    world.box(walls, {.1, 5}, {10, 5});
    for (int i = 0; i < count; ++i) {
        const auto body = world.createBody({.position = {1. + (i % 10) * .75, 1. + (i / 10) * .6},
                                            .velocity = {2. + i % 3, -1. - i % 4},
                                            .sleepingAllowed = false});
        if (composite) {
            world.circle(body, {-.09, 0}, .1, {.friction = 0, .restitution = 1});
            world.circle(body, {.09, 0}, .1, {.friction = 0, .restitution = 1});
            if (i < 5) {
                world.distance(walls, body, {1. + (i % 10) * .75, .6 + (i / 10) * .6}, {}, .4, 2, .5);
            }
        } else {
            world.circle(body, {}, .2, {.friction = 0, .restitution = 1});
        }
    }
    run.measure("physics-" + std::to_string(count) + (composite ? "-100-shapes-5-joints" : ""), count == 50 ? 1 : 2,
                [&](uint64_t) {
                    world.advance(1. / 60);
                    world.takeEvents();
                });
}

void particles(Run& run, uint32_t maximum = 1024) {
    ParticleSettings settings;
    settings.maximum = maximum;
    settings.rate = 4096;
    settings.minimumLife = settings.maximumLife = 1;
    ParticleEmitter emitter(settings);
    emitter.origin({128, 128});
    Canvas canvas;
    std::vector<uint8_t> pixels(256 * 256 * 4);
    run.measure("particles-" + std::to_string(maximum) + "-update-draw", maximum <= 256 ? 2 : 8, [&](uint64_t) {
        emitter.advance(1. / 60);
        canvas.render({256, 256, 1024, pixels.size(), pixels.data(), 1}, {}, [&](Canvas& draw, Rect) {
            emitter.draw(draw);
        });
    });
}

void scenes(Run& run, unsigned count, uint32_t width = 256, uint32_t height = 256, double dpi = 1,
            int appearance = -1) {
    struct Card {
        Canvas canvas;
        Scene scene;
        Scene::Node group, value;
        uint32_t width, height, pixelWidth, pixelHeight;
        double dpi;
        std::vector<uint8_t> pixels;

        Card(uint32_t logicalWidth, uint32_t logicalHeight, double ratio, int appearance)
            : width(logicalWidth),
              height(logicalHeight),
              pixelWidth(uint32_t(width * ratio)),
              pixelHeight(uint32_t(height * ratio)),
              dpi(ratio),
              pixels(size_t(pixelWidth) * pixelHeight * 4) {
            if (appearance >= 0) {
                scene.theme(WidgetTheme::desktop(appearance != 0));
            }
            View title;
            title.kind = ViewKind::Text;
            title.text = L"Animated card";
            title.layout.height = Length::points(28);
            scene.add(0, title);
            View art;
            art.layout.height = Length::points(160);
            art.cacheSubtree = true;
            art.paintLocal = [appearance](Canvas& draw, Rect bounds, const WidgetTheme&) {
                draw.gradientBox(bounds, 14, appearance == 1 ? 0xc9deefff : 0x303f60ff,
                                 appearance == 1 ? 0x87afd0ff : 0x607caaff);
                draw.oval(bounds.width / 2, bounds.height / 2, 35, 35, 0x91c6e8ff);
            };
            group = scene.add(0, art);
            title.text = L"CPU 0%";
            value = scene.add(0, title);
            scene.arrange(canvas, {0, 0, double(width), double(height)}, dpi);
        }

        void frame(uint64_t tick) {
            scene.text(value, L"CPU " + std::to_wstring((tick / 30) % 101) + L"%");
            scene.transform(group, {.translation = {0, std::sin(tick / 60.) * 2}, .opacity = .9});
            if (scene.needsLayout()) {
                scene.arrange(canvas, {0, 0, double(width), double(height)}, dpi);
            }
            canvas.render({pixelWidth, pixelHeight, pixelWidth * 4, pixels.size(), pixels.data(), dpi}, {},
                          [&](Canvas& draw, Rect) {
                              scene.draw(draw);
                          });
        }
    };

    std::vector<std::unique_ptr<Card>> cards;
    for (unsigned i = 0; i < count; ++i) {
        cards.push_back(std::make_unique<Card>(width, height, dpi, appearance));
    }
    const auto name = appearance < 0 ? "scene-cards-256-" + std::to_string(count)
                                     : "scene-matrix-" + std::to_string(width) + "x" + std::to_string(height) + "-dpi" +
                                           std::to_string(int(dpi * 100)) + (appearance ? "-light" : "-dark");
    run.measure(name, count == 1 ? 2 : 8, [&](uint64_t frame) {
        for (const auto& card : cards) {
            card->frame(frame);
        }
    });
}

void effects(Run& run, double dpi) {
    const uint32_t edge = uint32_t(128 * dpi);
    std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
    for (uint32_t y = edge / 4; y < edge * 3 / 4; ++y) {
        for (uint32_t x = edge / 4; x < edge * 3 / 4; ++x) {
            auto* p = pixels.data() + (size_t(y) * edge + x) * 4;
            p[0] = 32;
            p[1] = 96;
            p[2] = p[3] = 255;
        }
    }
    const auto first = ImageData::fromPremultipliedBgra(edge, edge, pixels);
    pixels[(size_t(edge / 2) * edge + edge / 2) * 4] = 64;
    const auto second = ImageData::fromPremultipliedBgra(edge, edge, std::move(pixels));
    EffectSurface surface;
    std::array styles{EffectStyle{EffectKind::Blur, 6, {}}};
    const auto suffix = "-dpi" + std::to_string(int(dpi));
    run.measure("effects-cached" + suffix, .1, [&](uint64_t) {
        surface.render(*first, styles, dpi);
    });
    run.measure("effects-content" + suffix, 2, [&](uint64_t frame) {
        surface.render(*(frame % 2 ? first : second), styles, dpi);
    });
    run.measure("effects-parameters" + suffix, 2, [&](uint64_t frame) {
        styles[0].color = frame % 2 ? 0xff0000ff : 0x0000ffff;
        styles[0].kind = EffectKind::Glow;
        surface.render(*first, styles, dpi);
    });
}

void vectors(Run& run, const std::filesystem::path& assets, int count) {
    auto budget = std::make_shared<MemoryBudget>(64 * 1024 * 1024);
    // Direct decoder invocation intentionally includes CPU rendering/allocation,
    // without timing UI polling or benefiting from the store's three-frame LRU.
    AssetContext context{assets, budget};
    std::vector<std::unique_ptr<AnimationSource>> sources;
    for (int i = 0; i < count; ++i) {
        sources.push_back(vectorAnimation("orbit.json", VectorKind::Lottie)(context));
    }
    run.measure("lottie-256-" + std::to_string(count), count == 1 ? 2 : 8, [&](uint64_t frame) {
        for (const auto& source : sources) {
            const auto rendered = source->frame(uint32_t(frame % source->info().frameEnds.size()), 256, 256);
            if (!rendered) {
                throw std::runtime_error("Missing decoded benchmark frame");
            }
        }
    });
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5) {
            throw std::invalid_argument(
                "Usage: rendering_runtime_benchmark OUTPUT ASSETS [MODE | --case NAME | --smoke-case NAME]");
        }
        Run run;
        run.output = argv[1];
        if (argc == 5) {
            const std::string_view option(argv[3]);
            if (option != "--case" && option != "--smoke-case") {
                throw std::invalid_argument("Expected --case or --smoke-case before the case name");
            }
            run.selectedCase = argv[4];
            if (run.selectedCase.empty()) {
                throw std::invalid_argument("Expected a nonempty case name");
            }
            if (option == "--smoke-case") {
                // A standalone effect can initialize its device on the first frame.
                // Keep that startup from consuming the entire short smoke window.
                run.warmup = 2;
                run.duration = 1;
                run.repetitions = 1;
            }
        }
        const bool selectedMatrix =
            run.selectedCase.starts_with("scene-matrix-") || run.selectedCase == "physics-50-100-shapes-5-joints";
        std::filesystem::create_directories(run.output);
        if (argc == 4 || selectedMatrix) {
            if (std::string_view(argv[3]) == "--effects" || std::string_view(argv[3]) == "--effects-vectors") {
                effects(run, 1);
                effects(run, 2);
                if (std::string_view(argv[3]) == "--effects-vectors") {
                    for (int count : {1, 5, 10}) {
                        vectors(run, argv[2], count);
                    }
                }
                return run.result();
            }
            if (std::string_view(argv[3]) == "--matrix" || std::string_view(argv[3]) == "--smoke-matrix" ||
                std::string_view(argv[3]) == "--matrix-high-dpi" || selectedMatrix) {
                const bool highDpiOnly = std::string_view(argv[3]) == "--matrix-high-dpi";
                if (std::string_view(argv[3]) == "--smoke-matrix") {
                    run.warmup = .1;
                    run.duration = .2;
                    run.repetitions = 1;
                }
                if (!highDpiOnly) {
                    physical(run, 50, true);
                    for (unsigned count : {1, 5, 10}) {
                        scenes(run, count);
                    }
                }
                for (const uint32_t width : {256, 512}) {
                    for (const double dpi : {1., 1.25, 1.5, 2.}) {
                        if (highDpiOnly && (width != 512 || dpi < 1.5)) {
                            continue;
                        }
                        for (const int appearance : {0, 1}) {
                            scenes(run, 1, width, 256, dpi, appearance);
                        }
                    }
                }
                return run.result();
            }
            if (std::string_view(argv[3]) == "--profile-particles") {
                run.warmup = 2;
                run.duration = 10;
                run.repetitions = 1;
                particles(run);
                return run.result();
            }
            if (std::string_view(argv[3]) != "--smoke") {
                throw std::invalid_argument("Unknown option");
            }
            run.warmup = .1;
            run.duration = .2;
            run.repetitions = 1;
        }
        physical(run, 50);
        physical(run, 128);
        particles(run, 256);
        particles(run, 1024);
        effects(run, 1);
        effects(run, 2);
        for (unsigned count : {1, 5, 10}) {
            scenes(run, count);
        }
        for (int count : {1, 5, 10}) {
            vectors(run, std::filesystem::absolute(argv[2]), count);
        }
        return run.result();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
