#include "libraries/crash-reporting/CrashReporter.h"

#include <Windows.h>
#include <client/windows/handler/exception_handler.h>
#include <dbghelp.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace cybercrash {
namespace {
struct HandleCloser {
    void operator()(void* handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

using Handle = std::unique_ptr<void, HandleCloser>;

std::filesystem::path executable() {
    std::wstring value(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, value.data(), DWORD(value.size()));
    if (!size || size >= value.size()) {
        throw std::runtime_error("Cannot resolve crash reporter executable");
    }
    value.resize(size);
    return value;
}

Handle acquireLock(const std::filesystem::path& path) noexcept {
    Handle lock;
    for (int attempt = 0; attempt < 100; ++attempt) {
        lock.reset(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
        if (lock.get() != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) {
            break;
        }
        Sleep(10);
    }
    return lock;
}

void retainNewest(const std::filesystem::path& directory, bool alreadyLocked = false) noexcept {
    try {
        Handle lock;
        if (!alreadyLocked) {
            lock = acquireLock(directory / L".retention.lock");
        }
        if (!alreadyLocked && lock.get() == INVALID_HANDLE_VALUE) {
            return;
        }

        struct Dump {
            std::filesystem::path path;
            std::filesystem::file_time_type time;
        };

        std::vector<Dump> dumps;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            // Only Breakpad's own GUID filenames belong to this retention policy.
            const auto name = entry.path().filename().wstring();
            if (name.size() != 40 || name.substr(36) != L".dmp" || entry.is_symlink() || !entry.is_regular_file()) {
                continue;
            }
            bool guid = true;
            for (size_t index = 0; index < 36; ++index) {
                const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
                guid &= dash ? name[index] == L'-' : std::iswxdigit(name[index]) != 0;
            }
            if (guid) {
                dumps.push_back({entry.path(), entry.last_write_time()});
            }
        }
        std::sort(dumps.begin(), dumps.end(), [](const Dump& first, const Dump& second) {
            return first.time != second.time ? first.time > second.time : first.path > second.path;
        });
        for (size_t index = 5; index < dumps.size(); ++index) {
            // A concurrent writer does not share deletion: leave its dump intact.
            Handle file(CreateFileW(dumps[index].path.c_str(), DELETE | GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.get() != INVALID_HANDLE_VALUE) {
                FILE_DISPOSITION_INFO disposition{TRUE};
                SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition));
            }
        }
    } catch (...) {
        OutputDebugStringW(L"CyberDesktop crash retention failed; dump preserved\n");
    }
}

[[noreturn]] void terminateFailure() noexcept {
    RaiseException(0xe043cd01, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), 0xe043cd01);
    _exit(1);
}

void abortFailure(int) {
    terminateFailure();
}

void invalidFailure(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    RaiseException(0xc000000d, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    terminateFailure();
}

void pureCallFailure() {
    RaiseException(0xe043cd02, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    terminateFailure();
}
}  // namespace

struct CrashReporter::Impl {
    std::filesystem::path directory;
    std::filesystem::path lockPath;
    std::filesystem::path errorPath;
    Handle dumpLock;
    std::atomic<DWORD> writeError{ERROR_SUCCESS};
    std::unique_ptr<google_breakpad::ExceptionHandler> handler;
    std::terminate_handler previousTerminate = nullptr;
    void (*previousAbort)(int) = SIG_DFL;
    unsigned previousAbortBehavior = 0;
    _invalid_parameter_handler previousInvalid = nullptr;
    _purecall_handler previousPureCall = nullptr;
    DWORD previousErrorMode = 0;

    static bool filter(void* context, EXCEPTION_POINTERS*, MDRawAssertionInfo*) noexcept {
        auto& self = *static_cast<Impl*>(context);
        // Breakpad serializes its handler thread. Keep the per-image file lease
        // through writing and retention so concurrent PIDs cannot race pruning.
        self.dumpLock = acquireLock(self.lockPath);
        return true;
    }

    static bool completed(const wchar_t* directory, const wchar_t* id, void* context, EXCEPTION_POINTERS*,
                          MDRawAssertionInfo*, bool succeeded) noexcept {
        auto& self = *static_cast<Impl*>(context);
        self.writeError.store(succeeded ? ERROR_SUCCESS : GetLastError());
        if (!succeeded) {
            // Breakpad has closed the failed file. Preserve the failure reason,
            // not an incomplete file that looks like a usable crash dump.
            std::array<wchar_t, MAX_PATH> path{};
            if (directory && id && wcsnlen_s(id, 37) == 36 &&
                _snwprintf_s(path.data(), path.size(), _TRUNCATE, L"%ls\\%ls.dmp", directory, id) > 0) {
                DeleteFileW(path.data());
            }
            std::array<char, 128> message{};
            const int length = std::snprintf(message.data(), message.size(), "Breakpad write failed: 0x%08lX\r\n",
                                             self.writeError.load());
            OutputDebugStringA(message.data());
            Handle file(CreateFileW(self.errorPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.get() != INVALID_HANDLE_VALUE && length > 0) {
                DWORD written = 0;
                WriteFile(file.get(), message.data(), DWORD(length), &written, nullptr);
            }
        }
        retainNewest(self.directory, self.dumpLock.get() != INVALID_HANDLE_VALUE && self.dumpLock != nullptr);
        self.dumpLock.reset();
        return succeeded;
    }
};

CrashReporter::CrashReporter(std::filesystem::path root) : impl_(std::make_unique<Impl>()) {
    const auto image = executable();
    if (root.empty()) {
        // SDK tools live below dist/sdk/bin. All suite processes must write
        // outside the clean distribution, including those nested executables.
        auto ancestor = image.parent_path();
        while (!ancestor.empty()) {
            if (_wcsicmp(ancestor.filename().c_str(), L"dist") == 0) {
                root = ancestor.parent_path() / L"crashes";
                break;
            }
            const auto parent = ancestor.parent_path();
            if (parent == ancestor) {
                break;
            }
            ancestor = parent;
        }
        if (root.empty()) {
            root = image.parent_path().parent_path() / L"crashes";
        }
    }
    auto name = image.stem().wstring();
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t value) {
        return std::towlower(value);
    });
    impl_->directory = std::filesystem::absolute(root / name).lexically_normal();
    impl_->lockPath = impl_->directory / L".retention.lock";
    impl_->errorPath = impl_->directory / L"last-error.txt";
    std::filesystem::create_directories(impl_->directory);
    if (std::filesystem::is_symlink(impl_->directory)) {
        throw std::runtime_error("Crash directory must not be a symbolic link");
    }
    retainNewest(impl_->directory);
    impl_->handler = std::make_unique<google_breakpad::ExceptionHandler>(
        impl_->directory.wstring(), &Impl::filter, &Impl::completed, impl_.get(),
        google_breakpad::ExceptionHandler::HANDLER_ALL, MiniDumpNormal, static_cast<const wchar_t*>(nullptr), nullptr);
    impl_->previousTerminate = std::set_terminate(&terminateFailure);
    impl_->previousAbort = std::signal(SIGABRT, &abortFailure);
    impl_->previousAbortBehavior = _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    impl_->previousInvalid = _set_invalid_parameter_handler(&invalidFailure);
    impl_->previousPureCall = _set_purecall_handler(&pureCallFailure);
    impl_->previousErrorMode = GetErrorMode();
    SetErrorMode(impl_->previousErrorMode | SEM_NOGPFAULTERRORBOX);
}

CrashReporter::~CrashReporter() {
    SetErrorMode(impl_->previousErrorMode);
    _set_purecall_handler(impl_->previousPureCall);
    _set_invalid_parameter_handler(impl_->previousInvalid);
    std::set_terminate(impl_->previousTerminate);
    std::signal(SIGABRT, impl_->previousAbort);
    _set_abort_behavior(impl_->previousAbortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

const std::filesystem::path& CrashReporter::directory() const {
    return impl_->directory;
}

bool CrashReporter::snapshot() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (impl_->handler->WriteMinidump()) {
            return true;
        }
        const auto recorded = impl_->writeError.load();
        const auto error = recorded ? recorded : ERROR_GEN_FAILURE;
        if (attempt == 2 || ((error & 0xffff) != ERROR_INVALID_USER_BUFFER && (error & 0xffff) != ERROR_PARTIAL_COPY)) {
            SetLastError(error);
            return false;
        }
        Sleep(10);
    }
    return false;
}

CrashReporter& processReporter() {
    static CrashReporter reporter;
    return reporter;
}
}  // namespace cybercrash
