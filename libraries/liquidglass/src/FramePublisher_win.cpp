#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "LiquidGlass/FramePublisher.h"
#include "libraries/liquidglass/src/FrameConsumer_win.h"

namespace liquidglass {
namespace {
using Microsoft::WRL::ComPtr;

struct Request {
    std::vector<Region> regions;
    std::function<void()> wake;
};

struct Channel {
    std::mutex mutex;
    uintptr_t window = 0;
    Region bounds;
    std::atomic_bool subscribed{false};
    std::atomic_bool active{true};
    std::function<void()> requestFrame;
    std::map<uint64_t, Request> requests;
    uint64_t nextRequest = 0, revision = 0;
    unsigned busyAttempts = 0;
    std::string error;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIKeyedMutex> keyed;
    HANDLE shared = nullptr;
};

struct Registry {
    std::mutex mutex;
    std::vector<std::weak_ptr<Channel>> channels;
};

Registry& registry() {
    static Registry value;
    return value;
}

Region clipped(Region area, Region bounds) {
    const int x = std::max(area.x, bounds.x), y = std::max(area.y, bounds.y);
    return {x, y, std::max(0, std::min(area.x + area.width, bounds.x + bounds.width) - x),
            std::max(0, std::min(area.y + area.height, bounds.y + bounds.height) - y)};
}
}  // namespace

struct FramePublisher::Impl {
    std::shared_ptr<Channel> channel = std::make_shared<Channel>();
    std::function<void()> requestFrame;
    std::atomic_uint busyAttempts{0};
};

FramePublisher::FramePublisher(uintptr_t window, Region bounds, std::function<void()> requestFrame)
    : m_impl(std::make_unique<Impl>()) {
    auto& state = *m_impl->channel;
    state.window = window;
    state.bounds = bounds;
    m_impl->requestFrame = requestFrame;
    state.requestFrame = std::move(requestFrame);
    auto& entries = registry();
    std::lock_guard lock(entries.mutex);
    std::erase_if(entries.channels, [](const auto& entry) {
        return entry.expired();
    });
    entries.channels.push_back(m_impl->channel);
}

FramePublisher::~FramePublisher() {
    auto& state = *m_impl->channel;
    std::lock_guard lock(state.mutex);
    state.active = false;
    state.requestFrame = {};
    state.texture.Reset();
    state.keyed.Reset();
    state.shared = nullptr;
    for (const auto& [id, request] : state.requests) {
        if (request.wake) {
            request.wake();
        }
    }
}

bool FramePublisher::subscribed() const {
    return m_impl->channel->subscribed.load();
}

void FramePublisher::publish(void* nativeDevice, void* nativeContext, void* nativeTexture) {
    if (!subscribed()) {
        return;
    }
    auto& state = *m_impl->channel;
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        if (m_impl->busyAttempts.fetch_add(1) < 3 && m_impl->requestFrame) {
            m_impl->requestFrame();
        }
        return;
    }
    m_impl->busyAttempts.store(0);
    try {
        if (!nativeDevice || !nativeContext || !nativeTexture) {
            throw std::invalid_argument("Published GPU source requires a device, context and texture");
        }
        auto& device = *static_cast<ID3D11Device*>(nativeDevice);
        auto& context = *static_cast<ID3D11DeviceContext*>(nativeContext);
        auto& source = *static_cast<ID3D11Texture2D*>(nativeTexture);
        if (!state.active || state.requests.empty()) {
            return;
        }
        if (!state.texture) {
            D3D11_TEXTURE2D_DESC description{};
            source.GetDesc(&description);
            if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM || description.SampleDesc.Count != 1 ||
                description.Width != UINT(state.bounds.width) || description.Height != UINT(state.bounds.height)) {
                throw std::runtime_error("Published desktop texture has unsupported size, format or multisampling");
            }
            description.Usage = D3D11_USAGE_DEFAULT;
            description.CPUAccessFlags = 0;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            description.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            winrt::check_hresult(device.CreateTexture2D(&description, nullptr, &state.texture));
            winrt::check_hresult(state.texture.As(&state.keyed));
            ComPtr<IDXGIResource> resource;
            winrt::check_hresult(state.texture.As(&resource));
            // Legacy shared handles are owned by the D3D resource, not CloseHandle.
            winrt::check_hresult(resource->GetSharedHandle(&state.shared));
        }
        const auto acquired = state.keyed->AcquireSync(0, 0);
        if (acquired == WAIT_TIMEOUT) {
            if (++state.busyAttempts < 3) {
                const auto request = state.requestFrame;
                lock.unlock();
                if (request) {
                    request();
                }
            } else {
                state.error = "Shared desktop GPU source remained busy after three bounded attempts";
                for (const auto& [id, request] : state.requests) {
                    if (request.wake) {
                        request.wake();
                    }
                }
            }
            return;
        }
        winrt::check_hresult(acquired);
        state.busyAttempts = 0;
        for (const auto& [id, request] : state.requests) {
            for (auto area : request.regions) {
                area = clipped(area, state.bounds);
                if (area.width < 1 || area.height < 1) {
                    continue;
                }
                const UINT x = UINT(area.x - state.bounds.x), y = UINT(area.y - state.bounds.y);
                const D3D11_BOX box{x, y, 0, x + UINT(area.width), y + UINT(area.height), 1};
                context.CopySubresourceRegion(state.texture.Get(), 0, x, y, 0, &source, 0, &box);
            }
        }
        winrt::check_hresult(state.keyed->ReleaseSync(0));
        context.Flush();
        ++state.revision;
        state.error.clear();
    } catch (const winrt::hresult_error& error) {
        state.error = winrt::to_string(error.message());
    } catch (const std::exception& error) {
        state.error = error.what();
    }
    for (const auto& [id, request] : state.requests) {
        if (request.wake) {
            request.wake();
        }
    }
}

