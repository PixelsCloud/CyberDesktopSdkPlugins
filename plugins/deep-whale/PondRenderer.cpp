#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

#include "plugins/deep-whale/Pond.h"
#include "plugins/koi-suite/FishArtwork.h"

namespace deep_pond {
namespace {
// Four simulation units per artwork pixel retain the mechanical fish's natural
// scale for fins, shadows and markings, with no image stretching.
constexpr std::array<PondSpec, 3> kPonds{{{{"com.cyberdesktop.koi.2x4", 4, 2}, {0, 0, 1080, 448}, 5},
                                          {{"com.cyberdesktop.koi.4x4", 4, 4}, {0, 0, 720, 912}, 8},
                                          {{"com.cyberdesktop.koi.3x6", 6, 3}, {0, 0, 1120, 448}, 9}}};
}  // namespace

const PondSpec& pondSpec(std::string_view id) {
    for (const auto& spec : kPonds) {
        if (id == spec.plate.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Deep Whale pond");
}

cdsdk::Settings pondDefaults(const PondSpec& spec) {
    auto result =
        koiplugin::defaults({spec.plate.id, int(spec.plate.rows), int(spec.plate.columns), int(spec.fishCount)});
    const CdSettingValue initial[]{{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "blessings", 0},
                                   {sizeof(CdSettingValue), CD_SETTING_CHOICE, "lighting", 0, 0, "day"},
                                   {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", 0xec794cff}};
    if (result.apply(initial, 3) != CD_OK) {
        throw std::logic_error("Invalid Deep Whale pond defaults");
    }
    return result;
}

struct PondFace::Impl {
    whaleskin::Skin skin;
    const PondSpec& spec;
    nlohmann::json mask;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> water;

    explicit Impl(const CdCreateContext& context, const PondSpec& value) : spec(value) {
        skin.bind(context);
        std::ifstream file(std::filesystem::u8path(context.plugin_directory) / "assets/skin.json");
        mask = nlohmann::json::parse(file).at("waterMask");
        if (mask.at("outline").size() < 3) {
            throw std::invalid_argument("Pond water outline has fewer than three vertices");
        }
    }

    void prepare(cybercanvas::Canvas& canvas) {
        if (water && factory == canvas.factory) {
            return;
        }
        auto path = canvas.path();
        bool first = true;
        for (const auto& point : mask.at("outline")) {
            const cybercanvas::Point p{point[0].get<double>(), point[1].get<double>()};
            if (first) {
                path.moveTo(p);
                first = false;
            } else {
                path.lineTo(p);
            }
        }
        path.close();
        auto holes = canvas.path();
        for (const auto& hole : mask.at("holes")) {
            for (unsigned i = 0; i < 48; ++i) {
                const double angle = i * 2 * std::numbers::pi / 48;
                const cybercanvas::Point p{hole[0].get<double>() + std::cos(angle) * hole[2].get<double>(),
                                           hole[1].get<double>() + std::sin(angle) * hole[3].get<double>()};
                if (i == 0) {
                    holes.moveTo(p);
                } else {
                    holes.lineTo(p);
                }
            }
            holes.close();
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        cybercanvas::require(canvas.factory->CreatePathGeometry(water.ReleaseAndGetAddressOf()));
        cybercanvas::require(water->Open(sink.GetAddressOf()));
        cybercanvas::require(path.geometry(canvas.factory.Get())
                                 ->CombineWithGeometry(holes.geometry(canvas.factory.Get()), D2D1_COMBINE_MODE_EXCLUDE,
                                                       nullptr, .25f, sink.Get()));
        cybercanvas::require(sink->Close());
        factory = canvas.factory;
    }

    bool contains(cybercanvas::Point point) const {
        if (!water) {
            return false;
        }
        BOOL inside = FALSE;
        cybercanvas::require(water->FillContainsPoint(D2D1::Point2F(float(point.x), float(point.y)), nullptr, &inside));
        return inside != FALSE;
    }
};

PondFace::PondFace(const CdCreateContext& context, const PondSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

PondFace::~PondFace() = default;

void PondFace::onChange(std::function<void()> callback) {
    impl_->skin.onChange(std::move(callback));
}

void PondFace::pause(bool paused) {
    impl_->skin.pause(paused);
}

bool PondFace::ready() const {
    return impl_->skin.ready() && impl_->water.Get();
}

uint64_t PondFace::cacheBuilds() const {
    return impl_->skin.cacheBuilds();
}

bool PondFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->skin.save(path, frame);
}

std::optional<koiplugin::Vec> PondFace::hit(CdRect safe, double x, double y) const {
    auto& skin = impl_->skin;
    const auto care = skin.displayRegion("care", safe);
    if (x - safe.x >= care.x && y - safe.y >= care.y && x - safe.x < care.x + care.width &&
        y - safe.y < care.y + care.height) {
        return koiplugin::Vec{impl_->spec.water.width / 2, impl_->spec.water.height / 2};
    }
    const auto shown = skin.displayRegion("water", safe), native = skin.region("water");
    if (shown.width <= 0 || shown.height <= 0) {
        return {};
    }
    const double u = (x - safe.x - shown.x) / shown.width, v = (y - safe.y - shown.y) / shown.height;
    if (!impl_->contains({native.x + u * native.width, native.y + v * native.height})) {
        return {};
    }
    return koiplugin::Vec{u * impl_->spec.water.width, v * impl_->spec.water.height};
}

bool PondFace::render(const koiplugin::Scene& scene, const koiplugin::Appearance& appearance, const CdFrame& frame,
                      CdRect safe) {
    auto& skin = impl_->skin;
    return skin.render(frame, safe, [&](auto& canvas) {
        impl_->prepare(canvas);
        skin.button(canvas, skin.region("care"), "droplet");
        const auto r = skin.region("water");
        canvas.masked(impl_->water.Get(), [&] {
            if (appearance.night > 0) {
                canvas.box(r.x, r.y, r.width, r.height, 0, 0x09244400 | uint32_t(appearance.night * 125));
            }
            canvas.saved([&] {
                canvas.translate(r.x, r.y);
                canvas.scale(r.width / scene.width(), r.height / scene.height());
                if (appearance.shimmer > 0) {
                    for (unsigned i = 0; i < 24; ++i) {
                        const double phase = appearance.motionTime * .7 + i * .91;
                        const double opacity = std::pow(std::max(0., std::sin(phase)), 3) * appearance.shimmer * .28;
                        const double x = std::fmod(i * 173. + 79, scene.width()),
                                     y = std::fmod(i * 97. + 83, scene.height());
                        canvas.line(x, y, x + 16, y, 0xe0faff00 | uint32_t(opacity * 255), 3);
                    }
                }
                for (const auto& fish : scene.fish()) {
                    koiplugin::drawFish(canvas, fish, appearance.night, appearance.koiColor);
                }
                for (const auto& ripple : scene.ripples()) {
                    const double radius = 12 + ripple.age * 58;
                    canvas.oval(ripple.center.x, ripple.center.y, radius, radius * .55,
                                0xe0faff00 | uint32_t(std::clamp(1 - ripple.age / 2.4, 0., 1.) * 130), 3);
                }
                const auto& blessing = scene.blessing();
                if (appearance.blessings && blessing.word >= 0 && blessing.opacity() > 0) {
                    const auto word = koiplugin::kBlessingWords[size_t(blessing.word)];
                    const double size = koiplugin::blessingFontSize(float(r.width), float(r.height), word.size()) *
                                        scene.width() / r.width;
                    const double width = size * word.size() * 1.3, height = size * 1.5;
                    skin.label(canvas, std::wstring(word),
                               {blessing.center.x - width / 2, blessing.center.y - height / 2 - blessing.age * 4, width,
                                height},
                               size, 0xffdf9d00 | uint32_t(blessing.opacity() * 255), false);
                }
            });
        });
    });
}
}  // namespace deep_pond
