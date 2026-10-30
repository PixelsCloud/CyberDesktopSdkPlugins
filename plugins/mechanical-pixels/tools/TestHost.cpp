#include "plugins/mechanical-pixels/tools/TestHost.h"

namespace mechanicalpixels::testing {
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
        auto& self = *static_cast<Host*>(pointer);
        self.timers.erase(token);
        self.audio.erase(token);
    };
    api.query_service = [](void*, const char* id, uint32_t version, void* output, uint32_t size) -> CdResult {
        if (version != 1 || !id || !output) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (std::string_view(id) == CD_SERVICE_ENVIRONMENT && size >= sizeof(CdEnvironmentApi)) {
            *static_cast<CdEnvironmentApi*>(output) = {
                sizeof(CdEnvironmentApi), 1, [](void* pointer, CdEnvironmentSnapshot* value) -> CdResult {
                    const auto& host = *static_cast<Host*>(pointer);
                    *value = {sizeof(*value),    1,      host.unixMs + host.now, host.now, host.offset,
                              CD_ENV_THEME_DARK, "zh-CN"};
                    return CD_OK;
                }};
            return CD_OK;
        }
        if (std::string_view(id) == CD_SERVICE_FFT && size >= sizeof(CdAudioServiceApi)) {
            *static_cast<CdAudioServiceApi*>(output) = {
                sizeof(CdAudioServiceApi), 1,
                [](void* pointer, const CdAudioOptions* options, CdAudioCallback callback, void* user,
                   uint64_t* token) -> CdResult {
                    auto& host = *static_cast<Host*>(pointer);
                    if (!host.allowAudio || !options || !callback || !token) {
                        return CD_ERROR_UNSUPPORTED;
                    }
                    *token = ++host.next;
                    host.audio.emplace(*token, Audio{callback, user, options->max_fps});
                    return CD_OK;
                },
                nullptr};
            return CD_OK;
        }
        return CD_ERROR_UNSUPPORTED;
    };
}

auto Host::context(const Spec& spec, const char* directory) -> CdCreateContext {
    return {sizeof(CdCreateContext), 1, spec.id, "test", directory, &api.input.media.base, this};
}

auto Host::advance(uint32_t milliseconds) -> void {
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

auto Host::feed(float level) -> void {
    std::array<float, widgetplugin::Spectrum::count> values;
    values.fill(level);
    const auto saved = audio;
    for (const auto& [token, item] : saved) {
        if (audio.contains(token)) {
            item.callback(item.user, values.data(), uint32_t(values.size()), 48000, 1);
        }
    }
}
}  // namespace mechanicalpixels::testing
