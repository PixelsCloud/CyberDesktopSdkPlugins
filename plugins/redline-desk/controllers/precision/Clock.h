#pragma once

#include "plugins/precision-clocks/Clock.h"
#include "plugins/redline-desk/Skin.h"

namespace redline_precision {
using namespace precisionclock;

class Renderer {
public:
    Renderer(Kind kind, const std::filesystem::path& assets = {});
    bool poll();
    bool pending() const;
    void pause(bool paused);
    cdsdk::ImageSlotStats assets() const;
    void draw(cybercanvas::Canvas& canvas, const State& state, const Options& options, double scale);

    redline::View view(CdRect safe, cybercanvas::Size world);

private:
    std::unique_ptr<redline::Skin> skin_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);

private:
    CdResult schedule(const cdsdk::Settings& next);
    void arrange();
    static void CD_CALL wake(void* value, uint64_t now) noexcept;
    Kind kind_;
    Model model_;
    // Renderer owns compatible targets and must release them before Canvas/COM.
    cdsdk::Canvas canvas_;
    Renderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    CdRect safe_{};
    cybercanvas::Point origin_{};
    double scale_ = .2;
    bool hidden_ = false;
};

}  // namespace redline_precision
