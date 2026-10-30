#include "runtime/live2d/Session.h"

#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSize>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <type_traits>

#include "runtime/live2d/Protocol.h"
#include "runtime/live2d/WorkerProcess.h"

namespace cd::live2d {
namespace {
std::atomic_int workers{0};
using NativeHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
using DesktopHandle = std::unique_ptr<std::remove_pointer_t<HDESK>, decltype(&CloseDesktop)>;

}  // namespace

class Session::Impl : public QObject {
public:
    explicit Impl(Session& owner);
    ~Impl() override;
    void configure(const QString& root, const QJsonObject& options);
    void pointer(double x, double y, bool tap);
    bool performMotion(const QString& name, unsigned priority);
    ID3D11ShaderResourceView* texture(ID3D11Device* device, ID3D11DeviceContext* context, QSize size,
                                      bool* changed = nullptr);
    void stop();

    State state() const;

    QString error() const;

    QJsonObject information() const;

    quint64 frames() const;

    QSize outputSize() const;

    qint64 processId() const;

    double lastCpuMs() const;

    double lastGpuMs() const;

    double lastInputLatencyMs() const;

    quint64 measuredInputSequence() const;

private:
    Session& owner_;

    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;
        NativeHandle handle{nullptr, &CloseHandle};
        bool pending = false;
        quint64 sequence = 0;
        quint64 inputSequence = 0;
    };

    void start();
    void send(const QJsonObject& message);
    void receive(const QJsonObject& message);
    void fail(const QString& message);
    void publish();
    void clearSurfaces();
    QLocalServer server_;
    QPointer<QLocalSocket> socket_;
    QPointer<WorkerProcess> process_;
    QTimer health_;
    QTimer inputTimer_;
    double pointerX_ = 0, pointerY_ = 0;
    bool pointerTap_ = false;
    QElapsedTimer inputClock_;
    quint64 requestedInput_ = 0, measuredInput_ = 0;
    double lastInputLatencyMs_ = -1;
    QElapsedTimer progress_, total_;
    QByteArray input_;
    QString root_, error_;
    QJsonObject options_, information_;
    QSize size_;
    State state_ = Empty;
    NativeHandle job_{nullptr, &CloseHandle};
    DesktopHandle desktop_{nullptr, &CloseDesktop};
    bool counted_ = false, loaded_ = false;
    bool notificationPending_ = false;
    quint64 frames_ = 0, sequence_ = 0;
    double lastCpuMs_ = 0;
    double lastGpuMs_ = -1;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> retained_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    std::array<Slot, 3> slots_;
};

Session::Impl::Impl(Session& owner) : owner_(owner) {
    inputTimer_.setSingleShot(true);
    inputTimer_.setInterval(16);
    connect(&inputTimer_, &QTimer::timeout, this, [this] {
        if (loaded_) {
            send({{"type", "input"},
                  {"x", pointerX_},
                  {"y", pointerY_},
                  {"tap", pointerTap_},
                  {"inputSequence", QString::number(requestedInput_)}});
        }
        pointerTap_ = false;
    });
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            auto* socket = server_.nextPendingConnection();
            if (socket_ || !process_) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            socket_ = socket;
            socket->setReadBufferSize(kMaxPacket * 4);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (socket != socket_) {
                    return;
                }
                input_ += socket->read(kMaxPacket * 4 + 1);
                if (!consume(input_, [this](const auto& message) {
                        receive(message);
                    })) {
                    fail("Invalid model worker response");
                }
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                if (socket == socket_ && state_ != Empty && state_ != Failed) {
                    fail("Model worker disconnected");
                }
            });
        }
    });
    health_.setInterval(250);
    connect(&health_, &QTimer::timeout, this, [this] {
        if (state_ == Loading && (progress_.elapsed() > owner_.loadTimeoutMs || total_.elapsed() > 60000)) {
            fail("Model loading timed out");
        } else if (state_ == Ready && !options_.value("paused").toBool() && progress_.elapsed() > 5000) {
            // Backpressure is legitimate when the host is not consuming a hidden surface.
            const bool full = std::all_of(slots_.begin(), slots_.end(), [](const auto& slot) {
                return slot.pending;
            });
            if (!full) {
                fail("Model rendering timed out");
            }
        }
    });
}

Session::Impl::~Impl() {
    stop();
    clearSurfaces();
}

