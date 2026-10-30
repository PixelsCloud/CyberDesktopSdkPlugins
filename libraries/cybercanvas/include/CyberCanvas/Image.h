#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cybercanvas {
enum class ImageState { Empty, Loading, Ready, Failed, Cancelled };
enum class ImageFit { Contain, Cover, Stretch };
enum class ImageSampling { Smooth, Nearest };

// Immutable, tightly packed premultiplied BGRA8. WIC objects never cross threads.
class ImageData {
public:
    // Adopt decoded CPU pixels without another copy. No file I/O, COM or upload;
    // callers supply tightly packed, premultiplied BGRA and relinquish mutation.
    static std::shared_ptr<const ImageData> fromPremultipliedBgra(uint32_t width, uint32_t height,
                                                                  std::vector<uint8_t> pixels);

    ImageData(const ImageData&) = delete;
    ImageData& operator=(const ImageData&) = delete;

    uint64_t id() const;

    uint32_t width() const;

    uint32_t height() const;

    uint32_t stride() const;

    const std::vector<uint8_t>& pixels() const;

private:
    friend class ImageStore;
    ImageData() = default;
    inline static std::atomic<uint64_t> nextId_{1};
    uint64_t id_ = nextId_.fetch_add(1, std::memory_order_relaxed);
    uint32_t width_ = 0, height_ = 0;
    std::vector<uint8_t> pixels_;
};

struct ImageSnapshot {
    ImageState state = ImageState::Empty;
    std::shared_ptr<const ImageData> image;
    std::string error;
    std::shared_ptr<const std::wstring> source;
    bool operator==(const ImageSnapshot&) const = default;
};

struct ImagePlacement {
    ImageFit fit = ImageFit::Contain;
    uint32_t cell = 0, cells = 1;
    double opacity = 1;
    // Disable when an existing mask/clip or the Contain/Stretch bounds suffice.
    bool clip = true;
    ImageSampling sampling = ImageSampling::Smooth;
};
}  // namespace cybercanvas
