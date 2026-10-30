#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <thread>

#include "Sign.h"
#include "plugins/mechanical-sign/tools/TestHost.h"
#include "sdk/support/Package.h"

namespace {
using namespace mechanicalsign;
unsigned checks = 0;
bool passed = true;

void check(bool value, const char* message) {
    ++checks;
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << message << '\n';
}

void settle(Model& model) {
    for (int i = 0; i < 100 && model.active(); ++i) {
        model.advance(.033);
    }
}

void textTests() {
    check(cdsdk::systemDependency("ICU.dll") && !cdsdk::systemDependency("icuuc78.dll"),
          "only the Windows ICU name is recognized; private Unicode runtimes remain packaged dependencies");
    check(pages("CyberDesktop").size() == 1 && pages("CyberDesktop")[0][11] == L"p",
          "twelve-letter sign fits one page");
    check(pages("ABCDEFGHIJKL\r\nNext").size() == 2 && pages("ABCDEFGHIJKL\r\nNext")[1][0] == L"N",
          "Windows newline starts one page without an extra blank page");
    check(pages("ABCDEFGHIJKLM").size() == 2 && pages("ABCDEFGHIJKLM")[1][0] == L"M",
          "long text pages at twelve characters");
    check(pages("桌面插件，机械翻牌")[0][1] == L"面", "Chinese characters remain complete");
    check(pages("e\xCC\x81")[0][0] == L"e\u0301" && pages("e\xCC\x81")[0][1].empty(),
          "combining marks share their base flap");
    check(pages("👨‍👩‍👧‍👦")[0][1].empty() && pages("👨‍👩‍👧‍👦")[0][0].size() > 2,
          "ICU preserves joined emoji and surrogate pairs");
    check(pages("").size() == 1 && pages("")[0][0].empty(), "empty content is one blank page");
    bool invalid = false;
    try {
        pages("\xc0\xaf");
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    check(invalid, "invalid UTF-8 is rejected before state mutation");
}

void modelTests() {
    Model model;
    check(model.active() && model.duration() < 1.4, "initial reveal is a bounded animation");
    model.advance(.1);
    model.advance(.05);
    check(model.progress(0) > 0 && model.progress(1) == 0, "flaps start in staggered order");
    settle(model);
    check(!model.active() && model.after() == pages("CyberDesktop").front() && model.power() == 0,
          "startup lands on final text and stops the motor");
    model.text(pages("CybeRDesktop"), true);
    check(model.progress(0) == 1 && model.progress(4) == 0 && model.duration() < .8,
          "editing moves only changed flaps");
    settle(model);
    model.request();
    for (int i = 0; i < 10000; ++i) {
        model.request();
    }
    check(model.queued(), "rapid clicks coalesce into one pending cycle");
    settle(model);
    check(!model.active() && !model.queued(), "click queue drains in at most two cycles");
    model.request();
    model.text(pages("Old update"), true);
    model.text(pages("Latest"), true);
    settle(model);
    check(model.after() == pages("Latest").front(), "updates during a flip keep the newest text");
    model.text(pages("First\nSecond"), false);
    model.request();
    settle(model);
    check(model.page() == 1 && model.after()[0] == L"S", "cycles advance to the next text page");
    model.request();
    settle(model);
    check(model.page() == 0 && model.after()[0] == L"F", "page traversal wraps without accumulating pages");
    model.request();
    const auto angle = model.angle();
    model.advance(std::numeric_limits<double>::quiet_NaN());
    model.advance(-1);
    check(model.angle() == angle, "non-finite or negative elapsed time cannot move the mechanism");
    model.finish();
    check(!model.active() && !model.queued(), "reduced motion settles immediately and clears queued work");
    bool stable = true;
    for (int i = 0; i < 20000; ++i) {
        model.request();
        while (model.active()) {
            model.advance(.1);
        }
        stable &= std::isfinite(model.angle()) && model.angle() >= 0 && model.angle() < 10 * std::numbers::pi;
    }
    check(stable && model.pageCount() == 2, "twenty thousand cycles keep angles and page storage bounded");
    std::vector<double> poses;
    for (int fps : {30, 60, 120, 144}) {
        Model scene;
        for (int i = 0; i < fps / 2; ++i) {
            scene.advance(1. / fps);
        }
        poses.push_back(scene.angle());
    }
    check(std::all_of(poses.begin(), poses.end(),
                      [&](double value) {
                          return std::abs(value - poses.front()) < 1e-8;
                      }),
          "animation phase is identical at 30/60/120/144 Hz");
}

void wait(Renderer& renderer) {
    for (int i = 0; renderer.pending() && i < 500; ++i) {
        renderer.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    renderer.poll();
}

void lifecycleTests() {
    Renderer ready(std::filesystem::path(CD_SIGN_TEST_ROOT) / "assets");
    wait(ready);
    check(ready.assets().ready == 1 && ready.assets().failed == 0 && ready.assets().referencedBytes < 3 * 1024 * 1024,
          "approved PNG backplate decodes within three MiB");
    Host host;
    {
        Instance instance(host.context());
        std::vector<uint8_t> bytes(600 * 200 * 4);
        const CdFrame frame{sizeof(CdFrame), 600, 200, 2400, bytes.size(), bytes.data(), 1};
        check(instance.render(frame) == CD_OK, "instance renders through the native Canvas");
        instance.render(frame);
        const auto initial = bytes;
        instance.render(frame);
        check(bytes == initial, "rendering alone never advances time");
        for (int i = 0; i < 60; ++i) {
            host.advance(33);
            instance.render(frame);
        }
        check(host.timers.size() == 1 && host.timers.begin()->second.interval == 5000,
              "idle replaces frame updates with one long wake timer");
        const auto idle = bytes;
        const auto redraws = host.redraws;
        host.advance(1000);
        instance.render(frame);
        check(bytes == idle && host.redraws == redraws, "stationary sign produces no idle redraw requests");
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 300, 100, 0});
        instance.input({sizeof(CdInput), CD_INPUT_RELEASE, 0, 0, 300, 100, 0});
        check(host.timers.size() == 1 && host.timers.begin()->second.interval == 33, "click begins a mechanical cycle");
        host.advance(100);
        instance.render(frame);
        const auto pose = bytes;
        instance.setPaused(true);
        check(host.timers.empty() && !host.captured, "hide releases frame and wake timers and input capture");
        host.advance(3600000);
        instance.setPaused(false);
        instance.render(frame);
        check(bytes == pose && host.timers.size() == 1, "resume excludes hidden time and preserves the physical pose");
        const CdSettingValue stop{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 0};
        check(instance.applySettings(&stop, 1) == CD_OK && host.timers.empty(),
              "disabling animation stops all scheduling");
        const CdSettingValue text{sizeof(CdSettingValue), CD_SETTING_TEXT, "text", 0, 0, "机械翻牌"};
        check(instance.applySettings(&text, 1) == CD_OK && host.timers.empty(),
              "text still updates while animation is disabled");
        const CdSettingValue bad{sizeof(CdSettingValue), CD_SETTING_TEXT, "text", 0, 0, "\xc0\xaf"};
        check(instance.applySettings(&bad, 1) == CD_ERROR_ARGUMENT && instance.settings.at("text").text == "机械翻牌",
              "invalid text update leaves accepted content intact");
        const CdSettingValue paged{sizeof(CdSettingValue), CD_SETTING_TEXT, "text", 0, 0, "第一页\n第二页"};
        instance.applySettings(&paged, 1);
        instance.render(frame);
        const auto firstPage = bytes;
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 300, 100, 0});
        instance.input({sizeof(CdInput), CD_INPUT_RELEASE, 0, 0, 300, 100, 0});
        instance.render(frame);
        check(bytes != firstPage && host.timers.empty(), "disabled motion still permits immediate manual pagination");
        host.allowTimers = false;
        const CdSettingValue start{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 1};
        check(instance.applySettings(&start, 1) != CD_OK && !instance.settings.at("animation").integer,
              "timer subscription failure does not partially commit settings");
        host.allowTimers = true;
        const CdSettingValue manual{sizeof(CdSettingValue), CD_SETTING_INTEGER, "interval", 0};
        check(instance.applySettings(&manual, 1) == CD_OK && instance.applySettings(&start, 1) == CD_OK &&
                  host.timers.empty(),
              "manual mode has no idle timer");
        instance.render(frame);
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 300, 100, 0});
        instance.input({sizeof(CdInput), CD_INPUT_CANCEL, 0, 0, 0, 0, 0});
        instance.input({sizeof(CdInput), CD_INPUT_RELEASE, 0, 0, 300, 100, 0});
        check(host.timers.empty(), "canceled press never flips the sign");
    }
    check(host.timers.empty() && !host.captured, "destruction leaves no host timers or capture");
}

