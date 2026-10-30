#include <sdk/Settings.h>
#include <sdk/cpp/Services.h>

#include <cmath>
#include <memory>
#include <optional>

#include "examples/Drawing.h"

namespace {
struct Instance {
    CdServiceHostApi completeHost{};
    CdHostApi host{};
    void* context = nullptr;
    bool paused = false;
    sample::FontResource font;
    cdsdk::Settings settings{{{"background", CD_SETTING_TOGGLE, 1},
                              {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
                              {"font_family", CD_SETTING_FONT, 0, 0, "@siyuan-heavy"},
                              {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
    cdsdk::InputWatch input;
    cdsdk::SystemWatch system;
    cdsdk::WeatherWatch weather;
    cdsdk::AirWatch air;
    cdsdk::MediaWatch media;
    cdsdk::AudioWatch audio;
    CdInputStats counts{};
    CdSystemInfoEx telemetry{};
    std::optional<cdsdk::WeatherSnapshot> weatherValue;
    std::optional<cdsdk::AirSnapshot> airValue;
    std::optional<cdsdk::MediaSnapshot> mediaValue;
    std::vector<float> bands;
    double logicalWidth = 384;

    ~Instance() {
        stop();
    }

    void redraw() {
        host.request_redraw(context);
    }

    void stop() {
        input.reset();
        system.reset();
        weather.reset();
        air.reset();
        media.reset();
        audio.reset();
    }

    CdResult start() {
        const cdsdk::Services services(&completeHost.input.media.base, context);
        // Services requires the full host prefix. Keep that table in the instance.
#if CD_DATA_SAMPLE_KIND != 2
        input = services.input([this](const CdInputStats& value) {
            if (counts.keyboard_today != value.keyboard_today || counts.mouse_today != value.mouse_today ||
                counts.status != value.status) {
                counts = value;
                redraw();
            }
        });
        if (input.result() != CD_OK)
            return input.result();
#endif
#if CD_DATA_SAMPLE_KIND == 1
        system = services.system(1000, [this](const auto& value) {
            if (telemetry.base.cpu_percent != value.data.base.cpu_percent ||
                telemetry.valid_fields != value.data.valid_fields) {
                telemetry = value.data;
                redraw();
            }
        });
        CdLocationQuery query{
            sizeof(query), CD_LOCATION_DEFAULT, 0, 0, nullptr, nullptr, "default", "zh-CN", 900, 5, 0, 0, nullptr};
        weather = services.weather(query, [this](const auto& value) {
            if (!weatherValue || weatherValue->temperatureCelsius != value.temperatureCelsius ||
                weatherValue->status != value.status || weatherValue->city != value.city) {
                weatherValue = value;
                redraw();
            }
        });
        air = services.air(query, [this](const auto& value) {
            if (!airValue || airValue->aqi != value.aqi || airValue->status != value.status ||
                airValue->station != value.station) {
                airValue = value;
                redraw();
            }
        });
        if (system.result() != CD_OK || weather.result() != CD_OK || air.result() != CD_OK)
            return CD_ERROR_UNSUPPORTED;
#elif CD_DATA_SAMPLE_KIND == 2
        media = services.media("auto", [this](const auto& value) {
            if (!mediaValue || mediaValue->title != value.title || mediaValue->status != value.status ||
                int(mediaValue->positionSeconds) != int(value.positionSeconds)) {
                mediaValue = value;
                redraw();
            }
        });
        if (media.result() != CD_OK)
            return media.result();
        const CdAudioOptions options{sizeof(options), 32, 30, 0, nullptr};
        audio = services.audio(CD_SERVICE_FFT, options, [this](const auto& value) {
            if (bands != value.samples) {
                bands = value.samples;
                redraw();
            }
        });
        if (audio.result() != CD_OK)
            return audio.result();
#endif
        return CD_OK;
    }
};

void CD_CALL destroy(void* pointer) {
    const auto instance = std::unique_ptr<Instance>(static_cast<Instance*>(pointer));
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        const auto& family = self.settings.at("font_family").text;
        const bool english = self.settings.at("language").text == "en";
        const int width = int(frame->width), height = int(frame->height);
        if (self.settings.at("background").integer)
            sample::roundedRectangle(
                *frame, 1, 1, width - 3, height - 4,
                int(self.settings.at("background_corner_radius").integer * width / std::max(1.0, self.logicalWidth)),
                {23, 35, 43, 240});
        const auto line = [&](const std::wstring& text, int row, int rows, CdColor color) {
            const auto lineHeight = height / rows;
            sample::text(*frame, text, row * lineHeight, lineHeight, std::max(9, lineHeight / 2), color, self.font,
                         family);
        };
#if CD_DATA_SAMPLE_KIND == 0
        line(english ? L"KEYBOARD / MOUSE" : L"键盘 / 鼠标 · 今日次数", 0, 2, {190, 215, 218, 255});
        const auto values =
            (self.counts.status & CD_INPUT_STATS_ACTIVE)
                ? std::to_wstring(self.counts.keyboard_today) + L"   /   " + std::to_wstring(self.counts.mouse_today)
                : L"--   /   --";
        line(values, 1, 2, {75, 227, 179, 255});
#elif CD_DATA_SAMPLE_KIND == 1
        line(english ? L"HOST DATA DASHBOARD" : L"本体数据服务", 0, 4, {190, 215, 218, 255});
        const auto cpu = (self.telemetry.valid_fields & CD_SYSTEM_CPU_VALID)
                             ? std::to_wstring(int(self.telemetry.base.cpu_percent))
                             : L"--";
        line(L"CPU " + cpu + L"%   KEY " + std::to_wstring(self.counts.keyboard_today), 1, 4, {75, 227, 179, 255});
        const auto temperature = self.weatherValue && self.weatherValue->valid()
                                     ? std::to_wstring(int(self.weatherValue->temperatureCelsius))
                                     : L"--";
        const auto aqi = self.airValue && self.airValue->valid() ? std::to_wstring(int(self.airValue->aqi)) : L"--";
        line(L"TEMP " + temperature + L" °C   AQI " + aqi, 2, 4, {103, 182, 255, 255});
        line(L"Open-Meteo / WAQI", 3, 4, {165, 184, 195, 255});
#else
        std::wstring title = english ? L"No media session" : L"暂无播放会话";
        if (self.mediaValue && !self.mediaValue->title.empty()) {
            const auto& text = self.mediaValue->title;
            const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
            title.resize(size);
            MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), title.data(), size);
        }
        line(title, 0, 3, {230, 240, 242, 255});
        line(english ? L"Click: previous / play / next" : L"点击：上一首 / 播放暂停 / 下一首", 1, 3,
             {190, 215, 218, 255});
        for (size_t index = 0; index < self.bands.size(); ++index) {
            const auto column = std::max(1, width / int(self.bands.size()));
            const auto barHeight = int(std::clamp(self.bands[index], 0.f, 1.f) * (height / 3 - 3));
            sample::rectangle(*frame, int(index) * column, height - barHeight, std::max(1, column - 2), barHeight,
                              {75, 227, 179, 255});
        }
#endif
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

void CD_CALL input(void* pointer, const CdInput* value) {
#if CD_DATA_SAMPLE_KIND == 2
    if (!pointer || !value || value->struct_size < sizeof(CdInput) || value->kind != CD_INPUT_RELEASE ||
        !(value->buttons & CD_BUTTON_LEFT))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused || !self.mediaValue)
        return;
    CdMediaServiceApi api{};
    const cdsdk::Services services(&self.completeHost.input.media.base, self.context);
    if (services.query(CD_SERVICE_MEDIA, api) == CD_OK) {
        const auto command = value->x < self.logicalWidth / 3       ? CD_MEDIA_PREVIOUS
                             : value->x > self.logicalWidth * 2 / 3 ? CD_MEDIA_NEXT
                                                                    : CD_MEDIA_TOGGLE;
        const auto flag = command == CD_MEDIA_PREVIOUS ? CD_MEDIA_CAN_PREVIOUS
                          : command == CD_MEDIA_NEXT   ? CD_MEDIA_CAN_NEXT
                                                       : CD_MEDIA_CAN_TOGGLE;
        if (self.mediaValue->controls & flag)
            api.command(self.context, self.media.token(), command, 0);
    }
#else
    (void)pointer;
    (void)value;
#endif
}

void CD_CALL layout(void* pointer, const CdLayout* value) {
    if (pointer && value && value->struct_size >= sizeof(CdLayout)) {
        auto& self = *static_cast<Instance*>(pointer);
        self.logicalWidth = value->logical_width;
        self.redraw();
    }
}

void CD_CALL pause(void* pointer, uint32_t paused) {
    if (!pointer)
        return;
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused == (paused != 0))
        return;
    self.paused = paused != 0;
    self.stop();
    if (!self.paused)
        self.start();
}

CdResult CD_CALL save(void*, uint8_t*, uint64_t, uint64_t* size) {
    if (!size)
        return CD_ERROR_ARGUMENT;
    *size = 0;
    return CD_OK;
}

CdResult CD_CALL restore(void*, const uint8_t*, uint64_t size) {
    return size ? CD_ERROR_ARGUMENT : CD_OK;
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* value, uint32_t size) {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, value, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) {
    if (!pointer)
        return CD_ERROR_ARGUMENT;
    auto& self = *static_cast<Instance*>(pointer);
    const auto result = self.settings.apply(values, count);
    if (result == CD_OK)
        self.redraw();
    return result;
}

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) noexcept {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) || !context->host ||
            context->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            context->host->struct_size < sizeof(CdServiceHostApi) || !context->host->unsubscribe ||
            !context->host->request_redraw || !output || size < sizeof(CdInstanceApi))
            return CD_ERROR_UNSUPPORTED;
        auto self = std::make_unique<Instance>();
        self->completeHost = *reinterpret_cast<const CdServiceHostApi*>(context->host);
        self->host = *context->host;
        self->context = context->host_context;
        // Preserve the full table behind the base pointer passed to Services.
        self->host.struct_size = sizeof(CdHostApi);
        const auto result = self->start();
        if (result != CD_OK)
            return result;
        *output = {sizeof(CdInstanceApi),
                   CD_PLUGIN_ABI_VERSION,
                   self.get(),
                   destroy,
                   render,
                   input,
                   layout,
                   pause,
                   save,
                   restore,
                   get,
                   apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t version, CdPluginApi* output, uint32_t size) {
    if (version != CD_PLUGIN_ABI_VERSION)
        return CD_ERROR_VERSION;
    if (!output || size < sizeof(CdPluginApi))
        return CD_ERROR_BUFFER;
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_DATA_SAMPLE_ID, "1.1.0", create};
    return CD_OK;
}
