#include <sdk/PluginApi.h>
#include <sdk/WeatherPresentation.h>
#include <sdk/cpp/PluginDiagnostics.h>
#include <sdk/cpp/Services.h>

#include <iostream>

int main() {
    cdsdk::plugin_detail::PluginDiagnostics diagnostics;
    const CdCreateContext context{};
    diagnostics.bind(context, nullptr, [](void*) {
        return cdsdk::plugin_detail::PhysicsCounters{};
    });
    cdsdk::Services services(nullptr, nullptr);
    CdInputServiceApi input{};
    if (services.query(CD_SERVICE_INPUT, input) != CD_ERROR_UNSUPPORTED || cdsdk::weather::iconName(0, true).empty()) {
        return 1;
    }
    std::cout << "Installed Core services and physics diagnostics without Canvas or private source: PASS\n";
    return 0;
}
