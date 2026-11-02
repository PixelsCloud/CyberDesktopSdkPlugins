#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>

#include "sdk/cpp/Background.h"
#include "sdk/cpp/BackgroundReference.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Counters {
    std::atomic<unsigned> disposed{0};
    std::atomic<bool> active{true};
    std::atomic<bool> wrongThread{false}, blocked{false};
    std::atomic<bool> holdDispose{false}, disposing{false};
    std::thread::id owner;
    std::mutex mutex;
    std::condition_variable release;
    bool released = false;
    std::array<std::atomic<uint64_t>, CD_BACKGROUND_SNAPSHOT_SLOTS> metadata{};
};

struct Frame {
    uint64_t value, generation, elapsed;
};

class Model final : public cdsdk::BackgroundModel {
public:
    explicit Model(std::shared_ptr<Counters> counters) : counters_(std::move(counters)) {}

    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        if (work.flags & CD_BACKGROUND_INITIALIZE) {
            counters_->owner = std::this_thread::get_id();
        }
        if (counters_->owner != std::this_thread::get_id()) {
            counters_->wrongThread = true;
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            return CD_OK;
        }
        if (work.flags & CD_BACKGROUND_RESET) {
            value_ = 0;
        }
        for (unsigned index = 0; index < work.command_count; ++index) {
            const auto& command = work.commands[index];
            if (command.kind == 10) {
                return CD_ERROR_INTERNAL;
            } else if (command.kind == 9) {
                counters_->blocked = true;
                std::unique_lock lock(counters_->mutex);
                counters_->release.wait(lock, [&] {
                    return counters_->released;
                });
            } else {
                uint32_t number = 0;
                if (command.size != sizeof(number)) {
                    return CD_ERROR_ARGUMENT;
                }
                std::memcpy(&number, command.data, sizeof(number));
                value_ = value_ * 131 + number;
            }
        }
        const Frame frame{value_, work.generation, work.elapsed_ns};
        require(output.slot < CD_BACKGROUND_SNAPSHOT_SLOTS, "Host supplied an invalid geometry slot");
        counters_->metadata[output.slot] = work.sequence;
        std::memcpy(output.data, &frame, sizeof(frame));
        output.size = sizeof(frame);
        output.flags = counters_->active ? CD_BACKGROUND_ACTIVE : 0;
        return CD_OK;
    }

    void dispose() noexcept override {
        if (counters_->holdDispose) {
            counters_->disposing = true;
            std::unique_lock lock(counters_->mutex);
            counters_->release.wait(lock, [&] {
                return counters_->released;
            });
        }
        if (counters_->owner != std::thread::id{} && counters_->owner != std::this_thread::get_id()) {
            counters_->wrongThread = true;
        }
        ++counters_->disposed;
    }

private:
    std::shared_ptr<Counters> counters_;
    uint64_t value_ = 0;
};

struct Host {
    cdsdk::BackgroundReferenceHost background;
    CdServiceHostApi table{};

    explicit Host(cdsdk::BackgroundReferenceHost::Mode mode) : background(mode) {
        table.input.media.base.struct_size = sizeof(table);
        table.input.media.base.abi_version = CD_PLUGIN_ABI_VERSION;
        table.query_service = [](void* pointer, const char* id, uint32_t version, void* data,
                                 uint32_t capacity) -> CdResult {
            if (std::strcmp(id, CD_SERVICE_BACKGROUND) != 0 || version != CD_BACKGROUND_VERSION) {
                return CD_ERROR_UNSUPPORTED;
            }
            if (capacity < sizeof(CdBackgroundApi)) {
                return CD_ERROR_BUFFER;
            }
            *static_cast<CdBackgroundApi*>(data) = static_cast<Host*>(pointer)->background.api();
            return CD_OK;
        };
    }

    CdCreateContext context() {
        CdCreateContext context{};
        context.host = &table.input.media.base;
        context.host_context = this;
        return context;
    }
};

Frame read(const cdsdk::BackgroundTask& task) {
    auto snapshot = task.acquire();
    require(bool(snapshot) && snapshot.bytes().size() == sizeof(Frame), "Acquire typed reference snapshot");
    Frame frame{};
    std::memcpy(&frame, snapshot.bytes().data(), sizeof(frame));
    return frame;
}

