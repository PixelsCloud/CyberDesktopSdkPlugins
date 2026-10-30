#pragma once

#include <CyberCanvas/Native.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace cdsdk {
struct PreviewImage {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

class PreviewImages {
public:
    PreviewImages();

    ~PreviewImages();

    PreviewImages(const PreviewImages&) = delete;
    PreviewImages& operator=(const PreviewImages&) = delete;

    PreviewImage load(const std::filesystem::path& path);

    void save(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba);

private:
    bool initialized_ = false;
    cybercanvas::ComPtr<IWICImagingFactory> factory_;
};

struct VisualDifference {
    uint64_t pixels = 0, differing = 0;
    uint32_t maximumDelta = 0;
    double ratio = 0, meanAbsoluteError = 0;
    PreviewImage highlight;
};

VisualDifference compareImages(const PreviewImage& expected, const PreviewImage& actual, uint32_t tolerance);
}  // namespace cdsdk