void renderingTests() {
    Renderer renderer(std::filesystem::path(CD_SIGN_TEST_ROOT) / "assets");
    wait(renderer);
    cdsdk::Canvas canvas;
    bool bounds = true, visible = true;
    for (auto format : {Format::Standard, Format::Strip, Format::Compact}) {
        const auto g = geometry(format);
        for (double zoom : {.72, 1., 1.6}) {
            for (double dpi : {1., 1.5, 2.}) {
                const auto width = uint32_t(g.width / 2 * zoom * dpi), height = uint32_t(g.height / 2 * zoom * dpi);
                std::vector<uint8_t> data(size_t(width) * height * 4 + 32, 0xa5);
                const CdFrame frame{sizeof(CdFrame), width, height, width * 4, data.size() - 32, data.data(), dpi};
                Model model;
                for (int pose = 0; pose < 4; ++pose) {
                    model.advance(.1);
                    canvas.render(frame, {}, [&](auto& c, auto) {
                        c.scale(zoom / 2, zoom / 2);
                        renderer.draw(c, model, true, zoom / 2, format);
                    });
                    bounds &= std::all_of(data.end() - 32, data.end(), [](auto byte) {
                        return byte == 0xa5;
                    });
                    size_t opaque = 0;
                    for (size_t i = 3; i < frame.capacity; i += 4) {
                        opaque += data[i] > 0;
                    }
                    visible &= opaque > width * height * .35 && opaque < width * height * .85;
                }
            }
        }
    }
    check(bounds, "108 renders across legacy format values, sizes, DPIs and flip poses preserve output buffer guards");
    check(visible, "all rendering cases contain the complete sign and transparent surround");
}

