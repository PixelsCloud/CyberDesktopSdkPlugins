#include <sdk/Appearance.h>
#include <sdk/Canvas.h>
#include <sdk/Settings.h>
#include <sdk/WindowsFonts.h>
#include <sdk/cpp/Plugin.h>
#include <sdk/services/Composition.h>
#include <sdk/services/Interaction.h>
#include <sdk/services/Live2D.h>

#include <filesystem>

#include "plugins/live2d-suite/StatusPresentation.h"

namespace {
class Live2DPlugin {
public:
    cdsdk::Settings settings{
        {"model", CD_SETTING_CHOICE, 0, 0, "mao", 0, 0, {"mao", "haru", "hiyori", "wanko", "whale-girl"}},
        {"directory", CD_SETTING_TEXT, 0, 0, {}, 0, 4096},
        {"motion", CD_SETTING_TEXT, 0, 0, "auto", 0, 256},
        {"expression", CD_SETTING_TEXT, 0, 0, {}, 0, 256},
        {"fps", CD_SETTING_CHOICE, 0, 0, "30", 0, 0, {"30", "60"}},
        {"scale", CD_SETTING_NUMBER, 0, 1, {}, .1, 4},
        {"x", CD_SETTING_NUMBER, 0, 0, {}, -2, 2},
        {"y", CD_SETTING_NUMBER, 0, 0, {}, -2, 2},
        {"paused", CD_SETTING_TOGGLE, 0},
        {"physics", CD_SETTING_TOGGLE, 1},
        {"follow", CD_SETTING_TOGGLE, 1}};

    explicit Live2DPlugin(const CdCreateContext& context)
        : context_(context.host_context), pluginDirectory_(std::filesystem::u8path(context.plugin_directory)) {
        cdsdk::modernDefaults(settings, cdsdk::WidgetColorway::Iris);
        canvas_.bindGpu(context);
        if (context.host && context.host->struct_size >= sizeof(CdServiceHostApi)) {
            const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
            if (host->query_service) {
                host->query_service(context_, CD_SERVICE_LIVE2D, 1, &api_, sizeof(api_));
                host->query_service(context_, CD_SERVICE_COMPOSITION, 1, &composition_, sizeof(composition_));
                CdInteractionApi interaction{};
                if (host->query_service(context_, CD_SERVICE_INTERACTION, 1, &interaction, sizeof(interaction)) ==
                    CD_OK) {
                    interaction.enable(context_, 1);
                }
            }
        }
        if (applySettings(nullptr, 0) != CD_OK) {
            throw std::runtime_error("Cannot configure the default Live2D character");
        }
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        auto next = settings;
        const auto result = next.apply(values, count);
        if (result != CD_OK) {
            return result;
        }
        for (const char* key : {"directory", "motion", "expression"}) {
            if (next.at(key).text.size() > (std::strcmp(key, "directory") == 0 ? 4096u : 256u)) {
                return CD_ERROR_ARGUMENT;
            }
        }
        bool modelChanged = !configured_;
        for (const auto key :
             {"model", "directory", "motion", "expression", "fps", "scale", "x", "y", "paused", "physics", "follow"}) {
            const auto& previous = settings.at(key);
            const auto& value = next.at(key);
            modelChanged |=
                previous.integer != value.integer || previous.number != value.number || previous.text != value.text;
        }
        if (api_.configure && modelChanged) {
            auto directory = next.at("directory").text;
            if (directory.empty()) {
                const auto path = (pluginDirectory_ / "models" / next.at("model").text).u8string();
                directory.assign(path.begin(), path.end());
            }
            CdLive2DOptions options{sizeof(options),
                                    next.at("fps").text == "60" ? 60u : 30u,
                                    directory.c_str(),
                                    next.at("motion").text.c_str(),
                                    next.at("expression").text.c_str(),
                                    next.at("scale").number,
                                    next.at("x").number,
                                    next.at("y").number,
                                    uint32_t(next.at("paused").integer),
                                    uint32_t(next.at("physics").integer),
                                    uint32_t(next.at("follow").integer),
                                    0};
            const auto configured = api_.configure(context_, &options);
            if (configured != CD_OK) {
                return configured;
            }
        }
        configured_ = true;
        cdsdk::normalizeModernColors(next);
        settings = std::move(next);
        if (composition_.invalidate) {
            composition_.invalidate(context_, CD_COMPOSITION_BACKGROUND | CD_COMPOSITION_FOREGROUND);
        }
        return CD_OK;
    }

