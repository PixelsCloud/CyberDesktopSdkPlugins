#include <filesystem>
#include <memory>
#include <string>

#include "plugins/mecha-desk/Ambience.h"

namespace aeris_ambience {
namespace {
constexpr atmosphere::themed::Defaults presentation{.trackToggle = true};

std::unique_ptr<atmosphere::themed::View> makeView(const CdCreateContext& context) {
    const auto directory = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(
                                                     context.plugin_directory ? context.plugin_directory : "")))
                               .wstring();
    return std::make_unique<Renderer>(directory);
}
}  // namespace

cybercanvas::SceneEvent Renderer::noiseInput(Spec spec, const cdsdk::Settings& settings, const Model& model,
                                             CdRect safe, const CdInput& input, double dpr, bool& needsPaint) {
    needsPaint = false;
    return noiseInput(spec, settings, model, safe, input);
}

cdsdk::Settings defaults(Spec spec) {
    return atmosphere::themed::defaults(spec, presentation);
}

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return atmosphere::themed::create(spec, context, out, size, makeView, presentation);
}
}  // namespace aeris_ambience
