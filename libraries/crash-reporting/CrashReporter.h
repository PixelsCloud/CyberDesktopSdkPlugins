#pragma once

#include <filesystem>
#include <memory>

namespace cybercrash {
// Install before application/GUI initialization; keep alive through shutdown.
class CrashReporter final {
public:
    explicit CrashReporter(std::filesystem::path root = {});
    ~CrashReporter();
    CrashReporter(const CrashReporter&) = delete;
    CrashReporter& operator=(const CrashReporter&) = delete;
    const std::filesystem::path& directory() const;
    bool snapshot();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

CrashReporter& processReporter();
}  // namespace cybercrash
