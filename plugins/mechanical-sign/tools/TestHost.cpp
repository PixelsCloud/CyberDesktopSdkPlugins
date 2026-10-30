#include "plugins/mechanical-sign/tools/TestHost.h"

Host::Host() {
    auto& base = api.input.media.base;
    base.struct_size = sizeof(api);
    base.abi_version = 1;
    base.request_redraw = [](void* value) {
        ++static_cast<Host*>(value)->redraws;
    };
    base.subscribe_timer = [](void* value, uint32_t interval, CdTimerCallback callback, void* user,
                              uint64_t* out) -> CdResult {
        auto& self = *static_cast<Host*>(value);
        if (!self.allowTimers) {
            return CD_ERROR_UNSUPPORTED;
        }
        *out = ++self.token;
        self.timers.emplace(*out, Timer{interval, self.now + interval, callback, user});
        return CD_OK;
    };
    base.unsubscribe = [](void* value, uint64_t token) {
        static_cast<Host*>(value)->timers.erase(token);
    };
    api.query_service = [](void*, const char* name, uint32_t version, void* out, uint32_t size) -> CdResult {
        if (version != 1) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (std::string_view(name) == CD_SERVICE_ENVIRONMENT && size >= sizeof(CdEnvironmentApi)) {
            *static_cast<CdEnvironmentApi*>(out) = {sizeof(CdEnvironmentApi), 1,
                                                    [](void* value, CdEnvironmentSnapshot* result) -> CdResult {
                                                        *result = {sizeof(CdEnvironmentSnapshot),
                                                                   1,
                                                                   1760000000000ull,
                                                                   static_cast<Host*>(value)->now,
                                                                   480,
                                                                   CD_ENV_THEME_DARK,
                                                                   "zh-CN"};
                                                        return CD_OK;
                                                    }};
            return CD_OK;
        }
        if (std::string_view(name) == CD_SERVICE_INTERACTION && size >= sizeof(CdInteractionApi)) {
            *static_cast<CdInteractionApi*>(out) = {sizeof(CdInteractionApi), 1,
                                                    [](void*, uint32_t) -> CdResult {
                                                        return CD_OK;
                                                    },
                                                    [](void* value, uint32_t capture) -> CdResult {
                                                        static_cast<Host*>(value)->captured = capture != 0;
                                                        return CD_OK;
                                                    }};
            return CD_OK;
        }
        return CD_ERROR_UNSUPPORTED;
    };
}

auto Host::context() -> CdCreateContext {
    return {sizeof(CdCreateContext), 1, mechanicalsign::kId, "test", CD_SIGN_TEST_ROOT, &api.input.media.base, this};
}

auto Host::advance(uint32_t delta) -> void {
    now += delta;
    const auto copy = timers;
    for (const auto& [id, timer] : copy) {
        if (timers.contains(id) && now >= timer.due) {
            timers.at(id).due = now + timer.interval;
            timer.callback(timer.user, now);
        }
    }
}