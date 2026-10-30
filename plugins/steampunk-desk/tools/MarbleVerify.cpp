#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "plugins/steampunk-desk/MarbleClock.h"
#include "plugins/steampunk-desk/tools/Support.h"

namespace {
using namespace steamdesk;
using namespace steamdesk::testing;

void step(marbleclock::Model& model, unsigned frames) {
    for (unsigned i = 0; i < frames; ++i) {
        model.advance(1. / 60);
    }
}

void decoded(MarbleInstance& instance, const CdFrame& frame) {
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        if (instance.render(frame) != CD_OK) {
            throw std::runtime_error("Marble instance render failed");
        }
        if (instance.ready()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Pixel chassis decoding timed out");
}

void lifecycle(const MarbleSpec& spec, const std::string& directory, const std::filesystem::path& output) {
    Host host;
    CdCreateContext context{
        sizeof(context), 1, spec.id, "marble-verify", directory.c_str(), &host.api.input.media.base, &host};
    const double width = spec.columns * 96. - 6, height = spec.rows * 120. - 6;
    Frame frame(unsigned(width * 2), unsigned(height * 2), 2);
    {
        MarbleInstance instance(context), neighbour(context);
        neighbour.setPaused(true);
        decoded(instance, frame.value);
        check(instance.model().shape().width == (spec.compact ? 720 : 1080), "correct original marble world selected");
        check(instance.model().marbles().size() == 30, "startup reconstructs the real current-minute pile");
        const auto steps = instance.model().stats().steps, releases = instance.model().releases();
        const auto first = frame.bytes;
        instance.render(frame.value);
        check(instance.model().stats().steps == steps && frame.bytes == first,
              "painting changes neither physics nor the current time");
        host.advance(1033);
        instance.render(frame.value);
        check(instance.model().releases() == releases + 1 && instance.model().timeText() == L"10:08:31",
              "one host second releases exactly one real marble");
        const auto view = marbleView({0, 0, width, height}, instance.model());
        const auto click = [&](double x, double y) {
            x = view.origin.x + x * view.scale;
            y = view.origin.y + y * view.scale;
            instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y});
            instance.input({sizeof(CdInput), CD_INPUT_RELEASE, 0, 0, x, y});
        };
        const auto before = instance.model().releases();
        click(65, 89);
        check(instance.model().releases() == before + 1, "pixel feeder clicks release a real marble");
        instance.render(frame.value);
        const auto& marble = instance.model().marbles().front();
        const auto body = instance.model().state(marble);
        const double x = view.origin.x + body.position.x * marbleclock::kPixelsPerMeter * view.scale,
                     y = view.origin.y + body.position.y * marbleclock::kPixelsPerMeter * view.scale;
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y});
        check(host.captured && instance.model().dragging(), "rendered marble hit-test captures a real mouse joint");
        instance.input({sizeof(CdInput), CD_INPUT_MOVE, CD_BUTTON_LEFT, 0, x + 18 * view.scale, y - 22 * view.scale});
        for (unsigned i = 0; i < 12; ++i) {
            host.advance(33);
            instance.render(frame.value);
        }
        check(instance.save(output / "drag.png", frame.value), "native marble drag pose saved");
        instance.input({sizeof(CdInput), CD_INPUT_CANCEL});
        check(!host.captured && !instance.model().dragging(), "lost capture releases the marble joint");
        click(instance.model().shape().pivot + 52, marbleclock::kFloorY);
        check(instance.model().phase() != marbleclock::Phase::Collecting, "pixel hinge opens the actual hinged floor");
        const auto cached = instance.cacheBuilds();
        host.advance(500);
        instance.render(frame.value);
        check(instance.cacheBuilds() == cached, "moving marbles and floor reuse the decoded static case");
        check(instance.save(output / "open.png", frame.value), "native open-floor evidence saved");
        const CdSettingValue color{sizeof(CdSettingValue), CD_SETTING_COLOR, "ball_color", 0x2671afff};
        check(instance.applySettings(&color, 1) == CD_OK && neighbour.settings.at("ball_color").integer == 0xa8adb0ff,
              "marble color settings remain local to each instance");
        instance.setPaused(true);
        const auto hiddenSteps = instance.model().stats().steps, redraws = host.redraws;
        host.advance(30000);
        check(host.timers.empty() && !host.captured && instance.model().stats().steps == hiddenSteps &&
                  host.redraws == redraws,
              "hidden clock releases all timers and input capture without simulating the gap");
        instance.setPaused(false);
        check(instance.model().second() == 1 || instance.model().second() == 2,
              "resume reconstructs the current minute without replaying missed balls");
        const auto reduced = toggle("reduced_motion", true);
        check(instance.applySettings(&reduced, 1) == CD_OK, "reduced motion is accepted");
        const auto frozenSteps = instance.model().stats().steps;
        host.advance(2000);
        check(instance.model().stats().steps <= frozenSteps && instance.model().timeText().substr(0, 6) == L"10:09:",
              "reduced motion keeps physical motion stopped while the displayed minute stays correct");
        check(instance.render(CdFrame{}) == CD_ERROR_ARGUMENT, "invalid frame is rejected");
        instance.setPaused(true);
    }
    check(host.timers.empty() && !host.captured, "marble instance destruction releases all resources");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    std::cout << std::unitbuf;
    check(argc == 4, "repository, output directory and single marble plugin supplied");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto folder = utf8(argv[3]);
    const auto found = std::find_if(kMarbleCatalog.begin(), kMarbleCatalog.end(), [&](const auto& spec) {
        return folder == spec.folder;
    });
    check(found != kMarbleCatalog.end(), "one implemented marble specification selected");
    const auto& spec = *found;
    const auto directory = utf8(root / "build/relwithdebinfo/plugins" / spec.id);
    std::filesystem::create_directories(output);
    lifecycle(spec, directory, output);
    marbleclock::Model flow(spec.compact);
    flow.setTime(18, 42, 0, false);
    for (unsigned second = 1; second <= 59; ++second) {
        flow.setTime(18, 42, int(second));
        step(flow, 60);
    }
    check(flow.releases() == 59 && flow.marbles().size() == 59 && flow.contacts() > 0,
          "a full minute uses real timed releases, gravity, contacts and collection");
    flow.setTime(18, 43, 0);
    check(flow.phase() == marbleclock::Phase::Opening, "minute rollover starts the actual gate-opening phase");
    step(flow, 720);
    check(flow.phase() == marbleclock::Phase::Collecting && flow.oldMarbles() == 0 && flow.recycled() > 0,
          "opening, draining, bottom return and reclosing complete in the original Box2D model");
    Host host;
    CdCreateContext context{
        sizeof(context), 1, spec.id, "native-marble", directory.c_str(), &host.api.input.media.base, &host};
    cdsdk::Canvas canvas;
    MarbleRenderer renderer(context, spec);
    marbleclock::Model model(spec.compact);
    model.setTime(18, 42, 35, false);
    step(model, 180);
    for (int second = 36; second <= 40; ++second) {
        model.setTime(18, 42, second);
        step(model, 60);
    }
    const auto settings = marbleDefaults();
    unsigned count = 0;
    for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
        for (double dpi : {1., 1.5, 2.}) {
            const double width = spec.columns * cell - 6., height = spec.rows * cell * 1.25 - 6.;
            const CdRect safe{7, 9, width, height};
            Frame frame(unsigned(std::ceil((width + 14) * dpi)), unsigned(std::ceil((height + 18) * dpi)), dpi);
            const auto paintValues = [&](const cdsdk::Settings& values, bool light) {
                canvas.render(frame.value, safe, [&](auto& layer, CdRect area) {
                    const auto view = marbleView(area, model);
                    layer.saved([&] {
                        layer.translate(view.origin.x - area.x, view.origin.y - area.y);
                        layer.scale(view.scale, view.scale);
                        renderer.draw(layer, model, values, light, view.scale, dpi);
                    });
                });
            };
            const auto paint = [&] {
                paintValues(settings, false);
            };
            for (unsigned attempt = 0; attempt < 200; ++attempt) {
                paint();
                if (renderer.ready()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(renderer.ready() && frame.valid(safe),
                  "native marble output respects actual DPI, transparency, padding and guards");
            const auto name =
                folder + "-w" + std::to_string(cell) + "-dpi" + std::to_string(unsigned(dpi * 100)) + ".png";
            check(canvas.save((output / name).wstring(), frame.value), "native marble preview saved");
            if (cell == 96 && dpi == 2) {
                std::filesystem::copy_file(output / name,
                                           root / "plugins/steampunk-desk/packages" / folder / "preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
            const auto before = frame.bytes;
            const auto cached = renderer.cacheBuilds(), requests = renderer.imageStats().requests;
            paint();
            check(
                frame.bytes == before && renderer.cacheBuilds() == cached && renderer.imageStats().requests == requests,
                "steady renders reuse chassis pixels and background decode requests");
            if (cell == 96 && dpi == 2) {
                auto blue = settings;
                CdSettingValue color{sizeof(CdSettingValue), CD_SETTING_COLOR, "ball_color", 0x2671afff};
                blue.apply(&color, 1);
                paintValues(blue, false);
                check(frame.bytes != before && renderer.cacheBuilds() == cached,
                      "native marble color changes independently of the static case");
                paintValues(settings, true);
                check(frame.bytes != before, "light appearance visibly changes the rear panel");
                auto clear = settings;
                const auto background = toggle("show_background", false);
                clear.apply(&background, 1);
                paintValues(clear, false);
                check(frame.bytes != before && frame.valid(safe),
                      "backplate can be hidden while tracks and live mechanics remain valid");
            }
            ++count;
        }
    }
    check(count == 15, "15 native grid-size/DPI renders pass for the selected plugin");
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
