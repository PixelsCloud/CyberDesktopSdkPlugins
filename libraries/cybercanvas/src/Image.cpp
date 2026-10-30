#include "CyberCanvas/Image.h"

namespace cybercanvas {
auto ImageData::fromPremultipliedBgra(uint32_t width, uint32_t height, std::vector<uint8_t> pixels)
    -> std::shared_ptr<const ImageData> {
    if (!width || !height || width > 4096 || height > 4096 || uint64_t(width) * height * 4 != pixels.size()) {
        throw std::invalid_argument("Invalid decoded image pixels");
    }
    auto image = std::shared_ptr<ImageData>(new ImageData);
    image->width_ = width;
    image->height_ = height;
    image->pixels_ = std::move(pixels);
    return image;
}

auto ImageData::id() const -> uint64_t {
    return id_;
}

auto ImageData::width() const -> uint32_t {
    return width_;
}

auto ImageData::height() const -> uint32_t {
    return height_;
}

auto ImageData::stride() const -> uint32_t {
    return width_ * 4;
}

auto ImageData::pixels() const -> const std::vector<uint8_t>& {
    return pixels_;
}
}  // namespace cybercanvas
