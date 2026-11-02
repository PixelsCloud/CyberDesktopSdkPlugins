#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "sdk/services/Background.h"

namespace cdsdk {
// Explicit developer comparison mode, selected before loading plugins with
// CD_PHYSICS_EXECUTION_MODE=owner-reference. Never selected by service failure.
// Normal/default mode requires the host's bounded background service.
bool ownerThreadPhysicsReference();

// Plugin-local shared state. Construct the actual model lazily from execute on
// INITIALIZE and release it in dispose on the same lane. UI may retain immutable
// geometry in this state, but may not access its mutable model. Host only sees
// an opaque holder and the C callbacks; no shared_ptr/vtable crosses the ABI.
class BackgroundModel {
public:
    virtual ~BackgroundModel();
    virtual CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) = 0;
    virtual void dispose() noexcept = 0;
};

class BackgroundSnapshot {
public:
    BackgroundSnapshot();
    explicit BackgroundSnapshot(CdBackgroundSnapshot snapshot);
    ~BackgroundSnapshot();
    BackgroundSnapshot(const BackgroundSnapshot&) = delete;
    BackgroundSnapshot& operator=(const BackgroundSnapshot&) = delete;
    BackgroundSnapshot(BackgroundSnapshot&& other) noexcept;
    BackgroundSnapshot& operator=(BackgroundSnapshot&& other) noexcept;
    void reset() noexcept;
    explicit operator bool() const noexcept;
    std::span<const std::byte> bytes() const noexcept;
    uint64_t generation() const noexcept;
    uint64_t sequence() const noexcept;
    uint64_t timestamp() const noexcept;
    uint32_t slot() const noexcept;

private:
    CdBackgroundSnapshot snapshot_{};
};

// This handle is UI-owned; cancellation is nonblocking. The transferred worker
// context must have separate lifetime and must never borrow this handle or UI.
class BackgroundTask {
public:
    BackgroundTask();
    explicit BackgroundTask(const CdCreateContext& context);
    ~BackgroundTask();
    BackgroundTask(const BackgroundTask&) = delete;
    BackgroundTask& operator=(const BackgroundTask&) = delete;
    BackgroundTask(BackgroundTask&& other) noexcept;
    BackgroundTask& operator=(BackgroundTask&& other) noexcept;
    bool available() const noexcept;
    CdResult create(const CdBackgroundTaskDesc& description);
    CdResult create(std::shared_ptr<BackgroundModel> model, uint32_t snapshotBytes, uint64_t periodNs);
    CdResult submit(uint32_t kind, std::span<const std::byte> bytes, bool latest = false, uint64_t key = 0);
    CdResult pause(bool paused);
    CdResult restart(std::span<const std::byte> bytes);
    void cancel() noexcept;
    BackgroundSnapshot acquire() const;
    CdBackgroundStats stats() const;
    uint64_t generation() const noexcept;
    uint64_t token() const noexcept;
    uint64_t sequence() const noexcept;

private:
    CdResult control(uint32_t operation, std::span<const std::byte> bytes);
    CdBackgroundApi api_{};
    uint64_t token_ = 0, generation_ = 1, sequence_ = 0;
};

// UI-side convenience for a fixed-size, pointer-free model frame. It owns no
// model data and never waits for computation. Unsupported services and queue
// exhaustion are explicit errors; callers select any reference mode at the host.
class BackgroundChannel {
public:
    explicit BackgroundChannel(const CdCreateContext& context);
    BackgroundChannel(const CdCreateContext& context, std::shared_ptr<BackgroundModel> model, uint32_t frameBytes,
                      uint64_t periodNs = 33000000);
    ~BackgroundChannel();
    BackgroundChannel(const BackgroundChannel&) = delete;
    BackgroundChannel& operator=(const BackgroundChannel&) = delete;
    void create(std::shared_ptr<BackgroundModel> model, uint32_t frameBytes, uint64_t periodNs = 33000000);
    bool consume(void* frame, uint32_t bytes);
    BackgroundSnapshot acquire();
    uint64_t submit(uint32_t kind, std::span<const std::byte> data = {}, bool latest = false, uint64_t key = 0);
    void pause(bool paused);
    void restart();
    uint64_t generation() const;
    bool paused() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
