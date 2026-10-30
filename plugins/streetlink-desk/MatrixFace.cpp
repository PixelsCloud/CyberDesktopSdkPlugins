#include "plugins/streetlink-desk/MatrixFace.h"

#include <CyberCanvas/Sprites.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/streetlink-desk/Scene.h"

namespace streetlink {
struct MatrixFace::Impl {
    Scene scene;
    cdsdk::Environment environment;
    cybercanvas::PrimitiveSprites lights;

    explicit Impl(const CdCreateContext& context) : scene(context) {
        environment.bind(context);
    }
};

MatrixFace::MatrixFace(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

MatrixFace::~MatrixFace() = default;

void MatrixFace::onChange(std::function<void()> callback) {
    impl_->scene.onChange(std::move(callback));
}

void MatrixFace::pause(bool paused) {
    impl_->scene.pause(paused);
}

bool MatrixFace::ready() const {
    return impl_->scene.ready();
}

uint64_t MatrixFace::cacheBuilds() const {
    return impl_->scene.cacheBuilds();
}

uint64_t MatrixFace::referencedBytes() const {
    return impl_->scene.referencedBytes();
}

bool MatrixFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->scene.save(path, frame);
}

bool MatrixFace::render(mechanicalpixels::Kind kind, const cdsdk::Settings& settings, std::string_view time,
                        bool colonVisible, const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) {
    return impl_->scene.render(frame, safe, [&](auto& c) {
        auto& scene = impl_->scene;
        const auto r = scene.region("plot");
        const double brightness = settings.at("brightness").number / 100;
        const auto tint = [&](uint32_t rgba) {
            return (rgba & 0xffffff00) | uint32_t((rgba & 255) * brightness);
        };
        if (mechanicalpixels::isClock(kind)) {
            // STREETLINK's approved time strips use solid, chamfered numerals. Keep
            // Pixels' time semantics/settings while rendering the approved face.
            auto value = std::wstring(time.begin(), time.end());
            if (!colonVisible) {
                std::replace(value.begin(), value.end(), L':', L' ');
            }
            const auto primary = tint(uint32_t(settings.at("text_color").integer));
            if (value.size() == 8) {
                const double cell = r.width / 7.2;
                for (size_t i = 0; i < value.size(); ++i) {
                    const double start = i <= 2 ? double(i) : i <= 5 ? i - .4 : i - .8;
                    const bool colon = i == 2 || i == 5;
                    scene.text(c, std::wstring(1, value[i]),
                               {r.x + start * cell, r.y, cell * (colon ? .6 : 1), r.height},
                               i >= 6 ? tint(uint32_t(settings.at("seconds_color").integer)) : primary);
                }
            } else {
                scene.text(c, value, r, primary);
            }
            auto now = impl_->environment.snapshot();
            const auto& zone = settings.at("timezone").text;
            if (zone != "local") {
                now.utc_offset_minutes = zone == "utc" ? 0 : int32_t(settings.at("utc_offset").integer);
            }
            const auto date = cdsdk::Environment::localTime(now);
            constexpr const wchar_t* weekdays[]{L"SUN", L"MON", L"TUE", L"WED", L"THU", L"FRI", L"SAT"};
            scene.text(c, weekdays[date.tm_wday], scene.region("weekday"), tint(0x00e6f2ff));
            scene.text(c, std::format(L"{:02}", date.tm_mday), scene.region("day"), primary);
            return;
        }
        const bool round = mechanicalpixels::isRound(kind);
        const unsigned rows = std::max(3u, unsigned(r.height / 3.5));
        const double pitch = r.height / rows;
        const unsigned columns = std::max(2u, unsigned(r.width / pitch));
        const double dx = r.width / columns, diameter = std::min(dx, pitch) * .70;
        impl_->lights.begin(c, diameter + 2, round ? 1 : 0);
        for (unsigned x = 0; x < columns; ++x) {
            const auto position = double(x) * (widgetplugin::Spectrum::count - 1) / (columns - 1);
            const double level = std::clamp(double(spectrum.sample(position)), 0., 1.);
            const double peak = std::clamp(double(spectrum.sample(position, true)), 0., 1.);
            for (unsigned y = 0; y < rows; ++y) {
                const bool peakDot = settings.at("peaks").integer && peak > level + .5 / rows &&
                                     y == std::min(rows - 1, unsigned(std::floor(peak * rows)));
                const bool active = y < unsigned(std::ceil(level * rows)) || peakDot;
                const auto color =
                    active ? tint(mechanicalpixels::cellColor(kind, settings, y, rows, peakDot)) : 0x12404b80;
                const cybercanvas::Point center{r.x + (x + .5) * dx, r.y + r.height - (y + .5) * pitch};
                impl_->lights.add(c, (uint64_t(color) << 1) | uint64_t(round), center, [&](auto& material) {
                    if (round) {
                        material.oval(0, 0, diameter / 2, diameter / 2, color);
                    } else {
                        material.box(-diameter / 2, -diameter / 2, diameter, diameter, 0, color);
                    }
                });
            }
        }
        impl_->lights.flush(c);
    });
}
}  // namespace streetlink
