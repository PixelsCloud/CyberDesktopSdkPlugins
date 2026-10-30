#include "plugins/redline-desk/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include <string_view>

#include "plugins/redline-desk/Kinetics.h"
#include "plugins/redline-desk/controllers/air/AirQuality.h"
#include "plugins/redline-desk/controllers/beat/Machine.h"
#include "plugins/redline-desk/controllers/calendar/Calendar.h"
#include "plugins/redline-desk/controllers/hourglass/MechanicalHourglass.h"
#include "plugins/redline-desk/controllers/lunar/Lunar.h"
#include "plugins/redline-desk/controllers/marble/Clock.h"
#include "plugins/redline-desk/controllers/matrix/Pixels.h"
#include "plugins/redline-desk/controllers/monitor/Monitor.h"
#include "plugins/redline-desk/controllers/precision/Clock.h"
#include "plugins/redline-desk/controllers/productivity/Productivity.h"
#include "plugins/redline-desk/controllers/sign/Sign.h"
#include "plugins/redline-desk/controllers/toys/Toys.h"
#include "plugins/redline-desk/controllers/weather/Weather.h"
#include "plugins/redline-desk/controllers/widget/Widget.h"

namespace {
template <class Instance>
CdResult create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    CdPluginApi api{};
    const auto result =
        cdsdk::PluginAdapter<Instance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api), context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
}  // namespace

extern "C" CD_REDLINE_API CdResult CD_CALL cd_redline_create(const char* family, const char* source, int kind, int rows,
                                                             int columns, const CdCreateContext* context,
                                                             CdInstanceApi* output, uint32_t size) try {
    if (!family || !source || !context || context->struct_size < sizeof(*context)) {
        return CD_ERROR_ARGUMENT;
    }
    auto local = *context;
    local.plugin_id = source;
    const std::string_view type(family);
    if (type == "kinetic") {
        return redline::createKinetic(kind, &local, output, size);
    }
    if (type == "matrix") {
        return create<redline_matrix::Instance>(&local, output, size);
    }
    if (type == "precision") {
        return create<redline_precision::Instance>(&local, output, size);
    }
    if (type == "toys") {
        if (std::string_view(source).find("windchime") != std::string_view::npos) {
            return create<redline_toys::Plugin<toys::Kind::Chime>>(&local, output, size);
        }
        return create<redline_toys::Plugin<toys::Kind::Clock>>(&local, output, size);
    }
    if (type == "marble") {
        return create<redline_marble::Instance>(&local, output, size);
    }
    if (type == "beat") {
        return create<redline_beat::Instance>(&local, output, size);
    }
    if (type == "calendar") {
        return create<redline_calendar::Instance>(&local, output, size);
    }
    if (type == "hourglass") {
        return create<redline_hourglass::Instance>(&local, output, size);
    }
    if (type == "sign") {
        return create<redline_sign::Instance>(&local, output, size);
    }
    if (type == "widget") {
        return redline_widget::create({source, widgetplugin::Kind(kind), rows, columns, true}, &local, output, size);
    }
    if (type == "productivity") {
        return redline_productivity::create({source, productivity::Kind(kind), rows, columns, true}, &local, output,
                                            size);
    }
    if (type == "weather") {
        return redline_weather::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "air") {
        return redline_air::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "lunar") {
        return redline_lunar::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "monitor") {
        return redline_monitor::create({source, rows, columns, true}, &local, output, size);
    }
    return CD_ERROR_UNSUPPORTED;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
