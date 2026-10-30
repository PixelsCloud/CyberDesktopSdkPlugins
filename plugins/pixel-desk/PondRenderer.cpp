#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelPond.h"

namespace pixeldesk {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Point;
constexpr std::array<PondSpec, 3> kPonds{{{{"com.cyberdesktop.pixeldesk.koi.4x2",
                                            "koi-4x2",
                                            BasicKind::Digital,
                                            4,
                                            2,
                                            {1467, 766},
                                            {2, 2, 1463, 762},
                                            {},
                                            0},
                                           {129, 95, 1200, 590},
                                           5},
                                          {{"com.cyberdesktop.pixeldesk.koi.4x4",
                                            "koi-4x4",
                                            BasicKind::Digital,
                                            4,
                                            4,
                                            {937, 1405},
                                            {2, 2, 933, 1401},
                                            {},
                                            0},
                                           {155, 175, 695, 1150},
                                           8,
                                           {300, 262}},
                                          {{"com.cyberdesktop.pixeldesk.koi.6x3",
                                            "koi-6x3",
                                            BasicKind::Digital,
                                            6,
                                            3,
                                            {1574, 950},
                                            {2, 2, 1570, 946},
                                            {},
                                            0},
                                           {145, 125, 1360, 765},
                                           9,
                                           {283, 298}}}};
constexpr std::array<Point, 14> kBank{{{129, 354},
                                       {219, 219},
                                       {429, 124},
                                       {699, 101},
                                       {969, 123},
                                       {1099, 203},
                                       {1299, 344},
                                       {1299, 434},
                                       {1149, 546},
                                       {999, 637},
                                       {699, 674},
                                       {419, 616},
                                       {209, 533},
                                       {135, 386}}};

Point quantized(Point value, double grid = 4) {
    return {std::round(value.x / grid) * grid, std::round(value.y / grid) * grid};
}

template <class Points>
void contour(cybercanvas::Path& path, const Points& points, double grid = 4) {
    if (points.empty()) {
        return;
    }
    const auto first = quantized({points[0].x, points[0].y}, grid);
    auto previous = first;
    path.moveTo(first);
    for (size_t index = 1; index <= points.size(); ++index) {
        const auto& source = points[index % points.size()];
        const auto end = quantized({source.x, source.y}, grid);
        const auto start = previous;
        const unsigned steps =
            std::max(1u, unsigned(std::ceil(std::max(std::abs(end.x - start.x), std::abs(end.y - start.y)) / grid)));
        for (unsigned step = 1; step <= steps; ++step) {
            const auto next = quantized(
                {std::lerp(start.x, end.x, double(step) / steps), std::lerp(start.y, end.y, double(step) / steps)},
                grid);
            path.lineTo({next.x, previous.y});
            path.lineTo(next);
            previous = next;
        }
    }
    path.close();
}

template <class Points>
cybercanvas::Path polygon(Canvas& layer, const Points& points) {
    auto path = layer.path();
    contour(path, points);
    return path;
}

std::array<Point, 24> ellipse(Point center, double x, double y, double angle = 0) {
    std::array<Point, 24> points;
    const double c = std::cos(angle), s = std::sin(angle);
    for (unsigned i = 0; i < points.size(); ++i) {
        const double a = i * 2 * std::numbers::pi / points.size(), px = x * std::cos(a), py = y * std::sin(a);
        points[i] = {center.x + px * c - py * s, center.y + px * s + py * c};
    }
    return points;
}

uint32_t shade(uint32_t day, uint32_t night, double amount) {
    uint32_t result = day & 255;
    for (unsigned shift : {8u, 16u, 24u}) {
        result |= uint32_t(std::round(std::lerp(double((day >> shift) & 255), double((night >> shift) & 255), amount)))
                  << shift;
    }
    return result;
}

void fish(Canvas& layer, const koiplugin::Fish& fish, const koiplugin::Appearance& appearance) {
    const auto cream = shade(0xfff3d6ff, 0xc9ded8ff, appearance.night);
    const auto orange = shade(appearance.koiColor, 0xaa684dff, appearance.night);
    const auto fin = shade(0xf5e7cddc, 0xb9d1ccd0, appearance.night);
    const auto bodyPoints = koiplugin::bodyOutline(fish);
    auto body = polygon(layer, bodyPoints);
    layer.saved([&] {
        layer.translate(4, 6);
        layer.fill(body, 0x153c3744);
    });
    for (int joint : {3, 7}) {
        for (int side : {-1, 1}) {
            const auto pose = koiplugin::finPose(fish, joint, side);
            auto shape =
                polygon(layer, ellipse({pose.center.x, pose.center.y}, pose.radiusX, pose.radiusY, pose.angle));
            layer.fill(shape, fin);
        }
    }
    auto tail = polygon(layer, koiplugin::tailOutline(fish));
    layer.fill(tail, fin);
    layer.fill(body, cream);
    layer.masked(body, [&] {
        for (int joint : {1, 4, 7}) {
            const auto p = fish.joints[joint];
            const double variation = fish.pattern % 2 ? .25 : -.2;
            const auto center =
                p + koiplugin::direction(fish.angles[joint] + koiplugin::kPi / 2) * (fish.link * variation);
            auto marking =
                polygon(layer, ellipse({center.x, center.y}, fish.link * 1.45, fish.link * .95, fish.angles[joint]));
            layer.fill(marking, orange);
        }
        if (fish.pattern == 3) {
            const auto p = fish.joints[5];
            auto spot = polygon(layer, ellipse({p.x, p.y}, fish.link * .6, fish.link * .45, fish.angles[5]));
            layer.fill(spot, 0x405349ff);
        }
    });
    for (int side : {-1, 1}) {
        const auto eye = fish.edge(0, side * koiplugin::kPi / 2, -18);
        const auto p = quantized({eye.x, eye.y});
        layer.box(p.x - 2, p.y - 2, 4, 4, 0, 0x203b35ff);
    }
}
}  // namespace

