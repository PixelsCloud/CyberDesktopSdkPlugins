#pragma once

#include <cstdint>
#include <memory>

#include "sdk/services/Background.h"

namespace cdsdk {
// Optional host adapter for independent SDK previews and deterministic tests.
// Threaded mode shares ONE fixed reference lane across hosts in this process;
// it is not the desktop's production execution policy or an isolation claim.
// Deterministic mode executes exclusively from tick() on the calling owner
// thread. It is explicit test execution, never a production rendering fallback.
class BackgroundReferenceHost {
public:
    enum class Mode { Threaded, Deterministic };
    explicit BackgroundReferenceHost(Mode mode = Mode::Threaded);
    ~BackgroundReferenceHost();
    BackgroundReferenceHost(const BackgroundReferenceHost&) = delete;
    BackgroundReferenceHost& operator=(const BackgroundReferenceHost&) = delete;
    CdBackgroundApi api();
    void tick(double seconds);
    bool takeChanged();
    bool active() const;
    bool waitForTest(uint32_t timeoutMs = 1000) const;
    void cancel();
    // Nonblocking retirement for DLL hot reload. Returns true only after model
    // disposal and the final read lease; callback-module references are released
    // before returning true. Keep the entry module loaded until this succeeds.
    bool retire();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
