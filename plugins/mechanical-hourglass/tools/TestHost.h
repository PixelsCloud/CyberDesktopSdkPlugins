#pragma once

#include <algorithm>
#include <map>
#include <string_view>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"

namespace mechanicalhourglass::testing {
struct Host {
    struct Timer {
        uint32_t interval;
        uint64_t due;
        CdTimerCallback callback;
        void* user;
    };

    CdServiceHostApi api{};
    std::map<uint64_t, Timer> timers;
    uint64_t now = 0, next = 0, redraws = 0;
    bool allowTimers = true;

    Host();

    CdCreateContext context(const Spec& spec, const char* directory);

    void advance(uint64_t milliseconds);
};
}  // namespace mechanicalhourglass::testing
