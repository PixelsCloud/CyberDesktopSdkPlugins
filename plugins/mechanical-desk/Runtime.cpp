#include "plugins/mechanical-desk/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include "plugins/mechanical-desk/BasicClock.h"
#include "plugins/mechanical-desk/Brick.h"
#include "plugins/mechanical-desk/Pond.h"
#include "plugins/mechanical-desk/controllers/ambience/Atmosphere.h"
#include "plugins/mechanical-desk/controllers/productivity/Productivity.h"
#include "plugins/mechanical-desk/controllers/toys/Toys.h"
#include "plugins/mechanical-desk/controllers/widget/Widget.h"

namespace {
template <class Instance>
CdResult create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    CdPluginApi api{};
    const auto result =
        cdsdk::PluginAdapter<Instance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api), context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
}  // namespace

extern "C" CD_MECHANICAL_DESK_API CdResult CD_CALL cd_mechanical_desk_create(const char* family, const char* kind,
                                                                             int rows, int columns,
                                                                             const CdCreateContext* context,
                                                                             CdInstanceApi* output, uint32_t size) try {
    if (!family || !kind || !context || context->struct_size < sizeof(*context) || !context->plugin_id) {
        return CD_ERROR_ARGUMENT;
    }
    const std::string_view type(family), variant(kind);
    if (type == "clock") {
        return create<mechanicaldesk::BasicClockInstance>(context, output, size);
    }
    if (type == "brick") {
        return create<mechanicaldesk::BrickInstance>(context, output, size);
    }
    if (type == "pond") {
        return create<mechanicaldesk::PondInstance>(context, output, size);
    }
    if (type == "chime") {
        return create<mechanicaldesk_toys::Plugin<toys::Kind::Chime>>(context, output, size);
    }
    if (type == "productivity") {
        const auto item = variant == "timer"     ? productivity::Kind::Timer
                          : variant == "disk"    ? productivity::Kind::Disk
                          : variant == "network" ? productivity::Kind::Network
                                                 : productivity::Kind::Countdown;
        return mechanicaldesk_productivity::create({context->plugin_id, item, rows, columns, true}, context, output,
                                                   size);
    }
    if (type == "spectrum") {
        return mechanicaldesk_widget::create({context->plugin_id, widgetplugin::Kind::Bars, rows, columns, true},
                                             context, output, size);
    }
    if (type == "ambience") {
        return mechanicaldesk_ambience::create(
            {context->plugin_id, variant == "plant" ? atmosphere::Kind::Plant : atmosphere::Kind::Noise, rows, columns},
            context, output, size);
    }
    return CD_ERROR_UNSUPPORTED;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
