#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "plugins/steampunk-desk/SpringClock.h"
#include "plugins/steampunk-desk/tools/Support.h"

namespace {
using namespace steamdesk;
using namespace steamdesk::testing;

void lifecycle(const SpringSpec& spec, const std::string& directory, const std::filesystem::path& output) {
    Host host;
    CdCreateContext context{
        sizeof(context), 1, spec.id, "physics-verify", directory.c_str(), &host.api.input.media.base, &host};
    const double width = spec.columns * 96. - 6, height = spec.rows * 120. - 6;
    Frame frame(unsigned(width * 2), unsigned(height * 2), 2);
    {
        SpringInstance instance(context), neighbour(context);
        neighbour.setPaused(true);
        check(instance.model().pieces().size() == 6 && instance.model().stats().joints == 6,
              "six independent Box2D digit bodies retain their original distance joints");
        check(instance.model().size().height == (spec.compact ? 112 : 216),
              "plugin footprint selects the correct physics world");
        check(instance.render(frame.value) == CD_OK, "real spring instance renders");
        for (unsigned attempt = 0; attempt < 200 && (!instance.ready() || !neighbour.ready()); ++attempt) {
            instance.render(frame.value);
            neighbour.render(frame.value);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(instance.ready() && neighbour.ready(),
              "both instances decode their own registered sprite atlas in background");
        instance.render(frame.value);
        const auto initial = frame.bytes;
        const auto steps = instance.model().stats().steps;
        instance.render(frame.value);
        check(frame.bytes == initial && instance.model().stats().steps == steps, "painting never advances physics");
        for (unsigned i = 0; i < 90; ++i) {
            host.advance(33);
            instance.render(frame.value);
        }
        check(instance.model().digits() != L"100830" && instance.model().stats().steps > steps,
              "host time updates the live digits while Box2D advances independently");
        const auto view = springView({0, 0, width, height}, instance.model());
        const auto before = instance.model().state(0).position;
        const double x = view.origin.x + before.x * 100 * view.scale, y = view.origin.y + before.y * 100 * view.scale;
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y});
        check(host.captured && instance.model().stats().joints == 7,
              "press captures the pointer and creates a real mouse joint");
        instance.input({sizeof(CdInput), CD_INPUT_MOVE, CD_BUTTON_LEFT, 0, x + 21 * view.scale, y - 6 * view.scale});
        for (unsigned i = 0; i < 30; ++i) {
            host.advance(33);
            instance.render(frame.value);
        }
        const auto dragged = instance.model().state(0).position;
        check(std::abs(dragged.x - before.x) > .025 && std::isfinite(instance.model().state(0).angle),
              "drag moves the actual rigid body and its pixel card");
        check(instance.save(output / "drag.png", frame.value), "native dragged-pose evidence saved");
        instance.input({sizeof(CdInput), CD_INPUT_CANCEL});
        check(!host.captured && instance.model().stats().joints == 6, "lost capture removes the mouse joint");
        for (unsigned i = 0; i < 60; ++i) {
            host.advance(33);
            instance.render(frame.value);
        }
        check(std::abs(instance.model().state(0).position.x - before.x) < std::abs(dragged.x - before.x),
              "released digit returns under the existing spring model");
        const auto cached = instance.cacheBuilds();
        const CdSettingValue color{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0x2671afff};
        check(instance.applySettings(&color, 1) == CD_OK &&
                  neighbour.settings.at("text_color").integer == (spec.compact ? 0x181613ff : 0xefe1bfff),
              "digit colors are independent per instance");
        instance.render(frame.value);
        check(instance.cacheBuilds() == cached + 1, "color changes rebuild the component sprites once");
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y});
        instance.setPaused(true);
        const auto pausedSteps = instance.model().stats().steps, redraws = host.redraws;
        host.advance(30000);
        check(!host.captured && host.timers.empty() && instance.model().stats().steps == pausedSteps &&
                  host.redraws == redraws,
              "hide releases capture, stops subscriptions and never advances hidden physics");
        instance.setPaused(false);
        host.advance(33);
        check(instance.model().stats().steps - pausedSteps <= 3 && host.timers.size() == 1,
              "resume updates wall time without simulating the hidden interval");
        const auto reduced = toggle("reduced_motion", true);
        check(instance.applySettings(&reduced, 1) == CD_OK, "reduced-motion mode is accepted");
        const auto frozen = instance.model().state(0);
        const auto oldDigits = instance.model().digits();
        host.advance(2000);
        check(instance.model().state(0).position.x == frozen.position.x &&
                  instance.model().state(0).position.y == frozen.position.y && instance.model().digits() != oldDigits,
              "reduced motion keeps cards still while real time continues");
        CdSettingValue invalid{sizeof(CdSettingValue), CD_SETTING_NUMBER, "speed", 0, -1};
        check(instance.applySettings(&invalid, 1) == CD_ERROR_ARGUMENT, "invalid motion settings are rejected");
        check(instance.render(CdFrame{}) == CD_ERROR_ARGUMENT, "invalid output buffer is rejected");
        instance.setPaused(true);
    }
    check(host.timers.empty() && !host.captured, "destroy releases all timers and pointer capture");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    std::cout << std::unitbuf;
    check(argc == 4, "repository, output directory and single plugin supplied");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto folder = utf8(argv[3]);
    const auto found = std::find_if(kSpringCatalog.begin(), kSpringCatalog.end(), [&](const auto& spec) {
        return folder == spec.folder;
    });
    check(found != kSpringCatalog.end(), "one implemented spring specification selected");
    const auto& spec = *found;
    std::filesystem::create_directories(output);
    lifecycle(spec, utf8(root / "build/relwithdebinfo/plugins" / spec.id), output);
    toys::Model model(toys::Kind::Clock, spec.compact, true);
    model.setTime(18, 42, 6, false, false);
    for (unsigned i = 0; i < 180; ++i) {
        model.advance(1. / 60, 0, .75);
    }
    cdsdk::Canvas canvas;
    // Cached Direct2D sprites must be released before their owning Canvas/COM.
    Host renderHost;
    const auto directory = utf8(root / "build/relwithdebinfo/plugins" / spec.id);
    CdCreateContext renderContext{
        sizeof(CdCreateContext),          1,          spec.id, "native-spring", directory.c_str(),
        &renderHost.api.input.media.base, &renderHost};
    SpringRenderer renderer(renderContext);
    const auto settings = springDefaults(spec.compact);
    unsigned count = 0;
    for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
        for (double dpi : {1., 1.5, 2.}) {
            const double width = spec.columns * cell - 6., height = spec.rows * cell * 1.25 - 6.;
            const CdRect safe{7, 9, width, height};
            const auto view = springView(safe, model);
            const auto bounds = springFrameBounds(model);
            const double left = view.origin.x + bounds.x * view.scale;
            const double top = view.origin.y + bounds.y * view.scale;
            check(left >= safe.x && top >= safe.y && left + bounds.width * view.scale <= safe.x + safe.width + .001 &&
                      top + bounds.height * view.scale <= safe.y + safe.height + .001,
                  "gantry fits the safe area without clipping or nonuniform scaling");
            if (spec.compact) {
                const double inset = std::min(width, height) * .025;
                check(std::abs(left - safe.x - inset) < .001 &&
                          std::abs(bounds.width * view.scale - (width - 2 * inset)) < .001,
                      "4x1 spring gantry spans the same visible width as the matrix clocks");
            }
            Frame frame(unsigned(std::ceil((width + 14) * dpi)), unsigned(std::ceil((height + 18) * dpi)), dpi);
            const auto paint = [&] {
                canvas.render(frame.value, safe, [&](auto& layer, CdRect area) {
                    const auto view = springView(area, model);
                    layer.saved([&] {
                        layer.translate(view.origin.x - area.x, view.origin.y - area.y);
                        layer.scale(view.scale, view.scale);
                        renderer.draw(layer, model, settings, view.scale, dpi);
                    });
                });
            };
            for (unsigned attempt = 0; attempt < 200; ++attempt) {
                paint();
                if (renderer.ready()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(renderer.ready(), "registered gantry and plaque atlas is loaded");
            check(frame.valid(safe), "native spring render respects safe area, row padding, guards and alpha");
            const auto name =
                folder + "-w" + std::to_string(cell) + "-dpi" + std::to_string(unsigned(dpi * 100)) + ".png";
            check(canvas.save((output / name).wstring(), frame.value), "native spring preview saved");
            if (cell == 96 && dpi == 2) {
                std::filesystem::copy_file(output / name,
                                           root / "plugins/steampunk-desk/packages" / folder / "preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
            const auto before = frame.bytes;
            const auto cached = renderer.cacheBuilds();
            const auto steps = model.stats().steps;
            paint();
            check(frame.bytes == before && renderer.cacheBuilds() == cached && model.stats().steps == steps,
                  "repeated paints reuse sprites and leave the physical world unchanged");
            ++count;
        }
    }
    check(count == 15, "15 native grid-size/DPI renders pass for the selected plugin");
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
