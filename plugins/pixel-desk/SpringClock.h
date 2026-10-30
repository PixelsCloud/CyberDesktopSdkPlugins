#pragma once

#include <sdk/Canvas.h>
#include <sdk/Images.h>
#include <sdk/Interaction.h>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "plugins/physics-toys/Toys.h"

namespace pixeldesk {
struct SpringSpec {
    const char* id;
    const char* folder;
    bool compact;
    unsigned columns, rows;
    cybercanvas::Size source;
    cybercanvas::Rect beamPart, cardPart;
    double cardFaceCenterX;
};

inline constexpr std::array<SpringSpec, 2> kSpringCatalog{{
    {"com.cyberdesktop.pixeldesk.clock.spring.4x1",
     "clock-spring-4x1",
     true,
     4,
     1,
     {2151, 487},
     {2, 2, 2147, 144},
     {366, 175, 183, 310},
     453},
    {"com.cyberdesktop.pixeldesk.clock.spring.4x2",
     "clock-spring-4x2",
     false,
     4,
     2,
     {841, 392},
     {2, 2, 837, 68},
     {3, 70, 131, 320},
     62.5},
}};

const SpringSpec& springSpec(std::string_view id);
cdsdk::Settings springDefaults();

struct SpringView {
    cybercanvas::Point origin;
    double scale;
};

SpringView springView(CdRect safe, const toys::Model& model);
cybercanvas::Point springDragTarget(const toys::Model& model, size_t index, cybercanvas::Point target,
                                    cybercanvas::Point localAnchor = {});

class SpringRenderer {
public:
    SpringRenderer();
    ~SpringRenderer();
    void bind(const CdCreateContext& context, std::function<void()> changed);
    void pause(bool paused);
    bool ready() const;
    void draw(cybercanvas::Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double scale,
              double dpi);
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
    uint64_t cacheBuilds() const;
    bool ready() const;
    cdsdk::Settings settings;

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
    std::optional<size_t> dragIndex_;
    cybercanvas::Point dragAnchor_{};
    bool hidden_ = false, laidOut_ = false;
};
}  // namespace pixeldesk
