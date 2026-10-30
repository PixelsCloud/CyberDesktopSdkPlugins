#include "plugins/mechanical-hourglass/tools/TestHost.h"

namespace mechanicalhourglass::testing {
Host::Host() {
    auto& base = api.input.media.base;
    base.struct_size = sizeof(api);
    base.abi_version = 1;
    base.request_redraw = [](void* pointer) {
        ++static_cast<Host*>(pointer)->redraws;
    };
    base.subscribe_timer = [](void* pointer, uint32_t interval, CdTimerCallback callback, void* user,
                              uint64_t* token) -> CdResult {
        auto& self = *static_cast<Host*>(pointer);
        if (!self.allowTimers || !interval || !callback || !token) {
            return CD_ERROR_UNSUPPORTED;
        }
        *token = ++self.next;
        self.timers.emplace(*token, Timer{interval, self.now + interval, callback, user});
        return CD_OK;
    };
    base.unsubscribe = [](void* pointer, uint64_t token) {
        static_cast<Host*>(pointer)->timers.erase(token);
    };
    api.query_service = [](void*, const char* id, uint32_t version, void* output, uint32_t size) -> CdResult {
        if (version == 1 && id && output && std::string_view(id) == CD_SERVICE_ENVIRONMENT &&
            size >= sizeof(CdEnvironmentApi)) {
            *static_cast<CdEnvironmentApi*>(output) = {
                sizeof(CdEnvironmentApi), 1, [](void* pointer, CdEnvironmentSnapshot* value) -> CdResult {
                    const auto& host = *static_cast<Host*>(pointer);
                    *value = {sizeof(*value), 1, 1792663710000ull + host.now, host.now, 0, CD_ENV_THEME_DARK, "zh-CN"};
                    return CD_OK;
                }};
            return CD_OK;
        }
        return CD_ERROR_UNSUPPORTED;
    };
}

auto Host::context(const Spec& spec, const char* directory) -> CdCreateContext {
    return {sizeof(CdCreateContext), 1, spec.id, "test", directory, &api.input.media.base, this};
}

auto Host::advance(uint64_t milliseconds) -> void {
    const auto until = now + milliseconds;
    while (now < until) {
        uint64_t due = until;
        for (const auto& [token, timer] : timers) {
            due = std::min(due, timer.due);
        }
        now = std::max(now + 1, due);
        const auto saved = timers;
        for (const auto& [token, timer] : saved) {
            if (timers.contains(token) && timer.due <= now) {
                timers.at(token).due = now + timer.interval;
                timer.callback(timer.user, now);
            }
        }
    }
}
}  // namespace mechanicalhourglass::testing
