#pragma once

#include <CyberCanvas/PanelLayout.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "plugins/mechanical-pixels/Face.h"
#include "plugins/widget-suite/Widget.h"

namespace mechanicalpixels {
enum class Kind { ClockSquare, ClockRound, MusicSquare, MusicRound };

struct Spec {
    const char* id;
    const char* folder;
    Kind kind;
    unsigned columns;
};

inline constexpr std::array<Spec, 8> kCatalog{{
    {"com.cyberdesktop.clock.mechanical.pixel.square.4x1", "clock-square", Kind::ClockSquare, 4},
    {"com.cyberdesktop.clock.mechanical.pixel.round.4x1", "clock-round", Kind::ClockRound, 4},
    {"com.cyberdesktop.music.mechanical.matrix.square.4x1", "music-square-4x1", Kind::MusicSquare, 4},
    {"com.cyberdesktop.music.mechanical.matrix.square.6x1", "music-square-6x1", Kind::MusicSquare, 6},
    {"com.cyberdesktop.music.mechanical.matrix.round.4x1", "music-round-4x1", Kind::MusicRound, 4},
    {"com.cyberdesktop.music.mechanical.matrix.round.6x1", "music-round-6x1", Kind::MusicRound, 6},
    {"com.cyberdesktop.music.mechanical.matrix.square.8x1", "music-square", Kind::MusicSquare, 8},
    {"com.cyberdesktop.music.mechanical.matrix.round.8x1", "music-round", Kind::MusicRound, 8},
}};

constexpr bool isClock(Kind kind) {
    return kind == Kind::ClockSquare || kind == Kind::ClockRound;
}

constexpr bool isRound(Kind kind) {
    return kind == Kind::ClockRound || kind == Kind::MusicRound;
}

const Spec& specification(std::string_view id);
cdsdk::Settings defaults(Kind kind);
std::string clockText(CdEnvironmentSnapshot snapshot, const cdsdk::Settings& settings);
bool clockCell(std::string_view text, unsigned column, unsigned row, bool colonVisible = true);
std::array<uint32_t, 4> palette(Kind kind, const cdsdk::Settings& settings);
uint32_t cellColor(Kind kind, const cdsdk::Settings& settings, unsigned rowFromBottom, unsigned rows,
                   bool peak = false);

struct Housing {
    cybercanvas::Size source;
    cybercanvas::Rect visible, aperture;
    double radius;
};

Housing housing(Kind kind);

struct Geometry {
    cybercanvas::Rect body, aperture;
    double radius = 0, pitch = 0, diameter = 0, extension = 0;
    cybercanvas::Point grid;
    unsigned columns = 0, rows = 0;
};

Geometry arrange(Kind kind, double width, double height, double dpi, bool seconds = true);

class Renderer final : public Face {
public:
    explicit Renderer(const CdCreateContext& context);
    ~Renderer() override;
    void onChange(std::function<void()> callback) override;
    void pause(bool paused) override;
    bool ready() const;
    bool render(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colonVisible,
                const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) override;
    uint64_t cacheBuilds() const;
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    Instance(const CdCreateContext& context, const Spec& spec, std::unique_ptr<Face> face);
    ~Instance();
    CdResult render(const CdFrame& frame);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();
    cdsdk::Settings settings;

private:
    void audio(const float* values, uint32_t count);
    bool synchronize();
    CdResult subscribe(bool reduced);
    CdResult schedule();
    bool advance();

    const Spec& spec_;
    CdHostApi host_{};
    CdAudioServiceApi audioService_{};
    void* hostContext_ = nullptr;
    cdsdk::Environment environment_;
    cdsdk::UpdateLoop updates_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::AudioWatch audio_;
    cdsdk::Subscription legacyAudio_;
    std::unique_ptr<Face> renderer_;
    widgetplugin::Spectrum spectrum_;
    CdRect safe_{};
    std::string text_;
    bool paused_ = false, colonVisible_ = true, haveAudio_ = false;
    uint64_t lastAudio_ = 0, lastAdvance_ = 0;
};
}  // namespace mechanicalpixels
