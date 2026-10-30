#pragma once

#include <compare>
#include <filesystem>

#include "CyberCanvas/Image.h"
#include "CyberCanvas/MemoryBudget.h"
#include "CyberCanvas/ResourceAccount.h"

namespace cybercanvas {
// Source pixels, clipped by WIC before scaling. An empty rectangle selects the
// entire source; decoded memory contains only this region at the requested DPI.
struct ImageRegion {
    uint32_t x = 0, y = 0, width = 0, height = 0;
    auto operator<=>(const ImageRegion&) const = default;
};

struct ImageLimits {
    uint64_t decodedBytes = 64 * 1024 * 1024;
    uint64_t fileBytes = 64 * 1024 * 1024;
    size_t entries = 128;
};

struct ImageStats {
    uint64_t decodedBytes = 0, peakDecodedBytes = 0;
    uint64_t decodes = 0, cacheHits = 0, failures = 0;
    size_t entries = 0, pending = 0;
    uint64_t decodeNanoseconds = 0;
};

struct ImageRequestStats {
    ImageState state = ImageState::Empty;
    uint64_t decodeNanoseconds = 0, payloadBytes = 0;
    size_t sharingRequests = 0;
    bool reused = false;
};

class ImageHandle {
public:
    ImageHandle() = default;
    ImageSnapshot snapshot() const;
    ImageRequestStats stats() const;
    // Releases this handle's interest. Copies keep their own interest alive.
    void cancel();

private:
    friend class ImageStore;
    struct Interest;
    std::shared_ptr<Interest> interest_;
    bool cancelled_ = false;
};

// One bounded worker, no callbacks. Poll snapshots on the owning UI thread.
// Share a store across instances to share requests and decoded pixel memory.
class ImageStore {
public:
    explicit ImageStore(ImageLimits limits = {}, std::shared_ptr<MemoryBudget> sharedBudget = {});
    ~ImageStore();
    ImageStore(const ImageStore&) = delete;
    ImageStore& operator=(const ImageStore&) = delete;

    // Purely lexical path normalization; file I/O and WIC run on the worker.
    // revision distinguishes replaced files; old requests remain independent.
    ImageHandle request(const std::filesystem::path& path, uint64_t revision = 0, uint32_t maximumEdge = 4096);
    // Photo-only EXIF orientation; atlas and skin coordinate systems are unchanged.
    ImageHandle requestOriented(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge);
    ImageHandle request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge, ImageRegion region);
    // Optional ARGB palette, including transparent entry when needed. Indexed
    // resources use nearest-neighbor decoding, binary alpha and no dithering.
    ImageHandle request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge, ImageRegion region,
                        const std::vector<uint32_t>& palette);
    // Sampling is independent of palette conversion: pixel art can retain its
    // original colors and alpha while reducing to the actual display size.
    ImageHandle request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge, ImageRegion region,
                        const std::vector<uint32_t>& palette, ImageSampling sampling);
    void trim();
    void resourceAccount(ResourceAccount account);
    bool retry(const ImageHandle& handle);
    ImageStats stats() const;
    static std::shared_ptr<ImageStore> shared();

private:
    ImageHandle requestInternal(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge,
                                ImageRegion region, const std::vector<uint32_t>& palette, ImageSampling sampling,
                                bool oriented);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