    void layoutChanged(const CdLayout& layout) {
        width_ = std::max(1., layout.logical_width);
        height_ = std::max(1., layout.logical_height);
        modelBounds_ = {6, 6, std::max(1., width_ - 12), std::max(1., height_ - 12)};
        if (composition_.configure) {
            composition_.configure(context_, &modelBounds_, 1);
        }
    }

    void input(const CdInput& event) {
        if (api_.pointer) {
            if (event.kind == CD_INPUT_LEAVE || event.kind == CD_INPUT_CANCEL) {
                pressed_ = false;
                api_.pointer(context_, 0, 0, 0);
            } else if (event.kind == CD_INPUT_PRESS) {
                pressed_ = (event.buttons & CD_BUTTON_LEFT) != 0;
            } else if (event.kind == CD_INPUT_MOVE || event.kind == CD_INPUT_RELEASE) {
                api_.pointer(context_, (event.x - modelBounds_.x) / modelBounds_.width * 2 - 1,
                             1 - (event.y - modelBounds_.y) / modelBounds_.height * 2,
                             event.kind == CD_INPUT_RELEASE && pressed_ ? 1u : 0u);
                if (event.kind == CD_INPUT_RELEASE) {
                    pressed_ = false;
                }
            }
        }
    }

    CdResult render(const CdFrame& frame) {
        if (!canvas_.valid(frame)) {
            return CD_ERROR_ARGUMENT;
        }
        CdLive2DStatus status{};
        status.struct_size = sizeof(status);
        if (api_.snapshot) {
            api_.snapshot(context_, &status);
        }
        canvas_.selectFont(settings.at("font_family").text);
        std::wstring text = L"Live2D\n正在准备角色…";
        if (!api_.configure) {
            text = L"Live2D 需要支持独立模型进程的 Desktop 版本";
        } else if (status.state == 1) {
            text = L"正在加载模型…";
        } else if (status.state == 2) {
            text = L"Live2D · GPU 画面";
        }
        canvas_.render(
            frame,
            {0, 0, double(frame.width) / frame.device_pixel_ratio, double(frame.height) / frame.device_pixel_ratio},
            [&](auto& draw, auto area) {
                const auto appearance = cdsdk::widgetAppearance(settings);
                const auto pass = composition_.current_pass ? composition_.current_pass(context_) : CD_COMPOSITION_ALL;
                if (pass != CD_COMPOSITION_FOREGROUND) {
                    cdsdk::widgetBackground(draw, appearance,
                                            {6, 6, std::max(0., area.width - 12), std::max(0., area.height - 12)});
                }
                if (pass == CD_COMPOSITION_BACKGROUND) {
                    return;
                }
                if (api_.configure && status.state != 3) {
                    return;
                }
                if (api_.configure && status.state == 3) {
                    live2dplugin::drawFailure(draw, {area.x, area.y, area.width, area.height}, status.message,
                                              appearance.foreground, draw.selectedFont());
                    return;
                }
                cybercanvas::TextStyle font;
                font.family = cdsdk::fontWide(settings.at("font_family").text);
                font.size = 13;
                font.align = cybercanvas::TextAlign::Center;
                font.wrap = true;
                draw.text(text, font, {10, area.height * .3, std::max(1., area.width - 20), area.height * .6},
                          appearance.foreground);
            });
        return CD_OK;
    }

private:
    cdsdk::Canvas canvas_;
    CdLive2DApi api_{};
    CdCompositionApi composition_{};
    CdRect modelBounds_{6, 6, 1, 1};
    void* context_ = nullptr;
    std::filesystem::path pluginDirectory_;
    double width_ = 1, height_ = 1;
    bool pressed_ = false, configured_ = false;
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<Live2DPlugin>::query(abi, output, size, "com.cyberdesktop.live2d", "1.3.0");
}
