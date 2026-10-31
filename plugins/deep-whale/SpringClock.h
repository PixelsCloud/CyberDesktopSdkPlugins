#pragma once

#include <sdk/Canvas.h>
#include <sdk/Images.h>
#include <sdk/Interaction.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include "plugins/physics-toys/Toys.h"

namespace deepwhale {
struct SpringSpec {
    const char* id;
    const char* folder;
    bool compact;
    unsigned columns, rows;
};

inline constexpr std::array<SpringSpec, 2> kSpringCatalog{{
    {"com.cyberdesktop.deepwhale.clock.spring.4x1", "clock-spring-4x1", true, 4, 1},
    {"com.cyberdesktop.deepwhale.clock.spring.4x2", "clock-spring-4x2", false, 4, 2},
}};

toys::ClockLayout springLayout(bool compact = true);
const SpringSpec& springSpec(std::string_view id);
cdsdk::Settings springDefaults(bool compact = true);

struct SpringView {
    cybercanvas::Point origin;
    double scale;
};

cybercanvas::Rect springFrameBounds(const toys::Model& model);
SpringView springView(CdRect safe, const toys::Model& model);

class SpringRenderer {
public:
    explicit SpringRenderer(const CdCreateContext& context);
    ~SpringRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    void draw(cybercanvas::Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double scale,
              double dpi, cybercanvas::Rect beam = {});
    cdsdk::ImageSlotStats imageStats() const;
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class SpringInstance {
public:
    explicit SpringInstance(const CdCreateContext& context);
    ~SpringInstance();
    CdResult render(const CdFrame& frame);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    const toys::Model& model() const;
    cdsdk::ImageSlotStats imageStats() const;
    uint64_t cacheBuilds() const;
    bool ready() const;
    cdsdk::Settings settings;

    std::array<uint64_t, 4> physicsCounters() const;

private:
    bool synchronize(bool animate);
    bool frozen(const cdsdk::Settings& values) const;
    CdResult schedule(const cdsdk::Settings& values);
    void cancel();

    toys::Model model_;
    cdsdk::Canvas canvas_;
    SpringRenderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::UpdateLoop updates_;
    cdsdk::RedrawRequests redraw_;
    CdRect safe_{};
    SpringView view_{{}, 1};
    bool hidden_ = false, laidOut_ = false;
};
}  // namespace deepwhale
