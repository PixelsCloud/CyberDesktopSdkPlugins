#pragma once

#include <sdk/Controls.h>
#include <sdk/Images.h>

#include <optional>

#include "plugins/productivity-suite/Productivity.h"

namespace pixeldesk {
struct ProductivitySpec {
    const char* id;
    const char* folder;
    productivity::Kind kind;
    unsigned columns, rows;
};

inline constexpr std::array<ProductivitySpec, 8> kProductivityCatalog{{
    {"com.cyberdesktop.pixeldesk.network.4x1", "network-4x1", productivity::Kind::Network, 4, 1},
    {"com.cyberdesktop.pixeldesk.disk.2x2", "disk-2x2", productivity::Kind::Disk, 2, 2},
    {"com.cyberdesktop.pixeldesk.timer.2x2", "timer-2x2", productivity::Kind::Timer, 2, 2},
    {"com.cyberdesktop.pixeldesk.timer.2x1", "timer-2x1", productivity::Kind::Timer, 2, 1},
    {"com.cyberdesktop.pixeldesk.network.2x1", "network-2x1", productivity::Kind::Network, 2, 1},
    {"com.cyberdesktop.pixeldesk.disk.2x1", "disk-2x1", productivity::Kind::Disk, 2, 1},
    {"com.cyberdesktop.pixeldesk.countdown.2x1", "countdown-2x1", productivity::Kind::Countdown, 2, 1},
    {"com.cyberdesktop.pixeldesk.countdown.4x1", "countdown-4x1", productivity::Kind::Countdown, 4, 1},
}};

const ProductivitySpec& productivitySpec(std::string_view id);
CdResult createProductivity(const CdCreateContext* context, CdInstanceApi* output, uint32_t size);

struct ByteLabel {
    std::string number;
    std::wstring unit;
};

ByteLabel pixelBytes(uint64_t bytes, bool rate = false, uint64_t scaleBytes = 0);

struct NetworkGeometry {
    cybercanvas::Rect artwork;
    double scale, worldWidth;
};

NetworkGeometry networkGeometry(double width, double height, bool compact = false);

struct DiskUsage {
    uint64_t used, total, available;
    double fraction;
};

std::optional<DiskUsage> pixelDiskUsage(const productivity::Data& data);

class NetworkFace {
public:
    NetworkFace();
    ~NetworkFace();
    void bind(const CdCreateContext& context);
    void pause(bool paused);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame, CdRect safe);
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DiskFace {
public:
    DiskFace();
    ~DiskFace();
    void bind(const CdCreateContext& context);
    void pause(bool paused);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame, CdRect safe);
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class CountdownFace {
public:
    CountdownFace();
    ~CountdownFace();
    void bind(const CdCreateContext& context);
    void pause(bool paused);
    bool ready() const;
    uint64_t cacheBuilds() const;
    bool render(const cdsdk::Settings&, const productivity::Data&, const CdFrame&, CdRect safe);
    bool save(const std::filesystem::path&, const CdFrame&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct TimerGeometry {
    cybercanvas::Rect artwork;
    std::array<cybercanvas::Rect, 2> buttons;
    cybercanvas::Point origin;
    double scale;
    cybercanvas::Rect display, phase;
};

TimerGeometry timerGeometry(double width, double height, bool compact = false);
double timerFraction(const cdsdk::Settings& settings, const productivity::Data& data);

class TimerFace {
public:
    TimerFace();
    ~TimerFace();
    void bind(const CdCreateContext& context, std::function<void(uint32_t)> action);
    void pause(bool paused);
    void cancelInput();
    void input(const CdInput& event);
    void syncInput(const cdsdk::Settings& settings, const productivity::Data& data, CdRect safe);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame, CdRect safe);
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace pixeldesk
