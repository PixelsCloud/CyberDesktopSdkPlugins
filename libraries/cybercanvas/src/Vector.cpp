#include "CyberCanvas/Vector.h"

#include <Windows.h>
#include <objidl.h>
#include <thorvg.h>
#include <wincrypt.h>
#include <xmllite.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string_view>

#include "CyberCanvas/Native.h"

namespace cybercanvas {
namespace {
std::mutex engineMutex;

void tvgCheck(tvg::Result status) {
    if (status != tvg::Result::Success) {
        throw std::runtime_error("ThorVG vector operation failed: " + std::to_string(int(status)));
    }
}

struct Engine {
    Engine() {
        std::lock_guard lock(engineMutex);
        tvgCheck(tvg::Initializer::init(0));
    }

    ~Engine() {
        std::lock_guard lock(engineMutex);
        tvg::Initializer::term();
    }
};

void validateSvg(const std::vector<uint8_t>& bytes) {
    ComPtr<IWICImagingFactory> factory;
    require(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    ComPtr<IWICStream> stream;
    require(factory->CreateStream(stream.GetAddressOf()));
    require(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), DWORD(bytes.size())));
    ComPtr<IXmlReader> reader;
    require(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr));
    require(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
    require(reader->SetInput(stream.Get()));
    const std::set<std::wstring> allowed{
        L"svg",     L"g",        L"defs",     L"path",    L"rect",           L"circle",
        L"ellipse", L"line",     L"polyline", L"polygon", L"linearGradient", L"radialGradient",
        L"stop",    L"clipPath", L"mask",     L"use",     L"title",          L"desc"};
    XmlNodeType type;
    HRESULT status;
    size_t count = 0;
    while ((status = reader->Read(&type)) == S_OK) {
        UINT depth = 0;
        require(reader->GetDepth(&depth));
        if (++count > 50000 || depth > 64) {
            throw std::length_error("SVG structural limit exceeded");
        }
        if (type == XmlNodeType_ProcessingInstruction || type == XmlNodeType_DocumentType) {
            throw std::invalid_argument("SVG external processing is unsupported");
        }
        if (type != XmlNodeType_Element) {
            continue;
        }
        const wchar_t* name;
        require(reader->GetLocalName(&name, nullptr));
        if (!allowed.contains(name)) {
            throw std::invalid_argument("SVG element outside supported subset");
        }
        if (reader->MoveToFirstAttribute() != S_OK) {
            continue;
        }
        do {
            const wchar_t* value;
            require(reader->GetLocalName(&name, nullptr));
            require(reader->GetValue(&value, nullptr));
            const std::wstring_view key(name), text(value);
            if ((key == L"href" && !text.starts_with(L"#")) || key.starts_with(L"on") || key == L"base") {
                throw std::invalid_argument("SVG external references and events are unsupported");
            }
            size_t offset = 0;
            while ((offset = text.find(L"url(", offset)) != std::wstring_view::npos) {
                offset += 4;
                while (offset < text.size() &&
                       (text[offset] == L' ' || text[offset] == L'\'' || text[offset] == L'\"')) {
                    ++offset;
                }
                if (offset == text.size() || text[offset] != L'#') {
                    throw std::invalid_argument("SVG URL must be a local fragment");
                }
            }
        } while (reader->MoveToNextAttribute() == S_OK);
        require(reader->MoveToElement());
    }
    require(status);
}

class VectorSource final : public AnimationSource {
public:
    VectorSource(AssetContext context, std::filesystem::path path, VectorKind kind)
        : context_(std::move(context)), path_(std::move(path)), kind_(kind) {
        auto bytes = context_.read(path_);
        if (bytes.empty()) {
            throw std::invalid_argument("Empty vector asset");
        }
        inputLease_ = context_.budget->reserve(bytes.size());
        if (kind_ == VectorKind::Svg) {
            validateSvg(bytes);
            data_.assign(bytes.begin(), bytes.end());
        } else {
            prepareLottie(bytes);
        }
        // Initialize only after validation, and keep the engine alive until all
        // pictures/canvases have been destroyed, including exceptional paths.
        engine_ = std::make_unique<Engine>();
        std::lock_guard lock(engineMutex);
        try {
            animation_.reset(tvg::Animation::gen());
            canvas_.reset(tvg::SwCanvas::gen());
            if (!animation_ || !canvas_) {
                throw std::bad_alloc();
            }
            auto* picture = animation_->picture();
            tvgCheck(picture->resolver(
                [this](tvg::Paint* paint, const char* source, void*) {
                    // Never fall through to ThorVG's filesystem loaders.
                    try {
                        const auto found = images_.find(source ? source : "");
                        if (paint->type() != tvg::Type::Picture || found == images_.end()) {
                            throw std::invalid_argument("Unresolved Lottie image/font asset");
                        }
                        const auto& image = found->second;
                        tvgCheck(static_cast<tvg::Picture*>(paint)->load(
                            reinterpret_cast<const uint32_t*>(image->pixels().data()), image->width(), image->height(),
                            tvg::ColorSpace::ARGB8888, false));
                    } catch (const std::exception& error) {
                        assetError_ = error.what();
                    } catch (...) {
                        assetError_ = "Lottie asset resolution failed";
                    }
                    return true;
                },
                nullptr));
            tvgCheck(picture->load(data_.c_str(), uint32_t(data_.size()), kind_ == VectorKind::Svg ? "svg" : "lottie",
                                   nullptr, false));
            if (!assetError_.empty()) {
                throw std::runtime_error(assetError_);
            }
            float width = 0, height = 0;
            tvgCheck(picture->size(&width, &height));
            if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || width > 4096 ||
                height > 4096) {
                throw std::length_error("Vector dimensions exceed limits");
            }
            logicalWidth_ = width;
            logicalHeight_ = height;
            info_.width = uint32_t(std::ceil(width));
            info_.height = uint32_t(std::ceil(height));
            if (kind_ == VectorKind::Lottie) {
                const auto count = animation_->totalFrame();
                const auto duration = animation_->duration();
                if (!std::isfinite(count) || count < 1 || count > 10000 || !std::isfinite(duration) || duration <= 0 ||
                    duration > 86400) {
                    throw std::length_error("Lottie duration/frame limits exceeded");
                }
                const auto integerCount = uint32_t(std::ceil(count));
                for (uint32_t i = 0; i < integerCount; ++i) {
                    info_.frameEnds.push_back(double(duration) * (i + 1) / integerCount);
                }
            } else {
                info_.frameEnds = {1};
            }
            tvgCheck(canvas_->add(picture));
        } catch (...) {
            // Constructor failure does not call ~VectorSource. Release native
            // objects under the same global lock used by every other source.
            canvas_.reset();
            animation_.reset();
            throw;
        }
    }

