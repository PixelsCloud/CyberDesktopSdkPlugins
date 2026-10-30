#pragma once

#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "plugins/mechanical-pixels/Pixels.h"

namespace deepwhale {
using MatrixKind = mechanicalpixels::Kind;

struct Spec {
    const char* id;
    const char* folder;
    const char* art;
    MatrixKind kind;
    unsigned columns, rows;
};

inline constexpr std::array<Spec, 2> kCatalog{{
    {"com.cyberdesktop.deepwhale.clock.square.4x1", "clock-square-4x1", "clock-square-4x1", MatrixKind::ClockSquare, 4,
     1},
    {"com.cyberdesktop.deepwhale.clock.round.4x1", "clock-round-4x1", "clock-round-4x1", MatrixKind::ClockRound, 4, 1},
}};

const Spec& specification(std::string_view id);
cdsdk::Settings defaults(MatrixKind kind);

struct Geometry {
    cybercanvas::Rect artwork, aperture;
    cybercanvas::Size originalBody;
    double bodyCut = 0;
    cybercanvas::Point grid;
    double pitch = 0, pitchX = 0, pitchY = 0, diameter = 0;
    unsigned columns = 0, rows = 0;
    std::array<double, 39> columnOffsets{};
    std::array<bool, 39> emitters{};
};

cybercanvas::ImageRegion artworkRegion(MatrixKind kind);
cybercanvas::Rect readingRegion(MatrixKind kind);
Geometry arrange(const Spec& spec, double width, double height, double dpi, bool seconds);

class Renderer {
public:
    Renderer(const CdCreateContext& context, const Spec& spec);
    ~Renderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    uint64_t cacheBuilds() const;
    cdsdk::ImageSlotStats imageStats() const;
    bool render(const cdsdk::Settings& settings, std::string_view text, bool colon, const CdFrame& frame,
                CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ClockInstance {
public:
    explicit ClockInstance(const CdCreateContext& context);
    ~ClockInstance();
    CdResult render(const CdFrame& frame);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();
    cdsdk::Settings settings;

private:
    CdResult schedule(const cdsdk::Settings& next);
    bool synchronize();
    static void CD_CALL wake(void* value, uint64_t) noexcept;

    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    Renderer renderer_;
    CdRect safe_{};
    std::string text_;
    bool colon_ = true, paused_ = false;
};
}  // namespace deepwhale
