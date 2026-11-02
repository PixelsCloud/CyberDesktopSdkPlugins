#include "sdk/cpp/BackgroundReference.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

namespace cdsdk {
namespace {
using Clock = std::chrono::steady_clock;
using Mode = BackgroundReferenceHost::Mode;

uint64_t clockNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

struct Budget {
    std::mutex mutex;
    size_t tasks = 0, bytes = 0;
};

class Modules {
public:
    ~Modules() {
        for (const auto module : modules_) {
            if (module) {
                FreeLibrary(module);
            }
        }
    }

    bool retain(const CdBackgroundTaskDesc& desc) {
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(desc.execute),
                                  &modules_[0]) &&
               GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(desc.destroy),
                                  &modules_[1]);
    }

    std::array<HMODULE, 2> detach() {
        return std::exchange(modules_, {});
    }

private:
    std::array<HMODULE, 2> modules_{};
};

struct Task;
struct ReferenceLane;

struct SnapshotSlot {
    std::weak_ptr<Task> owner;
    std::vector<std::max_align_t> bytes;
    uint32_t readers = 0, size = 0;
    uint64_t generation = 0, sequence = 0, timestamp = 0, command = 0;
};

struct Stored {
    CdBackgroundCommand command{};
    uint64_t serial = 0;
    alignas(std::max_align_t) std::array<std::byte, CD_BACKGROUND_COMMAND_BYTES> data{};
};

struct Task {
    std::mutex mutex;
    std::condition_variable completed;
    Modules modules;
    std::shared_ptr<Budget> budget;
    std::weak_ptr<ReferenceLane> lane;
    std::weak_ptr<std::atomic<bool>> changed;
    std::shared_ptr<Task> readOwner;
    CdBackgroundTaskDesc desc{};
    CdBackgroundStats stats{};
    std::array<SnapshotSlot, CD_BACKGROUND_SNAPSHOT_SLOTS> pool;
    std::array<Stored, CD_BACKGROUND_COMMANDS> commands;
    alignas(std::max_align_t) std::array<std::byte, CD_BACKGROUND_COMMAND_BYTES> reset{};
    uint32_t count = 0, inflight = 0, resetSize = 0;
    uint64_t token = 0, sequence = 0, last = 0, next = 0, virtualNow = 1, acquiredAt = 0, anchor = 0;
    size_t reservedBytes = 0;
    Mode mode = Mode::Threaded;
    std::thread::id owner;
    int published = -1;
    bool initialized = false, restarting = false, paused = false, suspended = false;
    bool needsPublication = true;
    bool canceled = false, active = false, blocked = false, finished = false, running = false;

    ~Task() {
        if (budget) {
            std::lock_guard lock(budget->mutex);
            --budget->tasks;
            budget->bytes -= reservedBytes;
        }
    }
};

struct ReferenceLane {
    std::mutex mutex;
    // A persistent coalesced signal closes the scan-to-wait race without making
    // UI producers acquire a lane lock held by bookkeeping or DLL retirement.
    std::binary_semaphore wake{0};
    std::atomic<bool> wakePending{false};

    void signal() {
        if (!wakePending.exchange(true)) {
            wake.release();
        }
    }

    void wait(uint64_t deadline) {
        if (deadline == UINT64_MAX) {
            wake.acquire();
        } else if (!wake.try_acquire_until(Clock::time_point(std::chrono::nanoseconds(deadline)))) {
            return;
        }
        wakePending = false;
    }

    std::shared_ptr<Budget> budget = std::make_shared<Budget>();
    std::vector<std::shared_ptr<Task>> tasks, retained;
    std::shared_ptr<ReferenceLane> retention;
    bool started = false, stopping = false;
    size_t cursor = 0;
    uint64_t nextToken = 1;

    ReferenceLane() {
        tasks.reserve(128);
        retained.reserve(128);
    }
};

void wake(const std::shared_ptr<Task>& task) {
    if (const auto lane = task->lane.lock()) {
        lane->signal();
    }
}

void changed(const std::shared_ptr<Task>& task) {
    if (const auto flag = task->changed.lock()) {
        *flag = true;
    }
}

void CD_CALL release(void* pointer) {
    auto& slot = *static_cast<SnapshotSlot*>(pointer);
    if (const auto task = slot.owner.lock()) {
        bool resumePublication = false;
        {
            std::lock_guard lock(task->mutex);
            if (!slot.readers) {
                return;
            }
            --slot.readers;
            --task->stats.snapshot_leases;
            resumePublication = std::exchange(task->blocked, false);
            if (!task->stats.snapshot_leases) {
                task->readOwner.reset();
            }
        }
        if (resumePublication) {
            wake(task);
        }
    }
}

