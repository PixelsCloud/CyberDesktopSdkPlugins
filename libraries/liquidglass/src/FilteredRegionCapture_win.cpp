#include <Magnification.h>
#include <Windows.h>
#include <d3d11.h>
#include <wil/resource.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "LiquidGlass/FilteredRegionCapture.h"

namespace liquidglass {
class FilteredCaptureSession final {
public:
    FilteredCaptureSession() {
        winrt::check_bool(MagInitialize());
    }

    ~FilteredCaptureSession() {
        MagUninitialize();
    }
};

namespace {
using Microsoft::WRL::ComPtr;

std::shared_ptr<FilteredCaptureSession> runtime() {
    static std::mutex mutex;
    static std::weak_ptr<FilteredCaptureSession> current;
    const std::lock_guard lock(mutex);
    auto result = current.lock();
    if (!result) {
        result = std::make_shared<FilteredCaptureSession>();
        current = result;
    }
    return result;
}

struct Frame {
    std::mutex mutex;
    std::vector<unsigned char> pixels;
    unsigned width = 0, height = 0, stride = 0;
    bool accepting = false, received = false;
    unsigned observedWidth = 0, observedHeight = 0;
};

constexpr size_t kCaptureSlots = 16;
std::mutex slotsMutex;
std::array<std::weak_ptr<Frame>, kCaptureSlots> slots;

// A callback outside a current request is a no-op, not an unsupported scaling
// operation. The separate received flag is the only permission to upload bytes.
BOOL copyFrame(size_t slot, void* source, MAGIMAGEHEADER description) noexcept {
    try {
        std::shared_ptr<Frame> frame;
        {
            const std::unique_lock lock(slotsMutex, std::try_to_lock);
            if (!lock) {
                return TRUE;
            }
            frame = slots[slot].lock();
        }
        if (!frame || !source || !IsEqualGUID(description.format, GUID_WICPixelFormat32bppRGBA)) {
            return TRUE;
        }
        const std::unique_lock lock(frame->mutex, std::try_to_lock);
        if (!lock) {
            return TRUE;
        }
        frame->observedWidth = description.width;
        frame->observedHeight = description.height;
        if (!frame->accepting || description.width != frame->width || description.height != frame->height ||
            description.stride < description.width * 4 || description.offset > description.cbSize) {
            return TRUE;
        }
        const auto bytes = size_t(description.stride) * description.height;
        if (bytes > description.cbSize - description.offset || bytes > frame->pixels.size()) {
            return TRUE;
        }
        std::memcpy(frame->pixels.data(), static_cast<const unsigned char*>(source) + description.offset, bytes);
        frame->stride = description.stride;
        frame->received = true;
        return TRUE;
    } catch (...) {
        return TRUE;
    }
}

// The Magnification callback can report an internal HWND instead of the
// registered control. A bounded trampoline pool gives each control its own
// weak context without depending on that HWND or allocating executable code.
template <size_t Slot>
BOOL CALLBACK receive(HWND, void* source, MAGIMAGEHEADER description, void*, MAGIMAGEHEADER, RECT, RECT,
                      HRGN) noexcept {
    return copyFrame(Slot, source, description);
}

template <size_t... Slots>
constexpr auto callbacks(std::index_sequence<Slots...>) {
    return std::array<MagImageScalingCallback, sizeof...(Slots)>{&receive<Slots>...};
}

constexpr auto kCallbacks = callbacks(std::make_index_sequence<kCaptureSlots>{});

class CallbackLease final {
public:
    explicit CallbackLease(const std::shared_ptr<Frame>& frame) {
        const std::lock_guard lock(slotsMutex);
        for (size_t slot = 0; slot < kCaptureSlots; ++slot) {
            if (slots[slot].expired()) {
                index = slot;
                slots[slot] = frame;
                return;
            }
        }
        throw std::runtime_error("Local filtered capture callback budget exceeded");
    }

    ~CallbackLease() {
        const std::lock_guard lock(slotsMutex);
        slots[index].reset();
    }

