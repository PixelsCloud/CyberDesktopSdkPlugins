#pragma once

#include "plugins/air-quality-suite/AirQuality.h"
#include "plugins/songyun-desk/Skin.h"

namespace songyun_air {
using namespace airplugin;

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
    std::unique_ptr<songyun::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace songyun_air
