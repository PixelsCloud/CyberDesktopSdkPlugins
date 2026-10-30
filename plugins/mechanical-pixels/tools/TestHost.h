#pragma once

#include <map>
#include <string_view>

#include "plugins/mechanical-pixels/Pixels.h"

namespace mechanicalpixels::testing {
struct Host {
    struct Timer {
        uint32_t interval;
        uint64_t due;
        CdTimerCallback callback;
        void* user;
    };

    struct Audio {
        CdAudioCallback callback;
        void* user;
        uint32_t fps;
    };

    CdServiceHostApi api{};
    std::map<uint64_t, Timer> timers;
    std::map<uint64_t, Audio> audio;
    uint64_t now = 0, next = 0, redraws = 0;
    uint64_t unixMs = 1792663710000ull;
    int32_t offset = 0;
    bool allowTimers = true, allowAudio = true;

    Host();

    CdCreateContext context(const Spec& spec, const char* directory);

    void advance(uint32_t milliseconds);

    void feed(float level);
};
}  // namespace mechanicalpixels::testing
