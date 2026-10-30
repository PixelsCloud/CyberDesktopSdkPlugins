#include "tools/PreviewImages.h"

namespace cdsdk {
PreviewImages::PreviewImages() {
    const auto status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    initialized_ = SUCCEEDED(status);
    if (FAILED(status) && status != RPC_E_CHANGED_MODE) {
        cybercanvas::require(status);
    }
    try {
        cybercanvas::require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(factory_.GetAddressOf())));
    } catch (...) {
        if (initialized_) {
            CoUninitialize();
        }
        throw;
    }
}

PreviewImages::~PreviewImages() {
    factory_.Reset();
    if (initialized_) {
        CoUninitialize();
    }
}

auto PreviewImages::load(const std::filesystem::path& path) -> PreviewImage {
    using namespace cybercanvas;
    if (std::filesystem::file_size(path) > 64 * 1024 * 1024) {
        throw std::length_error("Preview image file exceeds 64 MiB");
    }
    ComPtr<IWICBitmapDecoder> decoder;
    require(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                decoder.GetAddressOf()));
    ComPtr<IWICBitmapFrameDecode> frame;
    require(decoder->GetFrame(0, frame.GetAddressOf()));
    PreviewImage result;
    require(frame->GetSize(&result.width, &result.height));
    if (!result.width || !result.height || result.width > 4096 || result.height > 4096) {
        throw std::length_error("Preview image dimensions must be 1..4096 pixels");
    }
    ComPtr<IWICFormatConverter> converter;
    require(factory_->CreateFormatConverter(converter.GetAddressOf()));
    require(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                  WICBitmapPaletteTypeCustom));
    result.rgba.resize(size_t(result.width) * result.height * 4);
    require(converter->CopyPixels(nullptr, result.width * 4, UINT(result.rgba.size()), result.rgba.data()));
    return result;
}

auto PreviewImages::save(const std::filesystem::path& path, uint32_t width, uint32_t height,
                         const std::vector<uint8_t>& rgba) -> void {
    using namespace cybercanvas;
    if (!width || !height || width > 4096 || height > 4096 || rgba.size() != size_t(width) * height * 4) {
        throw std::invalid_argument("Invalid preview output image");
    }
    const auto extension = path.extension().wstring();
    const bool bmp = extension == L".bmp" || extension == L".BMP";
    if (!bmp && extension != L".png" && extension != L".PNG") {
        throw std::invalid_argument("Preview output must be PNG or BMP");
    }
    ComPtr<IWICBitmap> bitmap;
    require(factory_->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPRGBA, width * 4, UINT(rgba.size()),
                                             const_cast<BYTE*>(rgba.data()), bitmap.GetAddressOf()));
    ComPtr<IWICFormatConverter> straight;
    require(factory_->CreateFormatConverter(straight.GetAddressOf()));
    require(straight->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                 WICBitmapPaletteTypeCustom));
    ComPtr<IWICStream> stream;
    require(factory_->CreateStream(stream.GetAddressOf()));
    require(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    require(factory_->CreateEncoder(bmp ? GUID_ContainerFormatBmp : GUID_ContainerFormatPng, nullptr,
                                    encoder.GetAddressOf()));
    require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    require(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
    require(frame->Initialize(nullptr));
    require(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    require(frame->SetPixelFormat(&format));
    require(frame->WriteSource(straight.Get(), nullptr));
    require(frame->Commit());
    require(encoder->Commit());
}

VisualDifference compareImages(const PreviewImage& expected, const PreviewImage& actual, uint32_t tolerance) {
    if (expected.width != actual.width || expected.height != actual.height ||
        expected.rgba.size() != actual.rgba.size() || !expected.width || !expected.height || expected.width > 4096 ||
        expected.height > 4096 || expected.rgba.size() != uint64_t(expected.width) * expected.height * 4 ||
        tolerance > 255) {
        throw std::invalid_argument("Visual comparison requires equal nonempty dimensions and tolerance 0..255");
    }
    VisualDifference result;
    result.pixels = uint64_t(expected.width) * expected.height;
    result.highlight = {expected.width, expected.height, std::vector<uint8_t>(expected.rgba.size())};
    uint64_t error = 0;
    for (size_t pixel = 0; pixel < result.pixels; ++pixel) {
        uint32_t maximum = 0;
        for (int channel = 0; channel < 4; ++channel) {
            const auto delta =
                uint32_t(std::abs(int(expected.rgba[pixel * 4 + channel]) - int(actual.rgba[pixel * 4 + channel])));
            error += delta;
            maximum = std::max(maximum, delta);
        }
        const bool different = maximum > tolerance;
        result.differing += different;
        result.maximumDelta = std::max(result.maximumDelta, maximum);
        auto* out = result.highlight.rgba.data() + pixel * 4;
        const auto shade =
            uint8_t((uint32_t(actual.rgba[pixel * 4]) + actual.rgba[pixel * 4 + 1] + actual.rgba[pixel * 4 + 2]) / 6);
        out[0] = different ? 255 : shade;
        out[1] = different ? 55 : shade;
        out[2] = different ? 85 : shade;
        out[3] = 255;
    }
    result.ratio = double(result.differing) / result.pixels;
    result.meanAbsoluteError = double(error) / (result.pixels * 4);
    return result;
}
}  // namespace cdsdk
