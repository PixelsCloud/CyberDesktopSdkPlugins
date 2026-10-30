#include <CyberCanvas/EffectsDrawing.h>
#include <sdk/Controls.h>
#include <sdk/Icons.h>
#include <sdk/Language.h>

#include <cstdio>
#include <exception>
#include <filesystem>

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/ambience-suite/NoiseScene.h"
#include "plugins/living-common/Drawing.h"

namespace atmosphere {
struct Renderer::Impl {
    living::Drawing draw;
    cdsdk::Icons icons;
    cdsdk::Controls plantControls;
    std::filesystem::path directory;
    NoiseScene noise;
    cybercanvas::ShadowPainter potShadows;
    std::array<std::unique_ptr<cybercanvas::Scene>, 3> plantGroups;
    std::function<void()> paintPlantGroup;

    explicit Impl(std::wstring root) : directory(std::move(root)), noise(directory) {}

#include "plugins/ambience-suite/PlantViews.inl"
};

Renderer::Renderer(std::wstring directory) : impl_(std::make_unique<Impl>(std::move(directory))) {}

Renderer::~Renderer() = default;

void Renderer::bindHost(const CdCreateContext& context) {
    impl_->icons.bind(context);
    impl_->draw.bindGpu(context);
    impl_->noise.bind(context);
}

void Renderer::onInvalidate(std::function<void()> callback) {
    impl_->noise.onInvalidate(std::move(callback));
}

void Renderer::onAction(std::function<void(cybercanvas::SceneEvent)> callback) {
    impl_->noise.onAction(std::move(callback));
}

void Renderer::onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) {
    impl_->noise.onDiagnostics(std::move(callback));
}

cdsdk::Scene& Renderer::noiseScene(Spec spec, const cdsdk::Settings& settings, const Model& model, double width,
                                   double height, double dpr) {
    return impl_->noise.prepare(impl_->draw, spec, settings, model, width, height, dpr);
}

void Renderer::cancelInput() {
    impl_->noise.cancel();
    impl_->plantControls.cancel();
}

void Renderer::bindPlant(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->plantControls.bind(context, std::move(action));
}

bool Renderer::plantInput(Spec spec, const cdsdk::Settings& settings, CdRect safe, const CdInput& event) {
    const bool info = spec.rows > 1 && settings.at("show_info").integer;
    const cybercanvas::Rect bounds{safe.width - 28, safe.height - 25, 20, 20};
    impl_->plantControls.items(
        info ? std::vector<cdsdk::Controls::Item>{{1, bounds,
                                                   settings.at("language").text == "en" ? "Water plant" : "浇水"}}
             : std::vector<cdsdk::Controls::Item>{},
        {safe.x, safe.y});
    impl_->plantControls.input(event);
    return info && event.x >= safe.x + bounds.x && event.x < safe.x + bounds.x + bounds.width &&
           event.y >= safe.y + bounds.y && event.y < safe.y + bounds.y + bounds.height;
}

bool Renderer::advanceFeedback(double seconds) {
    return impl_->noise.advance(seconds);
}

bool Renderer::feedbackActive() const {
    return impl_->noise.active();
}

bool Renderer::imagesPending() const {
    return impl_->noise.imagesPending();
}

bool Renderer::pollImages() {
    return impl_->noise.pollImages();
}

void Renderer::pauseImages(bool paused) {
    impl_->noise.pauseImages(paused);
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Model& model, const CdFrame& frame,
                      CdRect safe) {
    const cdsdk::LanguageScope languageScope(settings.at("language").text);
    try {
        auto painting = impl_->draw.frame(frame, safe);
        const auto area = painting.area();
        if (area.width > 20 && area.height > 20) {
            const auto mode = settings.at("lighting").text;
            const bool dark = mode == "night" || mode == "theme" && model.dark;
            if (settings.at("show_background").integer && spec.kind == Kind::Plant) {
                impl_->draw.box(1, 1, area.width - 3, area.height - 4, settings.at("background_corner_radius").integer,
                                dark ? 0x202D34FF : 0xFFFBF2FF);
            }
            if (spec.kind == Kind::Plant) {
                const bool info = spec.rows > 1 && settings.at("show_info").integer;
                impl_->plantControls.items(
                    info ? std::vector<cdsdk::Controls::Item>{{1,
                                                               {area.width - 28, area.height - 25, 20, 20},
                                                               settings.at("language").text == "en" ? "Water plant"
                                                                                                    : "浇水"}}
                         : std::vector<cdsdk::Controls::Item>{},
                    {safe.x, safe.y});
                impl_->plants(spec, settings, model, area.width, area.height, dark);
                impl_->plantControls.drawFocus(
                    impl_->draw, dark ? cybercanvas::WidgetTheme::dark() : cybercanvas::WidgetTheme::light());
            } else {
                auto& scene = noiseScene(spec, settings, model, area.width, area.height, frame.device_pixel_ratio);
                scene.keyboard().origin({safe.x, safe.y});
                impl_->noise.draw(impl_->draw, impl_->icons.painter());
            }
        }
        painting.finish();
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Ambience render failed: %s\n", error.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "Ambience render failed: unknown exception\n");
        return false;
    }
}

bool Renderer::save(const std::wstring& filename, const CdFrame& frame) {
    return impl_->draw.save(filename, frame);
}
}  // namespace atmosphere
