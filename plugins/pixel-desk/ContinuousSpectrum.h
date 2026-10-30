#pragma once

#include "plugins/pixel-desk/PixelMatrix.h"

namespace pixeldesk {
// The audio subscription and original Spectrum model are shared. Only the
// actual-DPI face differs from the discrete dot-matrix instruments.
class ContinuousSpectrum {
public:
    ContinuousSpectrum(const CdCreateContext& context, const MatrixSpec& spec);
    ~ContinuousSpectrum();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    uint64_t cacheBuilds() const;
    bool render(const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum, const CdFrame& frame,
                CdRect safe);
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace pixeldesk
