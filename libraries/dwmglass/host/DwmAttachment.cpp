// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/host/DwmAttachment.h"

#include <Windows.h>
#include <WtsApi32.h>
#include <bcrypt.h>
#include <dbghelp.h>
#include <spdlog/spdlog.h>
#include <tlhelp32.h>
#include <wil/resource.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "libraries/dwmglass/host/DwmSymbols.h"
#include "libraries/dwmglass/host/RemoteCall.h"
#include "libraries/dwmglass/src/DwmDiagnosticPacket.h"
#include "libraries/liquidglass/src/NativeServiceProtocol_win.h"

namespace cyberglass::dwm::host {
namespace {

void enableDebugPrivilege() {
    wil::unique_handle token;
    winrt::check_bool(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()));
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    winrt::check_bool(LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &privileges.Privileges[0].Luid));
    winrt::check_bool(AdjustTokenPrivileges(token.get(), FALSE, &privileges, sizeof(privileges), nullptr, nullptr));
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD), L"Native DWM host requires elevation");
    }
}

struct ProcessListCloser {
    void operator()(WTS_PROCESS_INFOW* processes) const {
        WTSFreeMemory(processes);
    }
};

uint32_t dwmProcess(uint32_t session) {
    WTS_PROCESS_INFOW* raw = nullptr;
    DWORD count = 0;
    winrt::check_bool(WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &raw, &count));
    const std::unique_ptr<WTS_PROCESS_INFOW, ProcessListCloser> list(raw);
    for (DWORD index = 0; index < count; ++index) {
        if (raw[index].SessionId == session && _wcsicmp(raw[index].pProcessName, L"dwm.exe") == 0) {
            return raw[index].ProcessId;
        }
    }
    throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), L"No compositor process for this session");
}

uintptr_t moduleAddress(uint32_t process, const std::filesystem::path& requested, bool fullPath) {
    wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process));
    winrt::check_bool(snapshot.get() != INVALID_HANDLE_VALUE);
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.get(), &entry)) {
        winrt::throw_last_error();
    }
    do {
        const auto actual = fullPath ? std::filesystem::path(entry.szExePath).lexically_normal().wstring()
                                     : std::filesystem::path(entry.szModule).filename().wstring();
        const auto expected = fullPath ? requested.lexically_normal().wstring() : requested.filename().wstring();
        if (_wcsicmp(actual.c_str(), expected.c_str()) == 0) {
            return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        }
    } while (Module32NextW(snapshot.get(), &entry));
    return 0;
}

void requireSameImage(HANDLE process, uintptr_t remote, HMODULE local) {
    IMAGE_DOS_HEADER dos{};
    SIZE_T received = 0;
    winrt::check_bool(ReadProcessMemory(process, reinterpret_cast<const void*>(remote), &dos, sizeof(dos), &received));
    if (received != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        dos.e_lfanew > 1024 * 1024) {
        throw std::runtime_error("Invalid remote module image header");
    }
    IMAGE_NT_HEADERS64 image{};
    winrt::check_bool(ReadProcessMemory(process, reinterpret_cast<const void*>(remote + dos.e_lfanew), &image,
                                        sizeof(image), &received));
    const auto own = ImageNtHeader(local);
    if (received != sizeof(image) || image.Signature != IMAGE_NT_SIGNATURE || !own ||
        image.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || own->FileHeader.Machine != image.FileHeader.Machine ||
        image.FileHeader.TimeDateStamp != own->FileHeader.TimeDateStamp ||
        image.OptionalHeader.SizeOfImage != own->OptionalHeader.SizeOfImage) {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
                                   L"Remote module differs from the loader/export source");
    }
}

uintptr_t systemFunction(HANDLE handle, uint32_t process, const char* name) {
    const auto function = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    winrt::check_pointer(function);
    HMODULE owner = nullptr;
    winrt::check_bool(
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<const wchar_t*>(function), &owner));
    const auto path = wil::GetModuleFileNameW<std::wstring>(owner);
    const auto remote = moduleAddress(process, std::filesystem::path(path).filename(), false);
    if (!remote) {
        throw std::runtime_error("Cannot locate the compositor's system loader module");
    }
    requireSameImage(handle, remote, owner);
    return remote + reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(owner);
}

uintptr_t exportedFunction(HANDLE handle, const std::filesystem::path& library, uintptr_t remoteBase,
                           const char* name) {
    // Inspect exports without executing the native engine in this helper process.
    wil::unique_hmodule mapped(LoadLibraryExW(library.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES));
    winrt::check_bool(mapped != nullptr);
    requireSameImage(handle, remoteBase, mapped.get());
    const auto function = GetProcAddress(mapped.get(), name);
    winrt::check_pointer(function);
    return remoteBase + reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(mapped.get());
}