uint64_t deadline(const std::shared_ptr<Task>& task, uint64_t now) {
    std::lock_guard lock(task->mutex);
    if (task->finished || task->running) {
        return UINT64_MAX;
    }
    if (task->canceled || task->stats.fault || task->suspended) {
        return now;
    }
    if (task->blocked) {
        return UINT64_MAX;
    }
    if (!task->initialized || task->restarting || task->needsPublication) {
        return now;
    }
    if (task->paused) {
        return UINT64_MAX;
    }
    if (task->count) {
        return now;
    }
    return task->active ? task->next : UINT64_MAX;
}

void run(const std::shared_ptr<Task>& task, uint64_t now) {
    std::array<Stored, CD_BACKGROUND_COMMANDS> storage;
    std::array<CdBackgroundCommand, CD_BACKGROUND_COMMANDS> commands;
    alignas(std::max_align_t) std::array<std::byte, CD_BACKGROUND_COMMAND_BYTES> reset{};
    CdBackgroundWork work{};
    CdBackgroundOutput output{};
    uint64_t anchor = 0, serial = 0;
    bool dispose = false, suspend = false;
    int selected = -1;
    {
        std::lock_guard lock(task->mutex);
        if (task->finished || task->running) {
            return;
        }
        dispose = task->canceled || task->stats.fault;
        suspend = task->suspended;
        if (!dispose) {
            if (task->paused && !suspend && !task->restarting && !task->needsPublication && task->initialized) {
                return;
            }
            for (unsigned index = 0; !suspend && index < task->pool.size(); ++index) {
                if (int(index) != task->published && !task->pool[index].readers) {
                    selected = int(index);
                    break;
                }
            }
            if (!suspend && selected < 0) {
                ++task->stats.snapshot_backpressure;
                task->blocked = true;
                return;
            }
            if (!task->initialized) {
                task->owner = std::this_thread::get_id();
            }
            if (task->owner != std::this_thread::get_id()) {
                task->stats.fault = CD_ERROR_INTERNAL;
                return;
            }
            const bool resetWorld = task->restarting && !suspend;
            work.struct_size = sizeof(work);
            work.version = CD_BACKGROUND_VERSION;
            work.flags = (!task->initialized ? CD_BACKGROUND_INITIALIZE : 0u) |
                         (resetWorld ? CD_BACKGROUND_RESET : 0u) | (suspend ? CD_BACKGROUND_SUSPEND : 0u);
            if (!suspend && task->active && now >= task->next) {
                work.flags |= CD_BACKGROUND_TICK;
            }
            work.generation = task->stats.generation;
            work.sequence = ++task->sequence;
            work.monotonic_ns = now;
            work.elapsed_ns =
                !task->initialized || resetWorld || suspend || task->paused || !task->active || !task->last
                    ? 0
                    : std::min<uint64_t>(now - task->last, 100000000);
            anchor = task->anchor;
            work.command_count = suspend ? 0 : task->count;
            for (unsigned index = 0; index < work.command_count; ++index) {
                storage[index] = task->commands[index];
                commands[index] = storage[index].command;
                commands[index].data = storage[index].data.data();
                serial = std::max(serial, storage[index].serial);
            }
            work.commands = commands.data();
            if (resetWorld) {
                work.reset_size = task->resetSize;
                std::copy_n(task->reset.begin(), work.reset_size, reset.begin());
            }
            work.reset_data = reset.data();
            task->inflight = work.command_count;
            if (!suspend) {
                task->count = 0;
                task->restarting = false;
            }
            task->suspended = false;
            task->stats.state = CD_BACKGROUND_RUNNING;
            output = {sizeof(output),
                      CD_BACKGROUND_VERSION,
                      suspend ? 0 : task->desc.snapshot_bytes,
                      0,
                      suspend ? nullptr : task->pool[size_t(selected)].bytes.data(),
                      0,
                      suspend ? UINT32_MAX : uint32_t(selected)};
        }
        task->running = true;
    }
    if (dispose) {
        CdResult result = CD_OK;
        try {
            task->desc.destroy(task->desc.model_context);
        } catch (...) {
            result = CD_ERROR_INTERNAL;
        }
        {
            std::lock_guard lock(task->mutex);
            task->running = false;
            task->finished = true;
            if (result != CD_OK) {
                task->stats.fault = result;
                task->stats.state = CD_BACKGROUND_RETAINED;
            } else {
                task->stats.state = task->stats.fault ? CD_BACKGROUND_FAULTED : CD_BACKGROUND_FINISHED;
            }
        }
        if (result != CD_OK) {
            if (const auto lane = task->lane.lock()) {
                std::lock_guard lock(lane->mutex);
                lane->retained.push_back(task);
                lane->retention = lane;
            }
        }
        task->completed.notify_all();
        changed(task);
        return;
    }
    CdResult result = CD_ERROR_INTERNAL;
    const auto started = clockNs();
    try {
        result = task->desc.execute(task->desc.model_context, &work, &output);
    } catch (...) {
        result = CD_ERROR_INTERNAL;
    }
    const auto finished = clockNs();
    {
        std::lock_guard lock(task->mutex);
        task->running = false;
        task->initialized = true;
        task->inflight = 0;
        task->stats.queue_entries = task->count;
        task->stats.queue_bytes = 0;
        for (unsigned index = 0; index < task->count; ++index) {
            task->stats.queue_bytes += task->commands[index].command.size;
        }
        ++task->stats.callbacks;
        task->stats.callback_ns += finished - started;
        task->stats.maximum_callback_ns = std::max(task->stats.maximum_callback_ns, finished - started);
        if (result != CD_OK || output.struct_size != sizeof(output) || output.version != CD_BACKGROUND_VERSION ||
            output.capacity != (suspend ? 0 : task->desc.snapshot_bytes) || output.size > output.capacity ||
            output.data != (suspend ? nullptr : task->pool[size_t(selected)].bytes.data()) ||
            output.slot != (suspend ? UINT32_MAX : uint32_t(selected)) || (output.flags & ~CD_BACKGROUND_ACTIVE)) {
            task->stats.fault = result == CD_OK ? CD_ERROR_ARGUMENT : result;
        } else if (!suspend && !task->canceled && work.generation == task->stats.generation) {
            auto& slot = task->pool[size_t(selected)];
            slot.generation = work.generation;
            slot.sequence = work.sequence;
            slot.timestamp = task->mode == Mode::Deterministic ? now : finished;
            slot.size = output.size;
            task->stats.completed_command_sequence = std::max(task->stats.completed_command_sequence, serial);
            slot.command = task->stats.completed_command_sequence;
            task->published = selected;
            task->needsPublication = false;
            task->stats.completed_sequence = work.sequence;
            task->active = (output.flags & CD_BACKGROUND_ACTIVE) != 0;
            if (anchor == task->anchor) {
                task->last = now;
                task->next = now + task->desc.period_ns;
            }
        }
        if (task->paused && !task->suspended) {
            task->stats.state = CD_BACKGROUND_PAUSED;
            task->stats.pause_acknowledged_ns = task->mode == Mode::Deterministic ? now : finished;
        }
    }
    task->completed.notify_all();
    changed(task);
}

