#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/WindowsFonts.h>
#include <sdk/cpp/Plugin.h>
#include <sdk/services/Interaction.h>

namespace {
class Animated {
public:
    cdsdk::Settings settings{
        {"animate", CD_SETTING_TOGGLE, 1},
        {"speed", CD_SETTING_NUMBER, 0, .75, {}, 0, 3},
        {"color", CD_SETTING_COLOR, 0x8BBFF8FF},
        {"font_family", CD_SETTING_CHOICE, 0, 0, "Segoe UI", 0, 0, {"Segoe UI", "Consolas", "Microsoft YaHei"}},
        {"language", CD_SETTING_CHOICE, 0, 0, "en", 0, 0, {"en", "zh-CN"}}};

    explicit Animated(const CdCreateContext& context) {
        environment_.bind(context);
        redraw_.bind(context);
        updates_.bind(context);
        updates_.onInvalidate([this] {
            invalidate();
        });
        updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
            phase_ = std::fmod(phase_ + tick.delta * settings.at("speed").number * .25, 1.);
            return cdsdk::UpdateStep{true, true};
        });
        updates_.start(16);
    }

    ~Animated() {
        updates_.stop();
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
        updates_.beginFrame();
        const bool chinese = settings.at("language").text == "zh-CN";
        const bool light = environment_.snapshot().theme == CD_ENV_THEME_LIGHT;
        const uint32_t ink = light ? 0x203C5BFF : 0xECF4FFFF;
        cybercanvas::TextStyle font;
        font.family = cdsdk::fontWide(settings.at("font_family").text);
        font.locale = chinese ? L"zh-CN" : L"en-US";
        font.align = cybercanvas::TextAlign::Center;
        font.ellipsis = true;
        canvas_.render(frame, safe_, [&](auto& draw, auto area) {
            draw.box(0, 0, area.width, area.height, 12, light ? 0xEDF4FFFF : 0x192940FF);
            const double radius = std::max(2., std::min(area.width, area.height) * .12);
            const double angle = phase_ * 6.283185307179586;
            const double cx = area.width * .5 + std::cos(angle) * area.width * .25;
            const double cy = area.height * .44 + std::sin(angle) * area.height * .13;
            draw.oval(cx, cy + radius * .7, radius * 1.05, radius * .25, light ? 0x26456D18 : 0x00000033);
            draw.oval(cx, cy, radius, radius, uint32_t(settings.at("color").integer));
            font.size = 12;
            const bool running = settings.at("animate").integer && settings.at("speed").number > 0;
            draw.text(
                chinese ? (running ? L"点击暂停" : L"点击播放") : (running ? L"Click to pause" : L"Click to play"),
                font, {6, area.height * .75, std::max(0., area.width - 12), area.height * .19}, ink);
        });
        return CD_OK;
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        auto next = settings;
        const auto result = next.apply(values, count);
        if (result != CD_OK) {
            return result;
        }
        const bool run = next.at("animate").integer && next.at("speed").number > 0;
        if (run) {
            const auto started = updates_.start(16);
            if (started != CD_OK) {
                return started;
            }
        } else {
            updates_.stop();
        }
        settings = std::move(next);
        return CD_OK;
    }

    void input(const CdInput& event) {
        const bool inside = event.x >= safe_.x && event.y >= safe_.y &&
                            (safe_.width <= 0 || event.x < safe_.x + safe_.width) &&
                            (safe_.height <= 0 || event.y < safe_.y + safe_.height);
        if (event.kind == CD_INPUT_PRESS) {
            pressed_ = inside && bool(event.buttons & CD_BUTTON_LEFT);
        } else if (event.kind == CD_INPUT_RELEASE) {
            if (pressed_ && inside) {
                CdSettingValue toggle{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animate",
                                      !settings.at("animate").integer};
                if (applySettings(&toggle, 1) == CD_OK) {
                    invalidate();
                }
            }
            pressed_ = false;
        } else if (event.kind == CD_INPUT_LEAVE || event.kind == CD_INPUT_CANCEL) {
            pressed_ = false;
        }
    }

    void setPaused(bool paused) {
        pressed_ = false;
        redraw_.pause(paused);
        updates_.pause(paused);
    }

    std::vector<uint8_t> saveState() const {
        const auto phase = std::min(999999u, uint32_t(std::lround(phase_ * 1000000.)));
        return {'A', 'N', 1, 0, uint8_t(phase), uint8_t(phase >> 8), uint8_t(phase >> 16), uint8_t(phase >> 24)};
    }

    CdResult restoreState(std::span<const uint8_t> state) {
        if (state.empty()) {
            phase_ = 0;
            return CD_OK;
        }
        if (state.size() != 8 || state[0] != 'A' || state[1] != 'N' || state[2] != 1 || state[3] != 0) {
            return CD_ERROR_ARGUMENT;
        }
        const auto phase =
            uint32_t(state[4]) | uint32_t(state[5]) << 8 | uint32_t(state[6]) << 16 | uint32_t(state[7]) << 24;
        if (phase >= 1000000) {
            return CD_ERROR_ARGUMENT;
        }
        phase_ = phase / 1000000.;
        return CD_OK;
    }

private:
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    double phase_ = 0;
    bool pressed_ = false;
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<Animated>::query(abi, output, size, "com.example.animated", "1.0.0");
}