qint64 Session::Impl::processId() const {
    return process_ ? process_->processId() : 0;
}

void Session::Impl::publish() {
    if (notificationPending_) {
        return;
    }
    notificationPending_ = true;
    QTimer::singleShot(0, this, [this] {
        notificationPending_ = false;
        auto callback = owner_.onChanged;
        if (callback) {
            callback();
        }
    });
}

void Session::Impl::clearSurfaces() {
    for (auto& slot : slots_) {
        slot = {};
    }
    retained_.Reset();
    view_.Reset();
    size_ = {};
    device_.Reset();
}

void Session::Impl::stop() {
    health_.stop();
    inputTimer_.stop();
    pointerTap_ = false;
    requestedInput_ = measuredInput_ = 0;
    lastInputLatencyMs_ = -1;
    state_ = Empty;
    loaded_ = false;
    server_.close();
    if (socket_) {
        auto* old = socket_.data();
        socket_ = nullptr;
        disconnect(old, nullptr, this, nullptr);
        old->abort();
        old->deleteLater();
    }
    job_.reset();
    if (desktop_) {
        EnumDesktopWindows(
            desktop_.get(),
            [](HWND window, LPARAM) -> BOOL {
                PostMessageW(window, WM_CLOSE, 0, 0);
                return TRUE;
            },
            0);
        desktop_.reset();
    }
    if (process_) {
        auto* old = process_.data();
        process_ = nullptr;
        disconnect(old, nullptr, this, nullptr);
        old->setParent(QCoreApplication::instance());
        old->stop();
        connect(old, &QThread::finished, old, &QObject::deleteLater);
        if (old->isFinished()) {
            old->deleteLater();
        }
    }
    if (counted_) {
        --workers;
        counted_ = false;
    }
    input_.clear();
}

void Session::Impl::fail(const QString& message) {
    if (process_) {
        information_["supervisor"] = process_->diagnostics();
    }
    if (state_ == Failed) {
        return;
    }
    stop();
    state_ = Failed;
    error_ = message.left(512);
    publish();
}

void Session::Impl::configure(const QString& root, const QJsonObject& options) {
    const bool changed = root != root_;
    options_ = options;
    if (changed) {
        stop();
        clearSurfaces();
        root_ = root;
        error_.clear();
        information_ = {};
        state_ = root.isEmpty() ? Empty : Loading;
        publish();
    } else if (loaded_) {
        send({{"type", "options"}, {"options", options_}});
        progress_.restart();
    }
}

void Session::Impl::pointer(double x, double y, bool tap) {
    if (loaded_ && std::isfinite(x) && std::isfinite(y)) {
        pointerX_ = std::clamp(x, -1., 1.);
        pointerY_ = std::clamp(y, -1., 1.);
        pointerTap_ |= tap;
        ++requestedInput_;
        inputClock_.restart();
        if (!inputTimer_.isActive()) {
            inputTimer_.start();
        }
    }
}

void Session::Impl::send(const QJsonObject& message) {
    if (!socket_) {
        return;
    }
    const auto packet = encode(message);
    if (packet.isEmpty() || socket_->bytesToWrite() > kMaxPacket * 2) {
        fail("Model control queue exceeded limit");
        return;
    }
    socket_->write(packet);
}

bool Session::Impl::performMotion(const QString& name, unsigned priority) {
    if (state_ != Ready || !loaded_ || priority < 1 || priority > 3 || name.toUtf8().size() > 256 ||
        !information_.value("motions").toArray().contains(name)) {
        return false;
    }
    send({{"type", "action"}, {"motion", name}, {"priority", int(priority)}});
    return true;
}

