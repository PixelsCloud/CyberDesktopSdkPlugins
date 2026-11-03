#pragma once

#include <chrono>
#include <memory>

namespace cybercrash {
// An armed lease is independent of the application's event loop. Expiry saves
// a diagnostic dump and terminates this process; never use in an OS process.
class ProcessWatchdog final {
public:
    explicit ProcessWatchdog(std::chrono::milliseconds timeout);
    ~ProcessWatchdog();
    ProcessWatchdog(const ProcessWatchdog&) = delete;
    ProcessWatchdog& operator=(const ProcessWatchdog&) = delete;
    void arm();
    void disarm();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercrash
