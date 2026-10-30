#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <thread>

#include "plugins/mechanical-pixels/tools/TestHost.h"

namespace {
using namespace mechanicalpixels;

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

struct Frame {
    std::vector<uint8_t> bytes;
    CdFrame value;

    Frame(unsigned width, unsigned height, double dpi = 1)
        : bytes(size_t(width * 4 + 16) * height + 64, 0xa5),
          value{sizeof(CdFrame),   width, height, width * 4 + 16, size_t(width * 4 + 16) * height,
                bytes.data() + 32, dpi} {}

    bool guards() const {
        for (size_t i = 0; i < 32; ++i) {
            if (bytes[i] != 0xa5 || bytes[bytes.size() - 1 - i] != 0xa5) {
                return false;
            }
        }
        for (unsigned y = 0; y < value.height; ++y) {
            for (unsigned x = value.width * 4; x < value.stride; ++x) {
                if (value.rgba[size_t(y) * value.stride + x] != 0xa5) {
                    return false;
                }
            }
        }
        return true;
    }
};

CdSettingValue number(const char* id, double value) {
    return {sizeof(CdSettingValue), CD_SETTING_NUMBER, id, 0, value, ""};
}

CdSettingValue integer(const char* id, int64_t value, uint32_t type = CD_SETTING_TOGGLE) {
    return {sizeof(CdSettingValue), type, id, value, 0, ""};
}

CdSettingValue choice(const char* id, const char* value) {
    return {sizeof(CdSettingValue), CD_SETTING_CHOICE, id, 0, 0, value};
}

CdSettingValue color(const char* id, uint32_t rgba) {
    return {sizeof(CdSettingValue), CD_SETTING_COLOR, id, rgba, 0, ""};
}

void decoded(Renderer& renderer, Kind kind, const cdsdk::Settings& settings, const CdFrame& frame,
             const widgetplugin::Spectrum& spectrum = {}, CdRect safe = {}) {
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (!renderer.render(kind, settings, "10:08:30", true, spectrum, frame, safe)) {
            throw std::runtime_error("Native render failed");
        }
        if (renderer.ready()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Background chassis decoding timed out");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    using namespace mechanicalpixels;
    bool passed = true;
    const auto check = [&](const char* message, bool result) {
        passed &= result;
        std::cout << (result ? "PASS " : "FAIL ") << message << '\n';
    };
    if (argc != 2) {
        throw std::invalid_argument("Expected repository path");
    }
    const std::filesystem::path root(argv[1]);
    std::set<std::string_view> identities;
    for (const auto& spec : kCatalog) {
        identities.insert(spec.id);
    }
    check("eight immutable identities are separate from the original clock and matrix plugins", identities.size() == 8);

    testing::Host environment;
    auto settings = defaults(Kind::ClockSquare);
    const auto context = environment.context(kCatalog[0], "");
    cdsdk::Environment clock;
    clock.bind(context);
    check("clock reads deterministic host time rather than frozen preview digits",
          clockText(clock.snapshot(), settings) == "10:08:30");
    environment.offset = 480;
    check("local timezone follows the current host offset", clockText(clock.snapshot(), settings) == "18:08:30");
    auto utc = choice("timezone", "utc");
    settings.apply(&utc, 1);
    check("UTC does not inherit the system timezone", clockText(clock.snapshot(), settings) == "10:08:30");
    CdSettingValue quarterHour[]{choice("timezone", "offset"), integer("utc_offset", 345, CD_SETTING_INTEGER)};
    settings.apply(quarterHour, 2);
    check("custom quarter-hour timezone preserves minutes", clockText(clock.snapshot(), settings) == "15:53:30");
    auto twelve = choice("format", "12");
    settings.apply(&twelve, 1);
    check("twelve-hour clock is applied to the actual time", clockText(clock.snapshot(), settings) == "03:53:30");
    CdSettingValue invalidBatch[]{number("brightness", 60), choice("timezone", "invalid")};
    check("invalid settings batch is atomic",
          settings.apply(invalidBatch, 2) != CD_OK && settings.at("brightness").number == 100);

    for (const auto& spec : kCatalog) {
        const auto directory = utf8(root / "build/relwithdebinfo/plugins" / spec.id);
        testing::Host host;
        auto create = host.context(spec, directory.c_str());
        auto values = defaults(spec.kind);
        {
            Renderer renderer(create);
            for (double dpi : {1., 1.25, 1.5, 2.}) {
                Frame frame(unsigned(std::ceil((spec.columns * 96 + 12) * dpi)), unsigned(std::ceil(140 * dpi)), dpi);
                const CdRect safe{7, 9, spec.columns * 96. - 6, 114};
                widgetplugin::Spectrum full;
                full.level.fill(1);
                full.peak.fill(1);
                decoded(renderer, spec.kind, values, frame.value, full, safe);
                bool transparentOutside = true, premultiplied = true;
                unsigned green = 0, yellow = 0, red = 0;
                for (unsigned y = 0; y < frame.value.height; ++y) {
                    for (unsigned x = 0; x < frame.value.width; ++x) {
                        const auto* pixel = frame.value.rgba + size_t(y) * frame.value.stride + x * 4;
                        if (x / dpi < safe.x - 1 || y / dpi < safe.y - 1 || x / dpi > safe.x + safe.width + 1 ||
                            y / dpi > safe.y + safe.height + 1) {
                            transparentOutside &= pixel[3] == 0;
                        }
                        premultiplied &= pixel[0] <= pixel[3] && pixel[1] <= pixel[3] && pixel[2] <= pixel[3];
                        green += pixel[1] > pixel[0] * 1.2 && pixel[1] > pixel[2] * 1.2 && pixel[3] > 150;
                        yellow += pixel[0] > pixel[2] * 1.7 && pixel[1] > pixel[2] * 1.7 && pixel[3] > 150;
                        red += pixel[0] > pixel[1] * 1.5 && pixel[0] > pixel[2] * 1.5 && pixel[3] > 150;
                    }
                }
                check(
                    "fractional-DPI render respects row padding, guards, safe-area transparency and premultiplication",
                    frame.guards() && transparentOutside && premultiplied);
                if (spec.kind == Kind::MusicSquare) {
                    check("actual square spectrum has discrete green, amber and red zones", green && yellow && red);
                }
                const auto builds = renderer.cacheBuilds();
                renderer.render(spec.kind, values, "23:59:59", false, {}, frame.value, safe);
                check("live time and audio reuse the chassis, dark matrix and light caches",
                      renderer.cacheBuilds() == builds);
                const auto geometry = arrange(spec.kind, safe.width, safe.height, dpi);
                check("pixel pitch stays isotropic with a legible nonempty display",
                      geometry.pitch > 0 && geometry.diameter <= geometry.pitch && geometry.columns > 20);
            }
            Frame colored(spec.columns * 96, 140);
            widgetplugin::Spectrum full;
            full.level.fill(1);
            full.peak.fill(1);
            decoded(renderer, spec.kind, values, colored.value, full);
            const auto beforeColorChange = renderer.cacheBuilds();
            std::vector<CdSettingValue> colors;
            if (isClock(spec.kind)) {
                colors = {color("text_color", 0x2681e6ff), color("seconds_color", 0x2681e6ff)};
            } else {
                colors = {color("color", 0x2681e6ff), color("color_top", 0x2681e6ff),
                          color(spec.kind == Kind::MusicSquare ? "color_middle" : "peak_color", 0x2681e6ff)};
            }
            check("every pixel color picker accepts an instance-local color",
                  values.apply(colors.data(), uint32_t(colors.size())) == CD_OK);
            renderer.render(spec.kind, values, "10:08:30", true, full, colored.value, {});
            unsigned bluePixels = 0;
            for (unsigned y = 0; y < colored.value.height; ++y) {
                for (unsigned x = 0; x < colored.value.width; ++x) {
                    const auto* pixel = colored.value.rgba + size_t(y) * colored.value.stride + x * 4;
                    bluePixels += pixel[2] > pixel[0] * 1.7 && pixel[2] > pixel[1] * 1.4 && pixel[3] > 150;
                }
            }
            check("changing the selected colors rebuilds cached lamps and changes actual rendered pixels",
                  renderer.cacheBuilds() == beforeColorChange + 1 && bluePixels > 20);
            colors.clear();
            if (isClock(spec.kind)) {
                colors = {color("text_color", 0), color("seconds_color", 0)};
            } else {
                colors = {color("color", 0), color("color_top", 0),
                          color(spec.kind == Kind::MusicSquare ? "color_middle" : "peak_color", 0)};
            }
            values.apply(colors.data(), uint32_t(colors.size()));
            renderer.render(spec.kind, values, "10:08:30", true, full, colored.value, {});
            const auto invisibleLit = colored.bytes;
            renderer.render(spec.kind, values, "10:08:30", false, {}, colored.value, {});
            check("transparent selected colors have no residual lamp highlight or halo", colored.bytes == invisibleLit);
        }
        check("standalone renderer releases its image-completion timers", host.timers.empty());

        {
            Instance first(create), second(create);
            const auto setting = isClock(spec.kind) ? integer("seconds", 0) : number("gain", 2.0);
            check("instance settings are isolated",
                  first.applySettings(&setting, 1) == CD_OK &&
                      (isClock(spec.kind) ? second.settings.at("seconds").integer == 1
                                          : second.settings.at("gain").number == 3.5));
            const char* colorSetting = isClock(spec.kind) ? "text_color" : "color";
            const auto otherColor = second.settings.at(colorSetting).integer;
            const auto custom = color(colorSetting, 0x2681e6ff);
            check("changing one instance's pixel color leaves its neighbour unchanged",
                  first.applySettings(&custom, 1) == CD_OK && second.settings.at(colorSetting).integer == otherColor);
            const auto shape = choice("style", "original");
            check("a mechanical module exposes no switch into the original plugin's style",
                  first.applySettings(&shape, 1) == CD_ERROR_ARGUMENT);
            first.setPaused(true);
            second.setPaused(true);
            const auto redraws = host.redraws;
            host.advance(10000);
            check("hidden instances release timers and audio and do not redraw",
                  host.timers.empty() && host.audio.empty() && host.redraws == redraws);
            first.setPaused(false);
            if (isClock(spec.kind)) {
                check("clock resumes with a wall-clock timer", !host.timers.empty());
            } else {
                check("audio resumes with one independent subscription", host.audio.size() == 1);
                host.feed(1);
                host.advance(150);
                Frame frame(768, 140);
                first.render(frame.value);
                const auto active = frame.bytes;
                host.feed(0);
                for (unsigned i = 0; i < 80; ++i) {
                    host.advance(33);
                    first.render(frame.value);
                }
                check("silent input decays to an idle matrix and releases visual animation",
                      frame.bytes != active && host.timers.size() <= 1);
                // Finish any image-only completion timer before measuring idle wakeups.
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                host.advance(500);
                first.render(frame.value);
                const auto idleRedraws = host.redraws;
                host.advance(2000);
                check("settled silence requests no new frames", host.redraws == idleRedraws);

                host.feed(.7f);
                host.advance(66);
                const auto reduce = integer("reduced_motion", 1);
                host.allowAudio = false;
                check("failed audio replacement retains the prior subscription and setting",
                      first.applySettings(&reduce, 1) != CD_OK && host.audio.size() == 1 &&
                          first.settings.at("reduced_motion").integer == 0);
                host.allowAudio = true;
                host.feed(.01f);
                host.allowTimers = false;
                check("failed timer replacement rolls back settings and restores the prior audio rate",
                      first.applySettings(&reduce, 1) != CD_OK && host.audio.size() == 1 &&
                          host.audio.begin()->second.fps == 30 && first.settings.at("reduced_motion").integer == 0);
                host.allowTimers = true;
                check("reduced motion uses a lower audio frame rate",
                      first.applySettings(&reduce, 1) == CD_OK && host.audio.begin()->second.fps == 10);
                first.setPaused(true);
            }
        }
        check("instance destruction releases every remaining subscription", host.timers.empty() && host.audio.empty());
    }
    return passed ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
