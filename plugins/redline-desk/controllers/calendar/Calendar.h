#pragma once

#include "plugins/mechanical-calendar/Calendar.h"
#include "plugins/redline-desk/Skin.h"

namespace redline_calendar {
using namespace mechanicalcalendar;

class Renderer {
public:
    explicit Renderer(const CdCreateContext& context);
    ~Renderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(const Month& month, const cdsdk::Settings& settings, const Visual& visual, const CdFrame& frame,
                CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

    unsigned hit(CdRect safe, cybercanvas::Point point);

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
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    std::vector<uint8_t> saveState() const;
    CdResult restoreState(std::span<const uint8_t> bytes);
    CdResult navigate(int offset);
    const Month& displayed() const;
    int offset() const;
    bool rolling() const;

private:
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    Renderer renderer_;
    Month month_;
    Visual visual_;
    CdRect safe_{};
    uint64_t rollStart_ = 0;
    int offset_ = 0;
    bool paused_ = false, dragging_ = false;
    double pressX_ = 0, pressY_ = 0;
    CdResult schedule(uint32_t interval);
    CdResult setOffset(int offset, bool animate);
    void tick();
    void cancelInput();
    static void CD_CALL onTick(void* pointer, uint64_t now) noexcept;
};

}  // namespace redline_calendar
