#pragma once
#include <Windows.h>

#include <map>
#include <string_view>

#include "Machine.h"

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
    };

    struct Status {
        CdAudioStatusCallback callback;
        void* user;
    };

    std::map<uint64_t, Audio> audio;
    std::map<uint64_t, Status> status;
    std::map<uint64_t, Timer> timers;
    uint64_t token = 0, now = GetTickCount64(), redraws = 0;
    bool captured = false, enabled = false, captureAllowed = true;
    CdEnvironmentSnapshot environment{
        sizeof(CdEnvironmentSnapshot), 1, 1760000000000ull, now, 480, CD_ENV_THEME_DARK, "zh-CN"};
    CdServiceHostApi api{};

    Host();

    void send(float value);

    void advance(uint32_t milliseconds);

    CdCreateContext context(const char* id = "com.cyberdesktop.clock.marble.6x2");
};
