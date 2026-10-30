#include <CyberCanvas/Gradients.h>
#include <CyberCanvas/Native.h>
#include <CyberCanvas/Types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <span>
#include <vector>

namespace cybercanvas {
class GradientCache::Impl {
public:
    static constexpr size_t kTargets = 4, kStopsPerTarget = 32, kBrushesPerTarget = 64, kStopsPerGradient = 32;

    ComPtr<ID2D1LinearGradientBrush> linear(ID2D1RenderTarget* target, Point start, Point end,
                                            std::span<const D2D1_GRADIENT_STOP> stops) {
        const auto a = point(start), b = point(end);
        const BrushKey key{false, {a.x, a.y, b.x, b.y, 0, 0}, stopKey(stops)};
        auto& resources = forTarget(target);
        if (auto* cached = find(resources, key)) {
            return cached->linear;
        }
        auto collection = stopCollection(resources, key.stops, stops);
        ComPtr<ID2D1LinearGradientBrush> brush;
        require(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), collection.Get(),
                                                  brush.GetAddressOf()));
        insert(resources, {key, brush, {}});
        return brush;
    }

    ComPtr<ID2D1RadialGradientBrush> radial(ID2D1RenderTarget* target, Point center, Size radii,
                                            std::span<const D2D1_GRADIENT_STOP> stops, Point offset = {}) {
        const auto c = point(center), o = point(offset), r = point({radii.width, radii.height});
        if (radii.width <= 0 || radii.height <= 0 || r.x <= 0 || r.y <= 0) {
            throw std::invalid_argument("Gradient radii must be positive");
        }
        const BrushKey key{true, {c.x, c.y, r.x, r.y, o.x, o.y}, stopKey(stops)};
        auto& resources = forTarget(target);
        if (auto* cached = find(resources, key)) {
            return cached->radial;
        }
        auto collection = stopCollection(resources, key.stops, stops);
        ComPtr<ID2D1RadialGradientBrush> brush;
        require(target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(c, o, r.x, r.y), collection.Get(),
                                                  brush.GetAddressOf()));
        insert(resources, {key, {}, brush});
        return brush;
    }

    void forget(ID2D1RenderTarget* target) noexcept {
        targets_.remove_if([&](const auto& entry) {
            return entry.target.Get() == target;
        });
    }

    void clear() noexcept {
        targets_.clear();
    }

    GradientCacheStats stats() const {
        auto result = counters_;
        result.targets = targets_.size();
        for (const auto& entry : targets_) {
            result.stopCollections += entry.stops.size();
            result.brushes += entry.brushes.size();
        }
        return result;
    }

