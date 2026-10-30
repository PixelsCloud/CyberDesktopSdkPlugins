#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>

#include "plugins/mechanical-pixels/tools/TestHost.h"
#include "plugins/steampunk-desk/SteamDesk.h"
#include "plugins/steampunk-desk/tools/Support.h"

namespace {
using namespace steamdesk;

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void check(bool result, const char* message) {
    if (!result) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

struct Frame {
    std::vector<uint8_t> bytes;
    CdFrame value;

    Frame(unsigned width, unsigned height, double dpi)
        : bytes(size_t(width * 4 + 16) * height + 64, 0xa5),
          value{sizeof(CdFrame),   width, height, width * 4 + 16, size_t(width * 4 + 16) * height,
                bytes.data() + 32, dpi} {}

    bool guards() const {
        for (unsigned i = 0; i < 32; ++i) {
            if (bytes[i] != 0xa5 || bytes[bytes.size() - 1 - i] != 0xa5) {
                return false;
            }
        }
        for (unsigned row = 0; row < value.height; ++row) {
            for (unsigned byte = value.width * 4; byte < value.stride; ++byte) {
                if (value.rgba[size_t(row) * value.stride + byte] != 0xa5) {
                    return false;
                }
            }
        }
        return true;
    }

    bool transparentAndPremultiplied(CdRect safe) const {
        for (unsigned y = 0; y < value.height; ++y) {
            for (unsigned x = 0; x < value.width; ++x) {
                const auto* pixel = value.rgba + size_t(y) * value.stride + x * 4;
                if (pixel[0] > pixel[3] || pixel[1] > pixel[3] || pixel[2] > pixel[3]) {
                    return false;
                }
                const double px = x / value.device_pixel_ratio, py = y / value.device_pixel_ratio;
                if ((px < safe.x - 1 || px > safe.x + safe.width + 1 || py < safe.y - 1 ||
                     py > safe.y + safe.height + 1) &&
                    pixel[3]) {
                    return false;
                }
            }
        }
        return true;
    }
};

CdSettingValue toggle(const char* id, bool value) {
    return {sizeof(CdSettingValue), CD_SETTING_TOGGLE, id, value, 0, ""};
}

CdSettingValue choice(const char* id, const char* value) {
    return {sizeof(CdSettingValue), CD_SETTING_CHOICE, id, 0, 0, value};
}

void decoded(Renderer& renderer, const cdsdk::Settings& settings, const CdFrame& frame, CdRect safe) {
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        if (!renderer.render(settings, "18:42:06", true, frame, safe)) {
            throw std::runtime_error("Native render failed");
        }
        if (renderer.ready()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Background artwork decoding timed out");
}

void verifyClock(const CdCreateContext& context, mechanicalpixels::testing::Host& host) {
    const auto originalTime = host.unixMs;
    host.unixMs = (originalTime / 60000 + 1) * 60000 - 5;
    {
        ClockInstance boundary(context);
        const auto secondsOff = toggle("seconds", false);
        check(boundary.applySettings(&secondsOff, 1) == CD_OK && std::all_of(host.timers.begin(), host.timers.end(),
                                                                             [](const auto& entry) {
                                                                                 return entry.second.interval >= 16;
                                                                             }),
              "second and minute boundaries respect the real host minimum timer interval");
    }
    host.unixMs = originalTime;
    auto settings = steamdesk::defaults(specification(context.plugin_id).kind);
    cdsdk::Environment clock;
    clock.bind(context);
    check(mechanicalpixels::clockText(clock.snapshot(), settings) == "10:08:30", "host time produces live clock text");
    host.offset = 480;
    check(mechanicalpixels::clockText(clock.snapshot(), settings) == "18:08:30",
          "local time follows the host timezone");
    auto utc = choice("timezone", "utc");
    check(settings.apply(&utc, 1) == CD_OK && mechanicalpixels::clockText(clock.snapshot(), settings) == "10:08:30",
          "UTC is independent from local timezone");
    CdSettingValue offset[]{choice("timezone", "offset"),
                            {sizeof(CdSettingValue), CD_SETTING_INTEGER, "utc_offset", 345}};
    check(settings.apply(offset, 2) == CD_OK && mechanicalpixels::clockText(clock.snapshot(), settings) == "15:53:30",
          "custom timezone preserves quarter-hour offsets");
    auto twelve = choice("format", "12");
    check(settings.apply(&twelve, 1) == CD_OK && mechanicalpixels::clockText(clock.snapshot(), settings) == "03:53:30",
          "twelve-hour format uses actual time");
    host.offset = 0;
    {
        ClockInstance first(context), second(context);
        const CdSettingValue color{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0x2681e6ff};
        check(first.applySettings(&color, 1) == CD_OK && second.settings.at("text_color").integer == 0xffe5acff,
              "color settings are isolated between instances");
        auto seconds = toggle("seconds", false);
        check(first.applySettings(&seconds, 1) == CD_OK && second.settings.at("seconds").integer == 1,
              "time display settings are isolated between instances");
        first.setPaused(true);
        second.setPaused(true);
        const auto redraws = host.redraws;
        host.advance(70000);
        check(host.timers.empty() && host.audio.empty() && host.redraws == redraws,
              "paused instances own no timers or audio and request no frames");
        first.setPaused(false);
        check(host.timers.size() == 1 && host.redraws > redraws,
              "resume schedules real time and redraws only the widget");
        auto blink = toggle("blink_colon", true);
        host.allowTimers = false;
        const auto prior = first.settings.at("blink_colon").integer;
        check(first.applySettings(&blink, 1) != CD_OK && first.settings.at("blink_colon").integer == prior &&
                  host.timers.size() == 1,
              "failed cadence change preserves previous settings and timer");
        host.allowTimers = true;
        check(first.applySettings(&blink, 1) == CD_OK, "colon blinking can be enabled after cadence recovery");
        Frame a(756, 228, 2), b(756, 228, 2);
        first.render(a.value);
        host.advance(1000);
        first.render(b.value);
        check(a.bytes != b.bytes, "live timer changes the rendered clock instead of frozen preview digits");
        CdSettingValue invalid[]{color, choice("format", "invalid")};
        check(first.applySettings(invalid, 2) == CD_ERROR_ARGUMENT, "invalid settings batches fail atomically");
        check(first.render(CdFrame{}) == CD_ERROR_ARGUMENT, "invalid frame is rejected");
    }
    check(host.timers.empty() && host.audio.empty(), "instance destruction releases all subscriptions");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    check(argc == 4, "repository, output directory and single plugin folder supplied");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto folder = utf8(argv[3]);
    const auto found = std::find_if(kCatalog.begin(), kCatalog.end(), [&](const auto& spec) {
        return folder == spec.folder;
    });
    check(found != kCatalog.end(), "verification selects exactly one implemented plugin");
    const auto& spec = *found;
    const auto directory = utf8(root / "build/relwithdebinfo/plugins" / spec.id);
    steamdesk::testing::Host host;
    CdCreateContext context{
        sizeof(context), 1, spec.id, "verify", directory.c_str(), &host.api.input.media.base, &host};
    verifyClock(context, host);
    auto settings = steamdesk::defaults(spec.kind);
    Renderer renderer(context, spec);
    std::filesystem::create_directories(output);
    unsigned renders = 0;
    for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
        for (double dpi : {1., 1.5, 2.}) {
            const double width = spec.columns * cell - 6., height = spec.rows * cell * 1.25 - 6.;
            for (bool seconds : {false, true}) {
                const auto square = arrange(kCatalog[0], width, height, dpi, seconds);
                const auto round = arrange(kCatalog[1], width, height, dpi, seconds);
                check(square.artwork.x == round.artwork.x && square.artwork.y == round.artwork.y &&
                          square.artwork.width == round.artwork.width &&
                          square.artwork.height == round.artwork.height && square.pitch == round.pitch &&
                          square.diameter == round.diameter && square.grid.x == round.grid.x &&
                          square.grid.y == round.grid.y,
                      "square and round clocks share their physical case bounds, digit size and alignment");
            }
            const CdRect safe{7, 9, width, height};
            Frame frame(unsigned(std::ceil((width + 14) * dpi)), unsigned(std::ceil((height + 18) * dpi)), dpi);
            decoded(renderer, settings, frame.value, safe);
            check(frame.guards() && frame.transparentAndPremultiplied(safe),
                  "native DPI frame respects safe bounds, premultiplied alpha, row padding and guards");
            const auto name =
                folder + "-w" + std::to_string(cell) + "-dpi" + std::to_string(unsigned(dpi * 100)) + ".png";
            check(renderer.save(output / name, frame.value), "native preview saved");
            if (cell == 96 && dpi == 2) {
                std::filesystem::copy_file(output / name,
                                           root / "plugins/steampunk-desk/packages" / folder / "preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
            const auto cached = renderer.cacheBuilds();
            const auto requests = renderer.imageStats().requests;
            const auto before = frame.bytes;
            check(renderer.render(settings, "11:22:33", false, frame.value, safe) && renderer.cacheBuilds() == cached &&
                      renderer.imageStats().requests == requests && frame.bytes != before,
                  "changing live digits reuses decoded artwork and static layer");
            CdSettingValue custom{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0x2681e6ff};
            auto blue = settings;
            blue.apply(&custom, 1);
            const auto normal = frame.bytes;
            renderer.render(blue, "11:22:33", false, frame.value, safe);
            check(frame.bytes != normal && renderer.cacheBuilds() == cached,
                  "color changes affect native pixels without rebuilding the case");
            auto shortTime = settings;
            auto seconds = toggle("seconds", false);
            shortTime.apply(&seconds, 1);
            decoded(renderer, shortTime, frame.value, safe);
            const auto geometry = arrange(spec, width, height, dpi, false);
            check(geometry.columns == 25 && geometry.grid.x >= geometry.aperture.x &&
                      geometry.grid.x + geometry.columns * geometry.pitch <=
                          geometry.aperture.x + geometry.aperture.width,
                  "seconds-off matrix is centered inside the same glass opening");
            ++renders;
        }
    }
    check(renders == 15, "15 native grid-size/DPI renders pass for the selected plugin");
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
