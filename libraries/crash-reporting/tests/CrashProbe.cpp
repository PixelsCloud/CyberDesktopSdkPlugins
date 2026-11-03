#include <Windows.h>

#include <cstdlib>
#include <exception>
#include <string_view>

#include "libraries/crash-reporting/CrashReporter.h"
#include "libraries/crash-reporting/ProcessWatchdog.h"

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::wstring_view mode = argc > 1 ? argv[1] : L"startup";
    if (mode == L"access") {
        auto* page = static_cast<volatile LONG*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
        if (!page) {
            return 3;
        }
        *page = 42;
    } else if (mode == L"terminate") {
        std::terminate();
    } else if (mode == L"abort") {
        std::abort();
    } else if (mode == L"invalid") {
        _invalid_parameter_noinfo_noreturn();
    } else if (mode == L"purecall") {
        _purecall();
    } else if (mode == L"snapshot") {
        return cybercrash::processReporter().snapshot() ? 0 : 4;
    } else if (mode == L"watchdog") {
        cybercrash::ProcessWatchdog watchdog(std::chrono::milliseconds(200));
        watchdog.arm();
        Sleep(INFINITE);
    } else if (mode == L"disarm") {
        cybercrash::ProcessWatchdog watchdog(std::chrono::milliseconds(50));
        watchdog.arm();
        watchdog.disarm();
        Sleep(150);
    } else if (mode == L"renew") {
        cybercrash::ProcessWatchdog watchdog(std::chrono::milliseconds(300));
        for (int index = 0; index < 5; ++index) {
            watchdog.arm();
            Sleep(100);
        }
        watchdog.disarm();
    } else if (mode == L"idle") {
        cybercrash::ProcessWatchdog watchdog(std::chrono::milliseconds(50));
        Sleep(1000);
    } else if (mode != L"startup") {
        return 2;
    }
    return 0;
}
