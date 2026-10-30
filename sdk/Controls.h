#pragma once

#include <CyberCanvas/DesktopWidgets.h>

#include <set>

#include "sdk/Icons.h"
#include "sdk/Interaction.h"
#include "services/Keyboard.h"

namespace cdsdk {
// Immediate-mode controls share their exact paint rectangles with pointer and
// keyboard input. Scene remains the richer retained-tree option. No timers.
class Controls {
public:
    struct Item {
        uint32_t id;
        cybercanvas::Rect bounds;
        std::string name;
        bool enabled = true, toggle = false, checked = false;
    };

    Controls() = default;
    Controls(const Controls&) = delete;
    Controls& operator=(const Controls&) = delete;

    ~Controls();

    void bind(const CdCreateContext& context, std::function<void(uint32_t)> activate);

    void items(std::vector<Item> values, cybercanvas::Point origin = {});

    void input(const CdInput& event);

    void cancel();

    cybercanvas::ControlVisual visual(uint32_t id) const;

    bool hasFocus() const;

    void drawFocus(cybercanvas::Canvas& canvas, const cybercanvas::WidgetTheme& theme) const;

    Icons icons;

private:
    const Item* find(uint32_t id) const;

    void redraw();

    void publish();

    static CdResult CD_CALL deliver(void* user, const CdUiEvent* event) noexcept;

    Interaction pointer_;
    CdHostApi host_{};
    CdKeyboardApi keyboard_{};
    void* context_ = nullptr;
    std::vector<Item> items_;
    cybercanvas::Point origin_{};
    uint32_t focused_ = 0;
    bool granted_ = false, publishing_ = false;
    std::string signature_;
    std::function<void(uint32_t)> action_;
};
}  // namespace cdsdk
