#include "plugins/deep-whale/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>

#include "controllers/air/AirQuality.h"
#include "controllers/calendar/Calendar.h"
#include "controllers/hourglass/MechanicalHourglass.h"
#include "controllers/lunar/Lunar.h"
#include "controllers/matrix/Pixels.h"
#include "controllers/monitor/Monitor.h"
#include "controllers/precision/Clock.h"
#include "controllers/productivity/Productivity.h"
#include "controllers/sign/Sign.h"
#include "controllers/weather/Weather.h"
#include "controllers/widget/Widget.h"
#include "plugins/deep-whale/Ambience.h"
#include "plugins/deep-whale/BasicClock.h"
#include "plugins/deep-whale/BrickClock.h"
#include "plugins/deep-whale/Motion.h"
#include "plugins/deep-whale/Player.h"
#include "plugins/deep-whale/Pond.h"
#include "plugins/deep-whale/Spectrum.h"

namespace {
template <class Instance>
CdResult create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    CdPluginApi api{};
    const auto result =
        cdsdk::PluginAdapter<Instance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api), context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
}  // namespace

static CdResult dispatch(const char* family, const char* source, int kind, int rows, int columns,
                         const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    try {
        if (!family || !source || !context || context->struct_size < sizeof(*context)) {
            return CD_ERROR_ARGUMENT;
        }
        auto local = *context;
        local.plugin_id = source;
        const std::string_view type(family);
        if (type == "precision") {
            return create<deep_precision::Instance>(&local, output, size);
        }
        if (type == "calendar") {
            return create<deep_calendar::Instance>(&local, output, size);
        }
        if (type == "lunar") {
            return deep_lunar::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "matrix") {
            return create<deep_spectrum::MatrixInstance>(&local, output, size);
        }
        if (type == "widget" && (kind == int(widgetplugin::Kind::Bars) || kind == int(widgetplugin::Kind::Radial))) {
            return create<deep_spectrum::MatrixInstance>(&local, output, size);
        }
        if (type == "widget") {
            return deep_widget::create({source, widgetplugin::Kind(kind), rows, columns, true}, &local, output, size);
        }
        if (type == "productivity") {
            return deep_productivity::create({source, productivity::Kind(kind), rows, columns, true}, &local, output,
                                             size);
        }
        if (type == "weather") {
            return deep_weather::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "air") {
            return deep_air::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "monitor") {
            return deep_monitor::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "basic") {
            return create<deep_basic::BasicClockInstance>(&local, output, size);
        }
        if (type == "player") {
            return create<deep_player::PlayerInstance>(&local, output, size);
        }
        if (type == "brick") {
            return create<deep_brick::Instance>(&local, output, size);
        }
        if (type == "hourglass") {
            return create<deep_hourglass::Instance>(&local, output, size);
        }
        if (type == "sign") {
            return create<deep_sign::Instance>(&local, output, size);
        }
        if (type == "pond") {
            return create<deep_pond::PondInstance>(&local, output, size);
        }
        if (type == "ambience") {
            return deep_ambience::create({source, atmosphere::Kind(kind), rows, columns}, &local, output, size);
        }
        if (type == "beat" || type == "chime") {
            return create<deep_motion::Instance>(&local, output, size);
        }
        return CD_ERROR_UNSUPPORTED;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

extern "C" CD_DEEP_RUNTIME_API CdResult CD_CALL cd_deep_whale_create(const char* family, const char* source, int kind,
                                                                     int rows, int columns,
                                                                     const CdCreateContext* context,
                                                                     CdInstanceApi* output, uint32_t size) {
    const auto result = dispatch(family, source, kind, rows, columns, context, output, size);
    if (result != CD_OK) {
        return result;
    }
    try {
        const auto assets =
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context->plugin_directory))) /
            "assets";
        std::ifstream file(assets / "skin.json");
        const auto config = nlohmann::json::parse(file);
        if (!config.contains("defaults") || config.at("defaults").empty()) {
            return CD_OK;
        }
        std::vector<std::string> keys;
        std::vector<CdSettingValue> values;
        keys.reserve(config.at("defaults").size());
        values.reserve(keys.capacity());
        for (const auto& [key, color] : config.at("defaults").items()) {
            keys.push_back(key);
            if (color.is_string()) {
                values.push_back({sizeof(CdSettingValue), CD_SETTING_FONT, keys.back().c_str(), 0, 0,
                                  color.get_ref<const std::string&>().c_str()});
            } else {
                values.push_back({sizeof(CdSettingValue), CD_SETTING_COLOR, keys.back().c_str(), color.get<int64_t>()});
            }
        }
        const auto applied = output->apply_settings(output->instance, values.data(), uint32_t(values.size()));
        if (applied == CD_OK) {
            return CD_OK;
        }
        output->destroy(output->instance);
        *output = {};
        return applied;
    } catch (...) {
        output->destroy(output->instance);
        *output = {};
        return CD_ERROR_INTERNAL;
    }
}