void work(const std::shared_ptr<ReferenceLane>& lane) {
    std::vector<std::shared_ptr<Task>> retired;
    retired.reserve(128);
    std::unique_lock lock(lane->mutex);
    for (;;) {
        std::erase_if(lane->tasks, [&](const auto& task) {
            std::lock_guard lock(task->mutex);
            if (!task->finished) {
                return false;
            }
            retired.push_back(task);
            return true;
        });
        if (!retired.empty()) {
            lock.unlock();
            retired.clear();
            lock.lock();
            continue;
        }
        if (lane->stopping && lane->tasks.empty()) {
            return;
        }
        uint64_t earliest = UINT64_MAX;
        const auto now = clockNs();
        std::shared_ptr<Task> selected;
        for (size_t offset = 0; offset < lane->tasks.size(); ++offset) {
            const auto index = (lane->cursor + offset) % lane->tasks.size();
            const auto& task = lane->tasks[index];
            if (lane->stopping) {
                std::lock_guard taskLock(task->mutex);
                task->canceled = true;
            }
            const auto due = deadline(task, now);
            if (due <= now) {
                selected = task;
                lane->cursor = index + 1;
                break;
            }
            earliest = std::min(earliest, due);
        }
        if (selected) {
            lock.unlock();
            run(selected, now);
            lock.lock();
        } else {
            lock.unlock();
            lane->wait(earliest);
            lock.lock();
        }
    }
}

