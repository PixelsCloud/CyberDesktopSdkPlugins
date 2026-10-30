#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include "plugins/mechanical-hourglass/tools/TestHost.h"

namespace {
using namespace mechanicalhourglass;
using testing::Host;

void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

CdResult setting(Instance& instance, const char* id, uint32_t type, int64_t integer, const char* text = nullptr) {
    const CdSettingValue value{sizeof(value), type, id, integer, 0, text};
    return instance.applySettings(&value, 1);
}

void click(Instance& instance, double x, double y) {
    CdInput input{sizeof(input), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y};
    instance.input(input);
    input.kind = CD_INPUT_RELEASE;
    input.buttons = 0;
    instance.input(input);
}

void simulation() {
    for (double fps : {15., 20., 30.}) {
        Model model;
        model.reset(5);
        PhysicalSand sand;
        bool turning = false, refilled = false;
        for (int frame = 0; frame < int(fps * 18); ++frame) {
            model.advance(1 / fps, true);
            sand.advance(1 / fps, model);
            const auto particles = sand.sample();
            if (particles.size() != 105) {
                throw std::runtime_error("Physical sand lost a particle");
            }
            for (const auto point : particles) {
                const auto mapped = grainPosition(point, {0, 0, 100, 160});
                if (!std::isfinite(mapped.x) || !std::isfinite(mapped.y) || mapped.x < 5 || mapped.x > 95 ||
                    mapped.y < 20 || mapped.y > 140) {
                    throw std::runtime_error("Particle escaped the displayed glass");
                }
            }
            const auto cells = model.sample();
            if (cells.upper.size() + cells.lower.size() + cells.falling.size() != model.capacity()) {
                throw std::runtime_error("Classic sand lost a particle");
            }
            turning |= model.phase() == Phase::Turning;
            refilled |= model.cycles() > 0 && model.progress() < .1;
        }
        check(turning && refilled, "Physical and classic grains remain conserved through repeated flips");
    }
    check(remainingText(3600) == "60:00" && remainingText(18) == "00:18" && remainingText(0) == "00:00",
          "Countdown displays the full supported duration range");
    check(digitCell("00:18", 8, 2) && !digitCell("00:18", 8, 3) && !digitCell("", 0, 0),
          "Dot-matrix separators and invalid input stay within bounds");
}

void lifecycle(const Spec& spec) {
    Host host;
    {
        const auto context = host.context(spec, ".");
        Instance a(context), b(context);
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = spec.columns * 100;
        layout.logical_height = 100;
        layout.safe_content = {0, 0, layout.logical_width, 100};
        a.layoutChanged(layout);
        check(host.timers.size() == 2, "Independent instances own separate animation timers");
        host.advance(2500);
        check(a.model().remaining() == 28 && b.model().remaining() == 28, "Real timer updates move the countdown");
        a.setPaused(true);
        const auto progress = a.model().progress();
        host.advance(100000);
        check(a.model().progress() == progress && host.timers.size() == 1, "Hidden instance stops its timer and sand");
        a.setPaused(false);
        check(a.model().progress() == progress && host.timers.size() == 2, "Resume skips hidden elapsed time");
        const double x = spec.columns == 2 ? 160 : 50;
        click(a, x, 75);
        check(!a.settings.at("running").integer && host.timers.size() == 1, "Countdown or lower-half click pauses");
        host.allowTimers = false;
        check(setting(a, "running", CD_SETTING_TOGGLE, 1) != CD_OK && !a.settings.at("running").integer,
              "A failed timer subscription preserves the paused setting");
        host.allowTimers = true;
        click(a, x, 75);
        check(a.settings.at("running").integer && host.timers.size() == 2, "Click resumes without leaking timers");
        click(a, 30, 25);
        check(a.model().remaining() == 30 && a.model().progress() == 0, "Glass click restarts this instance only");
        host.advance(750);
        const auto beforeDrag = a.model().progress();
        CdInput drag{sizeof(drag), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 30, 25};
        a.input(drag);
        drag.kind = CD_INPUT_MOVE;
        drag.x = 55;
        a.input(drag);
        drag.kind = CD_INPUT_RELEASE;
        drag.buttons = 0;
        drag.x = 30;
        a.input(drag);
        check(a.model().progress() == beforeDrag, "Dragging never triggers a reset");
        host.allowTimers = false;
        check(setting(a, "duration", CD_SETTING_INTEGER, 60) != CD_OK && a.settings.at("duration").integer == 30 &&
                  a.model().progress() == beforeDrag,
              "Failed duration transaction preserves settings, timer and progress");
        host.allowTimers = true;
        check(setting(a, "duration", CD_SETTING_INTEGER, 4) == CD_ERROR_ARGUMENT && a.model().progress() == beforeDrag,
              "Out-of-range duration is rejected atomically");
        check(setting(a, "sand_color", CD_SETTING_COLOR, 0x2681e6ff) == CD_OK &&
                  b.settings.at("sand_color").integer == 0xf0bd65ff,
              "Custom sand color stays local to its instance");
        check(setting(a, "frame_rate", CD_SETTING_CHOICE, 0, "30") == CD_OK && host.timers.size() == 2,
              "Frame-rate changes replace rather than accumulate timers");
        check(setting(a, "repeat", CD_SETTING_TOGGLE, 0) == CD_OK &&
                  setting(a, "duration", CD_SETTING_INTEGER, 5) == CD_OK,
              "One-shot settings are accepted");
        host.advance(6000);
        check(a.model().phase() == Phase::Finished && a.model().remaining() == 0 && host.timers.size() == 1,
              "One-shot completion stops the animation timer");
        click(a, x, 75);
        check(a.model().remaining() == 5 && host.timers.size() == 2, "Click restarts a finished timer");
        check(setting(a, "physics", CD_SETTING_TOGGLE, 0) == CD_OK && a.model().progress() == 0,
              "Classic mode reuses the same timing and resets cleanly");
        CdSettingValue output{};
        check((a.settings.get("text_color", &output, sizeof(output)) == CD_OK) == (spec.columns == 2),
              "Only the wide layout exposes countdown color");
    }
    check(host.timers.empty(), "Instance destruction releases every timer");
}

void rendering(const Spec& spec, const std::filesystem::path& root) {
    Host host;
    const auto path = (root / "build/relwithdebinfo/plugins" / spec.id).u8string();
    const std::string directory(reinterpret_cast<const char*>(path.data()), path.size());
    const auto context = host.context(spec, directory.c_str());
    Renderer renderer(context);
    auto settings = defaults(spec.columns == 2);
    Model model;
    model.advance(12, true);
    for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
        const uint32_t w = uint32_t(spec.columns * 110 * dpi), h = uint32_t(120 * dpi), stride = w * 4 + 24;
        std::vector<uint8_t> pixels(size_t(stride) * h + 64, 0xa5);
        CdFrame frame{sizeof(frame), w, h, stride, uint64_t(stride) * h, pixels.data() + 32, dpi};
        for (unsigned pass = 0; pass < 200; ++pass) {
            check(renderer.render(spec.columns == 2, model, nullptr, settings, frame, {3, 3, w / dpi - 6, h / dpi - 6}),
                  "Native render accepts fractional DPI and an inset safe area");
            if (renderer.ready()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(renderer.ready(), "Both material sprites decode in the background");
        bool valid = std::all_of(pixels.begin(), pixels.begin() + 32,
                                 [](auto p) {
                                     return p == 0xa5;
                                 }) &&
                     std::all_of(pixels.end() - 32, pixels.end(), [](auto p) {
                         return p == 0xa5;
                     });
        size_t transparent = 0;
        for (unsigned y = 0; y < h; ++y) {
            for (unsigned x = 0; x < w; ++x) {
                const auto* p = frame.rgba + size_t(y) * stride + x * 4;
                valid &= p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3];
                transparent += p[3] == 0;
            }
            for (unsigned x = w * 4; x < stride; ++x) {
                valid &= frame.rgba[size_t(y) * stride + x] == 0xa5;
            }
        }
        check(valid && transparent > size_t(w) * h / 10,
              "Premultiplied alpha, cutouts, stride and buffer guards stay valid");
        const auto builds = renderer.cacheBuilds();
        model.advance(.05, true);
        renderer.render(spec.columns == 2, model, nullptr, settings, frame, {3, 3, w / dpi - 6, h / dpi - 6});
        check(renderer.cacheBuilds() == builds, "Live sand and countdown reuse static materials and sprites");
    }
    CdSettingValue color{sizeof(color), CD_SETTING_COLOR, "sand_color", 0x2681e6ff};
    check(settings.apply(&color, 1) == CD_OK, "Sand color accepts the standard color picker value");
    std::vector<uint8_t> pixels(360 * 180 * 4);
    CdFrame frame{sizeof(frame), 360, 180, 1440, pixels.size(), pixels.data(), 1};
    renderer.render(spec.columns == 2, model, nullptr, settings, frame);
    unsigned blue = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        blue += pixels[i + 2] > pixels[i] * 1.5 && pixels[i + 2] > 80;
    }
    check(blue > 30, "Selected sand color reaches the actual rendered grains");
    frame.capacity = 1;
    check(!renderer.render(spec.columns == 2, model, nullptr, settings, frame), "Undersized frame is rejected");
    frame.capacity = pixels.size();
    frame.device_pixel_ratio = std::numeric_limits<double>::quiet_NaN();
    check(!renderer.render(spec.columns == 2, model, nullptr, settings, frame), "Non-finite DPI is rejected");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    check(argc == 2, "Fixed repository path supplied");
    simulation();
    for (const auto& spec : mechanicalhourglass::kCatalog) {
        lifecycle(spec);
        rendering(spec, argv[1]);
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
