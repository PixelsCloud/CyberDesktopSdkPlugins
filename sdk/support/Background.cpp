#include "sdk/cpp/Background.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "sdk/cpp/Services.h"

namespace cdsdk {
bool ownerThreadPhysicsReference() {
    static const bool reference = [] {
        const auto* mode = std::getenv("CD_PHYSICS_EXECUTION_MODE");
        if (!mode || !*mode || std::strcmp(mode, "host-background") == 0) {
            return false;
        }
        if (std::strcmp(mode, "owner-reference") == 0) {
            return true;
        }
        throw std::invalid_argument("Unknown explicit physics execution mode");
    }();
    return reference;
}

namespace {
using ModelHolder = std::shared_ptr<BackgroundModel>;

CdResult CD_CALL executeModel(void* pointer, const CdBackgroundWork* work, CdBackgroundOutput* output) {
    return (*static_cast<ModelHolder*>(pointer))->execute(*work, *output);
}

void CD_CALL disposeModel(void* pointer) {
    const std::unique_ptr<ModelHolder> holder(static_cast<ModelHolder*>(pointer));
    (*holder)->dispose();
}
}  // namespace

BackgroundModel::~BackgroundModel() = default;

class BackgroundChannel::Impl {
public:
    explicit Impl(const CdCreateContext& context) : task(context) {
        if (context.host) {
            host = *context.host;
        }
        hostContext = context.host_context;
    }

    BackgroundTask task;
    CdHostApi host{};
    void* hostContext = nullptr;
    uint64_t sequence = 0;
    uint32_t frameBytes = 0;
    bool paused = false, failed = false;

