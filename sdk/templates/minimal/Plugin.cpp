#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/WindowsFonts.h>
#include <sdk/cpp/Plugin.h>
#include <sdk/services/Interaction.h>

#ifndef CD_SAMPLE_ID
#define CD_SAMPLE_ID "com.example.hello"
#endif
#ifndef CD_SAMPLE_VERSION
#define CD_SAMPLE_VERSION "1.0.0"
#endif

namespace {
class Starter {
public:
    cdsdk::Settings settings{
        {"opacity", CD_SETTING_NUMBER, 0, 1, {}, .1, 1},
        {"color", CD_SETTING_COLOR, 0x63D8B5FF},
        {"font_family", CD_SETTING_CHOICE, 0, 0, "Segoe UI", 0, 0, {"Segoe UI", "Consolas", "Microsoft YaHei"}},
        {"language", CD_SETTING_CHOICE, 0, 0, "en", 0, 0, {"en", "zh-CN"}}};

    explicit Starter(const CdCreateContext& context) {
        environment_.bind(context);
        redraw_.bind(context);
    }

    void invalidate() {
        redraw_.invalidate();
    }

    void layoutChanged(const CdLayout& layout) {
        safe_ = layout.safe_content;
        pressed_ = false;
    }

    CdResult render(const CdFrame& frame) {
        if (!canvas_.valid(frame)) {
            return CD_ERROR_ARGUMENT;
        }
        redraw_.beginFrame();
        const bool chinese = settings.at("language").text == "zh-CN";
        const bool light = environment_.snapshot().theme == CD_ENV_THEME_LIGHT;
        cybercanvas::TextStyle font;
        font.family = cdsdk::fontWide(settings.at("font_family").text);
        font.locale = chinese ? L"zh-CN" : L"en-US";
        font.align = cybercanvas::TextAlign::Center;
        font.ellipsis = true;
        canvas_.render(frame, safe_, [&](auto& draw, auto area) {
            draw.withOpacity(settings.at("opacity").number, [&] {
                draw.box(0, 0, area.width, area.height, 10, light ? 0xEFF8F5FF : 0x142E2BFF);
                draw.box(12, 12, std::max(0., area.width - 24), 3, 1.5, uint32_t(settings.at("color").integer));
                font.size = std::min(20., std::max(10., area.height * .20));
                font.weight = cybercanvas::FontWeight::SemiBold;
                draw.text(chinese ? L"你好，桌面" : L"Hello, desktop", font,
                          {8, 18, std::max(0., area.width - 16), area.height * .4}, light ? 0x153F36FF : 0xEBF7F2FF);
                font.size = 13;
                font.weight = cybercanvas::FontWeight::Normal;
                draw.text((chinese ? L"点击  " : L"Clicks  ") + std::to_wstring(clicks_), font,
                          {8, area.height * .58, std::max(0., area.width - 16), area.height * .28},
                          light ? 0x476D63FF : 0xBCE9DEFF);
            });
        });
        return CD_OK;
    }

    void input(const CdInput& event) {
        const bool inside = event.x >= safe_.x && event.y >= safe_.y &&
                            (safe_.width <= 0 || event.x < safe_.x + safe_.width) &&
                            (safe_.height <= 0 || event.y < safe_.y + safe_.height);
        if (event.kind == CD_INPUT_PRESS) {
            pressed_ = inside && (event.buttons & CD_BUTTON_LEFT);
        } else if (event.kind == CD_INPUT_RELEASE) {
            if (pressed_ && inside) {
                ++clicks_;
                invalidate();
            }
            pressed_ = false;
        } else if (event.kind == CD_INPUT_LEAVE || event.kind == CD_INPUT_CANCEL) {
            pressed_ = false;
        }
    }

    void setPaused(bool paused) {
        pressed_ = false;
        redraw_.pause(paused);
    }

    std::vector<uint8_t> saveState() const {
        return {
            'S', 'T', 1, 0, uint8_t(clicks_), uint8_t(clicks_ >> 8), uint8_t(clicks_ >> 16), uint8_t(clicks_ >> 24)};
    }

    CdResult restoreState(std::span<const uint8_t> state) {
        if (state.empty()) {
            clicks_ = 0;
            return CD_OK;
        }
        if (state.size() != 8 || state[0] != 'S' || state[1] != 'T' || state[2] != 1 || state[3] != 0) {
            return CD_ERROR_ARGUMENT;
        }
        clicks_ = uint32_t(state[4]) | uint32_t(state[5]) << 8 | uint32_t(state[6]) << 16 | uint32_t(state[7]) << 24;
        return CD_OK;
    }

private:
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    CdRect safe_{};
    uint32_t clicks_ = 0;
    bool pressed_ = false;
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<Starter>::query(abi, output, size, CD_SAMPLE_ID, CD_SAMPLE_VERSION);
}
