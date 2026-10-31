#pragma once

#include <sdk/Images.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "plugins/marble-clock/Clock.h"

namespace deepwhale {
struct MarbleSpec {
    const char* id;
    const char* folder;
    bool compact;
    unsigned columns, rows;
};

inline constexpr std::array<MarbleSpec, 2> kMarbleCatalog{{
    {"com.cyberdesktop.deepwhale.clock.marble.4x2", "clock-marble-4x2", true, 4, 2},
    {"com.cyberdesktop.deepwhale.clock.marble.6x2", "clock-marble-6x2", false, 6, 2},
}};

const MarbleSpec& marbleSpec(std::string_view id);
cdsdk::Settings marbleDefaults();

struct MarbleView {
    cybercanvas::Point origin;
    double scale;
};

MarbleView marbleView(CdRect safe, const marbleclock::Model& model);

class MarbleRenderer {
public:
    MarbleRenderer(const CdCreateContext& context, const MarbleSpec& spec);
    ~MarbleRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool pending() const;
    uint64_t cacheBuilds() const;
    cdsdk::ImageSlotStats imageStats() const;
    void draw(cybercanvas::Canvas& canvas, const marbleclock::Model& model, const cdsdk::Settings& settings, bool light,
              double scale, double dpi);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MarbleInstance {
public:
    explicit MarbleInstance(const CdCreateContext& context);
    ~MarbleInstance();
    CdResult render(const CdFrame& frame);
    void input(const CdInput& input);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    bool ready() const;
    const marbleclock::Model& model() const;
    cdsdk::ImageSlotStats imageStats() const;
    uint64_t cacheBuilds() const;
    cdsdk::Settings settings;

    std::array<uint64_t, 4> physicsCounters() const;

private:
    bool frozen(const cdsdk::Settings& values) const;
    bool synchronize(bool animate);
    CdResult schedule(const cdsdk::Settings& values);
    void cancel();

    marbleclock::Model model_;
    cdsdk::Canvas canvas_;
    MarbleRenderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    MarbleView view_{{}, 1};
    std::optional<uint64_t> lastSecond_;
    bool hidden_ = false, laidOut_ = false;
};
}  // namespace deepwhale
