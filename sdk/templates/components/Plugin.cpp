#include <sdk/cpp/Plugin.h>

#include "templates/components/Card.h"

#ifndef CD_COMPONENT_KIND
#define CD_COMPONENT_KIND 0
#endif

class Starter final : public componentdemo::Card {
public:
    explicit Starter(const CdCreateContext& context) : Card(context, CD_COMPONENT_KIND) {}
};

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<Starter>::query(abi, output, size, "com.example.components", "1.0.0");
}