    ~VectorSource() override {
        std::lock_guard lock(engineMutex);
        canvas_.reset();
        animation_.reset();
    }

    const AnimationInfo& info() const override {
        return info_;
    }

    std::shared_ptr<const ImageData> frame(uint32_t index, uint32_t width, uint32_t height) override {
        if (context_.stop.stop_requested()) {
            throw std::runtime_error("Animation cancelled");
        }
        if (!width || !height || width > 4096 || height > 4096 || index >= info_.frameEnds.size()) {
            throw std::invalid_argument("Invalid vector frame request");
        }
        auto lease = context_.budget->reserve(uint64_t(width) * height * 4);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        std::lock_guard lock(engineMutex);
        tvgCheck(canvas_->target(reinterpret_cast<uint32_t*>(pixels.data()), width, width, height,
                                 tvg::ColorSpace::ARGB8888));
        if (kind_ == VectorKind::Lottie) {
            const auto status = animation_->frame(float(index));
            if (status != tvg::Result::InsufficientCondition) {
                tvgCheck(status);
            }
        }
        const float scale = std::min(width / logicalWidth_, height / logicalHeight_);
        tvgCheck(animation_->picture()->transform(
            {scale, 0, (width - logicalWidth_ * scale) / 2, 0, scale, (height - logicalHeight_ * scale) / 2, 0, 0, 1}));
        tvgCheck(canvas_->update());
        if (!assetError_.empty()) {
            throw std::runtime_error(assetError_);
        }
        tvgCheck(canvas_->draw(true));
        tvgCheck(canvas_->sync());
        return MemoryBudget::attach(ImageData::fromPremultipliedBgra(width, height, std::move(pixels)),
                                    std::move(lease));
    }

private:
    AssetContext context_;
    std::filesystem::path path_;
    VectorKind kind_;
    std::shared_ptr<MemoryBudget::Lease> inputLease_;
    std::string data_, assetError_;
    std::map<std::string, std::shared_ptr<const ImageData>> images_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<tvg::Animation> animation_;
    std::unique_ptr<tvg::SwCanvas> canvas_;
    AnimationInfo info_;
    float logicalWidth_ = 0, logicalHeight_ = 0;