void Session::Impl::start() {
    if (workers.fetch_add(1) >= 8) {
        --workers;
        fail("At most eight Live2D instances may run");
        return;
    }
    counted_ = true;
    job_.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.ProcessMemoryLimit = 1024ull * 1024 * 1024;
    if (!job_ || !SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        fail("Cannot establish model resource limits");
        return;
    }
    const auto endpoint = "cyber-live2d-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!server_.listen(endpoint)) {
        fail("Cannot create model control channel");
        return;
    }
    // Windows hard-error dialogs can bypass process-local WER flags. Keep model
    // workers on a private desktop from process creation, before DLL startup.
    // It is never switched to; the parent still receives bounded process failures.
    const auto desktopName = "CyberDesktop.Live2D." + QUuid::createUuid().toString(QUuid::WithoutBraces);
    desktop_.reset(
        CreateDesktopW(reinterpret_cast<LPCWSTR>(desktopName.utf16()), nullptr, nullptr, 0, GENERIC_ALL, nullptr));
    if (!desktop_) {
        fail("Cannot create isolated model desktop");
        return;
    }
    process_ = new WorkerProcess(owner_.workerOverride.isEmpty()
                                     ? QCoreApplication::applicationDirPath() + "/cyber_live2d_host.exe"
                                     : owner_.workerOverride,
                                 endpoint, desktopName, job_.get(), this);
    const QPointer<WorkerProcess> worker = process_;
    connect(process_, &QThread::finished, this, [this, worker] {
        if (worker && process_ == worker) {
            fail(worker->error().isEmpty() ? "Model process exited unexpectedly" : worker->error());
        }
    });
    state_ = Loading;
    progress_.start();
    total_.start();
    health_.start();
    process_->start();
}

void Session::Impl::receive(const QJsonObject& message) {
    if (state_ == Failed || !process_) {
        return;
    }
    const auto type = message.value("type").toString();
    if (type == "hello" && !loaded_ && message.value("version").toInt() == kProtocolVersion) {
        NativeHandle child(OpenProcess(PROCESS_DUP_HANDLE, FALSE, DWORD(process_->processId())), &CloseHandle);
        if (!child) {
            fail("Cannot transfer GPU surfaces");
            return;
        }
        QJsonArray handles;
        for (const auto& slot : slots_) {
            HANDLE duplicate = nullptr;
            if (!DuplicateHandle(GetCurrentProcess(), slot.handle.get(), child.get(), &duplicate, 0, FALSE,
                                 DUPLICATE_SAME_ACCESS)) {
                fail("GPU surface handle transfer failed");
                return;
            }
            handles.append(QString::number(quintptr(duplicate)));
        }
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc{};
        if (FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) {
            fail("Cannot identify render adapter");
            return;
        }
        send({{"type", "load"},
              {"root", root_},
              {"width", size_.width()},
              {"height", size_.height()},
              {"handles", handles},
              {"adapterLow", double(desc.AdapterLuid.LowPart)},
              {"adapterHigh", double(desc.AdapterLuid.HighPart)},
              {"options", options_},
              {"fault", owner_.injectedFault}});
    } else if (type == "progress" && state_ == Loading) {
        progress_.restart();
    } else if (type == "ready" && state_ == Loading) {
        information_ = message;
        loaded_ = true;
        progress_.restart();
    } else if (type == "frame" && loaded_) {
        const int index = message.value("slot").toInt(-1);
        bool valid = false;
        const auto sequence = message.value("sequence").toString().toULongLong(&valid);
        if (index < 0 || index >= int(slots_.size()) || !valid || sequence <= sequence_ || slots_[index].pending) {
            fail("Invalid model frame sequence");
            return;
        }
        sequence_ = sequence;
        slots_[index].pending = true;
        slots_[index].sequence = sequence;
        slots_[index].inputSequence = message.value("inputSequence").toString().toULongLong();
        lastCpuMs_ = message.value("cpuMs").toDouble();
        lastGpuMs_ = message.value("gpuMs").toDouble(-1);
        information_["animation"] = message.value("animation").toObject();
        progress_.restart();
        publish();
    } else if (type == "error") {
        fail(message.value("message").toString("Model loading failed"));
    } else {
        fail("Unexpected model protocol state");
    }
}

