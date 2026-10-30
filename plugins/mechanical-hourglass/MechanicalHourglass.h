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
#include <string_view>

#include "plugins/hourglass-suite/Hourglass.h"
#include "plugins/hourglass-suite/PhysicalSand.h"

namespace mechanicalhourglass {
using hourglassplugin::Model;
using hourglassplugin::Phase;
using hourglassplugin::PhysicalSand;

struct Spec {
    const char* id;
    const char* folder;
    unsigned columns;
};

inline constexpr std::array<Spec, 2> kCatalog{{
    {"com.cyberdesktop.hourglass.mechanical.1x1", "1x1", 1},
    {"com.cyberdesktop.hourglass.mechanical.2x1", "2x1", 2},
}};

inline constexpr unsigned kDigitColumns = 17;

const Spec& specification(std::string_view id);
cdsdk::Settings defaults(bool timer);
std::string remainingText(int seconds);
bool digitCell(std::string_view text, unsigned column, unsigned row);

struct Geometry {
    cybercanvas::Rect body, jar, display, progress;
    double extension = 0, sliceStart = 0, sliceEnd = 0, pitch = 0;
};

Geometry arrange(bool timer, double width, double height);
cybercanvas::Point grainPosition(cybercanvas::Point point, cybercanvas::Rect jar);

class Renderer {
public:
    explicit Renderer(const CdCreateContext& context);
    ~Renderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(bool timer, const Model& model, const PhysicalSand* sand, const cdsdk::Settings& settings,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    const Model& model() const;

private:
    const Spec& spec_;
    CdHostApi host_{};
    void* context_ = nullptr;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    Renderer renderer_;
    Model model_;
    PhysicalSand sand_;
    CdRect safe_{};
    uint64_t lastTick_ = 0;
    bool paused_ = false, pressed_ = false;
    double pressX_ = 0, pressY_ = 0;
    void tick(uint64_t now);
    CdResult subscribe(const cdsdk::Settings& settings, bool restarting, cdsdk::Timer& timer);
    void restart();
};
}  // namespace mechanicalhourglass