void layoutTests() {
    bool fits = true, detected = true;
    for (auto format : {Format::Compact, Format::Strip, Format::Standard}) {
        const auto g = geometry(format);
        const int columns = format == Format::Compact ? 4 : 6, rows = format == Format::Standard ? 2 : 1;
        std::vector<CdCell> cells;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                cells.push_back({x + 7, y - 4});
            }
        }
        CdLayout layout{};
        layout.struct_size = sizeof(CdLayout);
        layout.cells = cells.data();
        layout.cell_count = uint32_t(cells.size());
        layout.logical_width = 300;
        layout.logical_height = 300;
        detected &= formatFor(layout) == Format::Compact;
        layout.cells = nullptr;
        layout.cell_count = 0;
        layout.logical_width = g.width / 2;
        layout.logical_height = g.height / 2;
        detected &= formatFor(layout) == Format::Compact;
        fits &= g.width == 2172 && g.height == 724;
        for (size_t i = 0; i < kSlots; ++i) {
            const auto r = g.slot(i);
            fits &= r.x >= 0 && r.y >= 0 && r.x + r.width < g.width && r.y + r.height < g.mechanismY();
        }
    }
    check(fits, "legacy layout values use the rigid approved plate and keep twelve flaps clear of the shaft");
    bool fixedFits = true;
    for (const auto format : {Format::Standard, Format::Compact, Format::Strip}) {
        const int columns = format == Format::Compact ? 4 : 6;
        const int rows = format == Format::Standard ? 2 : 1;
        for (int width : {80, 96, 112, 128, 160}) {
            const double aspect = double(columns * width - 6) / (rows * width * 1.25 - 6);
            const auto adaptive = geometry(format, aspect);
            fixedFits &= adaptive.width == 2172 && adaptive.height == 724;
            for (size_t index = 0; index < kSlots; ++index) {
                const auto slot = adaptive.slot(index);
                fixedFits &= slot.y > 0 && slot.y + slot.height < adaptive.mechanismY() &&
                             adaptive.mechanismY() + 47 * adaptive.mechanismScale() < adaptive.height;
            }
        }
    }
    check(fixedFits, "fixed presets preserve circular fittings and leave room around the rigid device");
    check(detected, "legacy occupied-cell layouts and older hosts resolve to the retained 4x1 design");
    Host host;
    Instance instance(host.context());
    const CdSettingValue stop{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 0};
    const CdSettingValue text{sizeof(CdSettingValue), CD_SETTING_TEXT, "text", 0, 0, "First\nSecond"};
    instance.applySettings(&stop, 1);
    instance.applySettings(&text, 1);
    bool switches = true;
    for (auto format : {Format::Standard, Format::Compact, Format::Strip, Format::Standard}) {
        const auto g = geometry(format);
        CdLayout layout{};
        layout.struct_size = sizeof(CdLayout);
        layout.logical_width = g.width / 2;
        layout.logical_height = g.height / 2;
        layout.safe_content = {0, 0, layout.logical_width, layout.logical_height};
        instance.layoutChanged(layout);
        const auto width = uint32_t(g.width), height = uint32_t(g.height);
        std::vector<uint8_t> bytes(size_t(width) * height * 4);
        const CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), 2};
        switches &= instance.render(frame) == CD_OK;
        // Asset decoding is asynchronous; settle its polling timer before checking idle scheduling.
        for (int i = 0; !host.timers.empty() && i < 500; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            host.advance(33);
            instance.render(frame);
        }
        const auto before = bytes;
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, g.width / 4, g.height / 4, 0});
        instance.input({sizeof(CdInput), CD_INPUT_RELEASE, 0, 0, g.width / 4, g.height / 4, 0});
        instance.render(frame);
        switches &= bytes != before && host.timers.empty() && instance.settings.at("text").text == "First\nSecond";
    }
    check(switches, "resizing the same instance preserves text, manual pagination, input mapping and stopped timers");
}
}  // namespace

int main() try {
    textTests();
    modelTests();
    lifecycleTests();
    layoutTests();
    renderingTests();
    std::cout << checks << " mechanical sign checks " << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
