#pragma once

#include <map>
#include <string_view>

#include "plugins/mechanical-sign/Sign.h"

struct Host {
    struct Timer {
        uint32_t interval;
        uint64_t due;
        CdTimerCallback callback;
        void* user;
    };

    CdServiceHostApi api{};
    std::map<uint64_t, Timer> timers;
    uint64_t now = GetTickCount64(), token = 0, redraws = 0;
    bool captured = false, allowTimers = true;

    Host();

    CdCreateContext context();

    void advance(uint32_t delta);
};
