#include <algorithm>
#include <array>
#include <limits>

#include "CyberCanvas/AnimatedImages.h"
#include "CyberCanvas/Native.h"

namespace cybercanvas {
namespace {
uint32_t metadata(IWICMetadataQueryReader* reader, const wchar_t* key, uint32_t fallback = 0) {
    PROPVARIANT value;
    PropVariantInit(&value);
    const auto status = reader->GetMetadataByName(key, &value);
    auto result = fallback;
    if (SUCCEEDED(status)) {
        if (value.vt == VT_UI1) {
            result = value.bVal;
        } else if (value.vt == VT_UI2) {
            result = value.uiVal;
        } else if (value.vt == VT_UI4) {
            result = value.ulVal;
        } else if (value.vt == VT_BOOL) {
            result = value.boolVal != VARIANT_FALSE;
        }
    }
    PropVariantClear(&value);
    return result;
}

void imageSize(uint32_t width, uint32_t height) {
    if (!width || !height || width > 4096 || height > 4096) {
        throw std::length_error("Raster animation dimensions exceed limits");
    }
}

class RasterSource final : public AnimationSource {
public:
    RasterSource(AssetContext context, RasterAnimation request)
        : context_(std::move(context)), request_(std::move(request)) {
        if (request_.files.empty() || request_.files.size() > 10000 ||
            (request_.kind != RasterAnimationKind::Sequence && request_.files.size() != 1)) {
            throw std::invalid_argument("Invalid raster animation files");
        }
        require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(factory_.GetAddressOf())));
        if (request_.kind == RasterAnimationKind::Gif) {
            loadGif();
        } else {
            const auto decoder = open(request_.files.front());
            ComPtr<IWICBitmapFrameDecode> frame;
            require(decoder->GetFrame(0, frame.GetAddressOf()));
            require(frame->GetSize(&info_.width, &info_.height));
            imageSize(info_.width, info_.height);
            uint64_t count = request_.files.size();
            if (request_.kind == RasterAnimationKind::Atlas) {
                count = uint64_t(request_.rows) * request_.columns;
                if (!count || count > 10000 || info_.width % request_.columns || info_.height % request_.rows) {
                    throw std::invalid_argument("Atlas grid must divide the image exactly");
                }
                info_.width /= request_.columns;
                info_.height /= request_.rows;
                imageSize(info_.width, info_.height);
            }
            if (request_.durations.size() != count && request_.durations.size() != 1) {
                throw std::invalid_argument("Provide one duration or one duration per frame");
            }
            double end = 0;
            for (size_t i = 0; i < count; ++i) {
                const auto duration = request_.durations[request_.durations.size() == 1 ? 0 : i];
                if (!std::isfinite(duration) || duration <= 0 || duration > 3600) {
                    throw std::invalid_argument("Invalid raster animation frame duration");
                }
                end += duration;
                info_.frameEnds.push_back(end);
            }
        }
    }

    const AnimationInfo& info() const override {
        return info_;
    }

    std::shared_ptr<const ImageData> frame(uint32_t index, uint32_t width, uint32_t height) override {
        if (context_.stop.stop_requested()) {
            throw std::runtime_error("Animation cancelled");
        }
        if (index >= info_.frameEnds.size()) {
            throw std::out_of_range("Raster animation frame index");
        }
        imageSize(width, height);
        if (request_.kind == RasterAnimationKind::Gif) {
            composeGif(index);
            ComPtr<IWICBitmap> bitmap;
            require(factory_->CreateBitmapFromMemory(info_.width, info_.height, GUID_WICPixelFormat32bppPBGRA,
                                                     info_.width * 4, UINT(composed_.size()), composed_.data(),
                                                     bitmap.GetAddressOf()));
            return scaled(bitmap.Get(), width, height);
        }
        const auto decoder = open(request_.files[request_.kind == RasterAnimationKind::Sequence ? index : 0]);
        ComPtr<IWICBitmapFrameDecode> frame;
        require(decoder->GetFrame(0, frame.GetAddressOf()));
        UINT sourceWidth, sourceHeight;
        require(frame->GetSize(&sourceWidth, &sourceHeight));
        imageSize(sourceWidth, sourceHeight);
        if (request_.kind == RasterAnimationKind::Sequence) {
            if (sourceWidth != info_.width || sourceHeight != info_.height) {
                throw std::invalid_argument("Sequence frames must have identical dimensions");
            }
            return scaled(frame.Get(), width, height);
        }
        ComPtr<IWICBitmapClipper> clipper;
        require(factory_->CreateBitmapClipper(clipper.GetAddressOf()));
        const WICRect rectangle{int(index % request_.columns * info_.width),
                                int(index / request_.columns * info_.height), int(info_.width), int(info_.height)};
        require(clipper->Initialize(frame.Get(), &rectangle));
        return scaled(clipper.Get(), width, height);
    }

private:
    struct GifFrame {
        uint32_t x, y, width, height, disposal;
    };

    AssetContext context_;
    RasterAnimation request_;
    AnimationInfo info_;
    ComPtr<IWICImagingFactory> factory_;
    ComPtr<IWICBitmapDecoder> gif_;
    std::vector<GifFrame> gifFrames_;
    std::shared_ptr<MemoryBudget::Lease> compositionLease_;
    std::vector<uint8_t> composed_, previous_;
    std::array<uint8_t, 4> background_{};
    int64_t composedIndex_ = -1;

