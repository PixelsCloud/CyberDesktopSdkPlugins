// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/host/RemoteCall.h"

#include <Windows.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <stdexcept>

namespace cyberglass::dwm::host {
struct RemoteCall::Impl {
    wil::unique_handle process;
    wil::unique_handle thread;
    void* argument = nullptr;

    explicit Impl(HANDLE source) {
        winrt::check_bool(DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), process.put(), 0, FALSE,
                                          DUPLICATE_SAME_ACCESS));
    }

    ~Impl() {
        // Never free an argument still used by a remote thread. If the host
        // exits during a stuck operation, retain this bounded allocation until
        // the target process exits. Do not terminate or unload the target.
        if (!thread || WaitForSingleObject(thread.get(), 0) == WAIT_OBJECT_0) {
            releaseArgument();
        }
    }

    void releaseArgument() {
        if (argument) {
            VirtualFreeEx(process.get(), argument, 0, MEM_RELEASE);
            argument = nullptr;
        }
    }
};

RemoteCall::RemoteCall(void* process) : impl_(std::make_unique<Impl>(process)) {}

RemoteCall::~RemoteCall() = default;

uint32_t RemoteCall::invoke(uintptr_t address, std::span<const std::byte> input, std::span<std::byte> output,
                            uint32_t timeoutMs) {
    auto& state = *impl_;
    if (!address || input.size() > 65536 || output.size() > input.size() || !timeoutMs || timeoutMs > 5000) {
        throw std::invalid_argument("Invalid bounded remote operation");
    }
    if (state.thread) {
        if (WaitForSingleObject(state.thread.get(), 0) != WAIT_OBJECT_0) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_BUSY), L"Previous remote call is still running");
        }
        state.thread.reset();
        state.releaseArgument();
    }
    if (!input.empty()) {
        state.argument =
            VirtualAllocEx(state.process.get(), nullptr, input.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        winrt::check_pointer(state.argument);
        SIZE_T written = 0;
        if (!WriteProcessMemory(state.process.get(), state.argument, input.data(), input.size(), &written) ||
            written != input.size()) {
            const auto error = GetLastError();
            state.releaseArgument();
            throw winrt::hresult_error(HRESULT_FROM_WIN32(error ? error : ERROR_PARTIAL_COPY));
        }
    }
    state.thread.reset(CreateRemoteThread(state.process.get(), nullptr, 0,
                                          reinterpret_cast<LPTHREAD_START_ROUTINE>(address), state.argument, 0,
                                          nullptr));
    if (!state.thread) {
        const auto error = GetLastError();
        state.releaseArgument();
        throw winrt::hresult_error(HRESULT_FROM_WIN32(error));
    }
    const auto waited = WaitForSingleObject(state.thread.get(), timeoutMs);
    if (waited != WAIT_OBJECT_0) {
        const auto error = waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        throw winrt::hresult_error(HRESULT_FROM_WIN32(error), L"Remote operation did not complete before deadline");
    }
    DWORD result = 0;
    winrt::check_bool(GetExitCodeThread(state.thread.get(), &result));
    if (!output.empty()) {
        SIZE_T received = 0;
        winrt::check_bool(
            ReadProcessMemory(state.process.get(), state.argument, output.data(), output.size(), &received));
        if (received != output.size()) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY));
        }
    }
    state.thread.reset();
    state.releaseArgument();
    return result;
}

uintptr_t RemoteCall::pendingArgument() const {
    return reinterpret_cast<uintptr_t>(impl_->argument);
}

bool RemoteCall::pending() const {
    return impl_->thread && WaitForSingleObject(impl_->thread.get(), 0) != WAIT_OBJECT_0;
}
}  // namespace cyberglass::dwm::host
