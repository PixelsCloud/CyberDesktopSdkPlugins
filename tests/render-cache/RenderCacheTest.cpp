#include <CyberCanvas/ResourceAccount.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "plugins/brick-clock/Clock.h"
#include "plugins/marble-clock/Clock.h"
#include "plugins/precision-clocks/Clock.h"

namespace {
struct Budget {
    uint64_t bytes = 0;
    uint64_t reservations = 0;
};

class Lease final {
public:
    Lease(std::shared_ptr<Budget> budget, uint64_t bytes);
    ~Lease();

private:
    std::shared_ptr<Budget> budget_;
    uint64_t bytes_;
};

Lease::Lease(std::shared_ptr<Budget> budget, uint64_t bytes) : budget_(std::move(budget)), bytes_(bytes) {
    budget_->bytes += bytes_;
    ++budget_->reservations;
}

Lease::~Lease() {
    budget_->bytes -= bytes_;
}

void* reserve(void* context, uint64_t bytes) {
    const auto& budget = *static_cast<std::shared_ptr<Budget>*>(context);
    return cybercanvas::makeMemoryLease(std::make_shared<Lease>(budget, bytes)).Detach();
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Make, typename Paint>
void scenario(const char* name, Make make, Paint paint) {
    auto budget = std::make_shared<Budget>();
    {
        std::array<cdsdk::Canvas, 2> canvases;
        for (auto& canvas : canvases) {
            canvas.resourceAccount({&budget, reserve});
        }
        // Two independent instances exercise simultaneous cache lifetimes.
        auto first = make();
        auto second = make();
        for (double dpi : {1., 1.5, 2., 1.}) {
            for (double scale : {.2001, .2002, 2., .2001}) {
                for (bool alternate : {false, true}) {
                    const auto width = uint32_t(320 * dpi), height = uint32_t(200 * dpi);
                    std::vector<uint8_t> pixels(size_t(width) * height * 4);
                    const CdFrame frame{sizeof(CdFrame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                    std::vector<uint8_t> expected;
                    for (auto& canvas : canvases) {
                        for (auto* renderer : {first.get(), second.get()}) {
                            const auto render = [&] {
                                canvas.render(frame, {}, [&](auto& draw, auto) {
                                    draw.scale(scale, scale);
                                    paint(*renderer, draw, scale, alternate);
                                });
                            };
                            render();
                            const auto reservations = budget->reservations;
                            const auto previous = pixels;
                            render();
                            require(previous == pixels, std::string(name) + ": stable frame changed pixels");
                            require(reservations == budget->reservations,
                                    std::string(name) + ": stable frame allocated another cached surface");
                            if (expected.empty()) {
                                expected = pixels;
                            } else {
                                require(expected == pixels,
                                        std::string(name) + ": target/instance switch changed pixels");
                            }
                        }
                    }
                    // A fresh renderer is the reference after size, DPI, content and factory changes.
                    cdsdk::Canvas reference;
                    auto fresh = make();
                    reference.render(frame, {}, [&](auto& draw, auto) {
                        draw.scale(scale, scale);
                        paint(*fresh, draw, scale, alternate);
                    });
                    require(expected == pixels, std::string(name) + ": retained cache differs from fresh rendering");
                    require(scale > 1 || std::any_of(pixels.begin(), pixels.end(),
                                                     [](uint8_t byte) {
                                                         return byte != 0;
                                                     }),
                            std::string(name) + ": empty render");
                    require(budget->bytes < 32 * 1024 * 1024, std::string(name) + ": unbounded cache memory");
                }
            }
        }
    }
    require(budget->bytes == 0, std::string(name) + ": cache lease outlived renderer and canvas");
    std::cout << "PASS " << name << ": 32 transitions, two instances/targets, stable frames and lease release\n";
}
}  // namespace

int main() try {
    brickclock::Model brick;
    brick.setTime(10, 20, 30);
    scenario(
        "brick",
        [] {
            return std::make_unique<brickclock::Renderer>();
        },
        [&](auto& renderer, auto& canvas, double scale, bool alternate) {
            renderer.draw(canvas, brick, alternate ? "sand" : "clay", scale);
        });
    marbleclock::Model marble;
    marble.setTime(10, 20, 30, false);
    scenario(
        "marble",
        [] {
            return std::make_unique<marbleclock::Renderer>();
        },
        [&](auto& renderer, auto& canvas, double scale, bool alternate) {
            renderer.draw(canvas, marble, {.light = alternate}, scale);
        });
    for (auto kind : {precisionclock::Kind::Skeleton, precisionclock::Kind::Green, precisionclock::Kind::Dress}) {
        scenario(
            "precision",
            [kind] {
                return std::make_unique<precisionclock::Renderer>(kind);
            },
            [](auto& renderer, auto& canvas, double scale, bool alternate) {
                renderer.draw(canvas, {}, {.background = alternate}, scale);
            });
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
