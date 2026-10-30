#include "plugins/precision-clocks/tools/TestHost.h"

Host::Host(precisionclock::Kind kind) {
    const auto path = std::filesystem::path(CD_PRECISION_PLUGINS) / precisionclock::specification(kind).id;
    const auto utf8 = path.u8string();
    directory.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    auto& base = api.input.media.base;
    base.struct_size = sizeof(api);
    base.abi_version = 1;
    base.request_redraw = [](void* pointer) {
        ++static_cast<Host*>(pointer)->redraws;
    };
    base.subscribe_timer = [](void* pointer, uint32_t interval, CdTimerCallback callback, void* user,
                              uint64_t* output) -> CdResult {
        auto& host = *static_cast<Host*>(pointer);
        if (interval < 16 || interval > 3600000 || !callback || !output) {
            return CD_ERROR_ARGUMENT;
        }
        if (!host.allowTimers) {
            return CD_ERROR_UNSUPPORTED;
        }
        *output = ++host.token;
        host.timers.emplace(*output, Subscription{interval, host.environment.monotonic_ms + interval, callback, user});
        return CD_OK;
    };
    base.unsubscribe = [](void* pointer, uint64_t token) {
        static_cast<Host*>(pointer)->timers.erase(token);
    };
    api.query_service = [](void*, const char* name, uint32_t version, void* output, uint32_t size) -> CdResult {
        if (version == 1 && std::string_view(name) == CD_SERVICE_ENVIRONMENT && size >= sizeof(CdEnvironmentApi)) {
            *static_cast<CdEnvironmentApi*>(output) = {sizeof(CdEnvironmentApi), 1,
                                                       [](void* pointer, CdEnvironmentSnapshot* output) -> CdResult {
                                                           *output = static_cast<Host*>(pointer)->environment;
                                                           return CD_OK;
                                                       }};
            return CD_OK;
        }
        return CD_ERROR_UNSUPPORTED;
    };
}

auto Host::context(precisionclock::Kind kind) -> CdCreateContext {
    return {sizeof(CdCreateContext), 1,   precisionclock::specification(kind).id, "test", directory.c_str(),
            &api.input.media.base,   this};
}

auto Host::advance(uint64_t delta) -> void {
    environment.unix_ms += delta;
    environment.monotonic_ms += delta;
    const auto snapshot = timers;
    for (const auto& [id, timer] : snapshot) {
        if (timers.contains(id) && environment.monotonic_ms >= timer.due) {
            timers.at(id).due = environment.monotonic_ms + timer.interval;
            timer.callback(timer.user, environment.monotonic_ms);
        }
    }
}