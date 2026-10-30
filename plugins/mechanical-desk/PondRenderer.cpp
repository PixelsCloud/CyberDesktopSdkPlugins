#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "plugins/koi-suite/FishArtwork.h"
#include "plugins/mechanical-desk/Pond.h"
#include "plugins/mechanical-desk/Skin.h"

namespace mechanicaldesk {
namespace {
const std::array<PondSpec, 3> kPonds{
    {{{"com.cyberdesktop.mechanicaldesk.koi.4x2",
       "koi-4x2",
       BasicKind::Digital,
       4,
       2,
       {1536, 1024},
       {536, 475, 687, 549}},
      {595, 550, 478, 316},
      3},
     {{"com.cyberdesktop.mechanicaldesk.koi.4x4", "koi-4x4", BasicKind::Digital, 4, 4, {1536, 1024}, {0, 0, 440, 555}},
      {60, 90, 341, 406},
      5},
     {{"com.cyberdesktop.mechanicaldesk.koi.6x3",
       "koi-6x3",
       BasicKind::Digital,
       6,
       3,
       {1536, 1024},
       {445, 0, 798, 570}},
      {488, 242, 717, 244},
      5}}};

template <class Points>
cybercanvas::Path polygon(Canvas& canvas, const Points& points) {
    auto path = canvas.path();
    if (!points.empty()) {
        path.moveTo({points[0].x, points[0].y});
        for (size_t i = 1; i < points.size(); ++i) {
            path.lineTo({points[i].x, points[i].y});
        }
        path.close();
    }
    return path;
}

void fish(Canvas& canvas, const koiplugin::Fish& item, uint32_t accent) {
    koiplugin::drawPlanFish(canvas, item, accent);
}

}  // namespace

const PondSpec& pondSpec(std::string_view id) {
    for (const auto& spec : kPonds) {
        if (id == spec.plate.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown mechanical pond");
}

const PondSpec& pondFolder(std::string_view folder) {
    for (const auto& spec : kPonds) {
        if (folder == spec.plate.folder) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown mechanical pond layout");
}

cdsdk::Settings pondDefaults(const PondSpec& spec) {
    auto settings =
        koiplugin::defaults({spec.plate.id, int(spec.plate.rows), int(spec.plate.columns), int(spec.fishCount)});
    const CdSettingValue clean{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "blessings", 0};
    settings.apply(&clean, 1);
    return settings;
}

struct PondFace::Impl {
    const PondSpec& spec;
    Skin skin;
    std::vector<Point> outline;
    PondHabitat habitat;

    explicit Impl(const CdCreateContext& context, const PondSpec& value)
        : spec(value), habitat(value.plate.folder, value.water.width, value.water.height) {
        skin.bind(context);
        const auto water = skin.region("water");
        outline = habitat.outline();
        for (auto& point : outline) {
            point.x += water.x;
            point.y += water.y;
        }
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
    return impl_->skin.ready();
}

uint64_t PondFace::cacheBuilds() const {
    return impl_->skin.revision();
}

bool PondFace::contains(koiplugin::Vec point, double clearance) const {
    return impl_->habitat.contains(point, clearance);
}

std::optional<koiplugin::Vec> PondFace::hit(CdRect safe, double x, double y) const {
    const auto view = impl_->skin.view(safe);
    const Point point{(x - view.origin.x) / view.scale, (y - view.origin.y) / view.scale};
    const auto water = impl_->skin.region("water");
    const koiplugin::Vec local{point.x - water.x, point.y - water.y};
    if (!contains(local)) {
        return {};
    }
    return local;
}

bool PondFace::render(const koiplugin::Scene& scene, const koiplugin::Appearance& appearance, const CdFrame& frame,
                      CdRect safe) {
    return impl_->skin.render(frame, safe, [&](Canvas& canvas) {
        const auto water = impl_->skin.region("water");
        auto mask = polygon(canvas, impl_->outline);
        canvas.masked(mask, [&] {
            canvas.box(water.x, water.y, water.width, water.height, 0, 0x12464bff);
            impl_->skin.part(canvas, "water");
            canvas.saved([&] {
                canvas.translate(water.x, water.y);
                for (const auto& item : scene.fish()) {
                    fish(canvas, item, appearance.koiColor);
                }
                for (const auto& ripple : scene.ripples()) {
                    const double radius = (14 + ripple.age * 40), opacity = std::clamp(1 - ripple.age / 2, 0., 1.);
                    canvas.oval(ripple.center.x, ripple.center.y, radius, radius, 0xffffff00 | uint32_t(opacity * 120),
                                1.2);
                }
                const auto& blessing = scene.blessing();
                if (appearance.blessings && blessing.word >= 0 && blessing.opacity() > 0) {
                    const auto text = std::wstring(koiplugin::kBlessingWords[size_t(blessing.word)]);
                    impl_->skin.label(canvas, text,
                                      {water.width * .2, water.height * .3, water.width * .6, water.height * .35},
                                      water.height * .12, 0xffefc700 | uint32_t(blessing.opacity() * 255), true,
                                      L"Microsoft YaHei UI");
                }
            });
            for (unsigned i = 0; i < 3; ++i) {
                const auto area = impl_->skin.region("lily" + std::to_string(i));
                if (area.width <= 0) {
                    continue;
                }
                auto leaf = canvas.path();
                const double cy = area.y + area.height / 2;
                leaf.moveTo({area.x + area.width, cy})
                    .arcTo({area.x, cy}, {area.width / 2, area.height / 2})
                    .arcTo({area.x + area.width, cy}, {area.width / 2, area.height / 2})
                    .close();
                canvas.masked(leaf, [&] {
                    impl_->skin.part(canvas, "lily" + std::to_string(i));
                });
            }
        });
    });
}

bool PondFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->skin.save(path, frame);
}
}  // namespace mechanicaldesk
