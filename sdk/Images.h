#pragma once
#include <CyberCanvas/Images.h>

#include <functional>
#include <memory>
#include <utility>

#include "sdk/PluginApi.h"

namespace cdsdk {
struct ImageSlotStats {
    uint64_t requests = 0, polls = 0, notifications = 0, reusedRequests = 0;
    uint64_t referencedBytes = 0, peakReferencedBytes = 0, referencedDecodeNanoseconds = 0;
    size_t ready = 0, pending = 0, failed = 0;
};

// UI-thread slots and completion delivery. Workers only update engine records.
// An optional host timer runs while loading; unbound clients explicitly poll().
class Images {
public:
    explicit Images(std::shared_ptr<cybercanvas::ImageStore> store = {});

    ~Images();

    Images(const Images&) = delete;

    Images& operator=(const Images&) = delete;

    void bind(const CdCreateContext& context);

    bool set(const std::string& slot, const std::filesystem::path& path, uint64_t revision = 0,
             uint32_t maximumEdge = 4096, cybercanvas::ImageRegion region = {},
             const std::vector<uint32_t>& palette = {},
             cybercanvas::ImageSampling sampling = cybercanvas::ImageSampling::Smooth);

    cybercanvas::ImageSnapshot get(const std::string& slot) const;

    void erase(const std::string& slot);

    void clear();

    bool poll();

    bool pending() const;

    bool retry(const std::string& name);

    void pause(bool paused);

    bool timerActive() const;

    void onChange(std::function<void()> callback);

    ImageSlotStats stats() const;

    cybercanvas::ImageStats sharedStoreStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
