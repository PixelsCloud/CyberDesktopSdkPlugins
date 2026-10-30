#include <Windows.h>
#include <sdk/WindowsFonts.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <tuple>

#include "plugins/clock-suite/FlipClock.h"

namespace {
void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct Host {
    int redraws = 0;
    uint64_t next = 1;

    struct Timer {
        CdTimerCallback callback;
        void* user;
    };

    std::map<uint64_t, Timer> timers;

    static void CD_CALL redraw(void* p) {
        ++static_cast<Host*>(p)->redraws;
    }

    static CdResult CD_CALL subscribe(void* p, uint32_t ms, CdTimerCallback cb, void* user, uint64_t* token) {
        check(ms == 16, "flip animation timer");
        auto& h = *static_cast<Host*>(p);
        *token = h.next++;
        h.timers[*token] = {cb, user};
        return CD_OK;
    }

    static void CD_CALL unsubscribe(void* p, uint64_t token) {
        static_cast<Host*>(p)->timers.erase(token);
    }

    CdHostApi api{sizeof(api), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, unsubscribe, subscribe};
};
}  // namespace

int main() {
    using namespace clockplugin;
    Spec spec{"test.flip", false, 2, 2, true};
    auto opts = options(defaults(spec), {240, 240, 240, 255});
    check(opts.seconds && opts.twelveHour && opts.flipDurationMs == 500 && opts.fontFamily == "@zqk-free",
          "reference defaults with ZQK font");
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),
          "font factory");
    cdsdk::WindowsFonts fonts(factory.Get());
    check(fonts.bundledAvailable("@zqk-free"), "external ZQK font file loads");
    fonts.select(factory.Get(), "@zqk-free");
    check(fonts.collection() != nullptr, "ZQK uses private collection without system installation");
    Time before{2026, 10, 12, 1, 23, 59, 59, 0}, after{2026, 10, 13, 2, 0, 0, 0, 0};
    FlipTimeline timeline;
    auto pose = timeline.sample(before, opts, 1000);
    check(!timeline.active(1000, 500) && pose.values[0].after == 11, "no startup flip");
    pose = timeline.sample(after, opts, 2000);
    check(pose.values[0].before == 11 && pose.values[0].after == 12 && pose.values[1].before == 59 &&
              pose.values[1].after == 0 && pose.values[2].after == 0,
          "midnight rollover");
    pose = timeline.sample(after, opts, 2250);
    check(pose.values[0].progress == .5 && timeline.active(2250, 500), "halfway hinge");
    pose = timeline.sample(after, opts, 2500);
    check(pose.values[0].progress == 1 && !timeline.active(2500, 500), "one flip completes after 500ms");
    pose = timeline.sample(after, opts, 2600);
    check(pose.values[0].before == 12 && pose.values[0].after == 12, "unchanged value never restarts");
    opts.seconds = false;
    after.second = 1;
    timeline.sample(after, opts, 2700);
    check(!timeline.active(2700, 500), "hidden seconds do not animate");
    opts.twelveHour = false;
    timeline.reset(after, opts, 3000);
    check(timeline.sample(after, opts, 3000).values[0].after == 0, "24-hour zero");
    FlipRenderer renderer;
    Renderer exporter;
    for (auto [rows, columns, w, h] :
         {std::tuple{2, 2, 136, 136}, std::tuple{2, 2, 136, 208}, std::tuple{2, 2, 184, 280}, std::tuple{1, 4, 280, 64},
          std::tuple{1, 4, 280, 100}, std::tuple{2, 4, 280, 136}, std::tuple{2, 4, 280, 208}}) {
        const Spec layoutSpec{"test.flip.layout", false, rows, columns, true};
        const auto geometry = arrangeFlip(layoutSpec, w, h, true);
        check((columns == 4 ? geometry.hours.x + geometry.hours.width < geometry.minutes.x
                            : geometry.hours.y + geometry.hours.height < geometry.minutes.y) &&
                  geometry.seconds.y + geometry.seconds.height <= h,
              "cards fit square and portrait grids");
        const auto& seconds = geometry.seconds;
        check(seconds.x >= geometry.minutes.x && seconds.y >= geometry.minutes.y &&
                  seconds.x + seconds.width <= geometry.minutes.x + geometry.minutes.width &&
                  seconds.y + seconds.height <= geometry.minutes.y + geometry.minutes.height,
              "seconds stays inside minute card");
        if (columns == 4) {
            check(seconds.y > geometry.minutes.y + geometry.minutes.height * .5,
                  "horizontal seconds uses lower right footer");
        } else {
            check(seconds.x > geometry.minutes.x + geometry.minutes.width * .5, "vertical seconds uses right column");
            check(
                std::abs(seconds.y + seconds.height * .5 - (geometry.minutes.y + geometry.minutes.height * .5)) < .001,
                "vertical seconds vertically centered");
        }
        check(geometry.hours.x == 1 && geometry.hours.y == 1 &&
                  std::abs(geometry.minutes.x + geometry.minutes.width - (w - 3)) < .001 &&
                  std::abs(geometry.minutes.y + geometry.minutes.height - (h - 4)) < .001,
              "outer face margins match spectrum");
        for (double dpr : {1.0, 1.5, 2.0}) {
            opts.seconds = true;
            opts.twelveHour = true;
            const uint32_t pw = uint32_t(w * dpr), ph = uint32_t(h * dpr);
            std::vector<uint8_t> pixels(size_t(pw) * ph * 4 + 32, 0xCD);
            CdFrame frame{sizeof(frame), pw, ph, pw * 4, uint64_t(pixels.size() - 32), pixels.data(), dpr};
            std::vector<uint8_t> initial, previous;
            for (double progress : {0.0, .25, .5, .75, 1.0}) {
                FlipPose animation{
                    {FlipValue{11, 12, progress}, FlipValue{59, 0, progress}, FlipValue{59, 0, progress}}};
                std::string error;
                check(renderer.render(layoutSpec, opts, after, animation, frame, {}, &error), error.c_str());
                check(std::all_of(pixels.end() - 32, pixels.end(),
                                  [](uint8_t v) {
                                      return v == 0xCD;
                                  }),
                      "no out-of-bounds frame writes");
                int opaque = 0;
                for (size_t i = 0; i < frame.capacity; i += 4) {
                    check(
                        pixels[i] <= pixels[i + 3] && pixels[i + 1] <= pixels[i + 3] && pixels[i + 2] <= pixels[i + 3],
                        "premultiplied RGBA");
                    opaque += pixels[i + 3] > 220;
                }
                check(opaque > pw * ph * .6 && pixels[3] == 0, "visible cards with transparent outer canvas");
                if (previous.empty())
                    initial = pixels;
                else
                    check(previous != pixels, "successive flip phases change pixels");
                previous = pixels;
            }
            check(initial != pixels, "completed flip shows new digits");
        }
    }
    // A cached renderer must match a fresh one after surface and theme changes.
    // Exercise both moving halves: each uses CPU sampling plus retained faces.
    for (const auto [rows, columns, w, h] :
         {std::tuple{2, 2, 136, 208}, std::tuple{1, 4, 280, 100}, std::tuple{2, 4, 280, 208}}) {
        const Spec layoutSpec{"test.flip.cache", false, rows, columns, true};
        for (double dpi : {1., 1.25, 1.5, 2., 1.}) {
            for (bool light : {false, true}) {
                auto appearance = options(defaults(layoutSpec), {240, 240, 240, 255});
                appearance.cardColor = light ? CdColor{235, 230, 218, 255} : CdColor{35, 40, 48, 255};
                const uint32_t pw = uint32_t((w + 16) * dpi), ph = uint32_t((h + 16) * dpi);
                std::vector<uint8_t> pixels(size_t(pw) * ph * 4), reference(pixels.size());
                CdFrame frame{sizeof(frame), pw, ph, pw * 4, pixels.size(), pixels.data(), dpi};
                auto expected = frame;
                expected.rgba = reference.data();
                FlipRenderer fresh;
                for (double progress : {.25, .75}) {
                    const FlipPose animation{
                        {FlipValue{11, 12, progress}, FlipValue{59, 0, progress}, FlipValue{59, 0, progress}}};
                    const Rect safe{8, 8, double(w), double(h)};
                    check(fresh.render(layoutSpec, appearance, after, animation, expected, safe),
                          "fresh flip cache reference");
                    check(renderer.render(layoutSpec, appearance, after, animation, frame, safe) && pixels == reference,
                          "flip cache matches fresh rendering after DPI and palette changes");
                    check(renderer.render(layoutSpec, appearance, after, animation, frame, safe) && pixels == reference,
                          "repeated flip sampling retains independent old and new faces");
                    check(pixels[3] == 0 && pixels.back() == 0, "flip face cache respects host padding");
                }
            }
        }
    }
    // Switch collections, language and fit sizes on one retained renderer.
    for (const auto* family : {"@zqk-free", "Segoe UI", "@zqk-free"}) {
        for (const auto* language : {"en", "zh-CN"}) {
            for (double size : {60., 100., 160.}) {
                const Spec layoutSpec{"test.flip.fonts", false, 2, 4, true};
                auto appearance = options(defaults(layoutSpec), {240, 240, 240, 255});
                appearance.fontFamily = family;
                appearance.language = language;
                appearance.sizePercent = size;
                std::vector<uint8_t> pixels(350 * 260 * 4), reference(pixels.size());
                CdFrame frame{sizeof(frame), 350, 260, 350 * 4, pixels.size(), pixels.data(), 1.25};
                auto expected = frame;
                expected.rgba = reference.data();
                FlipRenderer fresh;
                const FlipPose animation{{FlipValue{11, 12, .25}, FlipValue{59, 0, .75}, FlipValue{59, 0, .75}}};
                check(fresh.render(layoutSpec, appearance, after, animation, expected, {}),
                      "fresh fitted text reference");
                check(renderer.render(layoutSpec, appearance, after, animation, frame, {}) && pixels == reference,
                      "font collection, language and fitted size changes preserve face rendering");
            }
        }
    }
    Host host;
    CdInstanceApi api{};
    CdCreateContext context{sizeof(context), 1, spec.id, "flip-instance", "", &host.api, &host};
    check(create(spec, &context, &api, sizeof(api)) == CD_OK, "create flip instance");
    std::vector<uint8_t> pixels(136 * 208 * 4);
    CdFrame frame{sizeof(frame), 136, 208, 136 * 4, uint64_t(pixels.size()), pixels.data(), 1};
    check(api.render(api.instance, &frame) == CD_OK, "host render");
    api.set_paused(api.instance, 1);
    check(host.timers.empty(), "pause removes timer");
    check(api.render(api.instance, &frame) == CD_OK, "paused render");
    const auto frozen = pixels;
    Sleep(80);
    check(api.render(api.instance, &frame) == CD_OK && pixels == frozen, "pause freezes animation and time");
    CdSettingValue duration{sizeof(duration), CD_SETTING_NUMBER, "flip_duration", 0, 199, nullptr};
    check(api.apply_settings(api.instance, &duration, 1) == CD_ERROR_ARGUMENT, "invalid duration rejected");
    duration.number = 800;
    check(api.apply_settings(api.instance, &duration, 1) == CD_OK, "duration setting accepted");
    CdSettingValue saved{};
    check(api.get_setting(api.instance, "flip_duration", &saved, sizeof(saved)) == CD_OK && saved.number == 800,
          "duration persisted");
    api.set_paused(api.instance, 0);
    check(host.timers.size() == 1, "resume has one timer");
    check(api.render(api.instance, &frame) == CD_OK, "resume current time");
    CdSettingValue seconds{sizeof(seconds), CD_SETTING_TOGGLE, "seconds", 0, 0, nullptr};
    check(api.apply_settings(api.instance, &seconds, 1) == CD_OK, "hide seconds");
    check(api.render(api.instance, &frame) == CD_OK, "hide seconds frame");
    const auto redraws = host.redraws;
    const auto timer = host.timers.begin()->second;
    timer.callback(timer.user, 0);
    check(host.redraws == redraws, "idle animation timer avoids redraw");
    api.destroy(api.instance);
    check(host.timers.empty(), "destroy releases timer");
    std::cout << "Flip clock: external ZQK font, 3 layouts, rollover, animation "
                 "phases, 21 size/DPI cases, timers/pause/settings PASS\n";
}
