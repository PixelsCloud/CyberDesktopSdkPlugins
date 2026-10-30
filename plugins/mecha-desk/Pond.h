#pragma once

#include <sdk/Environment.h>
#include <sdk/Update.h>

#include <optional>

#include "plugins/koi-suite/Pond.h"
#include "plugins/mecha-desk/Skin.h"

namespace aeris_pond {
struct BasicSpec {
    const char* id;
    unsigned columns, rows;
};

struct PondSpec {
    BasicSpec plate;
    cybercanvas::Rect water;
    unsigned fishCount;
    cybercanvas::Point foregroundProbe{899, 192};
};

const PondSpec& pondSpec(std::string_view id);
const PondSpec& pondFolder(std::string_view folder);
cdsdk::Settings pondDefaults(const PondSpec& spec);

class PondFace {
public:
    PondFace(const CdCreateContext& context, const PondSpec& spec);
    ~PondFace();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    cybercanvas::Size waterSize() const;
    bool contains(koiplugin::Vec point, double clearance = 0) const;
    bool ready() const;
    uint64_t cacheBuilds() const;
    std::optional<koiplugin::Vec> hit(CdRect safe, double x, double y) const;
    bool render(const koiplugin::Scene& scene, const koiplugin::Appearance& appearance, const CdFrame& frame,
                CdRect safe);
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PondInstance {
public:
    explicit PondInstance(const CdCreateContext& context);
    ~PondInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();
    bool ready() const;
    const koiplugin::Scene& scene() const;

private:
    CdResult schedule(const cdsdk::Settings& next);
    void configure();
    double targetNight() const;
    static void CD_CALL tick(void* pointer, uint64_t elapsed) noexcept;
    const PondSpec& spec_;
    koiplugin::Scene scene_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    PondFace face_;
    CdRect safe_{};
    cybercanvas::Point press_{};
    uint64_t lastTick_ = 0;
    double night_ = 0, motion_ = 0;
    uint32_t startupSeed_ = 0;
    bool hidden_ = false, pressed_ = false, themeNight_ = false;
};
}  // namespace aeris_pond
