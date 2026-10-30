#include "CyberCanvas/AnimatedImages.h"

#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>

#include "CyberCanvas/Native.h"

namespace cybercanvas {
uint32_t AnimationInfo::frameAt(double seconds) const {
    if (frameEnds.empty() || !std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid animation position");
    }
    return uint32_t(std::min<size_t>(std::upper_bound(frameEnds.begin(), frameEnds.end(), seconds) - frameEnds.begin(),
                                     frameEnds.size() - 1));
}

std::filesystem::path AssetContext::resolve(const std::filesystem::path& relative) const {
    if (stop.stop_requested()) {
        throw std::runtime_error("Animation cancelled");
    }
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.native().find_first_of(L":\0", 0, 2) != std::wstring::npos) {
        throw std::invalid_argument("Asset path must be package-relative");
    }
    const auto base = std::filesystem::canonical(root);
    const auto path = std::filesystem::canonical(base / relative);
    auto a = base.begin(), b = path.begin();
    for (; a != base.end(); ++a, ++b) {
        if (b == path.end() || _wcsicmp(a->c_str(), b->c_str()) != 0) {
            throw std::invalid_argument("Asset path escapes package root");
        }
    }
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maximumFileBytes) {
        throw std::length_error("Animation file size limit exceeded");
    }
    return path;
}

std::vector<uint8_t> AssetContext::read(const std::filesystem::path& relative) const {
    const auto path = resolve(relative);
    const auto size = std::filesystem::file_size(path);
    if (size > maximumFileBytes) {
        throw std::length_error("Animation file size limit exceeded");
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes(size_t(size), 0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) {
        throw std::runtime_error("Cannot read animation asset");
    }
    return bytes;
}

namespace {
struct Record {
    AnimationSnapshot result{ImageState::Loading};
    AssetContext context;
    AnimationSourceFactory factory;
    std::stop_source cancellation;
    std::unique_ptr<AnimationSource> source;
    std::shared_ptr<const AnimationInfo> info;
    double seconds = 0;
    uint32_t width = 0, height = 0;
    uint64_t generation = 1, processed = 0;
    std::atomic<size_t> users = 0;

    struct Cached {
        uint32_t frame, width, height;
        std::shared_ptr<const ImageData> image;
    };

    std::list<Cached> cache;
};

struct Queue {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::shared_ptr<Record>> records;
    std::shared_ptr<MemoryBudget> budget;
    bool stopping = false;
};

void dimensions(uint32_t width, uint32_t height) {
    if (width > 4096 || height > 4096 || bool(width) != bool(height)) {
        throw std::invalid_argument("Invalid animation frame dimensions");
    }
}
}  // namespace

struct AnimationHandle::Interest {
    std::shared_ptr<Queue> queue;
    std::shared_ptr<Record> record;

    Interest(std::shared_ptr<Queue> q, std::shared_ptr<Record> r) : queue(std::move(q)), record(std::move(r)) {
        ++record->users;
    }

    ~Interest() {
        if (--record->users == 0) {
            record->cancellation.request_stop();
        }
        queue->wake.notify_one();
    }
};

AnimationSnapshot AnimationHandle::snapshot() const {
    if (!interest_) {
        return {cancelled_ ? ImageState::Cancelled : ImageState::Empty};
    }
    std::lock_guard lock(interest_->queue->mutex);
    return interest_->record->result;
}

void AnimationHandle::seek(double seconds, uint32_t width, uint32_t height) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9) {
        throw std::invalid_argument("Invalid animation seek");
    }
    dimensions(width, height);
    if (!interest_) {
        return;
    }
    const auto& q = interest_->queue;
    std::lock_guard lock(q->mutex);
    auto& r = *interest_->record;
    if (q->stopping || r.result.state == ImageState::Failed) {
        return;
    }
    const bool sameFrame =
        r.result.info ? r.result.info->frameAt(seconds) == r.result.info->frameAt(r.seconds) : seconds == r.seconds;
    if (sameFrame && width == r.width && height == r.height) {
        return;
    }
    r.seconds = seconds;
    r.width = width;
    r.height = height;
    ++r.generation;
    r.result.state = ImageState::Loading;
    q->wake.notify_one();
}

void AnimationHandle::cancel() {
    interest_.reset();
    cancelled_ = true;
}

