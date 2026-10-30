#include "CyberCanvas/EffectsDrawing.h"

#include <CyberCanvas/MemoryBudget.h>

namespace cybercanvas {
ShadowPainter::ShadowPainter(std::shared_ptr<MemoryBudget> budget)
    : budget_(budget ? std::move(budget) : std::make_shared<MemoryBudget>()),
      effects_(EffectBackend::Automatic, 16 * 1024 * 1024, budget_) {}

auto ShadowPainter::ellipse(Canvas& canvas, Rect bounds, double sigma, Point offset, uint32_t color) -> void {
    detail::layoutRect(bounds);
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const double dpi = canvas.devicePixelRatio();
    const Key key{bounds.width, bounds.height, dpi, sigma, offset.x, offset.y, color};
    auto found = std::find_if(cache_.begin(), cache_.end(), [&](const auto& entry) {
        return entry.key == key;
    });
    if (found == cache_.end()) {
        if (cache_.size() >= 8) {
            cache_.pop_back();
        }
        const auto width = uint32_t(std::ceil(bounds.width * dpi)), height = uint32_t(std::ceil(bounds.height * dpi));
        if (width > 4096 || height > 4096) {
            throw std::length_error("Shadow source dimensions exceed limits");
        }
        auto work = budget_->reserve(uint64_t(width) * height * 8);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        Canvas source;
        source.render({width, height, width * 4, pixels.size(), pixels.data(), dpi}, {}, [&](Canvas& target, Rect) {
            target.oval(bounds.width / 2, bounds.height / 2, bounds.width / 2, bounds.height / 2, 0xffffffff);
        });
        // White alpha mask has identical RGBA/BGRA bytes.
        const auto image = ImageData::fromPremultipliedBgra(width, height, std::move(pixels));
        const EffectStyle style{EffectKind::Shadow, sigma, offset, color, false};
        cache_.push_front({key, effects_.render(*image, std::span(&style, 1), dpi)});
        found = cache_.begin();
    } else {
        cache_.splice(cache_.begin(), cache_, found);
        found = cache_.begin();
    }
    const auto& result = found->result;
    canvas.image(*result.image,
                 {bounds.x + result.offset.x, bounds.y + result.offset.y, result.image->width() / dpi,
                  result.image->height() / dpi},
                 {.fit = ImageFit::Stretch});
}

auto ShadowPainter::reset() -> void {
    cache_.clear();
    effects_.reset();
}

auto ShadowPainter::stats() const -> EffectStats {
    return effects_.stats();
}
}  // namespace cybercanvas
