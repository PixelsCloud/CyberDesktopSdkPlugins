#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/WindowsFonts.h>
#include <sdk/cpp/Plugin.h>
#include <sdk/cpp/Services.h>

namespace {
class Telemetry {
public:
    cdsdk::Settings settings{
        {"color", CD_SETTING_COLOR, 0x6BD7C7FF},
        {"font_family", CD_SETTING_CHOICE, 0, 0, "Segoe UI", 0, 0, {"Segoe UI", "Consolas", "Microsoft YaHei"}},
        {"language", CD_SETTING_CHOICE, 0, 0, "en", 0, 0, {"en", "zh-CN"}}};

    explicit Telemetry(const CdCreateContext& context) : services_(context.host, context.host_context) {
        environment_.bind(context);
        redraw_.bind(context);
        start();
    }

    ~Telemetry() {
        watch_.reset();
    }

    void invalidate() {
        redraw_.invalidate();
    }

    void layoutChanged(const CdLayout& layout) {
        safe_ = layout.safe_content;
    }

    CdResult render(const CdFrame& frame) {
        if (!canvas_.valid(frame)) {
            return CD_ERROR_ARGUMENT;
        }
        redraw_.beginFrame();
        const bool chinese = settings.at("language").text == "zh-CN";
        const bool light = environment_.snapshot().theme == CD_ENV_THEME_LIGHT;
        const uint32_t ink = light ? 0x173F3DFF : 0xECF7F4FF;
        cybercanvas::TextStyle font;
        font.family = cdsdk::fontWide(settings.at("font_family").text);
        font.locale = chinese ? L"zh-CN" : L"en-US";
        font.ellipsis = true;
        canvas_.render(frame, safe_, [&](auto& draw, auto area) {
            draw.box(0, 0, area.width, area.height, 10, light ? 0xEEF8F5FF : 0x142D2EFF);
            const double padding = std::min(16., area.width * .1), width = std::max(0., area.width - padding * 2);
            font.size = 13;
            draw.text(chinese ? L"系统状态" : L"System status", font, {padding, 8, width, 25}, ink);
            font.size = std::clamp(area.height * .14, 12., 24.);
            font.weight = cybercanvas::FontWeight::SemiBold;
            draw.text(valid_ ? L"CPU  " + std::to_wstring(cpu_) + L"%" : (chinese ? L"等待数据" : L"Waiting for data"),
                      font, {padding, area.height * .28, width, area.height * .28}, ink);
            draw.box(padding, area.height * .61, width, 7, 3.5, light ? 0xC5DDD6FF : 0x294E4AFF);
            draw.box(padding, area.height * .61, width * cpu_ / 100., 7, 3.5, uint32_t(settings.at("color").integer));
            font.size = 12;
            font.weight = cybercanvas::FontWeight::Normal;
            draw.text((chinese ? L"内存  " : L"Memory  ") + (valid_ ? std::to_wstring(memory_) + L"%" : L"--"), font,
                      {padding, area.height * .72, width, area.height * .19}, ink);
        });
        return CD_OK;
    }

    void setPaused(bool paused) {
        redraw_.pause(paused);
        if (paused) {
            watch_.reset();
        } else {
            start();
        }
    }

private:
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::Services services_;
    cdsdk::SystemWatch watch_;
    CdRect safe_{};
    int cpu_ = 0, memory_ = 0;
    bool valid_ = false;

    void start() {
        watch_ = services_.system(1000, [this](const cdsdk::SystemSnapshot& snapshot) {
            const auto& value = snapshot.data.base;
            const uint32_t required = CD_SYSTEM_CPU_VALID | CD_SYSTEM_MEMORY_VALID;
            const bool extended = value.struct_size >= sizeof(CdSystemInfoEx);
            const bool valid = std::isfinite(value.cpu_percent) && value.memory_total_bytes > 0 &&
                               (!extended || (snapshot.data.valid_fields & required) == required);
            const int cpu = valid ? int(std::lround(std::clamp(value.cpu_percent, 0., 100.))) : 0;
            const int memory =
                valid
                    ? int(std::lround(
                          std::clamp(double(value.memory_used_bytes) / double(value.memory_total_bytes), 0., 1.) * 100))
                    : 0;
            if (cpu != cpu_ || memory != memory_ || valid != valid_) {
                cpu_ = cpu;
                memory_ = memory;
                valid_ = valid;
                invalidate();
            }
        });
    }
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<Telemetry>::query(abi, output, size, "com.example.data", "1.0.0");
}