    CallbackLease(const CallbackLease&) = delete;
    CallbackLease& operator=(const CallbackLease&) = delete;

    MagImageScalingCallback callback() const {
        return kCallbacks[index];
    }

private:
    size_t index = 0;
};
}  // namespace

struct FilteredRegionCapture::Impl {
    std::shared_ptr<FilteredCaptureSession> session = runtime();
    std::shared_ptr<Frame> frame = std::make_shared<Frame>();
    CallbackLease callback{frame};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> image;
    ComPtr<ID3D11RenderTargetView> clear;
    wil::unique_hwnd host, control;
    std::vector<HWND> excluded;
    CaptureRectangle last{};
    int controlWidth = 0, controlHeight = 0;
    DWORD thread = GetCurrentThreadId();

    explicit Impl(void* source) {
        if (!source) {
            throw std::invalid_argument("Local filtered capture needs a D3D11 device");
        }
        device = static_cast<ID3D11Device*>(source);
        device->GetImmediateContext(&context);
        host.reset(CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, L"STATIC",
                                   L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
        winrt::check_bool(bool(host));
        winrt::check_bool(SetLayeredWindowAttributes(host.get(), 0, 0, LWA_ALPHA));
        ShowWindow(host.get(), SW_HIDE);
    }

    void prepareControl(int width, int height) {
        if (control && controlWidth == width && controlHeight == height) {
            return;
        }
        // Resizing a live magnifier can reenter its paint path with the previous
        // source bitmap and the new destination size. Keep each control's size
        // immutable; finish its callback lifetime before replacing it.
        if (control) {
            MagSetImageScalingCallback(control.get(), nullptr);
            control.reset();
        }
        excluded.clear();
        control.reset(CreateWindowW(WC_MAGNIFIER, L"", WS_CHILD | WS_VISIBLE, 0, 0, width, height, host.get(), nullptr,
                                    GetModuleHandleW(nullptr), nullptr));
        winrt::check_bool(bool(control));
        winrt::check_bool(MagSetImageScalingCallback(control.get(), callback.callback()));
        controlWidth = width;
        controlHeight = height;
    }

    ~Impl() {
        if (control) {
            MagSetImageScalingCallback(control.get(), nullptr);
        }
        control.reset();
        host.reset();
    }

    void allocate(CaptureRectangle region) {
        if (image && last.width == region.width && last.height == region.height) {
            return;
        }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = unsigned(region.width);
        description.Height = unsigned(region.height);
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        // The legacy callback reports the RGBA GUID, but its desktop DIB bytes
        // are BGRA (also consumed as desktop-frame BGRA by WebRTC). Describe the
        // actual byte layout to D3D instead of swapping channels on the CPU.
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        clear.Reset();
        image.Reset();
        winrt::check_hresult(device->CreateTexture2D(&description, nullptr, &image));
        winrt::check_hresult(device->CreateRenderTargetView(image.Get(), nullptr, &clear));
        last.width = region.width;
        last.height = region.height;
        const std::lock_guard lock(frame->mutex);
        const auto required = size_t(region.width + 64) * region.height * 4 + 16384;
        if (frame->pixels.capacity() > required * 2) {
            std::vector<unsigned char>(required).swap(frame->pixels);
        } else {
            frame->pixels.resize(required);
        }
    }
};

std::shared_ptr<FilteredCaptureSession> FilteredRegionCapture::initializeSession() {
    return runtime();
}

FilteredRegionCapture::FilteredRegionCapture(void* d3dDevice) : m_impl(std::make_unique<Impl>(d3dDevice)) {}

FilteredRegionCapture::~FilteredRegionCapture() = default;

void FilteredRegionCapture::capture(CaptureRectangle rectangle, std::span<const uintptr_t> excludedWindows) {
    auto& state = *m_impl;
    if (GetCurrentThreadId() != state.thread || rectangle.width < 1 || rectangle.height < 1 || rectangle.width > 8192 ||
        rectangle.height > 8192 || int64_t(rectangle.width) * rectangle.height > 4000000 ||
        excludedWindows.size() > 64) {
        throw std::invalid_argument("Local filtered capture exceeds its thread, region or exclusion budget");
    }
    state.allocate(rectangle);
    const RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                      GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                      GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    const RECT region{std::max(rectangle.x, int(screen.left)), std::max(rectangle.y, int(screen.top)),
                      int(std::min(int64_t(rectangle.x) + rectangle.width, int64_t(screen.right))),
                      int(std::min(int64_t(rectangle.y) + rectangle.height, int64_t(screen.bottom)))};
    if (region.left != rectangle.x || region.top != rectangle.y || region.right - region.left != rectangle.width ||
        region.bottom - region.top != rectangle.height) {
        const float black[4]{0, 0, 0, 1};
        state.context->ClearRenderTargetView(state.clear.Get(), black);
    }
    if (region.right <= region.left || region.bottom <= region.top) {
        state.last = rectangle;
        return;
    }
    state.prepareControl(region.right - region.left, region.bottom - region.top);
    std::array<HWND, 66> windows{};
    size_t count = 0;
    for (const auto value : excludedWindows) {
        const auto window = reinterpret_cast<HWND>(value);
        if (window && IsWindow(window) && IsWindowVisible(window) &&
            std::find(windows.begin(), windows.begin() + count, window) == windows.begin() + count) {
            windows[count++] = window;
        }
    }
    windows[count++] = state.host.get();
    windows[count++] = state.control.get();
    if (count != state.excluded.size() ||
        !std::equal(windows.begin(), windows.begin() + count, state.excluded.begin())) {
        if (!MagSetWindowFilterList(state.control.get(), MW_FILTERMODE_EXCLUDE, int(count), windows.data())) {
            const auto error = GetLastError();
            throw winrt::hresult_error(HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE),
                                       L"MagSetWindowFilterList rejected the local exclusion list");
        }
        state.excluded.assign(windows.begin(), windows.begin() + count);
    }
    if (region.right > region.left && region.bottom > region.top) {
        {
            const std::lock_guard lock(state.frame->mutex);
            state.frame->width = unsigned(region.right - region.left);
            state.frame->height = unsigned(region.bottom - region.top);
            state.frame->received = false;
            state.frame->accepting = true;
        }
        // The hidden control must receive a fresh layout notification even for
        // an unchanged rectangle; this schedules the synchronous capture callback.
        winrt::check_bool(SetWindowPos(state.control.get(), nullptr, region.left, region.top,
                                       region.right - region.left, region.bottom - region.top,
                                       SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE));
        const BOOL captured = MagSetWindowSource(state.control.get(), region);
        const DWORD error = captured ? ERROR_SUCCESS : GetLastError();
        const std::lock_guard lock(state.frame->mutex);
        state.frame->accepting = false;
        if (!captured) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE),
                                       L"MagSetWindowSource failed");
        }
        if (!state.frame->received) {
            throw std::runtime_error("Local filtered capture returned no current RGBA frame: received " +
                                     std::to_string(state.frame->observedWidth) + "x" +
                                     std::to_string(state.frame->observedHeight));
        }
        const D3D11_BOX destination{unsigned(region.left - rectangle.x),  unsigned(region.top - rectangle.y),    0,
                                    unsigned(region.right - rectangle.x), unsigned(region.bottom - rectangle.y), 1};
        state.context->UpdateSubresource(state.image.Get(), 0, &destination, state.frame->pixels.data(),
                                         state.frame->stride, 0);
    }
    state.last = rectangle;
}

void* FilteredRegionCapture::texture() const {
    return m_impl->image.Get();
}

uint64_t FilteredRegionCapture::allocatedBytes() const {
    return uint64_t(m_impl->last.width) * m_impl->last.height * 4 + m_impl->frame->pixels.capacity();
}
}  // namespace liquidglass
