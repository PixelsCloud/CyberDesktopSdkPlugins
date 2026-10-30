#include <cmath>
#include <format>
#include <numbers>

#include "plugins/mecha-desk/Armour.h"
#include "plugins/mecha-desk/Spectrum.h"

namespace aeris_spectrum {
const MatrixSpec& matrixSpec(std::string_view id) {
    for (const auto& spec : kMatrixCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown AERIS spectrum");
}

cdsdk::Settings matrixDefaults(const MatrixSpec& spec) {
    return {{{"gain", CD_SETTING_NUMBER, 0, 3.5, "", .2, 5},
             {"release", CD_SETTING_INTEGER, 160, 0, "", 40, 600},
             {"peaks", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"color", CD_SETTING_COLOR, 0x91d7e2ff},
             {"color_middle", CD_SETTING_COLOR, 0xe9eeebff},
             {"color_top", CD_SETTING_COLOR, 0x91d7e2ff},
             {"peak_color", CD_SETTING_COLOR, 0x91d7e2ff},
             {"brightness", CD_SETTING_NUMBER, 0, 100, "", 30, 100},
             {"glow", CD_SETTING_NUMBER, 0, 0, "", 0, 40},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"background", CD_SETTING_COLOR, 0x092a73ff},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
             {"font_family", CD_SETTING_FONT, 0, 0, "Chakra Petch"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

struct MatrixRenderer::Impl {
    std::vector<ArmourPart> armour;
    std::vector<Actuator> actuators;
    const MatrixSpec& spec;
    std::unique_ptr<aerisskin::Skin> skin;
    std::unique_ptr<aeris_matrix::Renderer> dots;

    Impl(const CdCreateContext& context, const MatrixSpec& s) : spec(s) {
        if (s.form == MatrixForm::Dots) {
            dots = std::make_unique<aeris_matrix::Renderer>(context);
        } else {
            skin = std::make_unique<aerisskin::Skin>();
            skin->bind(context);
            if (s.form == MatrixForm::Rods) {
                actuators = loadActuators(std::filesystem::u8path(context.plugin_directory));
            }
            if (s.form == MatrixForm::Armour) {
                armour = loadArmour(std::filesystem::u8path(context.plugin_directory));
            }
        }
    }
};

MatrixRenderer::MatrixRenderer(const CdCreateContext& c, const MatrixSpec& s) : impl_(std::make_unique<Impl>(c, s)) {}

MatrixRenderer::~MatrixRenderer() = default;

void MatrixRenderer::onChange(std::function<void()> callback) {
    if (impl_->dots) {
        impl_->dots->onChange(std::move(callback));
    } else {
        impl_->skin->onChange(std::move(callback));
    }
}

void MatrixRenderer::pause(bool p) {
    if (impl_->dots) {
        impl_->dots->pause(p);
    } else {
        impl_->skin->pause(p);
    }
}

bool MatrixRenderer::ready() const {
    return impl_->dots ? impl_->dots->ready() : impl_->skin->ready();
}

uint64_t MatrixRenderer::cacheBuilds() const {
    return impl_->dots ? impl_->dots->cacheBuilds() : impl_->skin->cacheBuilds();
}

bool MatrixRenderer::save(const std::filesystem::path& p, const CdFrame& f) {
    return impl_->dots ? impl_->dots->save(p, f) : impl_->skin->save(p, f);
}

bool MatrixRenderer::render(const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum,
                            const CdFrame& frame, CdRect safe, double mechanismSeconds) {
    if (impl_->dots) {
        return impl_->dots->render(
            impl_->spec.round ? mechanicalpixels::Kind::MusicRound : mechanicalpixels::Kind::MusicSquare, settings, "",
            false, spectrum, frame, safe);
    }
    auto& skin = *impl_->skin;
    skin.appearance(settings);
    const double brightness = settings.at("brightness").number / 100;
    const auto tint = [&](const char* name) {
        const uint32_t c = uint32_t(settings.at(name).integer);
        return (uint32_t(((c >> 24) & 255) * brightness) << 24) | (uint32_t(((c >> 16) & 255) * brightness) << 16) |
               (uint32_t(((c >> 8) & 255) * brightness) << 8) | (c & 255);
    };
    const auto edge = tint("color");
    return skin.render(frame, safe, [&](auto& c) {
        if (impl_->spec.form == MatrixForm::Armour) {
            drawArmour(skin, c, impl_->armour, spectrum);
            return;
        }
        const auto r = skin.region("plot");
        double level = 0;
        for (unsigned i = 0; i < 64; ++i) {
            level = std::max(level, double(spectrum.sample(i)));
        }
        const auto meter = skin.region("meter");
        if (skin.terminal() && meter.width > 0) {
            skin.metric(c, level > .001 ? std::format(L"{:.0f}", 20 * std::log10(level)) : L"--", L"dB", meter,
                        meter.height * .75, meter.height * .38, 0xf3f8faff);
        }
        if (impl_->spec.form == MatrixForm::Rods) {
            if (skin.terminal()) {
                const unsigned count = std::max(8u, unsigned(r.width / 4));
                const double pitch = r.width / count;
                for (unsigned i = 0; i < count; ++i) {
                    const double value = std::clamp(double(spectrum.sample(i * 63. / (count - 1))), 0., 1.);
                    const double height = std::max(1., r.height * value);
                    c.box(r.x + i * pitch, r.y + r.height - height, pitch * .67, height, .2, edge);
                    if (settings.at("peaks").integer) {
                        const double peak = std::clamp(double(spectrum.sample(i * 63. / (count - 1), true)), 0., 1.);
                        c.box(r.x + i * pitch, r.y + r.height * (1 - peak), pitch * .67, 1, 0, 0xf4ce38ff);
                    }
                }
                return;
            }
            drawActuators(skin, c, impl_->actuators, spectrum, settings.at("peaks").integer != 0);
            return;
        }
        if (impl_->spec.form == MatrixForm::Radial) {
            const double cx = r.x + r.width / 2, cy = r.y + r.height / 2;
            const double radius = std::min(r.width, r.height) / 2;
            constexpr unsigned count = 48, segments = 8;
            const double inner = radius * .47, length = radius * .45;
            for (unsigned i = 0; i < count; ++i) {
                const double value = std::clamp(double(spectrum.sample(i * 63. / (count - 1))), 0., 1.);
                c.saved([&] {
                    c.translate(cx, cy);
                    c.rotate(i * 360. / count);
                    auto shaft = c.path();
                    shaft.moveTo({-1.05, -inner})
                        .lineTo({-1.9, -radius + 3.6})
                        .lineTo({1.9, -radius + 3.6})
                        .lineTo({1.05, -inner})
                        .close();
                    c.fill(shaft, 0x24394cff);
                    c.stroke(shaft, 0x526b79ff, {.width = .5});
                    c.box(-1.9, -radius, 3.8, 3.6, .6, 0xe9eeebff);
                    for (unsigned segment = 0; segment < segments; ++segment) {
                        const double amount = std::clamp(value * segments - segment, 0., 1.);
                        const double y = -inner - (segment + 1) * length / segments;
                        c.box(-.55, y, 1.1, length / segments - .9, .2, 0x526b79ff);
                        if (amount > 0) {
                            c.box(-.55, y, 1.1, (length / segments - .9) * amount, .2, edge);
                        }
                    }
                });
            }
            c.saved([&] {
                c.translate(cx, cy);
                c.rotate(std::fmod(mechanismSeconds * 6, 360.));
                skin.part(c, "core", {-32, -32, 64, 64}, true);
            });
            if (skin.terminal()) {
                skin.label(c, std::format(L"{:.0f}%", std::clamp(level, 0., 1.) * 100),
                           {cx - inner * .8, cy - inner * .32, inner * 1.6, inner * .64}, inner * .5, 0xf3f8faff);
            }
            return;
        }
    });
}
}  // namespace aeris_spectrum
