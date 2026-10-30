#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include "plugins/precision-clocks/Clock.h"

namespace pixeldesk {
struct PrecisionSpec {
    const char* id;
    const char* folder;
    precisionclock::Kind kind;
    cybercanvas::Size source;
    cybercanvas::Rect visible;
    cybercanvas::Point center;
    double radius;
    cybercanvas::Point moonCenter;
    double moonRadius;
    cybercanvas::Rect date{};
    double bezelRadius = 0;
};

inline constexpr std::array<PrecisionSpec, 3> kPrecisionCatalog{{
    {"com.cyberdesktop.pixeldesk.clock.precision.dress.2x2",
     "clock-precision-dress-2x2",
     precisionclock::Kind::Dress,
     {1236, 1251},
     {6, 6, 1224, 1239},
     {615, 630},
     450,
     {615, 800},
     62},
    {"com.cyberdesktop.pixeldesk.clock.precision.green.2x2",
     "clock-precision-green-2x2",
     precisionclock::Kind::Green,
     {1299, 1254},
     {6, 6, 1287, 1242},
     {609, 633},
     390,
     {},
     0,
     {852, 594, 84, 72},
     0},
    {"com.cyberdesktop.pixeldesk.clock.precision.skeleton.2x2",
     "clock-precision-skeleton-2x2",
     precisionclock::Kind::Skeleton,
     {1254, 1254},
     {61, 77, 1133, 1066},
     {626, 603},
     450,
     {},
     0},
}};

const PrecisionSpec& precisionSpec(std::string_view id);
cdsdk::Settings precisionDefaults(precisionclock::Kind kind);

struct PrecisionView {
    cybercanvas::Point origin;
    double scale;
};

PrecisionView precisionView(CdRect safe, const PrecisionSpec& spec);

class PrecisionRenderer {
public:
    PrecisionRenderer(const CdCreateContext& context, const PrecisionSpec& spec);
    ~PrecisionRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool pending() const;
    uint64_t cacheBuilds() const;
    cdsdk::ImageSlotStats imageStats() const;
    void draw(cybercanvas::Canvas& canvas, const precisionclock::State& state, const cdsdk::Settings& settings,
              double scale, double dpi);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PrecisionInstance {
public:
    explicit PrecisionInstance(const CdCreateContext& context);
    ~PrecisionInstance();
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    void invalidate();
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    bool ready() const;
    const precisionclock::State& state() const;
    cdsdk::Settings settings;

private:
    CdResult schedule(const cdsdk::Settings& next);
    static void CD_CALL wake(void* value, uint64_t) noexcept;
    const PrecisionSpec& spec_;
    precisionclock::Model model_;
    cdsdk::Canvas canvas_;
    PrecisionRenderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    CdRect safe_{};
    bool hidden_ = false;
};
}  // namespace pixeldesk
