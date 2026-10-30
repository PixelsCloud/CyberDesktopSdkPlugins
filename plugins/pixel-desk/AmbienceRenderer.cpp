#include <sdk/Controls.h>
#include <sdk/Icons.h>
#include <sdk/Images.h>
#include <sdk/services/AmbientAudio.h>

#include <set>

#include "plugins/pixel-desk/PixelAmbience.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
namespace {
constexpr std::array<BasicSpec, 5> kAmbience{{{"com.cyberdesktop.pixeldesk.plant.1x1",
                                               "plant-1x1",
                                               BasicKind::Digital,
                                               1,
                                               1,
                                               {305, 286},
                                               {-35, -245, 375, 535},
                                               {},
                                               0},
                                              {"com.cyberdesktop.pixeldesk.plant.2x2",
                                               "plant-2x2",
                                               BasicKind::Digital,
                                               2,
                                               2,
                                               {462, 597},
                                               {-35, -50, 535, 700},
                                               {},
                                               0},
                                              {"com.cyberdesktop.pixeldesk.plant.4x2",
                                               "plant-4x2",
                                               BasicKind::Digital,
                                               4,
                                               2,
                                               {1197, 333},
                                               {-25, -270, 1247, 630},
                                               {},
                                               0},
                                              {"com.cyberdesktop.pixeldesk.noise.2x1",
                                               "noise-2x1",
                                               BasicKind::Digital,
                                               2,
                                               1,
                                               {723, 452},
                                               {2, 2, 719, 448},
                                               {},
                                               0},
                                              {"com.cyberdesktop.pixeldesk.noise.4x2",
                                               "noise-4x2",
                                               BasicKind::Digital,
                                               4,
                                               2,
                                               {1088, 704},
                                               {2, 2, 1084, 700},
                                               {},
                                               0}}};
}

const BasicSpec& ambiencePlate(std::string_view id) {
    for (const auto& value : kAmbience) {
        if (value.id == id) {
            return value;
        }
    }
    throw std::invalid_argument("Unknown pixel ambience identity");
}

const BasicSpec& ambienceFolder(std::string_view folder) {
    for (const auto& value : kAmbience) {
        if (value.folder == folder) {
            return value;
        }
    }
    throw std::invalid_argument("Unknown pixel ambience folder");
}

atmosphere::Spec ambienceSpec(const BasicSpec& plate) {
    return {plate.id,
            std::string_view(plate.folder).starts_with("noise-") ? atmosphere::Kind::Noise : atmosphere::Kind::Plant,
            int(plate.rows), int(plate.columns)};
}
}  // namespace pixeldesk

namespace pixeldesk {
using atmosphere::Kind;
using atmosphere::Model;
using atmosphere::Spec;
using atmosphere::wallSeconds;

namespace {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;

struct Foliage {
    cybercanvas::Size size;
    Point root;
};

constexpr std::array<Foliage, 9> kFoliage{{{{184.297872, 142}, {96.680851, 130.670213}},
                                           {{308.75, 260}, {149.117647, 235.147059}},
                                           {{346.341463, 480}, {175.609756, 273.170732}},
                                           {{121.676471, 105}, {64.235294, 100.058824}},
                                           {{196.460177, 150}, {97.566372, 144.026549}},
                                           {{269.961977, 200}, {131.558935, 193.155894}},
                                           {{140.628571, 138}, {82.142857, 136.028571}},
                                           {{192.96, 201}, {102.108, 196.176}},
                                           {{301.538462, 280}, {146.627219, 273.372781}}}};

void label(Canvas& canvas, std::wstring text, Rect area, double size, uint32_t color) {
    cybercanvas::TextStyle style;
    style.size = size;
    style.family = L"Microsoft YaHei";
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    style.ellipsis = true;
    canvas.text(text, style, area, color);
}
}  // namespace

struct AmbienceRenderer::Impl {
    CdCreateContext context{};
    std::string directory;
    std::unique_ptr<pixeldesk::BasicFace> face;
    cdsdk::Images foliage;
    cdsdk::Icons icons;
    cdsdk::Controls controls;
    cdsdk::Scene scene;
    std::function<void()> invalidate;
    std::function<void(cybercanvas::SceneEvent)> action;
    std::set<unsigned> sampled;
    bool consumed = false;
    double pixelDpi = 1;

#include "plugins/pixel-desk/NoiseViews.inl"

    void ensure(Spec spec) {
        if (!face) {
            face = std::make_unique<pixeldesk::BasicFace>(context, pixeldesk::ambiencePlate(spec.id));
            face->onChange([this] {
                if (invalidate) {
                    invalidate();
                }
            });
        }
    }

