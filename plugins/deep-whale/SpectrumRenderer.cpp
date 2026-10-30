#include <cmath>
#include <numbers>

#include "plugins/deep-whale/Spectrum.h"
#include "plugins/deep-whale/SpectrumSprites.h"

namespace deep_spectrum {
const MatrixSpec& matrixSpec(std::string_view id) {
    for (const auto& spec : kMatrixCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown whale spectrum");
}

cdsdk::Settings matrixDefaults(const MatrixSpec& spec) {
    return {{{"gain", CD_SETTING_NUMBER, 0, 3.5, "", .2, 5},
             {"release", CD_SETTING_INTEGER, 160, 0, "", 40, 600},
             {"peaks", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"color", CD_SETTING_COLOR, 0x76aecfff},
             {"color_middle", CD_SETTING_COLOR, 0xd4edf6ff},
             {"color_top", CD_SETTING_COLOR, 0xd5b889ff},
             {"peak_color", CD_SETTING_COLOR, 0xd5b889ff},
             {"brightness", CD_SETTING_NUMBER, 0, 100, "", 30, 100},
             {"glow", CD_SETTING_NUMBER, 0, 0, "", 0, 40},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"background", CD_SETTING_COLOR, 0x092a73ff},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
             {"font_family", CD_SETTING_FONT, 0, 0, "Bree Serif"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

struct MatrixRenderer::Impl {
    const MatrixSpec& spec;
    std::unique_ptr<whaleskin::Skin> skin;
    std::unique_ptr<deep_matrix::Renderer> dots;
    SpectrumSprites sprites;

    Impl(const CdCreateContext& context, const MatrixSpec& s) : spec(s) {
        if (s.form == MatrixForm::Dots) {
            dots = std::make_unique<deep_matrix::Renderer>(context);
        } else {
            skin = std::make_unique<whaleskin::Skin>();
            skin->bind(context);
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
                            const CdFrame& frame, CdRect safe) {
    if (impl_->dots) {
        return impl_->dots->render(
            impl_->spec.round ? mechanicalpixels::Kind::MusicRound : mechanicalpixels::Kind::MusicSquare, settings, "",
            false, spectrum, frame, safe);
    }
    auto& skin = *impl_->skin;
    skin.appearance(settings);
    const double brightness = settings.at("brightness").number / 100;
    const double glow = settings.at("glow").number / 100;
    const auto tint = [&](const char* name) {
        const uint32_t c = uint32_t(settings.at(name).integer);
        return (uint32_t(((c >> 24) & 255) * brightness) << 24) | (uint32_t(((c >> 16) & 255) * brightness) << 16) |
               (uint32_t(((c >> 8) & 255) * brightness) << 8) | (c & 255);
    };
    const auto edge = tint("color"), middle = tint("color_middle"), top = tint("color_top");
    return skin.render(frame, safe, [&](auto& c) {
        const auto r = skin.region("plot");
        if (settings.at("show_background").integer) {
            const auto background = uint32_t(settings.at("background").integer);
            if (impl_->spec.form == MatrixForm::Radial) {
                const double radius = std::min(r.width, r.height) / 2;
                c.oval(r.x + r.width / 2, r.y + r.height / 2, radius, radius, background);
            } else {
                c.gradientBox(r, std::min(r.height / 2, double(settings.at("background_corner_radius").integer)),
                              background, background);
            }
        }
        const bool radial = impl_->spec.form == MatrixForm::Radial;
        const unsigned columns = std::max(24u, unsigned(std::floor(r.width / 4.8)));
        const double rodWidth = radial ? std::min(r.width, r.height) * .014 : r.width / columns * .58;
        impl_->sprites.begin(c, rodWidth, edge, middle, top, glow, radial);
        const auto rod = [&](cybercanvas::Rect b) {
            impl_->sprites.rod(c, b);
        };
        const auto gem = [&](double x, double y, double) {
            impl_->sprites.gem(c, x, y);
        };
        c.clipped(r, [&] {
            if (impl_->spec.form == MatrixForm::Radial) {
                const double cx = r.x + r.width / 2, cy = r.y + r.height / 2,
                             radius = std::min(r.width, r.height) * .23;
                c.oval(cx, cy, radius - 3, radius - 3, edge, 1.2);
                for (unsigned i = 0; i < 48; ++i) {
                    const double v = spectrum.sample(double(i) * 62 / 47),
                                 peak = spectrum.sample(double(i) * 62 / 47, true);
                    c.saved([&] {
                        c.translate(cx, cy);
                        c.rotate(i * 360. / 48);
                        const double width = std::min(r.width, r.height) * .014;
                        const double end = radius + radius * (.08 + .60 * v);
                        rod({-width / 2, -end, width, end - radius});
                        if (settings.at("peaks").integer) {
                            gem(0, -radius - radius * (.08 + .60 * peak), width * .8);
                        }
                    });
                }
            } else {
                const unsigned count = std::max(24u, unsigned(std::floor(r.width / 4.8)));
                for (unsigned i = 0; i < count; ++i) {
                    const double v = spectrum.sample(double(i) * 62 / (count - 1)),
                                 peak = spectrum.sample(double(i) * 62 / (count - 1), true);
                    const double step = r.width / count, width = step * .58, base = r.y + r.height - 1,
                                 x = r.x + (i + .5) * step;
                    const double h = std::max(1., v * (r.height - 2));
                    rod({x - width / 2, base - h, width, h});
                    if (settings.at("peaks").integer) {
                        gem(x, base - peak * (r.height - 2), width * .6);
                    }
                }
            }
            impl_->sprites.flush(c);
        });
    });
}
}  // namespace deep_spectrum