    ComPtr<IWICBitmapDecoder> open(const std::filesystem::path& relative) {
        const auto path = context_.resolve(relative);
        ComPtr<IWICBitmapDecoder> result;
        require(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                    result.GetAddressOf()));
        return result;
    }

    std::shared_ptr<const ImageData> scaled(IWICBitmapSource* source, uint32_t width, uint32_t height) {
        auto lease = context_.budget->reserve(uint64_t(width) * height * 4);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> converter;
        require(factory_->CreateBitmapScaler(scaler.GetAddressOf()));
        require(scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant));
        require(factory_->CreateFormatConverter(converter.GetAddressOf()));
        require(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom));
        require(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
        return MemoryBudget::attach(ImageData::fromPremultipliedBgra(width, height, std::move(pixels)),
                                    std::move(lease));
    }

    void loadGif() {
        gif_ = open(request_.files[0]);
        GUID format;
        require(gif_->GetContainerFormat(&format));
        if (format != GUID_ContainerFormatGif) {
            throw std::invalid_argument("Expected a GIF container");
        }
        UINT count = 0;
        require(gif_->GetFrameCount(&count));
        if (!count || count > 10000) {
            throw std::length_error("GIF frame limit exceeded");
        }
        ComPtr<IWICMetadataQueryReader> metadataReader;
        require(gif_->GetMetadataQueryReader(metadataReader.GetAddressOf()));
        info_.width = metadata(metadataReader.Get(), L"/logscrdesc/Width");
        info_.height = metadata(metadataReader.Get(), L"/logscrdesc/Height");
        imageSize(info_.width, info_.height);
        const auto backgroundIndex = metadata(metadataReader.Get(), L"/logscrdesc/BackgroundColorIndex");
        ComPtr<IWICPalette> palette;
        require(factory_->CreatePalette(palette.GetAddressOf()));
        if (SUCCEEDED(gif_->CopyPalette(palette.Get()))) {
            std::array<WICColor, 256> colors;
            UINT actual = 0;
            require(palette->GetColors(UINT(colors.size()), colors.data(), &actual));
            if (backgroundIndex < actual) {
                const auto c = colors[backgroundIndex];
                background_ = {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16), 255};
            }
        }
        double end = 0;
        for (UINT i = 0; i < count; ++i) {
            if (context_.stop.stop_requested()) {
                throw std::runtime_error("Animation cancelled");
            }
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICMetadataQueryReader> reader;
            require(gif_->GetFrame(i, frame.GetAddressOf()));
            require(frame->GetMetadataQueryReader(reader.GetAddressOf()));
            UINT width, height;
            require(frame->GetSize(&width, &height));
            const GifFrame current{metadata(reader.Get(), L"/imgdesc/Left"), metadata(reader.Get(), L"/imgdesc/Top"),
                                   width, height, metadata(reader.Get(), L"/grctlext/Disposal")};
            imageSize(width, height);
            if (current.x + width > info_.width || current.y + height > info_.height || current.disposal > 3) {
                throw std::invalid_argument("Invalid GIF frame bounds/disposal");
            }
            gifFrames_.push_back(current);
            // GIF has centisecond resolution. Zero delay is normalized to 10 ms.
            end += std::max(1u, metadata(reader.Get(), L"/grctlext/Delay")) / 100.;
            info_.frameEnds.push_back(end);
            if (i == 0 && metadata(reader.Get(), L"/grctlext/TransparencyFlag")) {
                // Transparent GIFs are composited over the caller's transparent layer.
                background_ = {};
            }
        }
        const auto bytes = uint64_t(info_.width) * info_.height * 4;
        compositionLease_ = context_.budget->reserve(bytes * 2);
        composed_.resize(size_t(bytes));
        previous_.resize(size_t(bytes));
        clear({0, 0, info_.width, info_.height, 0});
    }

    void clear(const GifFrame& frame) {
        for (uint32_t y = frame.y; y < frame.y + frame.height; ++y) {
            for (uint32_t x = frame.x; x < frame.x + frame.width; ++x) {
                std::copy(background_.begin(), background_.end(),
                          composed_.begin() + (size_t(y) * info_.width + x) * 4);
            }
        }
    }

    void composeGif(uint32_t target) {
        if (int64_t(target) < composedIndex_) {
            clear({0, 0, info_.width, info_.height, 0});
            composedIndex_ = -1;
        }
        while (composedIndex_ < int64_t(target)) {
            if (context_.stop.stop_requested()) {
                throw std::runtime_error("Animation cancelled");
            }
            if (composedIndex_ >= 0) {
                const auto& old = gifFrames_[size_t(composedIndex_)];
                if (old.disposal == 2) {
                    clear(old);
                } else if (old.disposal == 3) {
                    composed_ = previous_;
                }
            }
            const auto index = uint32_t(composedIndex_ + 1);
            const auto& current = gifFrames_[index];
            if (current.disposal == 3) {
                previous_ = composed_;
            }
            ComPtr<IWICBitmapFrameDecode> frame;
            require(gif_->GetFrame(index, frame.GetAddressOf()));
            const auto raw = scaled(frame.Get(), current.width, current.height);
            const auto& pixels = raw->pixels();
            for (uint32_t y = 0; y < current.height; ++y) {
                for (uint32_t x = 0; x < current.width; ++x) {
                    const auto from = (size_t(y) * current.width + x) * 4;
                    const auto to = (size_t(y + current.y) * info_.width + x + current.x) * 4;
                    const auto alpha = pixels[from + 3];
                    for (int c = 0; c < 4; ++c) {
                        composed_[to + c] = uint8_t(pixels[from + c] + (composed_[to + c] * (255 - alpha) + 127) / 255);
                    }
                }
            }
            composedIndex_ = index;
        }
    }
};
}  // namespace

AnimationSourceFactory rasterAnimation(RasterAnimation request) {
    return [request = std::move(request)](const AssetContext& context) {
        return std::make_unique<RasterSource>(context, request);
    };
}
}  // namespace cybercanvas