void FramePublisher::setUnavailable(std::string reason) {
    if (!subscribed()) {
        return;
    }
    auto& state = *m_impl->channel;
    std::lock_guard lock(state.mutex);
    if (state.error == reason) {
        return;
    }
    state.error = std::move(reason);
    state.texture.Reset();
    state.keyed.Reset();
    state.shared = nullptr;
    for (const auto& [id, request] : state.requests) {
        if (request.wake) {
            request.wake();
        }
    }
}

struct detail::FrameConsumer::Impl {
    std::shared_ptr<Channel> channel;
    uint64_t id = 0, revision = 0;
    HANDLE shared = nullptr;
    ComPtr<ID3D11Texture2D> imported, texture;
    ComPtr<IDXGIKeyedMutex> keyed;
    ComPtr<ID2D1Bitmap1> bitmap;
    bool ready = false;

    Impl(std::shared_ptr<Channel> value, std::function<void()> wake) : channel(std::move(value)) {
        std::lock_guard lock(channel->mutex);
        id = ++channel->nextRequest;
        channel->requests.emplace(id, Request{{}, std::move(wake)});
        channel->subscribed.store(true);
    }

    ~Impl() {
        std::lock_guard lock(channel->mutex);
        channel->requests.erase(id);
        if (channel->requests.empty()) {
            channel->subscribed.store(false);
            channel->texture.Reset();
            channel->keyed.Reset();
            channel->shared = nullptr;
        }
    }
};

std::unique_ptr<detail::FrameConsumer> detail::FrameConsumer::find(uintptr_t root, Region output,
                                                                   std::function<void()> wake) {
    auto& entries = registry();
    std::lock_guard lock(entries.mutex);
    for (const auto& entry : entries.channels) {
        const auto channel = entry.lock();
        if (channel && channel->active &&
            uintptr_t(GetAncestor(reinterpret_cast<HWND>(channel->window), GA_ROOT)) == root &&
            clipped(output, channel->bounds) == output) {
            return std::unique_ptr<FrameConsumer>(new FrameConsumer(std::make_unique<Impl>(channel, std::move(wake))));
        }
    }
    return {};
}

