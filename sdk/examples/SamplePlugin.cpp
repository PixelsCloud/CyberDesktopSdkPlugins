#include <sdk/Canvas.h>
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

#include "examples/Drawing.h"

#ifndef CD_SAMPLE_KIND
#define CD_SAMPLE_KIND 3
#endif
#ifndef CD_SAMPLE_VERSION
#define CD_SAMPLE_VERSION "1.0.0"
#endif
#ifndef CD_SAMPLE_ID
#define CD_SAMPLE_ID "com.example.systeminfo"
#endif
namespace {
cdsdk::Settings initialSettings() {
    if (CD_SAMPLE_KIND == 3)
        return {{{"interval", CD_SETTING_INTEGER, 500, 0, "", 500, 5000},
                 {"memory", CD_SETTING_TOGGLE, 1},
                 {"font_family", CD_SETTING_FONT, 0, 0, "@siyuan-heavy"},
                 {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
    if (CD_SAMPLE_KIND == 4)
        return {{{"gain", CD_SETTING_NUMBER, 0, 1, "", .2, 5},
                 {"color", CD_SETTING_COLOR, 0x46C8AAFF},
                 {"font_family", CD_SETTING_FONT, 0, 0, "@siyuan-heavy"},
                 {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
    return {{{"date", CD_SETTING_TOGGLE, 1},
             {"seconds", CD_SETTING_TOGGLE},
             {"font_family", CD_SETTING_FONT, 0, 0, "@siyuan-heavy"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

CdColor settingColor(int64_t value) {
    const auto color = uint32_t(value);
    return {uint8_t(color >> 24), uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color)};
}

struct Instance {
    cdsdk::Settings settings = initialSettings();
    sample::FontResource font;
    cdsdk::Canvas canvas;
    CdHostApi host{};
    void* context = nullptr;
    uint64_t token = 0;
    uint32_t count = 0;
    bool paused = false, pressed = false;
    CdColor text{235, 240, 245, 255}, accent{70, 200, 170, 255};
    CdSystemInfo system{};
    std::array<float, 128> fft{};
    uint32_t bands = 0;

    ~Instance() {
        if (token && host.unsubscribe) {
            host.unsubscribe(context, token);
        }
    }

    void redraw() {
        if (host.request_redraw)
            host.request_redraw(context);
    }
};

void CD_CALL tick(void* pointer, uint64_t) {
    auto* instance = static_cast<Instance*>(pointer);
    if (!instance->paused)
        instance->redraw();
}

void CD_CALL system(void* pointer, const CdSystemInfo* info) {
    auto* instance = static_cast<Instance*>(pointer);
    if (instance->paused || !info || info->struct_size < sizeof(CdSystemInfo))
        return;
    instance->system = *info;
    instance->redraw();
}

void CD_CALL audio(void* pointer, const float* values, uint32_t count, uint32_t, uint32_t) {
    auto* instance = static_cast<Instance*>(pointer);
    if (instance->paused || !values)
        return;
    instance->bands = std::min(count, uint32_t(instance->fft.size()));
    for (uint32_t i = 0; i < instance->bands; ++i)
        instance->fft[i] = std::clamp(values[i], 0.0f, 1.0f);
    instance->redraw();
}

void CD_CALL destroy(void* pointer) {
    const auto instance = std::unique_ptr<Instance>(static_cast<Instance*>(pointer));
}

void CD_CALL layout(void* pointer, const CdLayout* value) {
    if (!pointer || !value || value->struct_size < sizeof(CdLayout))
        return;
    auto* instance = static_cast<Instance*>(pointer);
    instance->text = value->text_color;
    instance->accent = value->accent_color;
    instance->redraw();
}

void CD_CALL input(void* pointer, const CdInput* event) {
    if (!pointer || !event || event->struct_size < sizeof(CdInput))
        return;
    auto* instance = static_cast<Instance*>(pointer);
    if (instance->paused)
        return;
    if (event->kind == CD_INPUT_PRESS)
        instance->pressed = true;
    if (event->kind == CD_INPUT_LEAVE)
        instance->pressed = false;
    if (event->kind == CD_INPUT_RELEASE && instance->pressed) {
        instance->pressed = false;
        instance->count += 1;
        instance->redraw();
    }
}

void CD_CALL pause(void* pointer, uint32_t value) {
    if (pointer)
        static_cast<Instance*>(pointer)->paused = value != 0;
}

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) {
    if (!pointer || !size)
        return CD_ERROR_ARGUMENT;
    *size = 8;
    if (!bytes && !capacity)
        return CD_OK;
    if (!bytes || capacity < 8)
        return CD_ERROR_BUFFER;
    bytes[0] = 'C';
    bytes[1] = 'D';
    bytes[2] = 1;
    bytes[3] = CD_SAMPLE_KIND;
    const uint32_t count = static_cast<Instance*>(pointer)->count;
    for (int i = 0; i < 4; ++i)
        bytes[i + 4] = uint8_t(count >> (i * 8));
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) {
    if (!pointer || (size && !bytes))
        return CD_ERROR_ARGUMENT;
    auto* instance = static_cast<Instance*>(pointer);
    instance->count = 0;
    if (!size)
        return CD_OK;
    if (size != 8 || bytes[0] != 'C' || bytes[1] != 'D' || bytes[2] != 1 || bytes[3] != CD_SAMPLE_KIND)
        return CD_ERROR_ARGUMENT;
    for (int i = 0; i < 4; ++i)
        instance->count |= uint32_t(bytes[i + 4]) << (i * 8);
    return CD_OK;
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) try {
    if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || frame->width > 4096 ||
        frame->height > 4096 || frame->stride < frame->width * 4 ||
        frame->capacity < uint64_t(frame->stride) * frame->height)
        return CD_ERROR_ARGUMENT;
    auto* instance = static_cast<Instance*>(pointer);
    const int w = int(frame->width), h = int(frame->height);
    auto& canvas = instance->canvas;
    auto drawing = canvas.frame(*frame);
    const double dpi = frame->device_pixel_ratio > 0 ? frame->device_pixel_ratio : 1;
    instance->font.fonts->select(canvas.nativeTextFactory(), instance->settings.at("font_family").text);
    canvas.fontCollection(instance->font.fonts->collection());
    const auto rgba = [](CdColor value) {
        return uint32_t(value.r) << 24 | uint32_t(value.g) << 16 | uint32_t(value.b) << 8 | value.a;
    };
    const auto text = [&](const std::wstring& value, int y, int height, int size, CdColor color) {
        cybercanvas::TextStyle style;
        style.family = instance->font.fonts->family();
        style.weight = cybercanvas::FontWeight(instance->font.fonts->weight());
        style.size = size / dpi;
        style.align = cybercanvas::TextAlign::Center;
        style.ellipsis = true;
        canvas.text(value, style, CdRect{0, y / dpi, w / dpi, height / dpi}, rgba(color));
    };
    const auto rectangle = [&](int x, int y, int width, int height, CdColor color) {
        canvas.rectangle({x / dpi, y / dpi, width / dpi, height / dpi}, rgba(color));
    };
    const auto color = instance->text;
    const int margin = std::max(4, w / 12);
    if (CD_SAMPLE_KIND == 3) {
        const auto cpu = std::clamp(instance->system.cpu_percent, 0.0, 100.0),
                   gpu = std::clamp(instance->system.gpu_percent, 0.0, 100.0);
        const auto ram = instance->system.memory_total_bytes
                             ? 100.0 * instance->system.memory_used_bytes / instance->system.memory_total_bytes
                             : 0;
        text(L"CPU " + std::to_wstring(int(cpu)) + L"%   GPU " + std::to_wstring(int(gpu)) + L"%", h / 8, h / 3,
             std::max(10, h / 6), color);
        if (instance->settings.at("memory").integer)
            rectangle(margin, h * 5 / 8, w - margin * 2, std::max(3, h / 14), {color.r, color.g, color.b, 45});
        if (instance->settings.at("memory").integer)
            rectangle(margin, h * 5 / 8, int((w - margin * 2) * std::clamp(ram, 0.0, 100.0) / 100), std::max(3, h / 14),
                      instance->accent);
    } else if (CD_SAMPLE_KIND == 4) {
        const uint32_t count = instance->bands ? instance->bands : 32;
        const int unit = std::max(2, (w - margin * 2) / int(count));
        for (uint32_t i = 0; i < count; ++i) {
            const int height = std::max(
                2,
                int((h - margin * 2) * std::clamp(instance->fft[i] * instance->settings.at("gain").number, 0.0, 1.0)));
            rectangle(margin + int(i) * unit, h - margin - height, std::max(1, unit - 2), height,
                      settingColor(instance->settings.at("color").integer));
        }
    } else {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t buffer[32]{};
        uint32_t hour = time.wHour;
        swprintf_s(buffer, L"%02u:%02u", hour, uint32_t(time.wMinute));
        if (instance->settings.at("seconds").integer)
            swprintf_s(buffer, L"%02u:%02u:%02u", hour, uint32_t(time.wMinute), uint32_t(time.wSecond));
        text(buffer, h / 5, h / 2, std::max(12, h / 3), color);
        if (CD_SAMPLE_KIND == 5 && instance->settings.at("date").integer) {
            swprintf_s(buffer,
                       instance->settings.at("language").text == "en" ? L"%04u-%02u-%02u" : L"%04u年%02u月%02u日",
                       uint32_t(time.wYear), uint32_t(time.wMonth), uint32_t(time.wDay));
            text(buffer, h * 2 / 3, h / 5, std::max(10, h / 9), color);
        }
    }
    drawing.finish();
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL getSetting(void* pointer, const char* id, CdSettingValue* output, uint32_t size) {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, output, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL applySettings(void* pointer, const CdSettingValue* values, uint32_t count) try {
    if (!pointer)
        return CD_ERROR_ARGUMENT;
    auto* self = static_cast<Instance*>(pointer);
    auto previous = self->settings;
    const auto result = self->settings.apply(values, count);
    if (result != CD_OK)
        return result;
    if (CD_SAMPLE_KIND == 3) {
        uint64_t token = 0;
        const auto subscribed = self->host.subscribe_system(
            self->context, uint32_t(self->settings.at("interval").integer), system, self, &token);
        if (subscribed != CD_OK) {
            self->settings = std::move(previous);
            return subscribed;
        }
        self->host.unsubscribe(self->context, self->token);
        self->token = token;
    }
    self->redraw();
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) try {
    if (!context || context->struct_size < sizeof(CdCreateContext) || context->abi_version != CD_PLUGIN_ABI_VERSION ||
        !context->host || context->host->struct_size < sizeof(CdHostApi) ||
        context->host->abi_version != CD_PLUGIN_ABI_VERSION || !output || size < sizeof(CdInstanceApi))
        return CD_ERROR_ARGUMENT;
    auto instance = std::make_unique<Instance>();
    instance->host = *context->host;
    instance->context = context->host_context;
    instance->canvas.bindGpu(*context);
    CdResult subscribed = CD_OK;
    if (CD_SAMPLE_KIND == 5 && instance->host.subscribe_timer)
        subscribed = instance->host.subscribe_timer(instance->context, 1000, tick, instance.get(), &instance->token);
    if (CD_SAMPLE_KIND == 3) {
        if (!instance->host.subscribe_system) {
            return CD_ERROR_VERSION;
        }
        subscribed = instance->host.subscribe_system(instance->context, 500, system, instance.get(), &instance->token);
    }
    if (CD_SAMPLE_KIND == 4) {
        if (!instance->host.subscribe_audio) {
            return CD_ERROR_VERSION;
        }
        subscribed = instance->host.subscribe_audio(instance->context, 32, audio, instance.get(), &instance->token);
    }
    if (subscribed != CD_OK) {
        return subscribed;
    }
    *output = {sizeof(CdInstanceApi),
               CD_PLUGIN_ABI_VERSION,
               instance.get(),
               destroy,
               render,
               input,
               layout,
               pause,
               save,
               restore,
               getSetting,
               applySettings};
    instance.release();
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION)
        return CD_ERROR_VERSION;
    if (!output || size < sizeof(CdPluginApi))
        return CD_ERROR_BUFFER;
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_SAMPLE_ID, CD_SAMPLE_VERSION, create};
    return CD_OK;
}
