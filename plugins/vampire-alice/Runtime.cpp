#include "plugins/vampire-alice/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include <string_view>

#include "plugins/vampire-alice/controllers/air/AirQuality.h"
#include "plugins/vampire-alice/controllers/beat/Machine.h"
#include "plugins/vampire-alice/controllers/calendar/Calendar.h"
#include "plugins/vampire-alice/controllers/hourglass/MechanicalHourglass.h"
#include "plugins/vampire-alice/controllers/lunar/Lunar.h"
#include "plugins/vampire-alice/controllers/marble/Clock.h"
#include "plugins/vampire-alice/controllers/matrix/Pixels.h"
#include "plugins/vampire-alice/controllers/monitor/Monitor.h"
#include "plugins/vampire-alice/controllers/precision/Clock.h"
#include "plugins/vampire-alice/controllers/productivity/Productivity.h"
#include "plugins/vampire-alice/controllers/sign/Sign.h"
#include "plugins/vampire-alice/controllers/toys/Toys.h"
#include "plugins/vampire-alice/controllers/weather/Weather.h"
#include "plugins/vampire-alice/controllers/widget/Widget.h"

namespace {
template <class Instance>
CdResult create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    CdPluginApi api{};
    const auto result =
        cdsdk::PluginAdapter<Instance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api), context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
}  // namespace

extern "C" CD_ALICE_API CdResult CD_CALL cd_alice_create(const char* family, const char* source, int kind, int rows,
                                                         int columns, const CdCreateContext* context,
                                                         CdInstanceApi* output, uint32_t size) try {
    if (!family || !source || !context || context->struct_size < sizeof(*context)) {
        return CD_ERROR_ARGUMENT;
    }
    auto local = *context;
    local.plugin_id = source;
    const std::string_view type(family);
    if (type == "matrix") {
        return create<alice_matrix::Instance>(&local, output, size);
    }
    if (type == "precision") {
        return create<alice_precision::Instance>(&local, output, size);
    }
    if (type == "toys") {
        if (std::string_view(source).find("windchime") != std::string_view::npos) {
            return create<alice_toys::Plugin<toys::Kind::Chime>>(&local, output, size);
        }
        return create<alice_toys::Plugin<toys::Kind::Clock>>(&local, output, size);
    }
    if (type == "marble") {
        return create<alice_marble::Instance>(&local, output, size);
    }
    if (type == "beat") {
        return create<alice_beat::Instance>(&local, output, size);
    }
    if (type == "calendar") {
        return create<alice_calendar::Instance>(&local, output, size);
    }
    if (type == "hourglass") {
        return create<alice_hourglass::Instance>(&local, output, size);
    }
    if (type == "sign") {
        return create<alice_sign::Instance>(&local, output, size);
    }
    if (type == "widget") {
        return alice_widget::create({source, widgetplugin::Kind(kind), rows, columns, true}, &local, output, size);
    }
    if (type == "productivity") {
        return alice_productivity::create({source, productivity::Kind(kind), rows, columns, true}, &local, output,
                                          size);
    }
    if (type == "weather") {
        return alice_weather::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "air") {
        return alice_air::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "lunar") {
        return alice_lunar::create({source, rows, columns, true}, &local, output, size);
    }
    if (type == "monitor") {
        return alice_monitor::create({source, rows, columns, true}, &local, output, size);
    }
    return CD_ERROR_UNSUPPORTED;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
