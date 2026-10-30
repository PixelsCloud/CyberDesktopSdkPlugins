#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <werapi.h>
#include <wrl/client.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLocalSocket>
#include <QTimer>
#include <array>
#include <memory>

#include "Model.h"
#include "Protocol.h"
#include "ShaderCache.h"
#undef D3DCompile
#include <Rendering/D3D11/CubismDeviceInfo_D3D11.hpp>

namespace {
using Microsoft::WRL::ComPtr;
using namespace cd::live2d;

void suppressErrorUi() {
    constexpr UINT mode = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    SetErrorMode(mode);
    if (!SetThreadErrorMode(mode, nullptr) || FAILED(WerSetFlags(WER_FAULT_REPORTING_NO_UI))) {
        throw Failure("Cannot configure silent model failure reporting");
    }
}

class Worker {
public:
    explicit Worker(const QString& endpoint) {
        QObject::connect(&socket_, &QLocalSocket::readyRead, [&] {
            input_ += socket_.read(kMaxPacket * 4 + 1);
            if (!consume(input_, [&](const QJsonObject& message) {
                    try {
                        receive(message);
                    } catch (const std::exception& error) {
                        timer_.stop();
                        send({{"type", "error"}, {"message", QString::fromUtf8(error.what()).left(512)}});
                    }
                })) {
                QCoreApplication::exit(2);
            }
        });
        QObject::connect(&socket_, &QLocalSocket::disconnected, [] {
            QCoreApplication::quit();
        });
        QObject::connect(&socket_, &QLocalSocket::errorOccurred, [] {
            QCoreApplication::exit(3);
        });
        QObject::connect(&socket_, &QLocalSocket::connected, [&] {
            send({{"type", "hello"}, {"version", kProtocolVersion}});
        });
        QObject::connect(&timer_, &QTimer::timeout, [&] {
            try {
                render();
            } catch (const std::exception& error) {
                timer_.stop();
                send({{"type", "error"}, {"message", QString::fromUtf8(error.what()).left(512)}});
            }
        });
        timer_.setTimerType(Qt::PreciseTimer);
        socket_.setReadBufferSize(kMaxPacket * 4);
        socket_.connectToServer(endpoint);
    }

    ~Worker() {
        model_.reset();
    }

private:
    struct Slot {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<IDXGIKeyedMutex> mutex;
        ComPtr<ID3D11Query> begin, end, disjoint;
        bool measured = false;
        bool pending = false;
    };

    void send(const QJsonObject& message) {
        const auto bytes = encode(message);
        if (bytes.isEmpty() || socket_.bytesToWrite() > kMaxPacket * 2) {
            QCoreApplication::exit(4);
            return;
        }
        socket_.write(bytes);
        socket_.flush();
    }