const PondSpec& pondSpec(std::string_view id) {
    for (const auto& spec : kPonds) {
        if (id == spec.plate.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown pixel pond");
}

const PondSpec& pondFolder(std::string_view folder) {
    for (const auto& spec : kPonds) {
        if (folder == spec.plate.folder) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown pixel pond folder");
}

cdsdk::Settings pondDefaults(const PondSpec& spec) {
    auto values =
        koiplugin::defaults({spec.plate.id, int(spec.plate.rows), int(spec.plate.columns), int(spec.fishCount)});
    const CdSettingValue defaults[]{{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "blessings", 0},
                                    {sizeof(CdSettingValue), CD_SETTING_CHOICE, "lighting", 0, 0, "day"},
                                    {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", 0xec794cff}};
    if (values.apply(defaults, 3) != CD_OK) {
        throw std::logic_error("Invalid pixel pond defaults");
    }
    return values;
}

struct PondFace::Impl {
    const PondSpec& spec;
    BasicFace face;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> water;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;

    explicit Impl(const CdCreateContext& context, const PondSpec& value) : spec(value), face(context, value.plate) {}

    void prepare(Canvas& layer) {
        if (water && factory == layer.factory) {
            return;
        }
        auto outer = layer.path();
        auto holes = layer.path(cybercanvas::FillRule::Winding);
        if (spec.plate.rows == 4) {
            const std::array<Point, 30> bank{
                {{412, 183},  {517, 216},  {632, 303},  {702, 393},  {717, 608},  {807, 698},  {832, 798},  {782, 898},
                 {732, 998},  {798, 1118}, {782, 1198}, {706, 1253}, {622, 1308}, {537, 1283}, {482, 1218}, {472, 1148},
                 {392, 1093}, {317, 1058}, {247, 1033}, {227, 978},  {247, 883},  {262, 778},  {217, 668},  {182, 588},
                 {232, 513},  {237, 463},  {182, 413},  {192, 318},  {237, 258},  {302, 218}}};
            contour(outer, bank);
            for (const auto& pad : std::array<Point, 4>{{{300, 262}, {240, 316}, {282, 353}, {300, 422}}}) {
                contour(holes, ellipse(pad, 45, 38));
            }
            contour(holes, ellipse({365, 286}, 22, 20));
        } else if (spec.plate.columns == 6) {
            const std::array<Point, 22> bank{
                {{308, 248},  {438, 273},  {578, 188},  {748, 188},  {918, 238},  {1093, 123}, {1203, 178}, {1258, 328},
                 {1388, 378}, {1488, 478}, {1428, 578}, {1268, 688}, {1148, 798}, {968, 868},  {768, 778},  {638, 728},
                 {528, 648},  {378, 693},  {228, 618},  {168, 568},  {153, 438},  {218, 328}}};
            contour(outer, bank);
            for (const auto& pad : std::array<Point, 15>{{{283, 298},
                                                          {323, 324},
                                                          {273, 343},
                                                          {473, 293},
                                                          {1113, 163},
                                                          {1149, 194},
                                                          {1316, 361},
                                                          {1450, 448},
                                                          {1403, 428},
                                                          {1314, 676},
                                                          {1243, 713},
                                                          {1198, 748},
                                                          {318, 653},
                                                          {258, 613},
                                                          {162, 398}}}) {
                contour(holes, ellipse(pad, 34, 25));
            }
            contour(holes, ellipse({205, 433}, 24, 21));
        } else {
            contour(outer, kBank);
            contour(holes, ellipse({899, 192}, 66, 66));
            contour(holes, ellipse({986, 280}, 58, 58));
            contour(holes, ellipse({1179, 145}, 110, 105));
        }
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> result;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        cybercanvas::require(layer.factory->CreatePathGeometry(result.GetAddressOf()));
        cybercanvas::require(result->Open(sink.GetAddressOf()));
        cybercanvas::require(outer.geometry(layer.factory.Get())
                                 ->CombineWithGeometry(holes.geometry(layer.factory.Get()), D2D1_COMBINE_MODE_EXCLUDE,
                                                       nullptr, .25f, sink.Get()));
        cybercanvas::require(sink->Close());
        water = std::move(result);
        factory = layer.factory;
    }
};

PondFace::PondFace(const CdCreateContext& context, const PondSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

PondFace::~PondFace() {
    impl_->face.onChange({});
}

void PondFace::onChange(std::function<void()> callback) {
    impl_->face.onChange(std::move(callback));
}

void PondFace::pause(bool paused) {
    impl_->face.pause(paused);
}

bool PondFace::ready() const {
    return impl_->face.ready() && impl_->water.Get();
}

uint64_t PondFace::cacheBuilds() const {
    return impl_->face.cacheBuilds();
}

bool PondFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->face.save(path, frame);
}

std::optional<koiplugin::Vec> PondFace::hit(CdRect safe, double x, double y) const {
    if (!impl_->water || safe.width <= 12 || safe.height <= 12) {
        return {};
    }
    const auto& spec = impl_->spec;
    const auto bounds = fitBody(safe.width, safe.height, spec.plate.visible.width, spec.plate.visible.height);
    const double scale = bounds.width / spec.plate.visible.width;
    const Point local{(x - safe.x - bounds.x) / scale + spec.plate.visible.x,
                      (y - safe.y - bounds.y) / scale + spec.plate.visible.y};
    BOOL inside = FALSE;
    cybercanvas::require(
        impl_->water->FillContainsPoint(D2D1::Point2F(float(local.x), float(local.y)), nullptr, &inside));
    return inside ? std::optional<koiplugin::Vec>{{local.x - spec.water.x, local.y - spec.water.y}} : std::nullopt;
}

bool PondFace::render(const koiplugin::Scene& scene, const koiplugin::Appearance& appearance, const CdFrame& frame,
                      CdRect safe) {
    const auto& spec = impl_->spec;
    return impl_->face.renderContent(frame, safe, [&](Canvas& layer) {
        impl_->prepare(layer);
        layer.masked(impl_->water.Get(), [&] {
            // Tint only the water; the original cream bank remains a distinct frame.
            if (appearance.night > 0) {
                layer.brush->SetOpacity(1);
                layer.brush->SetColor(cybercanvas::color(0x102b4d00 | uint32_t(appearance.night * 125)));
                layer.target->FillGeometry(impl_->water.Get(), layer.brush.Get());
            }
            layer.saved([&] {
                layer.translate(spec.water.x, spec.water.y);
                if (appearance.shimmer > 0) {
                    for (unsigned i = 0; i < 24; ++i) {
                        const double phase =
                            (appearance.motionTime >= 0 ? appearance.motionTime : scene.time() * .75) * .7 + i * .91;
                        const double opacity = std::pow(std::max(0., std::sin(phase)), 3) * appearance.shimmer * .28;
                        const double x = std::fmod(i * 173. + 79, spec.water.width),
                                     y = std::fmod(i * 97. + 83, spec.water.height);
                        layer.box(x, y, 16, 4, 0, 0xe0ffe500 | uint32_t(opacity * 255));
                    }
                }
                for (const auto& showing : scene.fish()) {
                    fish(layer, showing, appearance);
                }
                for (const auto& ripple : scene.ripples()) {
                    const double radius = 12 + ripple.age * 58;
                    auto wave = polygon(layer, ellipse({ripple.center.x, ripple.center.y}, radius, radius * .55));
                    layer.stroke(wave, 0xcdf0d800 | uint32_t(std::clamp(1 - ripple.age / 2.4, 0., 1.) * 100),
                                 {.width = 4});
                }
                if (appearance.blessings && scene.blessing().word >= 0 && scene.blessing().opacity() > 0) {
                    const auto& word = koiplugin::kBlessingWords[size_t(scene.blessing().word)];
                    const double scale =
                        fitBody(safe.width, safe.height, spec.plate.visible.width, spec.plate.visible.height).width /
                        spec.plate.visible.width;
                    cybercanvas::TextStyle style;
                    style.family = L"Microsoft YaHei UI";
                    style.bold = true;
                    style.align = cybercanvas::TextAlign::Center;
                    style.vertical = cybercanvas::VerticalAlign::Center;
                    style.size =
                        koiplugin::blessingFontSize(spec.water.width * scale, spec.water.height * scale, word.size()) /
                        scale;
                    const auto center = scene.blessing().center;
                    const double width = style.size * word.size() * 1.3, height = style.size * 1.5;
                    layer.text(std::wstring(word), style, {center.x - width / 2, center.y - height / 2, width, height},
                               0xffdf9d00 | uint32_t(scene.blessing().opacity() * 255));
                }
            });
        });
    });
}
}  // namespace pixeldesk
