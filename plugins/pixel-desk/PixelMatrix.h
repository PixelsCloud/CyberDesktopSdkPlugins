#pragma once

#include "plugins/pixel-desk/PixelDesk.h"

namespace pixeldesk {
enum class MatrixForm { Dots, Rods, Radial };

struct MatrixSpec {
    const char* id;
    const char* folder;
    unsigned columns;
    bool round;
    MatrixForm form = MatrixForm::Dots;
    unsigned rows = 1;
};

inline constexpr std::array<MatrixSpec, 10> kMatrixCatalog{{
    {"com.cyberdesktop.pixeldesk.music.matrix.square.4x1", "music-matrix-square-4x1", 4, false},
    {"com.cyberdesktop.pixeldesk.music.matrix.round.4x1", "music-matrix-round-4x1", 4, true},
    {"com.cyberdesktop.pixeldesk.music.matrix.square.6x1", "music-matrix-square-6x1", 6, false},
    {"com.cyberdesktop.pixeldesk.music.matrix.round.6x1", "music-matrix-round-6x1", 6, true},
    {"com.cyberdesktop.pixeldesk.music.matrix.square.8x1", "music-matrix-square-8x1", 8, false},
    {"com.cyberdesktop.pixeldesk.music.matrix.round.8x1", "music-matrix-round-8x1", 8, true},
    {"com.cyberdesktop.pixeldesk.spectrum.4x1", "spectrum-4x1", 4, false, MatrixForm::Rods},
    {"com.cyberdesktop.pixeldesk.spectrum.6x1", "spectrum-6x1", 6, false, MatrixForm::Rods},
    {"com.cyberdesktop.pixeldesk.spectrum.8x1", "spectrum-8x1", 8, false, MatrixForm::Rods},
    {"com.cyberdesktop.pixeldesk.radial.2x2", "radial-2x2", 2, false, MatrixForm::Radial, 2},
}};

const MatrixSpec& matrixSpec(std::string_view id);
cdsdk::Settings matrixDefaults(const MatrixSpec& spec);

struct MatrixGeometry {
    cybercanvas::Rect artwork, aperture;
    cybercanvas::Point grid, pitch;
    double diameter = 0;
    unsigned columns = 0, rows = 0;
};

MatrixGeometry matrixGeometry(const MatrixSpec& spec, double width, double height);

class MatrixRenderer {
public:
    MatrixRenderer(const CdCreateContext& context, const MatrixSpec& spec);
    ~MatrixRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    uint64_t cacheBuilds() const;
    bool render(const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum, const CdFrame& frame,
                CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MatrixInstance {
public:
    explicit MatrixInstance(const CdCreateContext& context);
    ~MatrixInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    void input(const CdInput& event);
    void invalidate();
    const widgetplugin::Spectrum& spectrum() const;
    bool ready() const;

private:
    CdResult subscribe(const cdsdk::Settings& candidate);
    CdResult schedule(const cdsdk::Settings& candidate);
    void receive(const float* values, uint32_t count);
    static void CD_CALL tick(void* pointer, uint64_t elapsed) noexcept;
    const MatrixSpec& spec_;
    CdHostApi host_{};
    void* context_ = nullptr;
    cdsdk::Services services_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    MatrixRenderer renderer_;
    cdsdk::AudioWatch watch_;
    cdsdk::Subscription legacy_;
    widgetplugin::Spectrum spectrum_;
    CdRect safe_{};
    uint64_t lastAudio_ = 0, lastTick_ = 0;
    bool hidden_ = false, received_ = false;
};
}  // namespace pixeldesk