detail::FrameConsumer::FrameConsumer(std::unique_ptr<Impl> state) : m_impl(std::move(state)) {}

detail::FrameConsumer::~FrameConsumer() = default;

void detail::FrameConsumer::configure(const std::vector<Region>& regions) {
    auto& state = *m_impl;
    std::lock_guard lock(state.channel->mutex);
    auto& request = state.channel->requests.at(state.id);
    if (request.regions != regions) {
        request.regions = regions;
        state.ready = false;
        if (state.channel->requestFrame) {
            state.channel->requestFrame();
        }
    }
}

bool detail::FrameConsumer::take(ID3D11Device& device, ID2D1DeviceContext& context) {
    auto& state = *m_impl;
    std::lock_guard lock(state.channel->mutex);
    const auto& channel = *state.channel;
    if (!channel.active) {
        throw std::runtime_error("Published desktop renderer became unavailable");
    }
    if (!channel.error.empty()) {
        state.bitmap.Reset();
        state.texture.Reset();
        state.imported.Reset();
        state.keyed.Reset();
        state.ready = false;
        state.shared = nullptr;
        return false;
    }
    if (!channel.shared || state.revision == channel.revision) {
        return false;
    }
    if (state.shared != channel.shared || !state.imported) {
        state.bitmap.Reset();
        state.texture.Reset();
        state.keyed.Reset();
        state.imported.Reset();
        winrt::check_hresult(device.OpenSharedResource(channel.shared, IID_PPV_ARGS(&state.imported)));
        winrt::check_hresult(state.imported.As(&state.keyed));
        D3D11_TEXTURE2D_DESC description{};
        state.imported->GetDesc(&description);
        description.MiscFlags = 0;
        winrt::check_hresult(device.CreateTexture2D(&description, nullptr, &state.texture));
        ComPtr<IDXGISurface> surface;
        winrt::check_hresult(state.texture.As(&surface));
        const auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::check_hresult(context.CreateBitmapFromDxgiSurface(surface.Get(), &properties, &state.bitmap));
        state.shared = channel.shared;
    }
    const auto acquired = state.keyed->AcquireSync(0, 5);
    if (acquired == WAIT_TIMEOUT) {
        const auto& request = channel.requests.at(state.id);
        if (request.wake) {
            request.wake();
        }
        return false;
    }
    winrt::check_hresult(acquired);
    ComPtr<ID3D11DeviceContext> immediate;
    device.GetImmediateContext(&immediate);
    for (auto area : channel.requests.at(state.id).regions) {
        area = clipped(area, channel.bounds);
        if (area.width < 1 || area.height < 1) {
            continue;
        }
        const UINT x = UINT(area.x - channel.bounds.x), y = UINT(area.y - channel.bounds.y);
        const D3D11_BOX box{x, y, 0, x + UINT(area.width), y + UINT(area.height), 1};
        immediate->CopySubresourceRegion(state.texture.Get(), 0, x, y, 0, state.imported.Get(), 0, &box);
    }
    winrt::check_hresult(state.keyed->ReleaseSync(0));
    immediate->Flush();
    state.revision = channel.revision;
    state.ready = true;
    return true;
}

Region detail::FrameConsumer::bounds() const {
    return m_impl->channel->bounds;
}

uint64_t detail::FrameConsumer::bytes() const {
    if (!m_impl->texture) {
        return 0;
    }
    const auto area = bounds();
    return uint64_t(area.width) * area.height * 8;
}

std::string detail::FrameConsumer::error() const {
    std::lock_guard lock(m_impl->channel->mutex);
    return m_impl->channel->error;
}

ID2D1Bitmap1* detail::FrameConsumer::bitmap() const {
    return m_impl->ready ? m_impl->bitmap.Get() : nullptr;
}
}  // namespace liquidglass
