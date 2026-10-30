#include "tools/PreviewIcons.h"

#include <thorvg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "tools/BundledIcons.h"

namespace cdsdk {
namespace {
void check(tvg::Result result) {
    if (result != tvg::Result::Success) {
        throw std::runtime_error("Lucide SVG rasterization failed");
    }
}
}  // namespace

struct PreviewIcons::Impl {
    using Key = std::tuple<std::string, uint32_t, uint32_t>;
    std::map<Key, std::vector<uint8_t>> masks;
    size_t bytes = 0;
    bool initialized = false;

    ~Impl() {
        if (initialized) {
            tvg::Initializer::term();
        }
    }

    const std::vector<uint8_t>& mask(const Key& key, std::string_view svg) {
        if (const auto found = masks.find(key); found != masks.end()) {
            return found->second;
        }
        if (!initialized) {
            check(tvg::Initializer::init(0));
            initialized = true;
        }
        const auto width = std::get<1>(key), height = std::get<2>(key);
        std::vector<uint32_t> pixels(size_t(width) * height);
        std::unique_ptr<tvg::Animation> animation(tvg::Animation::gen());
        std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
        if (!animation || !canvas) {
            throw std::bad_alloc();
        }
        auto* picture = animation->picture();
        check(picture->load(svg.data(), uint32_t(svg.size()), "svg", nullptr, false));
        float logicalWidth = 0, logicalHeight = 0;
        check(picture->size(&logicalWidth, &logicalHeight));
        if (logicalWidth <= 0 || logicalHeight <= 0) {
            throw std::runtime_error("Invalid Lucide SVG dimensions");
        }
        const float scale = std::min(width / logicalWidth, height / logicalHeight);
        check(picture->transform(
            {scale, 0, (width - logicalWidth * scale) / 2, 0, scale, (height - logicalHeight * scale) / 2, 0, 0, 1}));
        check(canvas->target(pixels.data(), width, width, height, tvg::ColorSpace::ABGR8888));
        check(canvas->add(picture));
        check(canvas->draw(true));
        check(canvas->sync());
        std::vector<uint8_t> alpha(pixels.size());
        std::transform(pixels.begin(), pixels.end(), alpha.begin(), [](uint32_t pixel) {
            return uint8_t(pixel >> 24);
        });
        // Keep only monochrome coverage. Colors/theme changes need no new raster.
        while (!masks.empty() && (masks.size() >= 128 || bytes + alpha.size() > 2 * 1024 * 1024)) {
            bytes -= masks.begin()->second.size();
            masks.erase(masks.begin());
        }
        bytes += alpha.size();
        return masks.emplace(key, std::move(alpha)).first->second;
    }
};

PreviewIcons::PreviewIcons() {
    static std::mutex creation;
    static std::weak_ptr<Impl> shared;
    std::lock_guard lock(creation);
    impl_ = shared.lock();
    if (!impl_) {
        impl_ = std::make_shared<Impl>();
        shared = impl_;
    }
}

PreviewIcons::~PreviewIcons() = default;

CdResult PreviewIcons::raster(const char* name, uint32_t color, const CdFrame* output) {
    if (!name || !output || output->struct_size < sizeof(CdFrame) || !output->rgba || !output->width ||
        !output->height || output->width > 256 || output->height > 256 || output->stride < output->width * 4 ||
        uint64_t(output->stride) * output->height > output->capacity || !std::isfinite(output->device_pixel_ratio) ||
        output->device_pixel_ratio <= 0) {
        return CD_ERROR_ARGUMENT;
    }
    const auto length = strnlen_s(name, 65);
    if (!length || length > 64 || !std::all_of(name, name + length, [](char value) {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-';
        })) {
        return CD_ERROR_ARGUMENT;
    }
    const auto svg = bundledIcon({name, length});
    if (svg.empty()) {
        return CD_ERROR_UNSUPPORTED;
    }
    const auto& coverage = impl_->mask({std::string(name, length), output->width, output->height}, svg);
    for (uint32_t y = 0; y < output->height; ++y) {
        auto* row = output->rgba + size_t(y) * output->stride;
        for (uint32_t x = 0; x < output->width; ++x) {
            const uint32_t alpha = (uint32_t(coverage[size_t(y) * output->width + x]) * (color & 255) + 127) / 255;
            row[x * 4] = uint8_t(((color >> 24) * alpha + 127) / 255);
            row[x * 4 + 1] = uint8_t((((color >> 16) & 255) * alpha + 127) / 255);
            row[x * 4 + 2] = uint8_t((((color >> 8) & 255) * alpha + 127) / 255);
            row[x * 4 + 3] = uint8_t(alpha);
        }
    }
    return CD_OK;
}
}  // namespace cdsdk
