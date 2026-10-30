#include <CyberCanvas/Images.h>
#include <Windows.h>
#include <sdk/Images.h>
#include <sdk/PluginApi.h>
#include <sdk/Update.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "sdk/services/ImageSkins.h"
#include "sdk/services/RenderImages.h"
#include "sdk/services/RenderMemory.h"

namespace cdsdk {
class Images::Impl {
public:
    // The default shared worker is acquired lazily on the first request.
    explicit Impl(std::shared_ptr<cybercanvas::ImageStore> store = {}) : store_(std::move(store)) {}

    ~Impl() {
        if (skinToken_ && skins_.unsubscribe) {
            skins_.unsubscribe(context_, skinToken_);
        }
        stop();
        if (telemetry_.update) {
            telemetry_.update(context_, uint64_t(reinterpret_cast<uintptr_t>(this)), 0, 0, 0);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void bind(const CdCreateContext& context) {
        if (skinToken_ && skins_.unsubscribe) {
            skins_.unsubscribe(context_, skinToken_);
        }
        skinToken_ = 0;
        skins_ = {};
        stop();
        host_ = {};
        context_ = context.host_context;
        telemetry_ = {};
        if (context.host && context.host->struct_size >= sizeof(CdHostApi)) {
            host_ = *context.host;
        }
        timer_.bind(host_, context_);
        if (context.host && context.host->struct_size >= sizeof(CdServiceHostApi)) {
            const auto* serviceHost = reinterpret_cast<const CdServiceHostApi*>(context.host);
            CdRenderMemoryApi memory{};
            if (serviceHost->query_service &&
                serviceHost->query_service(context_, CD_SERVICE_IMAGE_SKINS, 1, &skins_, sizeof(skins_)) == CD_OK &&
                skins_.struct_size >= sizeof(skins_) && skins_.version == 1 && skins_.resolve && skins_.subscribe &&
                skins_.unsubscribe) {
                if (skins_.subscribe(context_, skinChanged, this, &skinToken_) != CD_OK) {
                    skins_ = {};
                }
            } else {
                skins_ = {};
            }
            if (!serviceHost->query_service ||
                serviceHost->query_service(context_, CD_SERVICE_RENDER_IMAGES, 1, &telemetry_, sizeof(telemetry_)) !=
                    CD_OK ||
                telemetry_.struct_size < sizeof(telemetry_) || telemetry_.version != 1 || !telemetry_.update) {
                telemetry_ = {};
            }
            if (serviceHost->query_service &&
                serviceHost->query_service(context_, CD_SERVICE_RENDER_MEMORY, 1, &memory, sizeof(memory)) == CD_OK &&
                memory.struct_size >= sizeof(memory) && memory.version == 1 && memory.reserve) {
                if (!store_) {
                    store_ = cybercanvas::ImageStore::shared();
                }
                store_->resourceAccount({memory.resource_context, memory.reserve});
            }
        }
        if (!slots_.empty()) {
            skinChanged(this, 0);
        }
        schedule();
    }

    bool set(const std::string& slot, const std::filesystem::path& path, uint64_t revision = 0,
             uint32_t maximumEdge = 4096, cybercanvas::ImageRegion region = {},
             const std::vector<uint32_t>& palette = {},
             cybercanvas::ImageSampling sampling = cybercanvas::ImageSampling::Smooth) {
        if (slot.empty() || slot.size() > 256 || path.empty() || !maximumEdge || maximumEdge > 4096 ||
            palette.size() > 256 ||
            static_cast<unsigned>(sampling) > static_cast<unsigned>(cybercanvas::ImageSampling::Nearest)) {
            throw std::invalid_argument("Invalid image slot");
        }
        const auto original = std::filesystem::absolute(path).lexically_normal();
        const auto sourceRevision = revision;
        auto found = slots_.find(slot);
        if (candidate_.empty() && found != slots_.end() && found->second.source == original &&
            found->second.sourceRevision == sourceRevision && found->second.edge == maximumEdge &&
            found->second.region == region && found->second.palette == palette && found->second.sampling == sampling) {
            return false;
        }
        const auto normalized = resolveSkin(original, revision);
        if (candidate_.contains(slot)) {
            auto& next = candidate_[slot];
            if (next.path != normalized || next.revision != revision || next.edge != maximumEdge ||
                next.region != region || next.palette != palette || next.sampling != sampling) {
                next.path = normalized;
                next.revision = revision;
                next.source = original;
                next.sourceRevision = sourceRevision;
                next.edge = maximumEdge;
                next.region = region;
                next.palette = palette;
                next.sampling = sampling;
                next.handle = store_->request(normalized, revision, maximumEdge, region, palette, sampling);
                next.retryAt = 0;
                next.retryDelay = 1000;
                next.lastError.clear();
                ++stats_.requests;
                stats_.reusedRequests += next.handle.stats().reused;
                next.observed = next.handle.snapshot();
            }
            schedule();
            return false;
        }
        if (found != slots_.end() && found->second.path == normalized && found->second.revision == revision &&
            found->second.edge == maximumEdge && found->second.region == region && found->second.palette == palette &&
            found->second.sampling == sampling) {
            return false;
        }
        if (found == slots_.end() && !candidate_.contains(slot) &&
            (candidate_.empty() ? slots_.size() : candidate_.size()) >= 128) {
            throw std::length_error("Image slot limit reached");
        }
        slots_.erase(slot);
        if (!store_) {
            store_ = cybercanvas::ImageStore::shared();
        }
        Slot next{normalized,
                  revision,
                  maximumEdge,
                  region,
                  palette,
                  sampling,
                  store_->request(normalized, revision, maximumEdge, region, palette, sampling)};
        next.source = original;
        next.sourceRevision = sourceRevision;
        ++stats_.requests;
        stats_.reusedRequests += next.handle.stats().reused;
        next.observed = next.handle.snapshot();
        auto& destination = candidate_.empty() ? slots_ : candidate_;
        destination.emplace(slot, std::move(next));
        stats();
        notify();
        schedule();
        return true;
    }

    cybercanvas::ImageSnapshot get(const std::string& slot) const {
        const auto found = slots_.find(slot);
        return found == slots_.end() ? cybercanvas::ImageSnapshot{} : found->second.handle.snapshot();
    }

    void erase(const std::string& slot) {
        const auto staged = candidate_.erase(slot);
        if (slots_.erase(slot) || staged) {
            notify();
            schedule();
        }
    }

    void clear() {
        candidate_.clear();
        const bool changed = !slots_.empty();
        slots_.clear();
        stop();
        if (changed || deferred_) {
            deferred_ = false;
            notify();
        }
    }

    bool poll() {
        ++stats_.polls;
        if (paused_) {
            return false;
        }
        const bool skinCommitted = commitSkin();
        bool changed = false;
        auto& observed = candidate_.empty() ? slots_ : candidate_;
        for (auto& [id, slot] : observed) {
            auto current = slot.handle.snapshot();
            if (current != slot.observed) {
                slot.observed = std::move(current);
                changed = true;
            }
            if (slot.observed.state == cybercanvas::ImageState::Failed) {
                if (slot.lastError != slot.observed.error && host_.log) {
                    const auto path = slot.path.generic_u8string();
                    const std::string message =
                        "Image load failed: " + std::string(reinterpret_cast<const char*>(path.data()), path.size()) +
                        " / " + slot.observed.error;
                    host_.log(context_, 2, message.data(), uint32_t(message.size()));
                }
                slot.lastError = slot.observed.error;
                if (!candidate_.empty() || slot.observed.error.find("budget") != std::string::npos) {
                    const auto now = GetTickCount64();
                    if (!slot.retryAt) {
                        slot.retryAt = now + slot.retryDelay;
                    } else if (now >= slot.retryAt) {
                        store_->retry(slot.handle);
                        slot.observed = slot.handle.snapshot();
                        slot.retryAt = 0;
                        slot.retryDelay = std::min(30000u, slot.retryDelay * 2);
                        changed = true;
                    }
                }
            } else if (slot.observed.state == cybercanvas::ImageState::Ready) {
                slot.retryAt = 0;
                slot.retryDelay = 1000;
                slot.lastError.clear();
            }
        }
        changed |= deferred_ || skinCommitted;
        deferred_ = false;
        if (changed) {
            stats();
            notify();
        }
        schedule();
        return changed;
    }

    bool pending() const {
        if (!candidate_.empty()) {
            return true;
        }
        for (const auto& [id, slot] : slots_) {
            if (slot.handle.snapshot().state == cybercanvas::ImageState::Loading) {
                return true;
            }
        }
        return false;
    }

    bool retry(const std::string& name) {
        auto& destination = candidate_.empty() ? slots_ : candidate_;
        const auto found = destination.find(name);
        if (found == destination.end()) {
            return false;
        }
        auto& slot = found->second;
        store_->retry(slot.handle);
        slot.observed = slot.handle.snapshot();
        slot.retryAt = 0;
        notify();
        schedule();
        return true;
    }

    void pause(bool paused) {
        paused_ = paused;
        if (paused_) {
            stop();
        } else {
            poll();
        }
    }

    bool timerActive() const {
        return bool(timer_);
    }

    void onChange(std::function<void()> callback) {
        changed_ = std::move(callback);
    }

    ImageSlotStats stats() const {
        auto result = stats_;
        std::set<std::tuple<std::filesystem::path, uint64_t, uint32_t, cybercanvas::ImageRegion, std::vector<uint32_t>,
                            cybercanvas::ImageSampling>>
            resources;
        const auto collect = [&](const auto& slots) {
            for (const auto& [id, slot] : slots) {
                const auto request = slot.handle.stats();
                result.ready += request.state == cybercanvas::ImageState::Ready;
                result.pending += request.state == cybercanvas::ImageState::Loading;
                result.failed += request.state == cybercanvas::ImageState::Failed;
                if (resources.emplace(slot.path, slot.revision, slot.edge, slot.region, slot.palette, slot.sampling)
                        .second) {
                    result.referencedBytes += request.payloadBytes;
                    result.referencedDecodeNanoseconds += request.decodeNanoseconds;
                }
            }
        };
        collect(slots_);
        collect(candidate_);
        stats_.peakReferencedBytes = std::max(stats_.peakReferencedBytes, result.referencedBytes);
        result.peakReferencedBytes = stats_.peakReferencedBytes;
        if (telemetry_.update) {
            telemetry_.update(context_, uint64_t(reinterpret_cast<uintptr_t>(this)), uint32_t(result.ready),
                              uint32_t(result.pending), uint32_t(result.failed));
        }
        return result;
    }

    // Store-wide values may be shared by several instances; do not sum them
    // across cards as if each owned another decoder or another pixel buffer.
    cybercanvas::ImageStats sharedStoreStats() const {
        return store_ ? store_->stats() : cybercanvas::ImageStats{};
    }

private:
    struct Slot {
        std::filesystem::path path;
        uint64_t revision = 0;
        uint32_t edge = 4096;
        cybercanvas::ImageRegion region;
        std::vector<uint32_t> palette;
        cybercanvas::ImageSampling sampling = cybercanvas::ImageSampling::Smooth;
        cybercanvas::ImageHandle handle;
        cybercanvas::ImageSnapshot observed;
        uint64_t retryAt = 0;
        uint32_t retryDelay = 1000;
        std::string lastError;
        std::filesystem::path source;
        uint64_t sourceRevision = 0;
    };

    std::filesystem::path resolveSkin(const std::filesystem::path& original, uint64_t& revision) const {
        if (!skins_.resolve) {
            return original;
        }
        const auto input = original.u8string();
        std::array<char, 32768> output;
        output.back() = '\0';
        uint64_t skinRevision = 0;
        if (skins_.resolve(context_, reinterpret_cast<const char*>(input.c_str()), output.data(),
                           uint32_t(output.size()), &skinRevision) != CD_OK) {
            return original;
        }
        revision ^= skinRevision * UINT64_C(0x9e3779b97f4a7c15);
        return std::filesystem::u8path(output.data());
    }

    static void CD_CALL skinChanged(void* pointer, uint64_t) noexcept {
        try {
            auto& self = *static_cast<Impl*>(pointer);
            self.candidate_.clear();
            for (const auto& [name, slot] : self.slots_) {
                auto next = slot;
                next.revision = slot.sourceRevision;
                next.path = self.resolveSkin(slot.source, next.revision);
                next.handle =
                    self.store_->request(next.path, next.revision, next.edge, next.region, next.palette, next.sampling);
                ++self.stats_.requests;
                self.stats_.reusedRequests += next.handle.stats().reused;
                next.observed = next.handle.snapshot();
                self.candidate_.emplace(name, std::move(next));
            }
            self.deferred_ = true;
            self.stats();
            self.schedule();
        } catch (...) {
            static_cast<Impl*>(pointer)->candidate_.clear();
        }
    }

    bool commitSkin() {
        if (candidate_.empty()) {
            return false;
        }
        for (const auto& [name, slot] : candidate_) {
            if (slot.handle.snapshot().state != cybercanvas::ImageState::Ready) {
                return false;
            }
        }
        slots_ = std::move(candidate_);
        candidate_.clear();
        return true;
    }

    void notify() {
        stats();
        ++stats_.notifications;
        if (paused_) {
            deferred_ = true;
        } else if (changed_) {
            changed_();
        } else if (host_.request_redraw) {
            host_.request_redraw(context_);
        }
    }

    void stop() {
        timer_.stop();
    }

    void schedule() {
        // Retain the timer until completed results have been delivered on the UI thread.
        bool awaiting = !candidate_.empty();
        for (const auto& [id, slot] : slots_) {
            awaiting |= slot.observed.state == cybercanvas::ImageState::Loading || slot.retryAt != 0;
        }
        if (paused_ || !awaiting) {
            stop();
        } else if (!timer_ && host_.subscribe_timer && host_.unsubscribe) {
            timer_.replace(33, tick, this);
        }
    }

    static void CD_CALL tick(void* pointer, uint64_t) noexcept {
        try {
            static_cast<Impl*>(pointer)->poll();
        } catch (...) {
            static_cast<Impl*>(pointer)->stop();
        }
    }

    std::shared_ptr<cybercanvas::ImageStore> store_;
    std::map<std::string, Slot> slots_;
    std::map<std::string, Slot> candidate_;
    CdImageSkinsApi skins_{};
    uint64_t skinToken_ = 0;
    CdHostApi host_{};
    void* context_ = nullptr;
    Timer timer_;
    bool paused_ = false, deferred_ = false;
    std::function<void()> changed_;
    mutable ImageSlotStats stats_;
    CdRenderImagesApi telemetry_{};
};

Images::Images(std::shared_ptr<cybercanvas::ImageStore> store) : impl_(std::make_unique<Impl>(std::move(store))) {}

void Images::bind(const CdCreateContext& context) {
    return impl_->bind(context);
}

bool Images::set(const std::string& slot, const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge,
                 cybercanvas::ImageRegion region, const std::vector<uint32_t>& palette,
                 cybercanvas::ImageSampling sampling) {
    return impl_->set(slot, path, revision, maximumEdge, region, palette, sampling);
}

cybercanvas::ImageSnapshot Images::get(const std::string& slot) const {
    return impl_->get(slot);
}

void Images::erase(const std::string& slot) {
    return impl_->erase(slot);
}

void Images::clear() {
    return impl_->clear();
}

bool Images::poll() {
    return impl_->poll();
}

bool Images::pending() const {
    return impl_->pending();
}

bool Images::retry(const std::string& name) {
    return impl_->retry(name);
}

void Images::pause(bool paused) {
    return impl_->pause(paused);
}

bool Images::timerActive() const {
    return impl_->timerActive();
}

void Images::onChange(std::function<void()> callback) {
    return impl_->onChange(std::move(callback));
}

ImageSlotStats Images::stats() const {
    return impl_->stats();
}

cybercanvas::ImageStats Images::sharedStoreStats() const {
    return impl_->sharedStoreStats();
}

Images::~Images() = default;
}  // namespace cdsdk