    void check(CdResult result) {
        if (result == CD_OK) {
            return;
        }
        if (failed) {
            return;
        }
        const auto state = task.stats();
        if (result == CD_ERROR_BUSY &&
            (state.fault || state.state == CD_BACKGROUND_CANCELING || state.state == CD_BACKGROUND_FINISHED ||
             state.state == CD_BACKGROUND_RETAINED)) {
            // Host revokes work before destroying the UI instance. Its ordinary
            // input cleanup is harmless after revocation and must not throw
            // through a noexcept plugin destructor.
            return;
        }
        failed = true;
        task.pause(true);
        if (host.log) {
            constexpr char message[] = "Background model failed or exceeded its bounded budget; model paused";
            host.log(hostContext, 3, message, sizeof(message) - 1);
        }
    }
};

BackgroundChannel::BackgroundChannel(const CdCreateContext& context, std::shared_ptr<BackgroundModel> model,
                                     uint32_t frameBytes, uint64_t periodNs)
    : BackgroundChannel(context) {
    create(std::move(model), frameBytes, periodNs);
}

BackgroundChannel::BackgroundChannel(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

void BackgroundChannel::create(std::shared_ptr<BackgroundModel> model, uint32_t frameBytes, uint64_t periodNs) {
    impl_->frameBytes = frameBytes;
    impl_->check(impl_->task.create(std::move(model), frameBytes, periodNs));
    if (impl_->failed) {
        throw std::runtime_error("Model requires a supported host.background/v1 execution mode and budget");
    }
}

BackgroundChannel::~BackgroundChannel() = default;

bool BackgroundChannel::consume(void* frame, uint32_t bytes) {
    impl_->check(frame && bytes == impl_->frameBytes ? CD_OK : CD_ERROR_VERSION);
    const auto snapshot = acquire();
    if (!snapshot) {
        return false;
    }
    std::memcpy(frame, snapshot.bytes().data(), bytes);
    return true;
}

BackgroundSnapshot BackgroundChannel::acquire() {
    auto& self = *impl_;
    if (self.failed) {
        throw std::runtime_error("Background model channel is faulted");
    }
    auto snapshot = self.task.acquire();
    if (!snapshot) {
        // A healthy acquisition already validates the publication. Copy the
        // full statistics only on the unavailable/error path to surface faults.
        self.check(self.task.stats().fault);
        if (self.failed) {
            throw std::runtime_error("Background model channel is faulted");
        }
        return {};
    }
    if (snapshot.generation() != self.task.generation() || snapshot.sequence() == self.sequence) {
        return {};
    }
    self.check(snapshot.bytes().size() == self.frameBytes && snapshot.slot() < CD_BACKGROUND_SNAPSHOT_SLOTS
                   ? CD_OK
                   : CD_ERROR_VERSION);
    if (self.failed) {
        throw std::runtime_error("Invalid background model snapshot");
    }
    self.sequence = snapshot.sequence();
    return snapshot;
}

uint64_t BackgroundChannel::submit(uint32_t kind, std::span<const std::byte> data, bool latest, uint64_t key) {
    if (!impl_->paused && !impl_->failed) {
        const auto result = impl_->task.submit(kind, data, latest, key);
        impl_->check(result);
        if (result == CD_OK) {
            return impl_->task.sequence();
        }
    }
    return 0;
}

void BackgroundChannel::pause(bool paused) {
    if (!impl_->failed && impl_->paused != paused) {
        impl_->check(impl_->task.pause(paused));
        impl_->paused = paused;
    }
}

void BackgroundChannel::restart() {
    impl_->check(impl_->task.restart({}));
    impl_->sequence = 0;
}

uint64_t BackgroundChannel::generation() const {
    return impl_->task.generation();
}

bool BackgroundChannel::paused() const {
    return impl_->paused;
}

BackgroundSnapshot::BackgroundSnapshot() = default;

BackgroundSnapshot::BackgroundSnapshot(CdBackgroundSnapshot snapshot) : snapshot_(snapshot) {}

BackgroundSnapshot::~BackgroundSnapshot() {
    reset();
}

BackgroundSnapshot::BackgroundSnapshot(BackgroundSnapshot&& other) noexcept
    : snapshot_(std::exchange(other.snapshot_, {})) {}

BackgroundSnapshot& BackgroundSnapshot::operator=(BackgroundSnapshot&& other) noexcept {
    if (this != &other) {
        reset();
        snapshot_ = std::exchange(other.snapshot_, {});
    }
    return *this;
}

void BackgroundSnapshot::reset() noexcept {
    const auto snapshot = std::exchange(snapshot_, {});
    if (snapshot.release) {
        snapshot.release(snapshot.release_context);
    }
}

BackgroundSnapshot::operator bool() const noexcept {
    return snapshot_.release != nullptr;
}

std::span<const std::byte> BackgroundSnapshot::bytes() const noexcept {
    return {static_cast<const std::byte*>(snapshot_.data), snapshot_.size};
}

uint64_t BackgroundSnapshot::generation() const noexcept {
    return snapshot_.generation;
}

uint64_t BackgroundSnapshot::sequence() const noexcept {
    return snapshot_.sequence;
}

uint64_t BackgroundSnapshot::timestamp() const noexcept {
    return snapshot_.monotonic_ns;
}

uint32_t BackgroundSnapshot::slot() const noexcept {
    return snapshot_.slot;
}

BackgroundTask::BackgroundTask() = default;

BackgroundTask::BackgroundTask(const CdCreateContext& context) {
    Services(context.host, context.host_context).query(CD_SERVICE_BACKGROUND, api_);
    if (api_.struct_size < sizeof(api_) || api_.version != CD_BACKGROUND_VERSION || !api_.create || !api_.submit ||
        !api_.control || !api_.acquire || !api_.stats) {
        api_ = {};
    }
}

BackgroundTask::~BackgroundTask() {
    cancel();
}

BackgroundTask::BackgroundTask(BackgroundTask&& other) noexcept
    : api_(std::exchange(other.api_, {})),
      token_(std::exchange(other.token_, 0)),
      generation_(other.generation_),
      sequence_(other.sequence_) {}

BackgroundTask& BackgroundTask::operator=(BackgroundTask&& other) noexcept {
    if (this != &other) {
        cancel();
        api_ = std::exchange(other.api_, {});
        token_ = std::exchange(other.token_, 0);
        generation_ = other.generation_;
        sequence_ = other.sequence_;
    }
    return *this;
}

bool BackgroundTask::available() const noexcept {
    return api_.create != nullptr;
}

CdResult BackgroundTask::create(const CdBackgroundTaskDesc& description) {
    if (!available()) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (token_) {
        return CD_ERROR_BUSY;
    }
    const auto result = api_.create(api_.context, &description, &token_);
    if (result == CD_OK) {
        generation_ = 1;
        sequence_ = 0;
    }
    return result;
}

CdResult BackgroundTask::submit(uint32_t kind, std::span<const std::byte> bytes, bool latest, uint64_t key) {
    if (!token_) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (bytes.size() > CD_BACKGROUND_COMMAND_BYTES) {
        return CD_ERROR_BUFFER;
    }
    const CdBackgroundCommand command{
        sizeof(command), kind,        latest ? CD_BACKGROUND_LATEST : 0u, uint32_t(bytes.size()), generation_, key,
        ++sequence_,     bytes.data()};
    return api_.submit(api_.context, token_, &command);
}

CdResult BackgroundTask::create(std::shared_ptr<BackgroundModel> model, uint32_t snapshotBytes, uint64_t periodNs) {
    if (!model) {
        return CD_ERROR_ARGUMENT;
    }
    auto holder = std::make_unique<ModelHolder>(std::move(model));
    const CdBackgroundTaskDesc description{sizeof(description), CD_BACKGROUND_VERSION, snapshotBytes, 0, periodNs,
                                           holder.get(),        executeModel,          disposeModel};
    const auto result = create(description);
    if (result == CD_OK) {
        holder.release();
    }
    return result;
}

CdResult BackgroundTask::control(uint32_t operation, std::span<const std::byte> bytes) {
    if (!token_) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (bytes.size() > CD_BACKGROUND_COMMAND_BYTES) {
        return CD_ERROR_BUFFER;
    }
    const CdBackgroundControl request{sizeof(request), operation, uint32_t(bytes.size()), 0, generation_, bytes.data()};
    return api_.control(api_.context, token_, &request, &generation_);
}

CdResult BackgroundTask::pause(bool paused) {
    return control(paused ? CD_BACKGROUND_PAUSE : CD_BACKGROUND_RESUME, {});
}

CdResult BackgroundTask::restart(std::span<const std::byte> bytes) {
    return control(CD_BACKGROUND_RESTART, bytes);
}

void BackgroundTask::cancel() noexcept {
    if (token_) {
        control(CD_BACKGROUND_CANCEL, {});
        token_ = 0;
    }
}

BackgroundSnapshot BackgroundTask::acquire() const {
    CdBackgroundSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.version = CD_BACKGROUND_VERSION;
    if (!token_ || api_.acquire(api_.context, token_, &snapshot) != CD_OK) {
        return {};
    }
    return BackgroundSnapshot(snapshot);
}

CdBackgroundStats BackgroundTask::stats() const {
    CdBackgroundStats value{};
    value.struct_size = sizeof(value);
    value.version = CD_BACKGROUND_VERSION;
    if (!token_ || api_.stats(api_.context, token_, &value) != CD_OK) {
        value = {};
    }
    return value;
}

uint64_t BackgroundTask::generation() const noexcept {
    return generation_;
}

uint64_t BackgroundTask::token() const noexcept {
    return token_;
}

uint64_t BackgroundTask::sequence() const noexcept {
    return sequence_;
}
}  // namespace cdsdk