void checkStart(uint32_t result) {
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_INITIALIZED) {
        const auto error = result & 0x80000000u ? HRESULT(result) : HRESULT_FROM_WIN32(result);
        throw winrt::hresult_error(error, L"Native compositor initialization failed");
    }
}
}  // namespace

struct DwmAttachment::Impl {
    uint32_t process = 0;
    uint64_t generation = 0;
    wil::unique_handle handle;
    std::unique_ptr<RemoteCall> call;
};

DwmAttachment::DwmAttachment(uint32_t session) : m_impl(std::make_unique<Impl>()) {
    enableDebugPrivilege();
    auto& value = *m_impl;
    value.process = dwmProcess(session);
    value.handle.reset(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                       PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                                   FALSE, value.process));
    winrt::check_bool(value.handle != nullptr);
    value.call = std::make_unique<RemoteCall>(value.handle.get());
}

DwmAttachment::~DwmAttachment() = default;

void DwmAttachment::attach(const std::filesystem::path& library, const std::filesystem::path& symbols) {
    auto& value = *m_impl;
    const auto path = std::filesystem::absolute(library).lexically_normal();
    auto remote = moduleAddress(value.process, path, true);
    if (!remote) {
        const auto text = path.wstring();
        value.call->invoke(systemFunction(value.handle.get(), value.process, "LoadLibraryW"),
                           std::as_bytes(std::span(text.c_str(), text.size() + 1)));
        remote = moduleAddress(value.process, path, true);
        if (!remote) {
            throw std::runtime_error("The compositor did not load the native glass module");
        }
    }
    const auto text = std::filesystem::absolute(symbols).wstring();
    liquidglass::native::BootRequest boot;
    boot.pathCharacters = uint32_t(text.size());
    do {
        winrt::check_bool(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&boot.generation), sizeof(boot.generation),
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
    } while (!boot.generation);
    std::vector<std::byte> content(sizeof(boot) + (text.size() + 1) * sizeof(wchar_t));
    std::memcpy(content.data(), &boot, sizeof(boot));
    std::memcpy(content.data() + sizeof(boot), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    auto result = value.call->invoke(exportedFunction(value.handle.get(), path, remote, "CyberGlassDwmStart"), content,
                                     std::as_writable_bytes(std::span(&boot, 1)));
    if (result == uint32_t(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) || result == ERROR_FILE_NOT_FOUND) {
        prepareSymbols(symbols);
        result = value.call->invoke(exportedFunction(value.handle.get(), path, remote, "CyberGlassDwmStart"), content,
                                    std::as_writable_bytes(std::span(&boot, 1)));
    }
    checkStart(result);
    if (!boot.generation) {
        throw std::runtime_error("Native compositor did not acknowledge its generation");
    }
    value.generation = boot.generation;
    spdlog::info("Native DWM glass ready: process={} result=0x{:08x}", value.process, result);
}

void DwmAttachment::detach(const std::filesystem::path& library) {
    if (alive() && moduleAddress(m_impl->process, std::filesystem::absolute(library).lexically_normal(), true)) {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_BUSY),
                                   L"DWM glass remains resident until the compositor exits; live unload is unsafe");
    }
}

uint32_t DwmAttachment::process() const {
    return m_impl->process;
}

uint64_t DwmAttachment::generation() const {
    return m_impl->generation;
}

DiagnosticPacket DwmAttachment::diagnostics(const std::filesystem::path& library, bool enabled) {
    const auto path = std::filesystem::absolute(library).lexically_normal();
    const auto remote = moduleAddress(m_impl->process, path, true);
    if (!remote) {
        throw std::runtime_error("Native glass is not loaded");
    }
    DiagnosticPacket packet;
    packet.enabled = enabled ? 1 : 0;
    checkStart(m_impl->call->invoke(exportedFunction(m_impl->handle.get(), path, remote, "CyberGlassDwmInspect"),
                                    std::as_bytes(std::span(&packet, 1)),
                                    std::as_writable_bytes(std::span(&packet, 1))));
    return packet;
}

bool DwmAttachment::alive() const {
    return WaitForSingleObject(m_impl->handle.get(), 0) == WAIT_TIMEOUT;
}

void* DwmAttachment::processHandle() const {
    return m_impl->handle.get();
}

}  // namespace cyberglass::dwm::host
