#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Sprites.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace cybercanvas {
class SpriteList::Impl {
public:
    void begin(Canvas& canvas) {
        canvas.target->GetTransform(&base_);
        inverse_ = *D2D1::Matrix3x2F::ReinterpretBaseType(&base_);
        if (!inverse_.Invert()) {
            throw std::invalid_argument("Non-invertible sprite transform");
        }
        destinations_.clear();
        sources_.clear();
        poses_.clear();
        opacities_.clear();
    }

    D2D1_MATRIX_3X2_F pose(Canvas& canvas) const {
        D2D1_MATRIX_3X2_F current;
        canvas.target->GetTransform(&current);
        return (*D2D1::Matrix3x2F::ReinterpretBaseType(&current)) * inverse_;
    }

    void add(Rect destination, D2D1_RECT_U source, D2D1_MATRIX_3X2_F pose = D2D1::Matrix3x2F::Identity(),
             float opacity = 1) {
        destinations_.push_back(D2D1::RectF(float(destination.x), float(destination.y),
                                            float(destination.x + destination.width),
                                            float(destination.y + destination.height)));
        sources_.push_back(source);
        poses_.push_back(pose);
        opacities_.push_back(opacity);
    }

    void flush(Canvas& canvas, ID2D1Bitmap* atlas, ImageSampling sampling = ImageSampling::Smooth) {
        canvas.sprites(atlas, destinations_, sources_, poses_, sampling, opacities_);
    }

    void vertical(Rect destination, D2D1_RECT_U source, uint32_t capPixels, double capLength,
                  D2D1_MATRIX_3X2_F pose = D2D1::Matrix3x2F::Identity()) {
        const double cap = std::min(capLength, destination.height / 2);
        add({destination.x, destination.y, destination.width, cap},
            {source.left, source.top, source.right, source.top + capPixels}, pose);
        if (destination.height > 2 * cap) {
            const auto middle = source.top + (source.bottom - source.top) / 2;
            add({destination.x, destination.y + cap, destination.width, destination.height - 2 * cap},
                {source.left, middle, source.right, middle + 1}, pose);
        }
        add({destination.x, destination.y + destination.height - cap, destination.width, cap},
            {source.left, source.bottom - capPixels, source.right, source.bottom}, pose);
    }

private:
    D2D1_MATRIX_3X2_F base_{};
    D2D1::Matrix3x2F inverse_;
    std::vector<D2D1_RECT_F> destinations_;
    std::vector<D2D1_RECT_U> sources_;
    std::vector<D2D1_MATRIX_3X2_F> poses_;
    std::vector<float> opacities_;
};

void SpriteList::begin(Canvas& canvas) {
    return impl_->begin(canvas);
}

D2D1_MATRIX_3X2_F SpriteList::pose(Canvas& canvas) const {
    return impl_->pose(canvas);
}

void SpriteList::add(Rect destination, D2D1_RECT_U source, D2D1_MATRIX_3X2_F pose, float opacity) {
    return impl_->add(destination, source, pose, opacity);
}

void SpriteList::flush(Canvas& canvas, ID2D1Bitmap* atlas, ImageSampling sampling) {
    return impl_->flush(canvas, atlas, sampling);
}

void SpriteList::vertical(Rect destination, D2D1_RECT_U source, uint32_t capPixels, double capLength,
                          D2D1_MATRIX_3X2_F pose) {
    return impl_->vertical(destination, source, capPixels, capLength, pose);
}

SpriteList::SpriteList() : impl_(std::make_unique<Impl>()) {}

SpriteList::~SpriteList() = default;

class SpriteAtlas::Impl {
public:
    template <typename Paint>
    void prepare(Canvas& canvas, SurfaceSize size, uint64_t revision, Paint&& paint) {
        if (!surface_.matches(canvas.target.Get(), size) || revision_ != revision) {
            surface_.reset();
            canvas.renderLayer(surface_, size, std::forward<Paint>(paint));
            revision_ = revision;
        }
    }

    ID2D1Bitmap* bitmap() const {
        return surface_.bitmap();
    }

    void reset() {
        surface_.reset();
    }

private:
    OffscreenSurface surface_;
    uint64_t revision_ = 0;
};

void SpriteAtlas::prepareImpl(Canvas& canvas, SurfaceSize size, uint64_t revision, void* callbackContext,
                              void (*paint)(void*, Canvas&)) {
    return impl_->prepare(canvas, size, revision, [&](Canvas& layer) -> void {
        return paint(callbackContext, layer);
    });
}

ID2D1Bitmap* SpriteAtlas::bitmap() const {
    return impl_->bitmap();
}

void SpriteAtlas::reset() {
    return impl_->reset();
}

SpriteAtlas::SpriteAtlas() : impl_(std::make_unique<Impl>()) {}

SpriteAtlas::~SpriteAtlas() = default;

