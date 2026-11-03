#include "libraries/crash-reporting/ProcessWatchdog.h"

#include <Windows.h>

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "libraries/crash-reporting/CrashReporter.h"

namespace cybercrash {
namespace {
[[noreturn]] void expired() {
    // Dump writing also has a hard deadline. A blocked dump must not keep
    // global input hooks alive indefinitely. This branch always exits.
    OutputDebugStringW(L"CyberDesktop process watchdog expired\n");
    try {
        std::thread([] {
            processReporter().snapshot();
            TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
        }).detach();
    } catch (...) {
    }
    Sleep(2000);
    TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
    std::terminate();
}
}  // namespace

struct ProcessWatchdog::Impl {
    using Clock = std::chrono::steady_clock;
    std::chrono::milliseconds timeout;
    std::mutex mutex;
    std::condition_variable changed;
    Clock::time_point deadline{};
    bool armed = false;
    bool stopping = false;
    std::thread thread;

    explicit Impl(std::chrono::milliseconds value) : timeout(value) {
        if (timeout.count() <= 0) {
            throw std::invalid_argument("Watchdog timeout must be positive");
        }
        thread = std::thread([this] {
            run();
        });
    }

    ~Impl() {
        {
            const std::lock_guard lock(mutex);
            stopping = true;
        }
        changed.notify_one();
        thread.join();
    }

    void run() {
        std::unique_lock lock(mutex);
        while (!stopping) {
            if (!armed) {
                changed.wait(lock, [this] {
                    return stopping || armed;
                });
            } else if (changed.wait_until(lock, deadline) == std::cv_status::timeout && armed &&
                       Clock::now() >= deadline && !stopping) {
                lock.unlock();
                expired();
            }
        }
    }
};

ProcessWatchdog::ProcessWatchdog(std::chrono::milliseconds timeout) : impl_(std::make_unique<Impl>(timeout)) {}

ProcessWatchdog::~ProcessWatchdog() = default;

void ProcessWatchdog::arm() {
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->deadline = Impl::Clock::now() + impl_->timeout;
        impl_->armed = true;
    }
    impl_->changed.notify_one();
}

void ProcessWatchdog::disarm() {
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->armed = false;
    }
    impl_->changed.notify_one();
}
}  // namespace cybercrash
