#include "tools/ResourceWatcher.h"

namespace cdsdk {
ResourceWatcher::ResourceWatcher(const std::filesystem::path& root) {
    directory_ = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (directory_ == INVALID_HANDLE_VALUE || !stop_ || !ready_) {
        close();
        throw std::runtime_error("Cannot watch preview resources");
    }
    try {
        worker_ = std::jthread([this] {
            run();
        });
    } catch (...) {
        close();
        throw;
    }
}

ResourceWatcher::~ResourceWatcher() {
    SetEvent(stop_);
    if (worker_.joinable()) {
        worker_.join();
    }
    close();
}

auto ResourceWatcher::drain() -> Changes {
    std::scoped_lock lock(mutex_);
    if (!pending_ || std::chrono::steady_clock::now() - last_ < std::chrono::milliseconds(250)) {
        return {};
    }
    Changes result{true, {paths_.begin(), paths_.end()}, std::exchange(error_, {})};
    paths_.clear();
    pending_ = false;
    return result;
}

auto ResourceWatcher::close() -> void {
    if (directory_ != INVALID_HANDLE_VALUE) {
        CloseHandle(directory_);
    }
    if (ready_) {
        CloseHandle(ready_);
    }
    if (stop_) {
        CloseHandle(stop_);
    }
}

auto ResourceWatcher::publish(std::wstring path, std::string error) -> void {
    std::scoped_lock lock(mutex_);
    if (paths_.size() < 64) {
        paths_.insert(std::move(path));
    }
    pending_ = true;
    error_ = std::move(error);
    last_ = std::chrono::steady_clock::now();
}

auto ResourceWatcher::resource(const std::filesystem::path& path) -> bool {
    for (const auto& part : path) {
        if (part == L".git" || part == L"build" || part == L".preview") {
            return false;
        }
    }
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    static const std::set<std::wstring> extensions{L".png",  L".jpg", L".jpeg", L".bmp",      L".gif",
                                                   L".webp", L".svg", L".ttf",  L".otf",      L".wav",
                                                   L".flac", L".ogg", L".json", L".textproto"};
    return extensions.contains(extension);
}

auto ResourceWatcher::run() noexcept -> void {
    try {
        alignas(DWORD) std::array<uint8_t, 64 * 1024> buffer{};
        while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
            OVERLAPPED operation{};
            operation.hEvent = ready_;
            ResetEvent(ready_);
            if (!ReadDirectoryChangesW(directory_, buffer.data(), DWORD(buffer.size()), TRUE,
                                       FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                           FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                       nullptr, &operation, nullptr)) {
                publish({}, "Directory watch failed: " + std::to_string(GetLastError()));
                return;
            }
            HANDLE handles[]{stop_, ready_};
            const auto wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0 + 1) {
                CancelIoEx(directory_, &operation);
                DWORD ignored = 0;
                GetOverlappedResult(directory_, &operation, &ignored, TRUE);
                return;
            }
            DWORD bytes = 0;
            if (!GetOverlappedResult(directory_, &operation, &bytes, FALSE)) {
                publish({}, "Resource watch read failed: " + std::to_string(GetLastError()));
                return;
            }
            if (!bytes) {
                publish({}, "Notification buffer overflow; reloading all resources");
                continue;
            }
            for (size_t offset = 0; offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= bytes;) {
                const auto* entry = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                if (entry->FileNameLength % sizeof(wchar_t) ||
                    offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) + entry->FileNameLength > bytes) {
                    publish({}, "Invalid resource notification; reloading all resources");
                    break;
                }
                std::wstring name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
                if (resource(name)) {
                    publish(std::move(name));
                }
                if (!entry->NextEntryOffset) {
                    break;
                }
                if (entry->NextEntryOffset < offsetof(FILE_NOTIFY_INFORMATION, FileName) ||
                    entry->NextEntryOffset > bytes - offset) {
                    break;
                }
                offset += entry->NextEntryOffset;
            }
        }
    } catch (const std::exception& error) {
        publish({}, error.what());
    }
}
}  // namespace cdsdk