class SpritePalette::Impl {
public:
    void build(Canvas& canvas, std::span<const SurfaceImage> materials,
               ImageSampling sampling = ImageSampling::Smooth) {
        if (materials.empty() || !materials.front()) {
            throw std::invalid_argument("Empty sprite palette");
        }
        const auto size = materials.front()->GetPixelSize();
        width_ = size.width;
        height_ = size.height;
        columns_ = std::max(1u, std::min(uint32_t(materials.size()), 4096u / width_));
        const auto rows = (uint32_t(materials.size()) + columns_ - 1) / columns_;
        sampling_ = sampling;
        atlas_.prepare(canvas, {width_ * columns_, height_ * rows, 1}, ++revision_, [&](Canvas& layer) {
            for (size_t i = 0; i < materials.size(); ++i) {
                const auto pixels = materials[i]->GetPixelSize();
                if (pixels.width != width_ || pixels.height != height_) {
                    throw std::invalid_argument("Mismatched sprite palette dimensions");
                }
                const auto x = uint32_t(i) % columns_ * width_, y = uint32_t(i) / columns_ * height_;
                layer.target->DrawBitmap(
                    materials[i].Get(), D2D1::RectF(float(x), float(y), float(x + width_), float(y + height_)), 1,
                    sampling == ImageSampling::Nearest ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                                       : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        });
    }

    void begin(Canvas& canvas) {
        sprites_.begin(canvas);
    }

    void add(unsigned material, Rect destination, float opacity = 1) {
        const auto x = material % columns_ * width_, y = material / columns_ * height_;
        sprites_.add(destination, D2D1::RectU(x, y, x + width_, y + height_), D2D1::Matrix3x2F::Identity(), opacity);
    }

    void flush(Canvas& canvas) {
        sprites_.flush(canvas, atlas_.bitmap(), sampling_);
    }

private:
    SpriteAtlas atlas_;
    SpriteList sprites_;
    uint32_t width_ = 0, height_ = 0;
    uint32_t columns_ = 1;
    uint64_t revision_ = 0;
    ImageSampling sampling_ = ImageSampling::Smooth;
};

void SpritePalette::build(Canvas& canvas, std::span<const SurfaceImage> materials, ImageSampling sampling) {
    return impl_->build(canvas, materials, sampling);
}

void SpritePalette::begin(Canvas& canvas) {
    return impl_->begin(canvas);
}

void SpritePalette::add(unsigned material, Rect destination, float opacity) {
    return impl_->add(material, destination, opacity);
}

void SpritePalette::flush(Canvas& canvas) {
    return impl_->flush(canvas);
}

SpritePalette::SpritePalette() : impl_(std::make_unique<Impl>()) {}

SpritePalette::~SpritePalette() = default;

class PrimitiveSprites::Impl {
public:
    void begin(Canvas& canvas, double size, double style = 0) {
        D2D1_MATRIX_3X2_F matrix;
        canvas.target->GetTransform(&matrix);
        const double density = std::hypot(double(matrix._11), double(matrix._12)) * canvas.devicePixelRatio();
        if (!std::isfinite(size) || size <= 0 || !std::isfinite(density) || density <= 0) {
            throw std::invalid_argument("Invalid primitive sprite density");
        }
        if (owner_.Get() != canvas.target.Get() || size_ != size || density_ != density || style_ != style) {
            keys_.clear();
            materials_.clear();
            dirty_ = true;
        }
        owner_ = canvas.target.Get();
        size_ = size;
        density_ = density;
        style_ = style;
        pixels_ = uint32_t(std::ceil(size * 1.6 * density)) + 2;
        span_ = pixels_ / density;
        requests_.clear();
    }

    template <typename Paint>
    void add(Canvas& canvas, uint64_t key, Point center, Paint&& paint) {
        auto found = keys_.find(key);
        if (found == keys_.end()) {
            if (materials_.size() >= 128) {
                flush(canvas);
                requests_.clear();
                keys_.clear();
                materials_.clear();
                dirty_ = true;
            }
            OffscreenSurface material;
            canvas.renderLayer(material, {pixels_, pixels_, 1}, [&](Canvas& layer) {
                layer.translate(pixels_ / 2., pixels_ / 2.);
                layer.scale(density_, density_);
                std::forward<Paint>(paint)(layer);
            });
            const auto index = unsigned(materials_.size());
            materials_.push_back(material.snapshot());
            found = keys_.emplace(key, index).first;
            dirty_ = true;
        }
        requests_.push_back({found->second, {center.x - span_ / 2, center.y - span_ / 2, span_, span_}});
    }

    void flush(Canvas& canvas) {
        if (requests_.empty()) {
            return;
        }
        if (dirty_) {
            palette_.build(canvas, materials_);
            dirty_ = false;
        }
        palette_.begin(canvas);
        for (const auto& request : requests_) {
            palette_.add(request.material, request.area);
        }
        palette_.flush(canvas);
    }

private:
    struct Request {
        unsigned material;
        Rect area;
    };

    SpritePalette palette_;
    std::map<uint64_t, unsigned> keys_;
    std::vector<SurfaceImage> materials_;
    std::vector<Request> requests_;
    ComPtr<ID2D1RenderTarget> owner_;
    double size_ = 0, density_ = 0, style_ = 0, span_ = 0;
    uint32_t pixels_ = 0;
    bool dirty_ = true;
};

void PrimitiveSprites::begin(Canvas& canvas, double size, double style) {
    return impl_->begin(canvas, size, style);
}

void PrimitiveSprites::addImpl(Canvas& canvas, uint64_t key, Point center, void* callbackContext,
                               void (*paint)(void*, Canvas&)) {
    return impl_->add(canvas, key, center, [&](Canvas& layer) -> void {
        return paint(callbackContext, layer);
    });
}

void PrimitiveSprites::flush(Canvas& canvas) {
    return impl_->flush(canvas);
}

PrimitiveSprites::PrimitiveSprites() : impl_(std::make_unique<Impl>()) {}

PrimitiveSprites::~PrimitiveSprites() = default;
}  // namespace cybercanvas