    std::shared_ptr<const ImageData> decodeImage(const std::string& name) {
        std::vector<uint8_t> bytes;
        if (name.starts_with("data:image/png;base64,") || name.starts_with("data:image/jpeg;base64,")) {
            const auto encoded = name.substr(name.find(',') + 1);
            DWORD size = 0;
            if (!CryptStringToBinaryA(encoded.c_str(), DWORD(encoded.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                      nullptr, &size, nullptr, nullptr) ||
                size > context_.maximumFileBytes) {
                throw std::invalid_argument("Invalid embedded Lottie image");
            }
            bytes.resize(size);
            if (!CryptStringToBinaryA(encoded.c_str(), DWORD(encoded.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                      bytes.data(), &size, nullptr, nullptr)) {
                throw std::invalid_argument("Invalid embedded Lottie image");
            }
        } else {
            bytes = context_.read(path_.parent_path() / std::filesystem::u8path(name));
        }
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(factory.GetAddressOf())));
        require(factory->CreateStream(stream.GetAddressOf()));
        require(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size())));
        require(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                 decoder.GetAddressOf()));
        GUID format;
        require(decoder->GetContainerFormat(&format));
        if (format != GUID_ContainerFormatPng && format != GUID_ContainerFormatJpeg) {
            throw std::invalid_argument("Lottie image assets must be PNG or JPEG");
        }
        require(decoder->GetFrame(0, frame.GetAddressOf()));
        UINT width, height;
        require(frame->GetSize(&width, &height));
        if (!width || !height || width > 4096 || height > 4096) {
            throw std::length_error("Lottie image dimensions exceed limits");
        }
        auto lease = context_.budget->reserve(uint64_t(width) * height * 4);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        require(factory->CreateFormatConverter(converter.GetAddressOf()));
        require(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom));
        require(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
        return MemoryBudget::attach(ImageData::fromPremultipliedBgra(width, height, std::move(pixels)),
                                    std::move(lease));
    }

    void prepareLottie(const std::vector<uint8_t>& bytes) {
        size_t nodes = 0;
        auto json = nlohmann::json::parse(bytes.begin(), bytes.end(),
                                          [&](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                                              if (++nodes > 100000 || depth > 64) {
                                                  throw std::length_error("Lottie structural limit exceeded");
                                              }
                                              return true;
                                          });
        if (!json.is_object() || !json.contains("layers") || !json["layers"].is_array()) {
            throw std::invalid_argument("Invalid Lottie root");
        }
        const auto validate = [&](auto&& self, const nlohmann::json& value) -> void {
            if (value.is_object()) {
                if ((value.contains("x") && value["x"].is_string() &&
                     !value["x"].get_ref<const std::string&>().empty()) ||
                    (value.contains("ddd") && value["ddd"] != 0) || (value.contains("ef") && !value["ef"].empty()) ||
                    value.contains("fonts") || value.contains("chars")) {
                    throw std::invalid_argument("Lottie expression/3D/effect/font feature is unsupported");
                }
                if (value.contains("ty") && value["ty"].is_number_integer()) {
                    const auto type = value["ty"].get<int>();
                    if (type == 5 || type == 6 || type == 13) {
                        throw std::invalid_argument("Lottie text/audio/camera layer is unsupported");
                    }
                }
            }
            if (value.is_structured()) {
                for (const auto& child : value) {
                    self(self, child);
                }
            }
        };
        validate(validate, json);
        if (json.contains("assets")) {
            if (!json["assets"].is_array() || json["assets"].size() > 128) {
                throw std::length_error("Lottie asset limit exceeded");
            }
            for (auto& asset : json["assets"]) {
                if (!asset.contains("p")) {
                    continue;
                }
                const auto p = asset["p"].get<std::string>();
                const auto name = p.starts_with("data:") ? p : asset.value("u", std::string{}) + p;
                const auto key = "https://cybercanvas.invalid/asset/" + std::to_string(images_.size());
                images_[key] = decodeImage(name);
                // Synthetic HTTP reference bypasses ThorVG's eager file opening. Our
                // resolver substitutes already validated pixels; no network is used.
                asset["p"] = key;
                asset["u"] = "";
                asset["e"] = 0;
            }
        }
        data_ = json.dump();
    }
};
}  // namespace

AnimationSourceFactory vectorAnimation(std::filesystem::path relative, VectorKind kind) {
    return [relative = std::move(relative), kind](const AssetContext& context) {
        return std::make_unique<VectorSource>(context, relative, kind);
    };
}
}  // namespace cybercanvas
