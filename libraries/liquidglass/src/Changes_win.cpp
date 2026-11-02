#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "LiquidGlass/Capture.h"
#include "LiquidGlass/Changes.h"

namespace liquidglass {
namespace {
using Microsoft::WRL::ComPtr;

void require(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed: 0x" << std::hex << uint32_t(result);
        throw std::runtime_error(message.str());
    }
}

class FrameLease final {
public:
    explicit FrameLease(ComPtr<IDXGIOutputDuplication> output) : m_output(std::move(output)) {}

    ~FrameLease() {
        m_output->ReleaseFrame();
    }

private:
    ComPtr<IDXGIOutputDuplication> m_output;
};
}  // namespace

struct FrameChanges::Impl {
    uintptr_t monitor;
    std::function<void(Region)> changed;
    std::function<void()> status;
    std::atomic_bool ready{false};
    mutable std::mutex errorMutex;
    std::string error;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> output;
    DXGI_OUTPUT_DESC descriptor{};
    std::vector<RECT> rectangles;
    std::vector<DXGI_OUTDUPL_MOVE_RECT> moves;
    std::jthread worker;

    Impl(uintptr_t target, std::function<void(Region)> notify, std::function<void()> stateChanged)
        : monitor(target),
          changed(std::move(notify)),
          status(std::move(stateChanged)),
          worker([this](std::stop_token stop) {
              run(stop);
          }) {}

    ~Impl() {
        worker.request_stop();
        worker.join();
    }

    void initialize() {
        output.Reset();
        context.Reset();
        device.Reset();
        ComPtr<IDXGIFactory1> factory;
        require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Desktop change factory");
        for (UINT adapterIndex = 0;; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> adapter;
            const auto found = factory->EnumAdapters1(adapterIndex, &adapter);
            if (found == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            require(found, "Desktop change adapter");
            for (UINT outputIndex = 0;; ++outputIndex) {
                ComPtr<IDXGIOutput> candidate;
                const auto outputFound = adapter->EnumOutputs(outputIndex, &candidate);
                if (outputFound == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                require(outputFound, "Desktop change output");
                require(candidate->GetDesc(&descriptor), "Desktop change output description");
                if (!descriptor.AttachedToDesktop || uintptr_t(descriptor.Monitor) != monitor) {
                    continue;
                }
                require(
                    D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                      nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context),
                    "Desktop change device");
                ComPtr<IDXGIOutput1> native;
                require(candidate.As(&native), "Desktop change output interface");
                require(native->DuplicateOutput(device.Get(), &output), "Desktop change notifications");
                return;
            }
        }
        throw std::runtime_error("Glass output is no longer connected");
    }

    Region screenRect(RECT rect) const {
        const int width = descriptor.DesktopCoordinates.right - descriptor.DesktopCoordinates.left;
        const int height = descriptor.DesktopCoordinates.bottom - descriptor.DesktopCoordinates.top;
        RECT rotated = rect;
        if (descriptor.Rotation == DXGI_MODE_ROTATION_ROTATE90) {
            rotated = {width - rect.bottom, rect.left, width - rect.top, rect.right};
        } else if (descriptor.Rotation == DXGI_MODE_ROTATION_ROTATE180) {
            rotated = {width - rect.right, height - rect.bottom, width - rect.left, height - rect.top};
        } else if (descriptor.Rotation == DXGI_MODE_ROTATION_ROTATE270) {
            rotated = {rect.top, height - rect.right, rect.bottom, height - rect.left};
        }
        return {descriptor.DesktopCoordinates.left + rotated.left, descriptor.DesktopCoordinates.top + rotated.top,
                rotated.right - rotated.left, rotated.bottom - rotated.top};
    }

    void frames(std::stop_token stop) {
        while (!stop.stop_requested()) {
            DXGI_OUTDUPL_FRAME_INFO info{};
            ComPtr<IDXGIResource> frame;
            const auto acquired = output->AcquireNextFrame(250, &info, &frame);
            if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            require(acquired, "Desktop change wait");
            FrameLease lease(output);
            if (!info.TotalMetadataBufferSize || !changed) {
                continue;
            }
            if (info.TotalMetadataBufferSize > 256 * 1024) {
                changed({descriptor.DesktopCoordinates.left, descriptor.DesktopCoordinates.top,
                         descriptor.DesktopCoordinates.right - descriptor.DesktopCoordinates.left,
                         descriptor.DesktopCoordinates.bottom - descriptor.DesktopCoordinates.top});
                continue;
            }
            UINT required = 0;
            // Both APIs reject a null buffer. TotalMetadataBufferSize bounds the
            // combined metadata, so it also bounds each individual typed array.
            moves.resize((info.TotalMetadataBufferSize + sizeof(DXGI_OUTDUPL_MOVE_RECT) - 1) /
                         sizeof(DXGI_OUTDUPL_MOVE_RECT));
            require(
                output->GetFrameMoveRects(UINT(moves.size() * sizeof(DXGI_OUTDUPL_MOVE_RECT)), moves.data(), &required),
                "Desktop move rectangles");
            if (required) {
                for (size_t index = 0; index < required / sizeof(DXGI_OUTDUPL_MOVE_RECT); ++index) {
                    const auto& move = moves[index];
                    changed(screenRect(move.DestinationRect));
                    changed(screenRect({move.SourcePoint.x, move.SourcePoint.y,
                                        move.SourcePoint.x + move.DestinationRect.right - move.DestinationRect.left,
                                        move.SourcePoint.y + move.DestinationRect.bottom - move.DestinationRect.top}));
                }
            }
            rectangles.resize((info.TotalMetadataBufferSize + sizeof(RECT) - 1) / sizeof(RECT));
            require(output->GetFrameDirtyRects(UINT(rectangles.size() * sizeof(RECT)), rectangles.data(), &required),
                    "Desktop dirty rectangles");
            for (size_t index = 0; index < required / sizeof(RECT); ++index) {
                changed(screenRect(rectangles[index]));
            }
        }
    }

    void run(std::stop_token stop) {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        int failures = 0;
        while (!stop.stop_requested() && failures < 3) {
            try {
                initialize();
                {
                    std::lock_guard lock(errorMutex);
                    error.clear();
                }
                ready.store(true);
                if (status) {
                    status();
                }
                frames(stop);
                break;
            } catch (const std::exception& failure) {
                ready.store(false);
                {
                    std::lock_guard lock(errorMutex);
                    error = failure.what();
                }
                if (status) {
                    status();
                }
                ++failures;
                std::mutex waitMutex;
                std::unique_lock lock(waitMutex);
                std::condition_variable_any wait;
                wait.wait_for(lock, stop, std::chrono::seconds(1), [] {
                    return false;
                });
            }
        }
        output.Reset();
        context.Reset();
        device.Reset();
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    }
};

FrameChanges::FrameChanges(uintptr_t monitor, std::function<void(Region)> changed, std::function<void()> status)
    : m_impl(std::make_unique<Impl>(monitor, std::move(changed), std::move(status))) {}

FrameChanges::~FrameChanges() = default;

bool FrameChanges::ready() const {
    return m_impl->ready.load();
}

std::string FrameChanges::error() const {
    std::lock_guard lock(m_impl->errorMutex);
    return m_impl->error;
}
}  // namespace liquidglass
