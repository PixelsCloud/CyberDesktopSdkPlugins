#include "tools/PreviewLive2D.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "runtime/live2d/Session.h"
#include "tools/PreviewEvents.h"

namespace cdsdk {
namespace {
bool validText(const char* value, size_t maximum) {
    if (!value) {
        return false;
    }
    const auto size = strnlen_s(value, maximum + 1);
    return size <= maximum &&
           (!size || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, int(size), nullptr, 0));
}

bool contained(const std::filesystem::path& root, const std::filesystem::path& value) {
    auto child = value.begin();
    for (auto parent = root.begin(); parent != root.end(); ++parent, ++child) {
        if (child == value.end() || _wcsicmp(parent->c_str(), child->c_str()) != 0) {
            return false;
        }
    }
    return child != value.end();
}

void check(HRESULT status) {
    if (FAILED(status)) {
        throw std::runtime_error("Preview Live2D texture allocation/readback failed");
    }
}
}  // namespace

struct PreviewLive2D::Impl {
    PreviewEvents events;
    cd::live2d::Session session;
    std::filesystem::path root;
    QString directory;
    QJsonObject options;
    bool paused = false;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    uint32_t width = 0, height = 0;
};

PreviewLive2D::PreviewLive2D(std::function<void()> redraw) : impl_(std::make_unique<Impl>()) {
    impl_->session.onChanged = std::move(redraw);
}

PreviewLive2D::~PreviewLive2D() {
    impl_->session.onChanged = {};
    impl_->session.stop();
}

void PreviewLive2D::root(const std::filesystem::path& directory) {
    impl_->root = std::filesystem::weakly_canonical(directory);
}

CdResult PreviewLive2D::configure(const CdLive2DOptions* options) {
    if (!options || options->struct_size < sizeof(*options) || !validText(options->directory, 4096) ||
        !validText(options->motion, 256) || !validText(options->expression, 256) || options->fps < 1 ||
        options->fps > 60 || !std::isfinite(options->scale) || options->scale < .1 || options->scale > 4 ||
        !std::isfinite(options->x) || !std::isfinite(options->y) || std::abs(options->x) > 2 ||
        std::abs(options->y) > 2 || options->paused > 1 || options->physics > 1 || options->follow > 1) {
        return CD_ERROR_ARGUMENT;
    }
    if (!*options->directory) {
        impl_->directory.clear();
        impl_->session.stop();
        return CD_OK;
    }
    const auto requested = std::filesystem::u8path(options->directory);
    std::error_code error;
    const auto path = std::filesystem::canonical(requested, error);
    if (error || !requested.is_absolute() || !std::filesystem::is_directory(path) || !contained(impl_->root, path) ||
        requested.relative_path().wstring().find(L':') != std::wstring::npos) {
        return CD_ERROR_ARGUMENT;
    }
    impl_->directory = QString::fromStdWString(path.wstring());
    impl_->options = {{"fps", int(options->fps)},
                      {"motion", QString::fromUtf8(options->motion)},
                      {"expression", QString::fromUtf8(options->expression)},
                      {"scale", options->scale},
                      {"x", options->x},
                      {"y", options->y},
                      {"paused", bool(options->paused)},
                      {"physics", bool(options->physics)},
                      {"follow", bool(options->follow)}};
    auto effective = impl_->options;
    effective["paused"] = impl_->paused || options->paused;
    impl_->session.configure(impl_->directory, effective);
    return CD_OK;
}

CdResult PreviewLive2D::snapshot(CdLive2DStatus* output) const {
    if (!output || output->struct_size < sizeof(*output)) {
        return CD_ERROR_ARGUMENT;
    }
    CdLive2DStatus result{sizeof(result), uint32_t(impl_->session.state()), impl_->session.frames()};
    const auto info = impl_->session.information();
    const auto list = [&](const char* key) {
        QStringList items;
        for (const auto& value : info[key].toArray()) {
            items.push_back(value.toString());
        }
        return items.join('\n').toUtf8();
    };
    const auto message = impl_->session.error().toUtf8();
    const auto motions = list("motions"), expressions = list("expressions");
    strncpy_s(result.message, message.constData(), _TRUNCATE);
    strncpy_s(result.motions, motions.constData(), _TRUNCATE);
    strncpy_s(result.expressions, expressions.constData(), _TRUNCATE);
    *output = result;
    return CD_OK;
}

CdResult PreviewLive2D::perform(const char* motion, uint32_t priority) {
    if (!validText(motion, 256) || !*motion || priority < 1 || priority > 3) {
        return CD_ERROR_ARGUMENT;
    }
    if (impl_->paused || impl_->session.state() != cd::live2d::Session::Ready) {
        return CD_ERROR_BUSY;
    }
    return impl_->session.performMotion(QString::fromUtf8(motion), priority) ? CD_OK : CD_ERROR_ARGUMENT;
}

void PreviewLive2D::pointer(double x, double y, uint32_t tap) {
    if (!impl_->paused && std::isfinite(x) && std::isfinite(y) && tap <= 1) {
        impl_->session.pointer(std::clamp(x, -1., 1.), std::clamp(y, -1., 1.), tap != 0);
    }
}

void PreviewLive2D::pause(bool paused) {
    impl_->paused = paused;
    if (!impl_->directory.isEmpty()) {
        auto options = impl_->options;
        options["paused"] = paused || options["paused"].toBool();
        impl_->session.configure(impl_->directory, options);
    }
}

void PreviewLive2D::tick() {
    impl_->events.pump();
}

bool PreviewLive2D::render(const CdFrame& frame) {
    if (impl_->directory.isEmpty() || !frame.width || !frame.height || frame.width > 2048 || frame.height > 2048) {
        return false;
    }
    if (!impl_->device) {
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                0, D3D11_SDK_VERSION, &impl_->device, nullptr, &impl_->context));
    }
    auto* view = impl_->session.texture(impl_->device.Get(), impl_->context.Get(), QSize(frame.width, frame.height));
    if (!view || impl_->session.state() != cd::live2d::Session::Ready || !impl_->session.frames()) {
        return false;
    }
    if (impl_->width != frame.width || impl_->height != frame.height) {
        impl_->staging.Reset();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = frame.width;
        description.Height = frame.height;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(impl_->device->CreateTexture2D(&description, nullptr, &impl_->staging));
        impl_->width = frame.width;
        impl_->height = frame.height;
    }
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    impl_->context->CopyResource(impl_->staging.Get(), resource.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(impl_->context->Map(impl_->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    // The standalone CPU preview/export explicitly reads back model pixels.
    // It never advertises the zero-readback render.composition service.
    for (uint32_t y = 0; y < frame.height; ++y) {
        std::memcpy(frame.rgba + size_t(y) * frame.stride,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(frame.width) * 4);
    }
    impl_->context->Unmap(impl_->staging.Get(), 0);
    return true;
}
}  // namespace cdsdk
