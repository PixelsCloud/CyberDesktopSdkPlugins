#include <filesystem>
#include <memory>
#include <string>

#include "plugins/ambience-suite/Atmosphere.h"

namespace atmosphere {
namespace {
constexpr atmosphere::themed::Defaults presentation{};

std::unique_ptr<atmosphere::themed::View> makeView(const CdCreateContext& context) {
    const auto directory = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(
                                                     context.plugin_directory ? context.plugin_directory : "")))
                               .wstring();
    return std::make_unique<Renderer>(directory);
}
}  // namespace

cybercanvas::SceneEvent Renderer::noiseInput(Spec spec, const cdsdk::Settings& settings, const Model& model,
                                             CdRect safe, const CdInput& input, double dpr, bool& needsPaint) {
    auto& scene = noiseScene(spec, settings, model, safe.width, safe.height, dpr);
    auto event = scene.input(input, {safe.x, safe.y});
    needsPaint = scene.needsPaint();
    return event;
}

cdsdk::Settings defaults(Spec spec) {
    return atmosphere::themed::defaults(spec, presentation);
}

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return atmosphere::themed::create(spec, context, out, size, makeView, presentation);
}
}  // namespace atmosphere