ID3D11ShaderResourceView* Session::Impl::texture(ID3D11Device* device, ID3D11DeviceContext* context, QSize size,
                                                 bool* changed) {
    if (changed) {
        *changed = false;
    }
    if (root_.isEmpty() || state_ == Failed || !device || !context) {
        return nullptr;
    }
    if (!validSurfaceSize(size.width(), size.height())) {
        fail("Live2D output exceeds the surface pixel budget");
        return nullptr;
    }
    if (device_.Get() != device || size_ != size) {
        stop();
        clearSurfaces();
        device_ = device;
        size_ = size;
        sequence_ = 0;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = size.width();
        desc.Height = size.height();
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &retained_)) ||
            FAILED(device->CreateShaderResourceView(retained_.Get(), nullptr, &view_))) {
            fail("Cannot allocate retained model frame");
            return nullptr;
        }
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        for (auto& slot : slots_) {
            Microsoft::WRL::ComPtr<IDXGIResource1> resource;
            HANDLE sharedHandle = nullptr;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &slot.texture)) ||
                FAILED(slot.texture.As(&slot.mutex)) || FAILED(slot.texture.As(&resource)) ||
                FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                    nullptr, &sharedHandle))) {
                fail("Cannot allocate shared model frames");
                return nullptr;
            }
            slot.handle.reset(sharedHandle);
        }
        start();
    }
    // Consume in sequence order; never replace a new frame with an older slot.
    std::array<int, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return slots_[a].sequence < slots_[b].sequence;
    });
    for (int index : order) {
        auto& slot = slots_[index];
        if (!slot.pending) {
            continue;
        }
        const auto acquired = slot.mutex->AcquireSync(1, 0);
        if (acquired == WAIT_TIMEOUT) {
            // Arrange another local attempt without blocking the compositor.
            QTimer::singleShot(2, this, [this] {
                publish();
            });
            break;
        }
        if (acquired != S_OK) {
            fail("Model GPU surface became unavailable");
            return nullptr;
        }
        context->CopyResource(retained_.Get(), slot.texture.Get());
        const auto released = slot.mutex->ReleaseSync(0);
        context->Flush();
        if (FAILED(released)) {
            fail("Model GPU synchronization failed");
            return nullptr;
        }
        slot.pending = false;
        ++frames_;
        if (requestedInput_ && slot.inputSequence == requestedInput_ && measuredInput_ != requestedInput_) {
            measuredInput_ = requestedInput_;
            lastInputLatencyMs_ = inputClock_.nsecsElapsed() / 1e6;
        }
        state_ = Ready;
        if (changed) {
            *changed = true;
        }
        send({{"type", "ack"}, {"slot", index}});
    }
    return state_ == Ready ? view_.Get() : nullptr;
}
}  // namespace cd::live2d

namespace cd::live2d {
auto Session::Impl::state() const -> State {
    return state_;
}

auto Session::Impl::error() const -> QString {
    return error_;
}

auto Session::Impl::information() const -> QJsonObject {
    return information_;
}

auto Session::Impl::frames() const -> quint64 {
    return frames_;
}

auto Session::Impl::outputSize() const -> QSize {
    return size_;
}

auto Session::Impl::lastCpuMs() const -> double {
    return lastCpuMs_;
}

auto Session::Impl::lastGpuMs() const -> double {
    return lastGpuMs_;
}

auto Session::Impl::lastInputLatencyMs() const -> double {
    return lastInputLatencyMs_;
}

auto Session::Impl::measuredInputSequence() const -> quint64 {
    return measuredInput_;
}

Session::Session(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>(*this)) {}

Session::~Session() = default;

void Session::configure(const QString& root, const QJsonObject& options) {
    impl_->configure(root, options);
}

void Session::pointer(double x, double y, bool tap) {
    impl_->pointer(x, y, tap);
}

bool Session::performMotion(const QString& name, unsigned priority) {
    return impl_->performMotion(name, priority);
}

ID3D11ShaderResourceView* Session::texture(ID3D11Device* device, ID3D11DeviceContext* context, QSize size,
                                           bool* changed) {
    return impl_->texture(device, context, size, changed);
}

void Session::stop() {
    impl_->stop();
}

auto Session::state() const -> State {
    return impl_->state();
}

auto Session::error() const -> QString {
    return impl_->error();
}

auto Session::information() const -> QJsonObject {
    return impl_->information();
}

auto Session::frames() const -> quint64 {
    return impl_->frames();
}

auto Session::outputSize() const -> QSize {
    return impl_->outputSize();
}

auto Session::processId() const -> qint64 {
    return impl_->processId();
}

auto Session::lastCpuMs() const -> double {
    return impl_->lastCpuMs();
}

auto Session::lastGpuMs() const -> double {
    return impl_->lastGpuMs();
}

auto Session::lastInputLatencyMs() const -> double {
    return impl_->lastInputLatencyMs();
}

auto Session::measuredInputSequence() const -> quint64 {
    return impl_->measuredInputSequence();
}
}  // namespace cd::live2d