void deterministic() {
    Host host(cdsdk::BackgroundReferenceHost::Mode::Deterministic);
    auto counters = std::make_shared<Counters>();
    cdsdk::BackgroundTask task(host.context());
    require(task.create(std::make_shared<Model>(counters), sizeof(Frame), 33000000) == CD_OK,
            "Register deterministic reference");
    host.background.tick(0);
    require(read(task).elapsed == 0 && counters->owner == std::this_thread::get_id(),
            "Explicit deterministic owner/time anchor");
    const auto send = [&](uint32_t kind, uint32_t value, bool latest) {
        return task.submit(kind, std::as_bytes(std::span(&value, 1)), latest, kind);
    };
    require(send(1, 1, true) == CD_OK && send(1, 2, true) == CD_OK && send(2, 3, false) == CD_OK &&
                send(1, 4, true) == CD_OK && send(1, 5, true) == CD_OK,
            "Submit ordered reference commands");
    host.background.tick(.034);
    require(read(task).value == (uint64_t(2) * 131 + 3) * 131 + 5, "Coalescing crossed a discrete barrier");
    require(task.pause(true) == CD_OK, "Pause deterministic model");
    host.background.tick(1);
    require(task.stats().state == CD_BACKGROUND_PAUSED, "Reference pause did not acknowledge suspend");
    require(task.pause(false) == CD_OK, "Resume deterministic model");
    host.background.tick(2);
    require(read(task).elapsed == 0, "Reference resumed hidden-time catchup");
    std::array<cdsdk::BackgroundSnapshot, 4> held;
    double now = 2;
    for (auto& snapshot : held) {
        host.background.tick(now += .034);
        snapshot = task.acquire();
        require(bool(snapshot), "Acquire bounded reference read lease");
    }
    host.background.tick(now += .034);
    require(task.stats().snapshot_backpressure > 0, "Reference pool overwrote held readers");
    require(task.pause(true) == CD_OK, "Read pressure blocked reference pause");
    host.background.tick(now += .034);
    require(task.stats().state == CD_BACKGROUND_PAUSED, "Read pressure blocked reference suspend acknowledgement");
    task.cancel();
    host.background.tick(now += .034);
    require(counters->disposed == 1 && !counters->wrongThread, "Deterministic model disposal lifetime/affinity");
    Frame retained{};
    std::memcpy(&retained, held[0].bytes().data(), sizeof(retained));
    require(retained.generation == 1, "Canceled reference invalidated a retained reader");
}

void pausedPublication() {
    Host host(cdsdk::BackgroundReferenceHost::Mode::Deterministic);
    auto counters = std::make_shared<Counters>();
    cdsdk::BackgroundTask task(host.context());
    require(task.create(std::make_shared<Model>(counters), sizeof(Frame), 33000000) == CD_OK,
            "Register initially paused model");
    require(task.pause(true) == CD_OK, "Pause before initialization");
    host.background.tick(0);
    const auto initial = read(task);
    require(initial.elapsed == 0 && initial.generation == 1 && task.stats().state == CD_BACKGROUND_PAUSED,
            "Initially paused model did not publish a static frame");
    require(task.restart({}) == CD_OK, "Reset an acknowledged paused model");
    host.background.tick(10);
    const auto reset = read(task);
    require(reset.generation == 2 && reset.elapsed == 0 && task.stats().state == CD_BACKGROUND_PAUSED,
            "Paused reset failed to publish its new generation without advancing time");
    const auto callbacks = task.stats().callbacks;
    host.background.tick(20);
    require(task.stats().callbacks == callbacks, "Paused static publication introduced polling");
}

void revokedCleanup() {
    Host host(cdsdk::BackgroundReferenceHost::Mode::Deterministic);
    auto counters = std::make_shared<Counters>();
    cdsdk::BackgroundChannel channel(host.context(), std::make_shared<Model>(counters), sizeof(Frame));
    host.background.tick(0);
    host.background.cancel();
    channel.submit(1);  // Plugin UI teardown may run after host revocation.
    channel.pause(true);
    require(counters->disposed == 1, "Revoked channel cleanup retained a model");
}

