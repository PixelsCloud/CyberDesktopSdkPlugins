// SPDX-License-Identifier: GPL-3.0-only
#include <KNSoft/NDK/NDK.h>
#include <KNSoft/SlimDetours/SlimDetours.h>
#include <Windows.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include "libraries/dwmglass/host/RemoteCall.h"
#include "libraries/dwmglass/src/DwmLifecycleLock.h"
#include "libraries/dwmglass/src/HookTransactionPolicy.h"
#include "third_party/OpenGlass/Common/HookHelper.hpp"

namespace {
struct Section {
    CRITICAL_SECTION value{};

    Section() {
        InitializeCriticalSection(&value);
    }

    ~Section() {
        DeleteCriticalSection(&value);
    }
};

struct Payload {
    DWORD delay = 0;
    DWORD value = 0;
};

DWORD WINAPI finish(void* pointer) {
    auto& payload = *static_cast<Payload*>(pointer);
    Sleep(payload.delay);
    payload.value = 42;
    return 42;
}

int pointerOriginal() {
    return 17;
}

int pointerReplacement() {
    return 23;
}

void legacyDeadlock() {
    Section compositor;
    wil::unique_handle entered(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle transaction(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    std::jthread render([&] {
        const auto lock = wil::EnterCriticalSection(&compositor.value);
        SetEvent(entered.get());
        WaitForSingleObject(transaction.get(), INFINITE);
        OpenGlass::HookHelper::HookTransaction lazy(OpenGlass::HookHelper::HookMode::Install);
    });
    WaitForSingleObject(entered.get(), INFINITE);
    OpenGlass::HookHelper::HookTransaction control(OpenGlass::HookHelper::HookMode::Install);
    SetEvent(transaction.get());
    const auto lock = wil::EnterCriticalSection(&compositor.value);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--legacy-deadlock") {
        legacyDeadlock();
        return 1;
    }
    int failures = 0;
    const auto check = [&](bool passed, const char* name) {
        failures += !passed;
        std::cout << (passed ? "PASS " : "FAIL ") << name << std::endl;
    };
    using cyberglass::dwm::CompositorAccess;
    using cyberglass::dwm::host::RemoteCall;
    // Reproduce only in this console process: the default transaction freezes
    // a collaborating thread even when no code is being patched.
    for (const bool legacy : {true, false}) {
        wil::unique_handle proceed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        wil::unique_handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        std::jthread collaborator([&] {
            WaitForSingleObject(proceed.get(), INFINITE);
            SetEvent(completed.get());
        });
        const HRESULT started = legacy ? SlimDetoursTransactionBegin() : cyberglass::dwm::beginHookTransaction(0, 0);
        if (SUCCEEDED(started)) {
            SetEvent(proceed.get());
            const auto result = WaitForSingleObject(completed.get(), 100);
            SlimDetoursTransactionAbort();
            const DWORD expected = legacy ? DWORD(WAIT_TIMEOUT) : DWORD(WAIT_OBJECT_0);
            check(result == expected, legacy ? "legacy pointer transaction suspends a collaborating thread"
                                             : "pointer-only transaction leaves collaborating threads running");
        } else {
            SetEvent(proceed.get());
            check(false, "owned transaction starts successfully");
        }
    }
    auto slot = &pointerOriginal;
    decltype(slot) original = nullptr;
    OpenGlass::HookHelper::GetHookRundown().Open();
    OpenGlass::HookHelper::PointerHook<&pointerReplacement> pointerHook;
    pointerHook.AttachOnce(&slot, &original);
    check(slot() == 23 && original() == 17, "actual lazy pointer hook installs with the non-suspending policy");
    {
        OpenGlass::HookHelper::HookTransaction transaction(OpenGlass::HookHelper::HookMode::Remove);
        transaction.Apply(pointerHook);
        transaction.Commit();
    }
    check(slot() == 17 && !pointerHook.IsInstalled(), "actual pointer hook removal restores the original function");
    std::array<wchar_t, 32768> path{};
    GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    std::wstring command = L"\"" + std::wstring(path.data()) + L"\" --legacy-deadlock";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    const bool created = CreateProcessW(path.data(), command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &child);
    check(created, "owned windowless ABBA regression child starts");
    if (created) {
        wil::unique_handle process(child.hProcess), thread(child.hThread);
        wil::unique_handle job(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool contained =
            job && SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
            AssignProcessToJobObject(job.get(), process.get());
        check(contained, "regression child is confined to its owned kill-on-close job");
        if (contained) {
            ResumeThread(thread.get());
            check(WaitForSingleObject(process.get(), 500) == WAIT_TIMEOUT,
                  "old compositor-to-transaction inversion reproduces a deadlock without touching DWM");
        }
        TerminateProcess(process.get(), 0);
        WaitForSingleObject(process.get(), 2000);
    }
    Section compositor;
    wil::unique_handle held(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle proceed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    std::jthread render([&] {
        const auto lock = wil::EnterCriticalSection(&compositor.value);
        SetEvent(held.get());
        WaitForSingleObject(proceed.get(), 2000);
        OpenGlass::HookHelper::HookTransaction lazy(OpenGlass::HookHelper::HookMode::Install);
    });
    WaitForSingleObject(held.get(), 2000);
    const auto started = std::chrono::steady_clock::now();
    {
        const CompositorAccess access(&compositor.value);
        check(!access, "busy compositor returns before acquiring a hook transaction");
    }
    check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(50),
          "busy lifecycle operation is nonblocking");
    SetEvent(proceed.get());
    render.join();
    {
        const CompositorAccess access(&compositor.value);
        check(bool(access), "compositor access succeeds after the renderer finishes");
        OpenGlass::HookHelper::HookTransaction control(OpenGlass::HookHelper::HookMode::Install);
    }
    Payload input{100, 0}, output{};
    RemoteCall call(GetCurrentProcess());
    bool timedOut = false;
    try {
        call.invoke(uintptr_t(&finish), std::as_bytes(std::span(&input, 1)), {}, 2);
    } catch (const winrt::hresult_error& error) {
        timedOut = error.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    check(timedOut && call.pending(), "actual owned remote thread returns a bounded timeout");
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(reinterpret_cast<void*>(call.pendingArgument()), &memory, sizeof(memory));
    check(memory.State == MEM_COMMIT, "timed-out remote argument remains allocated while the thread uses it");
    bool busy = false;
    try {
        call.invoke(uintptr_t(&finish), std::as_bytes(std::span(&input, 1)), {}, 2);
    } catch (const winrt::hresult_error& error) {
        busy = error.code() == HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    check(busy, "a pending operation cannot queue another remote thread");
    Sleep(150);
    input.delay = 0;
    check(call.invoke(uintptr_t(&finish), std::as_bytes(std::span(&input, 1)),
                      std::as_writable_bytes(std::span(&output, 1))) == 42 &&
              output.value == 42,
          "completion reclaims the previous buffer and safely returns output for a fresh call");
    check(!call.pending() && !call.pendingArgument(), "completed operations release their native resources");
    std::cout << "failures=" << failures << "\n";
    return failures ? 1 : 0;
}
