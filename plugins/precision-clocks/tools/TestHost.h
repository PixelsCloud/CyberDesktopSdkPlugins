#pragma once

#include <map>
#include <string>

#include "plugins/precision-clocks/Clock.h"

struct Host {
    struct Subscription {
        uint32_t interval;
        uint64_t due;
        CdTimerCallback callback;
        void* user;
    };

    CdServiceHostApi api{};
    CdEnvironmentSnapshot environment{
        sizeof(CdEnvironmentSnapshot), 1, 1792462236000ull, 1000, 480, CD_ENV_THEME_DARK, "zh-CN"};
    std::map<uint64_t, Subscription> timers;
    uint64_t token = 0, redraws = 0;
    bool allowTimers = true;
    std::string directory;

    Host(precisionclock::Kind kind = precisionclock::Kind::Skeleton);

    CdCreateContext context(precisionclock::Kind kind);

    void advance(uint64_t delta);
};
