#include "sdk/Icons.h"

namespace cdsdk {
auto Icons::bind(const CdCreateContext& context) -> void {
    context_ = context.host_context;
    api_ = {};
    cache_.clear();
    if (context.host && context.host->struct_size >= sizeof(CdServiceHostApi)) {
        const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
        if (!host->query_service || host->query_service(context_, CD_SERVICE_ICONS, 1, &api_, sizeof(api_)) != CD_OK ||
            api_.struct_size < sizeof(api_) || api_.version != 1 || !api_.raster) {
            api_ = {};
        }
    }
}

auto Icons::paint(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, std::string_view name, uint32_t color)
    -> bool {
    if (!api_.raster || name.empty() || name.size() > 64 || bounds.width <= 0 || bounds.height <= 0 ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) || bounds.width > 1e7 || bounds.height > 1e7 ||
        !std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        })) {
        return false;
    }
    const double dpr = canvas.devicePixelRatio();
    const double scale = std::min(1., 256. / (std::max(bounds.width, bounds.height) * dpr));
    const uint32_t width = std::max(1u, uint32_t(std::ceil(bounds.width * dpr * scale)));
    const uint32_t height = std::max(1u, uint32_t(std::ceil(bounds.height * dpr * scale)));
    const auto key = std::make_tuple(std::string(name), color, width, height, dpr);
    auto found = cache_.find(key);
    if (found == cache_.end()) {
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        const CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpr};
        const std::string owned(name);
        if (api_.raster(context_, owned.c_str(), color, &frame) != CD_OK) {
            return false;
        }
        if (cache_.size() >= 64) {
            cache_.clear();
        }
        found = cache_.emplace(key, std::move(pixels)).first;
    }
    // All content identity, dimensions and DPI are part of the upload key.
    const std::wstring imageKey = L"lucide:" + std::wstring(name.begin(), name.end()) + L":" + std::to_wstring(color) +
                                  L":" + std::to_wstring(width) + L":" + std::to_wstring(height) + L":" +
                                  std::to_wstring(std::bit_cast<uint64_t>(dpr));
    canvas.pixels(imageKey, {width, height, width * 4, found->second.size(), found->second.data(), dpr}, bounds);
    return true;
}

auto Icons::painter() -> cybercanvas::IconPainter {
    return [this](auto& canvas, auto bounds, auto name, auto color) {
        return paint(canvas, bounds, name, color);
    };
}
}  // namespace cdsdk
