#include "plugins/songyun-desk/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include <string_view>

#include "controllers/air/AirQuality.h"
#include "controllers/beat/Machine.h"
#include "controllers/calendar/Calendar.h"
#include "controllers/hourglass/MechanicalHourglass.h"
#include "controllers/lunar/Lunar.h"
#include "controllers/marble/Clock.h"
#include "controllers/matrix/Pixels.h"
#include "controllers/monitor/Monitor.h"
#include "controllers/precision/Clock.h"
#include "controllers/productivity/Productivity.h"
#include "controllers/sign/Sign.h"
#include "controllers/toys/Toys.h"
#include "controllers/weather/Weather.h"
#include "controllers/widget/Widget.h"

namespace {
template <class Instance>
CdResult create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    CdPluginApi api{};
    const auto result =
        cdsdk::PluginAdapter<Instance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api), context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
}  // namespace

extern "C" CD_SONGYUN_API CdResult CD_CALL cd_songyun_create(const char* family, const char* source, int kind, int rows,
                                                             int columns, const CdCreateContext* context,
                                                             CdInstanceApi* output, uint32_t size) try {
    if (!family || !source || !context || context->struct_size < sizeof(*context)) {
        return CD_ERROR_ARGUMENT;
    }
    auto local = *context;
    local.plugin_id = source;
    const std::string_view type(family);
    if (type == "matrix") {
        return create<songyun_matrix::Instance>(&local, output, size);
    }
    if (type == "precision") {
        return create<songyun_precision::Instance>(&local, output, size);
    }
    if (type == "toys") {
        if (std::string_view(source).find("windchime") != std::string_view::npos) {
            return create<songyun_toys::Plugin<toys::Kind::Chime>>(&local, output, size);
        }
        return create<songyun_toys::Plugin<toys::Kind::Clock>>(&local, output, size);
    }
    if (type == "marble") {
        return create<songyun_marble::Instance>(&local, output, size);
    }
    if (type == "beat") {
        return create<songyun_beat::Instance>(&local, output, size);
    }
    if (type == "calendar") {
        return create<songyun_calendar::Instance>(&local, output, size);
    }
    if (type == "hourglass") {
        return create<songyun_hourglass::Instance>(&local, output, size);
    }
    if (type == "sign") {
        return create<songyun_sign::Instance>(&local, output, size);
    }
    if (type == "widget") {
        return songyun_widget::create({source, widgetplugin::Kind(kind), rows, columns, true}, &local, output, size);
    }
    if (type == "productivity") {
        return songyun_productivity::create({source, productivity::Kind(kind), rows, columns, true}, &local, output,
                                            size);
    }
    if (type == "weather") {
        return songyun_weather::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "air") {
        return songyun_air::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "lunar") {
        return songyun_lunar::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "monitor") {
        return songyun_monitor::create({source, rows, columns, true}, &local, output, size);
    }
    return CD_ERROR_UNSUPPORTED;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