std::shared_ptr<ReferenceLane> referenceLane() {
    struct Owner {
        std::shared_ptr<ReferenceLane> lane = std::make_shared<ReferenceLane>();

        ~Owner() {
            {
                std::lock_guard lock(lane->mutex);
                lane->stopping = true;
            }
            lane->signal();
        }
    };

    static Owner owner;
    return owner.lane;
}
}  // namespace

class BackgroundReferenceHost::Impl {
public:
    Mode mode;
    std::shared_ptr<ReferenceLane> lane = referenceLane();
    std::shared_ptr<std::atomic<bool>> changed = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<Task> task;

    explicit Impl(Mode value) : mode(value) {}

    std::shared_ptr<Task> find(uint64_t token) const {
        return task && task->token == token ? task : nullptr;
    }

    static CdResult CD_CALL create(void* pointer, const CdBackgroundTaskDesc* desc, uint64_t* token) noexcept {
        try {
            auto& self = *static_cast<Impl*>(pointer);
            if (!desc || !token || desc->struct_size < sizeof(*desc) || desc->version != CD_BACKGROUND_VERSION ||
                !desc->execute || !desc->destroy || !desc->model_context || desc->reserved || !desc->snapshot_bytes ||
                desc->snapshot_bytes > CD_BACKGROUND_SNAPSHOT_BYTES || desc->period_ns < 1000000 ||
                desc->period_ns > 1000000000) {
                return CD_ERROR_ARGUMENT;
            }
            if (self.task) {
                std::lock_guard lock(self.task->mutex);
                if (!self.task->finished || self.task->stats.snapshot_leases ||
                    self.task->stats.state == CD_BACKGROUND_RETAINED) {
                    return CD_ERROR_BUSY;
                }
            }
            self.task.reset();
            const size_t aligned = (desc->snapshot_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
            const size_t bytes = aligned * sizeof(std::max_align_t) * CD_BACKGROUND_SNAPSHOT_SLOTS;
            auto task = std::make_shared<Task>();
            if (!task->modules.retain(*desc)) {
                return CD_ERROR_ARGUMENT;
            }
            for (auto& slot : task->pool) {
                slot.bytes.resize(aligned);
                slot.owner = task;
            }
            {
                std::lock_guard lock(self.lane->budget->mutex);
                if (self.lane->budget->tasks >= 128 || self.lane->budget->bytes + bytes > 64 * 1024 * 1024) {
                    return CD_ERROR_BUSY;
                }
                ++self.lane->budget->tasks;
                self.lane->budget->bytes += bytes;
                task->budget = self.lane->budget;
                task->reservedBytes = bytes;
            }
            task->desc = *desc;
            task->mode = self.mode;
            task->lane = self.lane;
            task->changed = self.changed;
            task->stats.struct_size = sizeof(task->stats);
            task->stats.version = CD_BACKGROUND_VERSION;
            task->stats.generation = 1;
            task->stats.state = CD_BACKGROUND_STARTING;
            task->stats.lane = self.mode == Mode::Deterministic ? UINT32_MAX : 0;
            task->stats.snapshot_bytes = bytes;
            {
                std::lock_guard lock(self.lane->mutex);
                if (self.lane->stopping) {
                    return CD_ERROR_BUSY;
                }
                if (self.mode == Mode::Threaded) {
                    if (!self.lane->started) {
                        std::thread worker([lane = self.lane] {
                            work(lane);
                        });
                        worker.detach();
                        self.lane->started = true;
                    }
                    self.lane->tasks.push_back(task);
                }
                task->token = self.lane->nextToken++;
                *token = task->token;
                self.task = task;
            }
            self.lane->signal();
            return CD_OK;
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }

    static CdResult CD_CALL submit(void* pointer, uint64_t token, const CdBackgroundCommand* command) noexcept {
        const auto task = static_cast<Impl*>(pointer)->find(token);
        if (!task || !command || command->struct_size < sizeof(*command) ||
            command->size > CD_BACKGROUND_COMMAND_BYTES || (command->size && !command->data) ||
            (command->flags & ~CD_BACKGROUND_LATEST)) {
            return CD_ERROR_ARGUMENT;
        }
        {
            std::lock_guard lock(task->mutex);
            if (task->finished || task->canceled || task->paused || task->stats.fault ||
                command->generation != task->stats.generation) {
                return CD_ERROR_BUSY;
            }
            Stored* destination = nullptr;
            if (command->flags & CD_BACKGROUND_LATEST) {
                for (unsigned index = task->count; index > 0; --index) {
                    auto& existing = task->commands[index - 1];
                    if (!(existing.command.flags & CD_BACKGROUND_LATEST)) {
                        break;
                    }
                    if (existing.command.kind == command->kind && existing.command.key == command->key) {
                        destination = &existing;
                        task->stats.queue_bytes -= existing.command.size;
                        ++task->stats.coalesced_commands;
                        break;
                    }
                }
            }
            if (!destination) {
                if (task->count + task->inflight >= CD_BACKGROUND_COMMANDS) {
                    ++task->stats.command_backpressure;
                    return CD_ERROR_BUSY;
                }
                destination = &task->commands[task->count++];
                ++task->stats.queue_entries;
            }
            destination->command = *command;
            destination->command.data = nullptr;
            destination->serial = ++task->stats.submitted_command_sequence;
            if (command->size) {
                std::memcpy(destination->data.data(), command->data, command->size);
            }
            task->stats.queue_bytes += command->size;
        }
        wake(task);
        return CD_OK;
    }

    static CdResult CD_CALL control(void* pointer, uint64_t token, const CdBackgroundControl* value,
                                    uint64_t* generation) noexcept {
        const auto task = static_cast<Impl*>(pointer)->find(token);
        if (!task || !value || !generation || value->struct_size < sizeof(*value) || value->reserved ||
            value->size > CD_BACKGROUND_COMMAND_BYTES || (value->size && !value->data) ||
            value->operation < CD_BACKGROUND_PAUSE || value->operation > CD_BACKGROUND_CANCEL ||
            (value->operation != CD_BACKGROUND_RESTART && value->size)) {
            return CD_ERROR_ARGUMENT;
        }
        {
            std::lock_guard lock(task->mutex);
            if (task->finished || value->generation != task->stats.generation) {
                return CD_ERROR_BUSY;
            }
            const auto now = task->mode == Mode::Deterministic ? task->virtualNow : clockNs();
            if (value->operation == CD_BACKGROUND_CANCEL) {
                task->canceled = true;
                task->stats.state = CD_BACKGROUND_CANCELING;
            } else if (task->canceled || task->stats.fault) {
                return CD_ERROR_BUSY;
            } else if (value->operation == CD_BACKGROUND_PAUSE) {
                task->paused = task->suspended = true;
                ++task->anchor;
                task->count = 0;
                task->stats.queue_entries = task->inflight;
                task->stats.revoked_command_sequence = task->stats.submitted_command_sequence;
                task->stats.pause_requested_ns = now;
                task->blocked = false;
            } else if (value->operation == CD_BACKGROUND_RESUME) {
                task->paused = false;
                ++task->anchor;
                task->last = 0;
                task->next = now;
            } else {
                if (task->stats.generation == UINT64_MAX) {
                    return CD_ERROR_INTERNAL;
                }
                ++task->stats.generation;
                ++task->anchor;
                task->restarting = true;
                task->needsPublication = true;
                task->resetSize = value->size;
                if (value->size) {
                    std::memcpy(task->reset.data(), value->data, value->size);
                }
                task->count = 0;
                task->stats.queue_entries = task->inflight;
                task->stats.revoked_command_sequence = task->stats.submitted_command_sequence;
                task->stats.completed_command_sequence = task->stats.acquired_command_sequence = 0;
                task->published = -1;
                task->acquiredAt = task->last = 0;
                task->blocked = false;
            }
            *generation = task->stats.generation;
        }
        wake(task);
        return CD_OK;
    }

    static CdResult CD_CALL acquire(void* pointer, uint64_t token, CdBackgroundSnapshot* output) noexcept {
        const auto task = static_cast<Impl*>(pointer)->find(token);
        if (!task || !output || output->struct_size < sizeof(*output) || output->version != CD_BACKGROUND_VERSION) {
            return CD_ERROR_ARGUMENT;
        }
        std::lock_guard lock(task->mutex);
        if (task->published < 0 || task->canceled || task->finished || task->stats.fault) {
            return CD_ERROR_BUSY;
        }
        auto& slot = task->pool[size_t(task->published)];
        ++slot.readers;
        ++task->stats.snapshot_leases;
        task->readOwner = task;
        task->stats.acquired_command_sequence = slot.command;
        task->acquiredAt = slot.timestamp;
        *output = {sizeof(*output), CD_BACKGROUND_VERSION,
                   slot.size,       uint32_t(task->published),
                   slot.generation, slot.sequence,
                   slot.timestamp,  slot.bytes.data(),
                   &slot,           release};
        return CD_OK;
    }

    static CdResult CD_CALL stats(void* pointer, uint64_t token, CdBackgroundStats* output) noexcept {
        const auto task = static_cast<Impl*>(pointer)->find(token);
        if (!task || !output || output->struct_size < sizeof(*output) || output->version != CD_BACKGROUND_VERSION) {
            return CD_ERROR_ARGUMENT;
        }
        std::lock_guard lock(task->mutex);
        *output = task->stats;
        const auto now = task->mode == Mode::Deterministic ? task->virtualNow : clockNs();
        if (task->published >= 0) {
            output->snapshot_age_ns = now - task->pool[size_t(task->published)].timestamp;
        }
        if (task->acquiredAt) {
            output->acquired_snapshot_age_ns = now - task->acquiredAt;
        }
        return CD_OK;
    }
};

BackgroundReferenceHost::BackgroundReferenceHost(Mode mode) : impl_(std::make_unique<Impl>(mode)) {}

BackgroundReferenceHost::~BackgroundReferenceHost() {
    cancel();
}

CdBackgroundApi BackgroundReferenceHost::api() {
    return {sizeof(CdBackgroundApi), CD_BACKGROUND_VERSION, impl_.get(), Impl::create, Impl::submit,
            Impl::control,           Impl::acquire,         Impl::stats};
}

void BackgroundReferenceHost::tick(double seconds) {
    if (impl_->mode != Mode::Deterministic || !std::isfinite(seconds) || seconds < 0 ||
        seconds > double(UINT64_MAX - 1) / 1e9) {
        return;
    }
    if (const auto task = impl_->task) {
        const auto now = uint64_t(seconds * 1e9) + 1;
        {
            std::lock_guard lock(task->mutex);
            if (now < task->virtualNow) {
                return;
            }
            task->virtualNow = now;
        }
        if (deadline(task, now) <= now) {
            run(task, now);
        }
        if (deadline(task, now) <= now) {
            // Complete a requested fault/cancel on the same explicit owner
            // thread; never run arbitrary extra physics catchup here.
            bool cleanup = false;
            {
                std::lock_guard lock(task->mutex);
                cleanup = task->canceled || task->stats.fault ||
                          (task->paused && (task->needsPublication || task->restarting));
            }
            if (cleanup) {
                run(task, now);
            }
        }
    }
}

bool BackgroundReferenceHost::takeChanged() {
    return impl_->changed->exchange(false);
}

bool BackgroundReferenceHost::active() const {
    if (const auto task = impl_->task) {
        std::lock_guard lock(task->mutex);
        return !task->paused && !task->finished && (task->active || !task->initialized || task->count);
    }
    return false;
}

bool BackgroundReferenceHost::waitForTest(uint32_t timeoutMs) const {
    const auto task = impl_->task;
    if (!task) {
        return true;
    }
    std::unique_lock lock(task->mutex);
    return task->completed.wait_for(lock, std::chrono::milliseconds(std::min(timeoutMs, 10000u)), [&] {
        return task->finished || (!task->canceled && !task->stats.fault && task->initialized && !task->running &&
                                  !task->count && !task->restarting && !task->needsPublication && !task->suspended);
    });
}

void BackgroundReferenceHost::cancel() {
    if (const auto task = impl_->task) {
        {
            std::lock_guard lock(task->mutex);
            if (task->finished) {
                return;
            }
            task->canceled = true;
            task->stats.state = CD_BACKGROUND_CANCELING;
        }
        if (impl_->mode == Mode::Deterministic) {
            run(task, task->virtualNow);
        } else {
            wake(task);
        }
    }
}

bool BackgroundReferenceHost::retire() {
    cancel();
    std::array<HMODULE, 2> modules{};
    if (const auto task = impl_->task) {
        {
            std::lock_guard lock(task->mutex);
            if (!task->finished || task->running || task->stats.snapshot_leases ||
                task->stats.state == CD_BACKGROUND_RETAINED) {
                return false;
            }
            modules = task->modules.detach();
        }
        for (const auto module : modules) {
            if (module) {
                FreeLibrary(module);
            }
        }
    }
    return true;
}
}  // namespace cdsdk
