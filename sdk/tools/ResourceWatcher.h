#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace cdsdk {
// The worker only collects directory notifications. It never calls a plugin.
class ResourceWatcher {
public:
    explicit ResourceWatcher(const std::filesystem::path& root);

    ~ResourceWatcher();

    ResourceWatcher(const ResourceWatcher&) = delete;
    ResourceWatcher& operator=(const ResourceWatcher&) = delete;

    struct Changes {
        bool changed = false;
        std::vector<std::wstring> paths;
        std::string error;
    };

    Changes drain();

private:
    HANDLE directory_ = INVALID_HANDLE_VALUE, stop_ = nullptr, ready_ = nullptr;
    std::mutex mutex_;
    std::set<std::wstring> paths_;
    std::string error_;
    bool pending_ = false;
    std::chrono::steady_clock::time_point last_;
    std::jthread worker_;

    void close();

    void publish(std::wstring path, std::string error = {});

    static bool resource(const std::filesystem::path& path);

    void run() noexcept;
};
}  // namespace cdsdk