    void receive(const QJsonObject& message) {
        const auto type = message.value("type").toString();
        if (type == "load") {
            suppressErrorUi();
            if (model_ || device_) {
                throw Failure("Worker accepts one model per lifetime");
            }
#ifdef CD_LIVE2D_TEST_WORKER
            const auto fault = message.value("fault").toString();
            if (fault == "crash") {
                RaiseFailFastException(nullptr, nullptr, 0);
            }
            if (fault == "seh") {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
            }
            if (fault == "hang") {
                Sleep(INFINITE);
            }
            fault_ = fault;
#endif
            const int width = message.value("width").toInt(), height = message.value("height").toInt();
            if (!validSurfaceSize(width, height)) {
                throw Failure("Invalid surface dimensions");
            }
            LUID luid{message.value("adapterLow").toVariant().toUInt(), message.value("adapterHigh").toInt()};
            ComPtr<IDXGIFactory1> factory;
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
                throw Failure("DXGI factory failed");
            }
            for (UINT index = 0;; ++index) {
                adapter.Reset();
                if (FAILED(factory->EnumAdapters1(index, &adapter))) {
                    throw Failure("Requested GPU is unavailable");
                }
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);
                if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) {
                    break;
                }
            }
            if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_,
                                         nullptr, &context_))) {
                throw Failure("D3D11 device creation failed");
            }
            ComPtr<ID3D11Device1> device1;
            if (FAILED(device_.As(&device1))) {
                throw Failure("D3D11.1 is required");
            }
            const auto handles = message.value("handles").toArray();
            if (handles.size() != int(slots_.size())) {
                throw Failure("Invalid surface slots");
            }
            for (int i = 0; i < handles.size(); ++i) {
                bool valid = false;
                const auto raw = handles[i].toString().toULongLong(&valid);
                if (!valid || !raw) {
                    throw Failure("Invalid surface handle");
                }
                HANDLE handle = reinterpret_cast<HANDLE>(quintptr(raw));
                auto& slot = slots_[i];
                const auto result = device1->OpenSharedResource1(handle, IID_PPV_ARGS(&slot.texture));
                CloseHandle(handle);
                if (FAILED(result) || FAILED(slot.texture.As(&slot.mutex)) ||
                    FAILED(device_->CreateRenderTargetView(slot.texture.Get(), nullptr, &slot.target))) {
                    throw Failure("Shared surface open failed");
                }
                D3D11_TEXTURE2D_DESC desc{};
                slot.texture->GetDesc(&desc);
                if (desc.Width != UINT(width) || desc.Height != UINT(height) ||
                    desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                    throw Failure("Shared surface contract mismatch");
                }
            }
            suppressErrorUi();
            const auto progress = [&](const QString& path) {
                send({{"type", "progress"}, {"resource", path.left(256)}});
            };
            auto assets = loadAssets(message.value("root").toString(), progress);
            UINT quality = 0;
            if (SUCCEEDED(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality)) && quality) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = UINT(width);
                desc.Height = UINT(height);
                desc.MipLevels = desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 4;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                if (FAILED(device_->CreateTexture2D(&desc, nullptr, &antialiased_)) ||
                    FAILED(device_->CreateRenderTargetView(antialiased_.Get(), nullptr, &antialiasedTarget_))) {
                    throw Failure("Model antialiasing allocation failed");
                }
            }
            model_ = std::make_unique<Model>(assets, device_.Get(), width, height, progress);
            auto information = model_->info();
            // Every validated referenced file participates, including textures and
            // motions. Replacing an asset must invalidate its semantic bindings.
            auto names = assets.files.keys();
            names.sort();
            QCryptographicHash fingerprint(QCryptographicHash::Sha256);
            for (const auto& name : names) {
                const auto encoded = name.toUtf8();
                const auto& bytes = assets.files[name];
                fingerprint.addData(QByteArray::number(encoded.size()) + ':');
                fingerprint.addData(encoded);
                fingerprint.addData(QByteArray::number(bytes.size()) + ':');
                fingerprint.addData(bytes);
            }
            information.insert("fingerprint", QString::fromLatin1(fingerprint.result().toHex()));
            wchar_t desktopName[256]{};
            DWORD needed = 0;
            constexpr wchar_t privatePrefix[] = L"CyberDesktop.Live2D.";
            const bool isolatedDesktop = GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                                                                   desktopName, sizeof(desktopName), &needed) &&
                                         wcsncmp(desktopName, privatePrefix, std::size(privatePrefix) - 1) == 0;
            if (!isolatedDesktop) {
                throw Failure("Model worker desktop isolation is missing");
            }
            information.insert("isolatedDesktop", true);
            information.insert("exceptionSupervision", bool(IsDebuggerPresent()));
            information.insert("standardHeapRequested", qEnvironmentVariable("_NO_DEBUG_HEAP") == "1");
            information.insert("samples", antialiased_ ? 4 : 1);
            information.insert("textureBytes", double(assets.textureBytes));
            information.insert("type", "ready");
            send(information);
            configure(message.value("options").toObject());
        } else if (type == "options" && model_) {
            configure(message.value("options").toObject());
        } else if (type == "action" && model_) {
            const auto motion = message.value("motion");
            const auto priority = message.value("priority");
            if (message.size() != 3 || !motion.isString() || motion.toString().toUtf8().size() > 256 ||
                !priority.isDouble() || priority.toDouble() != priority.toInt() || priority.toInt() < 1 ||
                priority.toInt() > 3) {
                throw Failure("Invalid explicit model action");
            }
            model_->performMotion(motion.toString(), unsigned(priority.toInt()));
        } else if (type == "input" && model_) {
            inputSequence_ = message.value("inputSequence").toString();
            model_->pointer(float(message.value("x").toDouble()), float(message.value("y").toDouble()),
                            message.value("tap").toBool());
        } else if (type == "ack") {
            const int slot = message.value("slot").toInt(-1);
            if (slot < 0 || slot >= int(slots_.size())) {
                throw Failure("Invalid frame acknowledgement");
            }
            slots_[slot].pending = false;
        } else {
            throw Failure("Unexpected worker message");
        }
    }

    void configure(const QJsonObject& options) {
        const int fps = options.value("fps").toInt(30);
        const double scale = options.value("scale").toDouble(1);
        if ((fps != 30 && fps != 60) || !std::isfinite(scale) || scale < .1 || scale > 4 ||
            std::abs(options.value("x").toDouble()) > 2 || std::abs(options.value("y").toDouble()) > 2) {
            throw Failure("Invalid model options");
        }
        model_->configure(options);
        paused_ = options.value("paused").toBool();
        clock_.restart();
        if (paused_) {
            timer_.stop();
            render(true);
        } else {
            frameNs_ = 1000000000ll / fps;
            cadence_.start();
            nextFrame_ = frameNs_;
            timer_.setSingleShot(true);
            timer_.start(fps == 60 ? 17 : 33);
        }
    }

    void render(bool still = false) {
        if (!model_) {
            return;
        }
        suppressErrorUi();
        if (!paused_) {
            const auto now = cadence_.nsecsElapsed();
            nextFrame_ = std::max(nextFrame_ + frameNs_, now + 1000000);
            timer_.start(int(std::max(1ll, (nextFrame_ - now + 500000) / 1000000)));
        }
        const float delta = still ? 0.f : std::min(.05f, float(clock_.nsecsElapsed() / 1e9));
        clock_.restart();
        for (int i = 0; i < int(slots_.size()); ++i) {
            auto& slot = slots_[i];
            if (slot.pending) {
                continue;
            }
            const HRESULT acquired = slot.mutex->AcquireSync(0, 0);
            if (acquired == WAIT_TIMEOUT) {
                continue;
            }
            if (acquired != S_OK) {
                throw Failure("Shared surface synchronization lost");
            }
#ifdef CD_LIVE2D_TEST_WORKER
            if (fault_ == "owned-crash") {
                RaiseFailFastException(nullptr, nullptr, 0);
            }
            if (fault_ == "owned-seh") {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
            }
            if (fault_ == "owned-hang") {
                Sleep(INFINITE);
            }
#endif
            double gpuMs = -1;
            if (slot.measured) {
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
                UINT64 begin = 0, end = 0;
                if (context_->GetData(slot.disjoint.Get(), &timing, sizeof(timing), D3D11_ASYNC_GETDATA_DONOTFLUSH) ==
                        S_OK &&
                    !timing.Disjoint && timing.Frequency &&
                    context_->GetData(slot.begin.Get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) ==
                        S_OK &&
                    context_->GetData(slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
                    gpuMs = double(end - begin) * 1000. / double(timing.Frequency);
                }
            } else {
                D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP, 0};
                if (FAILED(device_->CreateQuery(&query, &slot.begin)) ||
                    FAILED(device_->CreateQuery(&query, &slot.end))) {
                    throw Failure("GPU timestamp allocation failed");
                }
                query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
                if (FAILED(device_->CreateQuery(&query, &slot.disjoint))) {
                    throw Failure("GPU timing allocation failed");
                }
            }
            context_->Begin(slot.disjoint.Get());
            context_->End(slot.begin.Get());
            QElapsedTimer work;
            work.start();
            model_->draw(context_.Get(), antialiasedTarget_ ? antialiasedTarget_.Get() : slot.target.Get(), delta);
            if (antialiased_) {
                context_->ResolveSubresource(slot.texture.Get(), 0, antialiased_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            }
            const double cpuMs = work.nsecsElapsed() / 1e6;
            context_->End(slot.end.Get());
            context_->End(slot.disjoint.Get());
            slot.measured = true;
            if (FAILED(slot.mutex->ReleaseSync(1))) {
                throw Failure("Shared surface release failed");
            }
            context_->Flush();
            slot.pending = true;
            send({{"type", "frame"},
                  {"slot", i},
                  {"sequence", QString::number(++sequence_)},
                  {"inputSequence", inputSequence_},
                  {"animation", model_->animation()},
                  {"cpuMs", cpuMs},
                  {"gpuMs", gpuMs}});
            return;
        }
    }

    QLocalSocket socket_;
    QTimer timer_;
    QByteArray input_;
    QElapsedTimer clock_;
    QElapsedTimer cadence_;
    qint64 frameNs_ = 33333333, nextFrame_ = 0;
#ifdef CD_LIVE2D_TEST_WORKER
    QString fault_;
#endif
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> antialiased_;
    ComPtr<ID3D11RenderTargetView> antialiasedTarget_;
    std::array<Slot, 3> slots_;
    std::unique_ptr<Model> model_;
    quint64 sequence_ = 0;
    QString inputSequence_;
    bool paused_ = false;
};
}  // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // Fail-fast bypasses exception handlers. WER flags reduce reporting UI; Session
    // also assigns a private desktop because Windows hard errors can bypass WER.
    if (FAILED(WerSetFlags(WER_FAULT_REPORTING_NO_UI))) {
        return 8;
    }
    if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, nullptr)) {
        return 8;
    }
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        return 2;
    }
    try {
        initializeCubism();
        if (app.arguments()[1] == "--prepare-shaders") {
            qputenv("CD_LIVE2D_COMPILE_SHADERS", "1");
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                         &device, nullptr, &context))) {
                return 6;
            }
            Csm::Rendering::CubismDeviceInfo_D3D11::GetDeviceInfo(device.Get());
            const bool healthy = live2dShaderCacheHealthy();
            Csm::Rendering::CubismDeviceInfo_D3D11::ReleaseAllDeviceInfo();
            disposeCubism();
            return healthy ? 0 : 7;
        }
        int result;
        {
            Worker worker(app.arguments()[1]);
            result = app.exec();
        }
        Csm::Rendering::CubismDeviceInfo_D3D11::ReleaseAllDeviceInfo();
        disposeCubism();
        return result;
    } catch (...) {
        return 5;
    }
}
