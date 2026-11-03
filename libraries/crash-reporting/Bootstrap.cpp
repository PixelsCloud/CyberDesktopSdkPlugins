#include <Windows.h>

#include "libraries/crash-reporting/CrashReporter.h"

namespace {
struct Bootstrap {
    Bootstrap() noexcept {
        try {
            cybercrash::processReporter();
        } catch (...) {
            OutputDebugStringW(L"CyberDesktop: crash reporting initialization failed\n");
        }
    }
};

Bootstrap bootstrap;
}  // namespace
