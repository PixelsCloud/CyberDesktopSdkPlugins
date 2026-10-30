// Controller binding reused from plugins/vampire-alice/controllers/weather/Weather.h.
#pragma once

#include <memory>

#include "plugins/weather-suite/Weather.h"

namespace whaleskin {
class Skin;
}

namespace deep_weather {
using namespace weatherplugin;

class Renderer {
public:
    explicit Renderer(std::filesystem::path assets);
    ~Renderer();
    void bind(const CdCreateContext&);
    void pause(bool paused);
    bool render(Spec, const cdsdk::Settings&, const Snapshot&, bool loading, const CdFrame&, CdRect);
    // Blocking readiness is reserved for offline design exports and tests.
    // Production instances use render() and receive local image-completion redraws.
    bool renderReference(Spec, const cdsdk::Settings&, const Snapshot&, bool loading, const CdFrame&, CdRect);
    bool savePng(const std::filesystem::path&, const CdFrame&);

private:
    std::unique_ptr<whaleskin::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace deep_weather
