#pragma once

#include <filesystem>
#include <functional>
#include <stop_token>
#include <vector>

#include "CyberCanvas/Animation.h"
#include "CyberCanvas/Image.h"
#include "CyberCanvas/MemoryBudget.h"

namespace cybercanvas {
struct AnimationInfo {
    uint32_t width = 0, height = 0;
    std::vector<double> frameEnds;  // Strictly increasing seconds, at most 10000 frames.

    double duration() const;

    uint32_t frameAt(double seconds) const;
};

struct AnimationSnapshot {
    ImageState state = ImageState::Empty;
    std::shared_ptr<const AnimationInfo> info;
    std::shared_ptr<const ImageData> image;
    uint32_t frame = 0;
    uint64_t revision = 0;
    std::string error;
};

struct AssetContext {
    std::filesystem::path root;
    std::shared_ptr<MemoryBudget> budget;
    uint64_t maximumFileBytes = 8 * 1024 * 1024;
    std::stop_token stop;
    // Worker-only; canonical containment check blocks .., links, ADS and URLs.
    std::filesystem::path resolve(const std::filesystem::path& relative) const;
    std::vector<uint8_t> read(const std::filesystem::path& relative) const;
};

// Every decoder method and destructor runs on the store's single worker.
// Implementations must check stop during multi-frame replay and reserve their
// retained/temporary pixel buffers. Returned immutable frames retain leases.
class AnimationSource {
public:
    virtual ~AnimationSource() = default;
    virtual const AnimationInfo& info() const = 0;
    virtual std::shared_ptr<const ImageData> frame(uint32_t index, uint32_t width, uint32_t height) = 0;
};

using AnimationSourceFactory = std::function<std::unique_ptr<AnimationSource>(const AssetContext&)>;

enum class RasterAnimationKind { Gif, Sequence, Atlas };

struct RasterAnimation {
    RasterAnimationKind kind = RasterAnimationKind::Gif;
    std::vector<std::filesystem::path> files;
    std::vector<double> durations;
    uint32_t columns = 1, rows = 1;
};

AnimationSourceFactory rasterAnimation(RasterAnimation request);

class AnimationHandle {
public:
    AnimationHandle() = default;
    AnimationSnapshot snapshot() const;
    // Latest request wins; retains the previous frame while decoding. Zero
    // dimensions request the source dimensions. All sizes are physical pixels.
    void seek(double seconds, uint32_t width = 0, uint32_t height = 0);
    void cancel();

private:
    friend class AnimationStore;
    struct Interest;
    std::shared_ptr<Interest> interest_;
    bool cancelled_ = false;
};

// One bounded worker shared by instances, no UI callbacks. Requests are bounded
// and coalesced. Unload joins the worker before destroying decoder state.
class AnimationStore {
public:
    explicit AnimationStore(std::shared_ptr<MemoryBudget> budget = std::make_shared<MemoryBudget>());
    ~AnimationStore();
    AnimationStore(const AnimationStore&) = delete;
    AnimationStore& operator=(const AnimationStore&) = delete;
    AnimationHandle request(std::filesystem::path packageRoot, AnimationSourceFactory factory);
    std::shared_ptr<MemoryBudget> budget() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// No timers or callbacks: host uses active() to schedule local refresh and
// takeCompleted() on its update path. Hidden/reduced-motion freezes the clock.
class FramePlayer {
public:
    explicit FramePlayer(AnimationHandle handle, Repeat repeat = Repeat::Loop);
    void tick(uint64_t monotonicMilliseconds);
    void advance(double seconds);
    void seek(double seconds);
    void paused(bool value);
    void visible(bool value);
    void reducedMotion(bool value);
    void speed(double value);
    void size(uint32_t width, uint32_t height);
    bool active() const;
    bool takeCompleted();
    AnimationSnapshot snapshot() const;
    double position() const;

private:
    void update();
    void synchronizePause();
    AnimationHandle handle_;
    AnimationClock clock_;
    Repeat repeat_;
    uint32_t width_ = 0, height_ = 0;
    bool paused_ = false, visible_ = true, reduced_ = false, finished_ = false, completed_ = false;
};
}  // namespace cybercanvas