void reloadRetirement() {
    Host host(cdsdk::BackgroundReferenceHost::Mode::Threaded);
    auto counters = std::make_shared<Counters>();
    cdsdk::BackgroundTask task(host.context());
    require(task.create(std::make_shared<Model>(counters), sizeof(Frame), 33000000) == CD_OK,
            "Create reload-retirement task");
    require(host.background.waitForTest(), "Reload task did not initialize");
    auto held = task.acquire();
    require(bool(held), "Hold snapshot through reference retirement");
    require(counters->metadata[held.slot()] == held.sequence(), "Snapshot/immutable geometry slots disagree");
    host.background.cancel();
    require(host.background.waitForTest(), "Reference retirement did not dispose the model");
    require(!host.background.retire(), "Reload released callback module references before its last reader");
    held.reset();
    require(host.background.retire() && counters->disposed == 1, "Reload retirement did not finish exactly once");
}

void idleWake() {
    Host host(cdsdk::BackgroundReferenceHost::Mode::Threaded);
    auto counters = std::make_shared<Counters>();
    counters->active = false;
    cdsdk::BackgroundTask task(host.context());
    require(task.create(std::make_shared<Model>(counters), sizeof(Frame), 33000000) == CD_OK,
            "Create input-driven reference model");
    require(host.background.waitForTest(), "Input-driven model did not initialize");
    uint64_t expected = 0;
    for (uint32_t value = 1; value <= 2048; ++value) {
        require(task.submit(1, std::as_bytes(std::span(&value, 1))) == CD_OK, "Submit to idle reference lane");
        require(host.background.waitForTest(), "Idle transition lost a reference wake");
        expected = expected * 131 + value;
        require(read(task).value == expected, "Input-driven reference lost or repeated a command");
    }
    task.cancel();
    require(host.background.waitForTest() && counters->disposed == 1, "Idle cancellation did not retire its model");
}

void threaded() {
    auto counters = std::make_shared<Counters>();
    auto host = std::make_unique<Host>(cdsdk::BackgroundReferenceHost::Mode::Threaded);
    auto task = std::make_unique<cdsdk::BackgroundTask>(host->context());
    require(task->create(std::make_shared<Model>(counters), sizeof(Frame), 16666667) == CD_OK,
            "Register threaded SDK reference");
    require(host->background.waitForTest(), "Threaded reference did not initialize");
    require(counters->owner != std::this_thread::get_id(), "Preview reference executes the model on the UI");
    auto held = task->acquire();
    require(bool(held), "Acquire preview reference snapshot");
    require(task->submit(9, {}) == CD_OK, "Inject releasable preview worker block");
    require(counters->metadata[held.slot()] == held.sequence(), "Leased geometry slot changed before release");
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!counters->blocked) {
        require(std::chrono::steady_clock::now() < timeout, "Reference blocker not reached");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto started = std::chrono::steady_clock::now();
    task.reset();
    host.reset();
    require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(50),
            "Preview deletion joined a blocked callback");
    {
        std::lock_guard lock(counters->mutex);
        counters->released = true;
    }
    counters->release.notify_all();
    while (!counters->disposed) {
        require(std::chrono::steady_clock::now() < timeout, "Reference model did not finish cancellation");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!counters->wrongThread && held.bytes().size() == sizeof(Frame), "Preview context/read lease lifetime");
}

void faultedChannelRead() {
    Host host(cdsdk::BackgroundReferenceHost::Mode::Threaded);
    auto counters = std::make_shared<Counters>();
    counters->active = false;
    counters->holdDispose = true;
    cdsdk::BackgroundChannel channel(host.context(), std::make_shared<Model>(counters), sizeof(Frame));
    const auto unblock = [&] {
        {
            std::lock_guard lock(counters->mutex);
            counters->released = true;
        }
        counters->release.notify_all();
    };
    bool rejected = false;
    try {
        require(host.background.waitForTest(), "Initialize a healthy channel before fault injection");
        require(bool(channel.acquire()), "Acquire a healthy channel publication");
        channel.submit(10);
        const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!counters->disposing) {
            require(std::chrono::steady_clock::now() < timeout, "Fault did not enter owner-lane destruction");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        try {
            channel.acquire();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
    } catch (...) {
        unblock();
        throw;
    }
    unblock();
    require(host.background.waitForTest(), "Complete the faulted model destruction");
    require(rejected && counters->disposed == 1 && !counters->wrongThread,
            "A previous healthy publication hid a fault while owner-lane destruction was still running");
}
}  // namespace

int main() try {
    deterministic();
    pausedPublication();
    revokedCleanup();
    reloadRetirement();
    idleWake();
    threaded();
    faultedChannelRead();
    std::cout << "SDK deterministic and single-lane threaded references: commands, snapshots, pause and cancellation "
                 "passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