    void leaf(Canvas& canvas, unsigned species, double growth, Point root, double scale, double rotation,
              double dpiScale) {
        // The three hand-painted stages share source pixel units and explicit root
        // registration. Young foliage retains its real size; the pot never scales.
        const unsigned stage = growth < .38 ? 0 : growth < .72 ? 1 : 2;
        const unsigned slot = species * 3 + stage;
        sampled.insert(slot);
        const auto& info = kFoliage[slot];
        pixeldesk::requestPixelImage(
            foliage, std::to_string(slot),
            std::filesystem::u8path(context.plugin_directory) / "assets" / ("foliage-" + std::to_string(slot) + ".png"),
            1,
            uint32_t(std::clamp(std::ceil(std::max(info.size.width, info.size.height) * scale * dpiScale / 32) * 32,
                                32., 4096.)),
            pixelDpi);
        if (const auto image = foliage.get(std::to_string(slot)).image) {
            canvas.saved([&] {
                canvas.translate(root.x, root.y);
                canvas.rotate(rotation);
                canvas.scale(scale, scale);
                canvas.image(*image, {-info.root.x, -info.root.y, info.size.width, info.size.height},
                             {.fit = cybercanvas::ImageFit::Stretch,
                              .clip = false,
                              .sampling = cybercanvas::ImageSampling::Nearest});
            });
        }
    }

    void plants(Canvas& canvas, Spec spec, const cdsdk::Settings& settings, const Model& model, double dpiScale) {
        const double growth = model.growth(wallSeconds(), settings.at("mode").text == "grow");
        const unsigned selected = settings.at("species").text == "pothos"      ? 0
                                  : settings.at("species").text == "succulent" ? 1
                                                                               : 2;
        const double wind = model.sway() * 1.3 + model.reaction() * 2.1;
        sampled.clear();
        if (spec.columns >= 4) {
            const std::array<Point, 3> roots{{{400, 55}, {640, 73}, {890, 42}}};
            for (unsigned i = 0; i < 3; ++i) {
                leaf(canvas, settings.at("garden").integer ? i : selected, growth, roots[i], i == 2 ? .9 : .85,
                     wind * (i == 1 ? .55 : 1), dpiScale);
            }
            cybercanvas::TextStyle plaque;
            plaque.family = L"Segoe UI";
            plaque.size = 34;
            plaque.bold = true;
            plaque.align = cybercanvas::TextAlign::Center;
            plaque.vertical = cybercanvas::VerticalAlign::Center;
            // Register to the visible cream inset, excluding the right bevel and
            // lower frame shadow; the previous text box sat right and below it.
            const auto fitted = canvas.fitTextInk(L"Garden", plaque, {930, 201, 190, 52});
            canvas.brush->SetColor(cybercanvas::color(pixeldesk::kInk));
            canvas.brush->SetOpacity(1);
            canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)),
                                          fitted.layout.Get(), canvas.brush.Get());
        } else {
            leaf(canvas, selected, growth, spec.rows == 1 ? Point{151, 61} : Point{160, 300},
                 spec.rows == 1 ? (selected == 0 ? .65 : .90) : 1., wind, dpiScale);
            if (spec.rows > 1 && settings.at("show_info").integer) {
                label(canvas, std::to_wstring(int(growth * 100)) + L"%", {82, 422, 155, 45}, 30, pixeldesk::kInk);
            }
        }
        for (unsigned i = 0; i < 9; ++i) {
            if (!sampled.contains(i)) {
                foliage.erase(std::to_string(i));
            }
        }
        foliage.poll();
        if (model.waterFeedback && model.waterOpacity() > .01) {
            const Point p = spec.columns >= 4 ? Point{1090, 175} : spec.rows == 1 ? Point{150, 155} : Point{355, 455};
            const double a = model.waterOpacity();
            const uint32_t color = 0x6fadc900 | uint32_t(a * 255);
            for (unsigned i = 0; i < 3; ++i) {
                canvas.box(p.x - 24 + i * 24, p.y + (1 - a) * 30 + (i % 2) * 12, 8, 17, 0, color);
            }
        }
    }
};

AmbienceRenderer::AmbienceRenderer(std::wstring) : impl_(std::make_unique<Impl>()) {}

AmbienceRenderer::~AmbienceRenderer() = default;

void AmbienceRenderer::bindHost(const CdCreateContext& context) {
    impl_->context = context;
    impl_->directory = context.plugin_directory ? context.plugin_directory : "";
    impl_->context.plugin_directory = impl_->directory.c_str();
    impl_->foliage.bind(context);
    impl_->icons.bind(context);
    impl_->scene.bind(context);
}

void AmbienceRenderer::bindPlant(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->controls.bind(context, std::move(action));
}

void AmbienceRenderer::onInvalidate(std::function<void()> callback) {
    impl_->invalidate = std::move(callback);
    impl_->foliage.onChange([this] {
        if (impl_->invalidate) {
            impl_->invalidate();
        }
    });
}

void AmbienceRenderer::onAction(std::function<void(cybercanvas::SceneEvent)> callback) {
    impl_->action = std::move(callback);
    impl_->scene.onAction(impl_->action);
}