struct AnimationStore::Impl {
    std::shared_ptr<Queue> queue = std::make_shared<Queue>();
    std::jthread worker;

    explicit Impl(std::shared_ptr<MemoryBudget> budget) {
        if (!budget) {
            throw std::invalid_argument("Missing animation memory budget");
        }
        queue->budget = std::move(budget);
        worker = std::jthread([this] {
            run();
        });
    }

    ~Impl() {
        {
            std::lock_guard lock(queue->mutex);
            queue->stopping = true;
            for (const auto& record : queue->records) {
                record->cancellation.request_stop();
            }
        }
        worker.request_stop();
        queue->wake.notify_all();
        worker.join();
    }

    void run() noexcept {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            std::shared_ptr<Record> record;
            uint64_t generation = 0;
            double seconds = 0;
            uint32_t width = 0, height = 0;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] {
                    return queue->stopping ||
                           std::any_of(queue->records.begin(), queue->records.end(), [](const auto& r) {
                               return !r->users || r->processed != r->generation;
                           });
                });
                const auto unused = std::find_if(queue->records.begin(), queue->records.end(), [](const auto& r) {
                    return !r->users;
                });
                if (unused != queue->records.end()) {
                    auto retired = std::move(*unused);
                    queue->records.erase(unused);
                    lock.unlock();
                    // Native decoder teardown must not block UI snapshot/seek calls.
                    retired->source.reset();
                    retired->cache.clear();
                    continue;
                }
                if (queue->stopping) {
                    auto retired = std::move(queue->records);
                    for (auto& r : retired) {
                        r->result = {ImageState::Cancelled};
                    }
                    lock.unlock();
                    for (auto& r : retired) {
                        r->source.reset();
                        r->cache.clear();
                    }
                    break;
                }
                for (const auto& r : queue->records) {
                    if (r->processed != r->generation) {
                        record = r;
                        break;
                    }
                }
                if (!record) {
                    continue;
                }
                generation = record->generation;
                seconds = record->seconds;
                width = record->width;
                height = record->height;
            }
            AnimationSnapshot result;
            try {
                require(initialized);
                if (!record->source) {
                    record->context.stop = record->cancellation.get_token();
                    record->source = record->factory(record->context);
                    if (!record->source) {
                        throw std::runtime_error("Animation decoder did not create a source");
                    }
                    const auto& info = record->source->info();
                    dimensions(info.width, info.height);
                    if (!info.width || info.frameEnds.empty() || info.frameEnds.size() > 10000) {
                        throw std::length_error("Invalid animation metadata");
                    }
                    double previous = 0;
                    for (auto end : info.frameEnds) {
                        if (!std::isfinite(end) || end <= previous || end > 86400) {
                            throw std::invalid_argument("Invalid animation durations");
                        }
                        previous = end;
                    }
                    record->info = std::make_shared<AnimationInfo>(info);
                }
                result.info = record->info;
                result.frame = result.info->frameAt(seconds);
                if (!width) {
                    width = result.info->width;
                    height = result.info->height;
                }
                auto found = std::find_if(record->cache.begin(), record->cache.end(), [&](const auto& c) {
                    return c.frame == result.frame && c.width == width && c.height == height;
                });
                if (found != record->cache.end()) {
                    result.image = found->image;
                    record->cache.splice(record->cache.begin(), record->cache, found);
                } else {
                    if (record->cache.size() >= 3) {
                        record->cache.pop_back();
                    }
                    try {
                        result.image = record->source->frame(result.frame, width, height);
                    } catch (const std::length_error&) {
                        // Release cached frames, but never revoke snapshots held by callers.
                        record->cache.clear();
                        result.image = record->source->frame(result.frame, width, height);
                    }
                    if (!result.image || result.image->width() != width || result.image->height() != height) {
                        throw std::runtime_error("Animation decoder returned incorrect dimensions");
                    }
                    record->cache.push_front({result.frame, width, height, result.image});
                }
                result.state = ImageState::Ready;
            } catch (const std::exception& error) {
                result.state = ImageState::Failed;
                result.error = error.what();
            } catch (...) {
                result.state = ImageState::Failed;
                result.error = "Animation decode failed";
            }
            if (result.state == ImageState::Failed) {
                record->source.reset();
                record->cache.clear();
                record->info.reset();
            }
            {
                std::lock_guard lock(queue->mutex);
                record->processed = generation;
                if (generation == record->generation && record->users) {
                    result.revision = generation;
                    record->result = std::move(result);
                }
                // Rotate serviced records so a rapidly changing clip cannot starve others.
                const auto found = std::find(queue->records.begin(), queue->records.end(), record);
                if (found != queue->records.end()) {
                    std::rotate(found, found + 1, queue->records.end());
                }
            }
        }
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
    }
};

