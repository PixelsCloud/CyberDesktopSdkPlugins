#include "plugins/mecha-desk/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>

#include "plugins/mecha-desk/Ambience.h"
#include "plugins/mecha-desk/BasicClock.h"
#include "plugins/mecha-desk/BrickClock.h"
#include "plugins/mecha-desk/Player.h"
#include "plugins/mecha-desk/Pond.h"
#include "plugins/mecha-desk/Spectrum.h"
#include "plugins/mecha-desk/controllers/air/AirQuality.h"
#include "plugins/mecha-desk/controllers/calendar/Calendar.h"
#include "plugins/mecha-desk/controllers/hourglass/MechanicalHourglass.h"
#include "plugins/mecha-desk/controllers/lunar/Lunar.h"
#include "plugins/mecha-desk/controllers/matrix/Pixels.h"
#include "plugins/mecha-desk/controllers/monitor/Monitor.h"
#include "plugins/mecha-desk/controllers/precision/Clock.h"
#include "plugins/mecha-desk/controllers/productivity/Productivity.h"
#include "plugins/mecha-desk/controllers/sign/Sign.h"
#include "plugins/mecha-desk/controllers/toys/Toys.h"
#include "plugins/mecha-desk/controllers/weather/Weather.h"
#include "plugins/mecha-desk/controllers/widget/Widget.h"

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
            return create<aeris_precision::Instance>(&local, output, size);
        }
        if (type == "calendar") {
            return create<aeris_calendar::Instance>(&local, output, size);
        }
        if (type == "lunar") {
            return aeris_lunar::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "spring") {
            return create<aeris_toys::Plugin<toys::Kind::Clock>>(&local, output, size);
        }
        if (type == "matrix" && std::string_view(source).find("clock.") != std::string_view::npos) {
            return create<aeris_matrix::Instance>(&local, output, size);
        }
        if (type == "matrix" || type == "armour") {
            return create<aeris_spectrum::MatrixInstance>(&local, output, size);
        }
        if (type == "widget" && (kind == int(widgetplugin::Kind::Bars) || kind == int(widgetplugin::Kind::Radial))) {
            return create<aeris_spectrum::MatrixInstance>(&local, output, size);
        }
        if (type == "widget") {
            return aeris_widget::create({source, widgetplugin::Kind(kind), rows, columns, true}, &local, output, size);
        }
        if (type == "productivity") {
            return aeris_productivity::create({source, productivity::Kind(kind), rows, columns, true}, &local, output,
                                              size);
        }
        if (type == "weather") {
            return aeris_weather::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "air") {
            return aeris_air::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "monitor") {
            return aeris_monitor::create({source, rows, columns, true}, &local, output, size);
        }
        if (type == "basic") {
            return create<aeris_basic::BasicClockInstance>(&local, output, size);
        }
        if (type == "player") {
            return create<aeris_player::PlayerInstance>(&local, output, size);
        }
        if (type == "brick") {
            return create<aeris_brick::Instance>(&local, output, size);
        }
        if (type == "hourglass") {
            return create<aeris_hourglass::Instance>(&local, output, size);
        }
        if (type == "sign") {
            return create<aeris_sign::Instance>(&local, output, size);
        }
        if (type == "pond") {
            return create<aeris_pond::PondInstance>(&local, output, size);
        }
        if (type == "ambience") {
            return aeris_ambience::create({source, atmosphere::Kind(kind), rows, columns}, &local, output, size);
        }
        if (type == "chime") {
            return create<aeris_toys::Plugin<toys::Kind::Chime>>(&local, output, size);
        }
        return CD_ERROR_UNSUPPORTED;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

extern "C" CD_MECHA_RUNTIME_API CdResult CD_CALL cd_artwork_create(const char* family, const char* source, int kind,
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
            CdSettingValue current{sizeof(CdSettingValue)};
            if (output->get_setting(output->instance, key.c_str(), &current, sizeof(current)) != CD_OK) {
                throw std::invalid_argument("Unknown artwork default: " + key);
            }
            if (color.is_string()) {
                values.push_back({sizeof(CdSettingValue), current.type, keys.back().c_str(), 0, 0,
                                  color.get_ref<const std::string&>().c_str()});
            } else {
                values.push_back({sizeof(CdSettingValue), current.type, keys.back().c_str(), color.get<int64_t>(),
                                  color.get<double>()});
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

extern "C" CD_MECHA_RUNTIME_API CdResult CD_CALL cd_mecha_create(const char* family, const char* source, int kind,
                                                                 int rows, int columns, const CdCreateContext* context,
                                                                 CdInstanceApi* output, uint32_t size) {
    return cd_artwork_create(family, source, kind, rows, columns, context, output, size);
}