void AmbienceRenderer::onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) {
    impl_->scene.diagnostics().onSample([callback = std::move(callback)] {
        cdsdk::InstanceStats stats;
        callback(stats);
        return stats;
    });
}

cdsdk::Scene& AmbienceRenderer::noiseScene(Spec spec, const cdsdk::Settings& settings, const Model& model, double width,
                                           double height, double dpi) {
    return impl_->prepareNoise(spec, settings, model, width, height, dpi);
}

bool AmbienceRenderer::plantInput(Spec spec, const cdsdk::Settings&, CdRect safe, const CdInput& event) {
    const auto& plate = pixeldesk::ambiencePlate(spec.id);
    const auto bounds = pixeldesk::fitBody(safe.width, safe.height, plate.visible.width, plate.visible.height);
    const double scale = bounds.width / plate.visible.width;
    const Rect pot = spec.columns >= 4 ? Rect{300, 0, 850, 240}
                     : spec.rows == 1  ? Rect{30, 5, 250, 260}
                                       : Rect{25, 265, 400, 330};
    const Rect target{bounds.x + (pot.x - plate.visible.x) * scale, bounds.y + (pot.y - plate.visible.y) * scale,
                      pot.width * scale, pot.height * scale};
    impl_->controls.items({{1, target, "Water plant"}}, {safe.x, safe.y});
    impl_->controls.input(event);
    // All plant input is routed here, including transparent margins, so the
    // production controller's rectangular legacy fallback cannot water empty air.
    return true;
}

void AmbienceRenderer::cancelInput() {
    impl_->controls.cancel();
    impl_->scene.cancel();
}

bool AmbienceRenderer::advanceFeedback(double seconds) {
    return impl_->scene.advance(seconds);
}

bool AmbienceRenderer::feedbackActive() const {
    return impl_->scene.active();
}

bool AmbienceRenderer::imagesPending() const {
    return !impl_->face || !impl_->face->ready() || impl_->foliage.pending() || impl_->foliage.stats().failed ||
           impl_->foliage.stats().ready < impl_->sampled.size();
}

bool AmbienceRenderer::pollImages() {
    return impl_->foliage.poll();
}

void AmbienceRenderer::pauseImages(bool paused) {
    if (impl_->face) {
        impl_->face->pause(paused);
    }
    impl_->foliage.pause(paused);
}

bool AmbienceRenderer::render(Spec spec, const cdsdk::Settings& settings, const Model& model, const CdFrame& frame,
                              CdRect safe) try {
    impl_->pixelDpi = frame.device_pixel_ratio;
    impl_->ensure(spec);
    if (spec.kind == Kind::Noise) {
        noiseScene(spec, settings, model, safe.width, safe.height, frame.device_pixel_ratio);
        impl_->scene.keyboard().origin({safe.x, safe.y});
        return impl_->face->renderContent(
            frame, safe,
            [&](Canvas& canvas) {
                impl_->noiseMotion(canvas, spec, settings, model);
            },
            [&](Canvas& canvas) {
                impl_->activeNoiseCanvas = &canvas;
                impl_->scene.arrange(canvas, {0, 0, safe.width, safe.height}, frame.device_pixel_ratio);
                impl_->scene.draw(canvas, impl_->icons.painter());
                impl_->noiseThumbs(canvas, spec, safe.width, safe.height);
            });
    }
    const auto& plate = pixeldesk::ambiencePlate(spec.id);
    const auto bounds = pixeldesk::fitBody(safe.width, safe.height, plate.visible.width, plate.visible.height);
    return impl_->face->renderContent(frame, safe, [&](Canvas& canvas) {
        impl_->plants(canvas, spec, settings, model, bounds.width / plate.visible.width * frame.device_pixel_ratio);
    });
} catch (...) {
    return false;
}

bool AmbienceRenderer::save(const std::wstring& filename, const CdFrame& frame) {
    return impl_->face && impl_->face->save(filename, frame);
}

namespace {
constexpr atmosphere::themed::Defaults presentation{
    .plantBackground = false, .plantInfo = false, .compactSucculent = true, .indicateMissingAudio = true};

std::unique_ptr<atmosphere::themed::View> makeAmbienceView(const CdCreateContext&) {
    return std::make_unique<AmbienceRenderer>();
}
}  // namespace

cybercanvas::SceneEvent AmbienceRenderer::noiseInput(Spec spec, const cdsdk::Settings& settings, const Model& model,
                                                     CdRect safe, const CdInput& input, double dpr, bool& needsPaint) {
    auto& scene = noiseScene(spec, settings, model, safe.width, safe.height, dpr);
    auto result = scene.input(input, {safe.x, safe.y});
    needsPaint = scene.needsPaint();
    return result;
}

cdsdk::Settings ambienceDefaults(Spec spec) {
    return atmosphere::themed::defaults(spec, presentation);
}

CdResult createAmbience(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return atmosphere::themed::create(spec, context, out, size, makeAmbienceView, presentation);
}
}  // namespace pixeldesk