AnimationStore::AnimationStore(std::shared_ptr<MemoryBudget> budget)
    : impl_(std::make_unique<Impl>(std::move(budget))) {}

AnimationStore::~AnimationStore() = default;

AnimationHandle AnimationStore::request(std::filesystem::path root, AnimationSourceFactory factory) {
    if (root.empty() || !factory) {
        throw std::invalid_argument("Missing animation package or factory");
    }
    auto record = std::make_shared<Record>();
    record->context = {std::filesystem::absolute(root).lexically_normal(), impl_->queue->budget};
    record->factory = std::move(factory);
    AnimationHandle handle;
    handle.interest_ = std::make_shared<AnimationHandle::Interest>(impl_->queue, record);
    std::lock_guard lock(impl_->queue->mutex);
    if (impl_->queue->records.size() >= 128) {
        throw std::length_error("Animation request limit reached");
    }
    impl_->queue->records.push_back(record);
    impl_->queue->wake.notify_one();
    return handle;
}

std::shared_ptr<MemoryBudget> AnimationStore::budget() const {
    return impl_->queue->budget;
}

FramePlayer::FramePlayer(AnimationHandle handle, Repeat repeat) : handle_(std::move(handle)), repeat_(repeat) {}

void FramePlayer::tick(uint64_t milliseconds) {
    clock_.tick(milliseconds);
    update();
}

void FramePlayer::advance(double seconds) {
    clock_.advance(seconds);
    update();
}

void FramePlayer::seek(double seconds) {
    clock_.seek(seconds);
    clock_.resetAnchor();
    finished_ = completed_ = false;
    synchronizePause();
    update();
}

void FramePlayer::paused(bool value) {
    paused_ = value;
    synchronizePause();
}

void FramePlayer::visible(bool value) {
    visible_ = value;
    synchronizePause();
}

void FramePlayer::reducedMotion(bool value) {
    reduced_ = value;
    synchronizePause();
}

void FramePlayer::speed(double value) {
    clock_.setSpeed(value);
}

void FramePlayer::size(uint32_t width, uint32_t height) {
    dimensions(width, height);
    width_ = width;
    height_ = height;
    update();
}

void FramePlayer::synchronizePause() {
    clock_.setPaused(paused_ || !visible_ || reduced_ || finished_);
}

bool FramePlayer::active() const {
    const auto state = handle_.snapshot();
    return !paused_ && visible_ && !reduced_ && !finished_ && clock_.speed() > 0 && state.info &&
           state.info->frameEnds.size() > 1 && state.state != ImageState::Failed &&
           state.state != ImageState::Cancelled;
}

bool FramePlayer::takeCompleted() {
    return std::exchange(completed_, false);
}

AnimationSnapshot FramePlayer::snapshot() const {
    return handle_.snapshot();
}

double FramePlayer::position() const {
    return clock_.seconds();
}

void FramePlayer::update() {
    const auto snapshot = handle_.snapshot();
    if (!snapshot.info) {
        return;
    }
    const double duration = snapshot.info->duration();
    double position = clock_.seconds();
    if (repeat_ == Repeat::Once) {
        if (position >= duration && !finished_) {
            finished_ = completed_ = true;
            synchronizePause();
        }
        position = std::min(position, duration);
    } else {
        position = std::fmod(position, duration * (repeat_ == Repeat::PingPong ? 2 : 1));
        if (position > duration) {
            position = duration * 2 - position;
        }
    }
    handle_.seek(position, width_, height_);
}
}  // namespace cybercanvas

namespace cybercanvas {
auto AnimationInfo::duration() const -> double {
    return frameEnds.empty() ? 0 : frameEnds.back();
}
}  // namespace cybercanvas
