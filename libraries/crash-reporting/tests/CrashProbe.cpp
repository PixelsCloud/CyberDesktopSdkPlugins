#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
        if (!cybercrash::processReporter().snapshot()) {
            std::fprintf(stderr, "Snapshot failed: %lu\n", GetLastError());
            return 4;
        }
        return 0;
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
        const auto cpuTime = [] {
            FILETIME created{}, exited{}, kernel{}, user{};
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
            return (ULONGLONG(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime +
                   (ULONGLONG(user.dwHighDateTime) << 32) + user.dwLowDateTime;
        };
        const auto before = cpuTime();
        Sleep(1000);
        std::printf("{\"idle_cpu_ms\":%.3f}\n", double(cpuTime() - before) / 10000.0);
    } else if (mode == L"write-failure" || mode == L"watchdog-write-failure") {
        const auto directory = cybercrash::processReporter().directory();
        if (directory.parent_path().parent_path().filename() != L"crash" ||
            directory.parent_path().parent_path().parent_path().filename() != L"Testing") {
            return 5;
        }
        const auto unavailable = directory.parent_path() / (directory.filename().wstring() + L".unavailable");
        std::filesystem::rename(directory, unavailable);
        if (mode == L"watchdog-write-failure") {
            cybercrash::ProcessWatchdog watchdog(std::chrono::milliseconds(200));
            watchdog.arm();
            Sleep(INFINITE);
        }
        const bool failed = !cybercrash::processReporter().snapshot();
        std::filesystem::rename(unavailable, directory);
        return failed ? 0 : 6;
    } else if (mode != L"startup") {
        return 2;
    }
    return 0;
}