private:
    using StopKey = std::vector<std::array<float, 5>>;

    struct BrushKey {
        bool radial;
        std::array<float, 6> geometry;
        StopKey stops;
        bool operator==(const BrushKey&) const = default;
    };

    struct StopEntry {
        StopKey key;
        ComPtr<ID2D1GradientStopCollection> collection;
    };

    struct BrushEntry {
        BrushKey key;
        ComPtr<ID2D1LinearGradientBrush> linear;
        ComPtr<ID2D1RadialGradientBrush> radial;
    };

    struct TargetEntry {
        // Retention prevents a released COM pointer address from aliasing a new target.
        ComPtr<ID2D1RenderTarget> target;
        std::list<StopEntry> stops;
        std::list<BrushEntry> brushes;
    };

    static D2D1_POINT_2F point(Point value) {
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || std::abs(value.x) > 1e7 || std::abs(value.y) > 1e7) {
            throw std::invalid_argument("Invalid gradient coordinate");
        }
        return D2D1::Point2F(float(value.x), float(value.y));
    }

    static StopKey stopKey(std::span<const D2D1_GRADIENT_STOP> stops) {
        if (stops.size() < 2 || stops.size() > kStopsPerGradient) {
            throw std::invalid_argument("Gradients require 2..32 stops");
        }
        StopKey key;
        key.reserve(stops.size());
        float previous = 0;
        for (const auto& stop : stops) {
            const std::array<float, 5> values{stop.position, stop.color.r, stop.color.g, stop.color.b, stop.color.a};
            for (const auto value : values) {
                if (!std::isfinite(value) || value < 0 || value > 1) {
                    throw std::invalid_argument("Gradient offsets and RGBA must be finite values in 0..1");
                }
            }
            if (stop.position < previous) {
                throw std::invalid_argument("Gradient offsets must not decrease");
            }
            previous = stop.position;
            key.push_back(values);
        }
        return key;
    }

    TargetEntry& forTarget(ID2D1RenderTarget* target) {
        if (!target) {
            throw std::invalid_argument("Gradient requires a render target");
        }
        const auto found = std::find_if(targets_.begin(), targets_.end(), [&](const auto& entry) {
            return entry.target.Get() == target;
        });
        if (found != targets_.end()) {
            targets_.splice(targets_.begin(), targets_, found);
        } else {
            targets_.push_front({ComPtr<ID2D1RenderTarget>(target), {}, {}});
            if (targets_.size() > kTargets) {
                targets_.pop_back();
            }
        }
        return targets_.front();
    }

    BrushEntry* find(TargetEntry& resources, const BrushKey& key) {
        const auto found = std::find_if(resources.brushes.begin(), resources.brushes.end(), [&](const auto& entry) {
            return entry.key == key;
        });
        if (found == resources.brushes.end()) {
            return nullptr;
        }
        resources.brushes.splice(resources.brushes.begin(), resources.brushes, found);
        ++counters_.brushHits;
        return &resources.brushes.front();
    }

    ComPtr<ID2D1GradientStopCollection> stopCollection(TargetEntry& resources, const StopKey& key,
                                                       std::span<const D2D1_GRADIENT_STOP> stops) {
        const auto found = std::find_if(resources.stops.begin(), resources.stops.end(), [&](const auto& entry) {
            return entry.key == key;
        });
        if (found != resources.stops.end()) {
            resources.stops.splice(resources.stops.begin(), resources.stops, found);
            ++counters_.stopHits;
            return resources.stops.front().collection;
        }
        ComPtr<ID2D1GradientStopCollection> collection;
        require(resources.target->CreateGradientStopCollection(stops.data(), UINT32(stops.size()), D2D1_GAMMA_2_2,
                                                               D2D1_EXTEND_MODE_CLAMP, collection.GetAddressOf()));
        resources.stops.push_front({key, collection});
        if (resources.stops.size() > kStopsPerTarget) {
            resources.stops.pop_back();
        }
        ++counters_.stopCreates;
        return collection;
    }

    void insert(TargetEntry& resources, BrushEntry entry) {
        resources.brushes.push_front(std::move(entry));
        if (resources.brushes.size() > kBrushesPerTarget) {
            resources.brushes.pop_back();
        }
        ++counters_.brushCreates;
    }

    std::list<TargetEntry> targets_;
    GradientCacheStats counters_;
};

ComPtr<ID2D1LinearGradientBrush> GradientCache::linear(ID2D1RenderTarget* target, Point start, Point end,
                                                       std::span<const D2D1_GRADIENT_STOP> stops) {
    return impl_->linear(target, start, end, stops);
}

ComPtr<ID2D1RadialGradientBrush> GradientCache::radial(ID2D1RenderTarget* target, Point center, Size radii,
                                                       std::span<const D2D1_GRADIENT_STOP> stops, Point offset) {
    return impl_->radial(target, center, radii, stops, offset);
}

void GradientCache::forget(ID2D1RenderTarget* target) noexcept {
    return impl_->forget(target);
}

void GradientCache::clear() noexcept {
    return impl_->clear();
}

GradientCacheStats GradientCache::stats() const {
    return impl_->stats();
}

GradientCache::GradientCache() : impl_(std::make_unique<Impl>()) {}

GradientCache::~GradientCache() = default;
}  // namespace cybercanvas
