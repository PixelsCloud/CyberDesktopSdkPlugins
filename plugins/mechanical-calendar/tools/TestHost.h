#pragma once

#include <algorithm>
#include <map>
#include <string_view>

#include "plugins/mechanical-calendar/Calendar.h"

namespace mechanicalcalendar::testing {
struct Host {
    struct Timer {
        uint32_t interval;
        uint64_t due;
        CdTimerCallback callback;
        void* user;
    };

    CdServiceHostApi api{};
    CdIconsApi iconApi{};
    std::map<uint64_t, Timer> timers;
    uint64_t now = 0, next = 0, redraws = 0, iconCalls = 0, callbacks = 0;
    uint64_t unixMs = 1791849600000ull;
    int32_t utcOffset = 0;
    bool allowTimers = true;

    Host();

    CdCreateContext context(const char* directory);

    void advance(uint64_t milliseconds);
};
}  // namespace mechanicalcalendar::testing
