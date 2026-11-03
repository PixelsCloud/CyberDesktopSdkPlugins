#include <Windows.h>
#include <bcrypt.h>
#include <wil/resource.h>

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "LiquidGlass/NativeService.h"
#include "libraries/liquidglass/src/NativeServiceProtocol_win.h"
#include "libraries/liquidglass/src/NativeServiceWatcher_win.h"
#include "libraries/liquidglass/src/NativeSurfaceProtocol_win.h"

namespace liquidglass {
namespace {
namespace asio = boost::asio;
using Error = boost::system::error_code;

struct Values {
    std::mutex mutex;
    NativeServiceStatus status{{}, ERROR_SERVICE_NOT_ACTIVE};
    std::vector<std::pair<uintptr_t, uint32_t>> windows;
};

void notify(const Values& values) {
    for (const auto& [window, cookie] : values.windows) {
        const auto handle = reinterpret_cast<HWND>(window);
        if (reinterpret_cast<uintptr_t>(GetPropW(handle, native::kSurfaceProperty)) == cookie) {
            const auto message = values.status.backend.process ? native::SurfaceNotification::BackendReady
                                                               : native::SurfaceNotification::BackendUnavailable;
            PostMessageW(handle, native::surfaceNotificationMessage(), uintptr_t(message), cookie);
        }
    }
}

bool trustedServer(HANDLE pipe) {
    ULONG server = 0;
    if (!GetNamedPipeServerProcessId(pipe, &server)) {
        return false;
    }
    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    wil::unique_schandle service(manager ? OpenServiceW(manager.get(), native::kServiceName, SERVICE_QUERY_STATUS)
                                         : nullptr);
    SERVICE_STATUS_PROCESS status{};
    DWORD received = 0;
    return service &&
           QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status),
                                &received) &&
           server == status.dwProcessId && status.dwCurrentState == SERVICE_RUNNING;
}

struct Transport final : std::enable_shared_from_this<Transport> {
    asio::io_context io;
    asio::windows::stream_handle pipe{io};
    asio::steady_timer retry{io};
    std::unique_ptr<native::ServiceWatcher> watcher;
    std::mutex mutex;
    std::vector<std::weak_ptr<Values>> clients;
    NativeServiceStatus last{{}, ERROR_SERVICE_NOT_ACTIVE};
    native::HostRequest request;
    native::HostResponse response;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds delay{100};
    bool stopping = false, started = false;

    void publish(NativeServiceStatus status) {
        std::lock_guard lock(mutex);
        last = status;
        std::erase_if(clients, [](const auto& weak) {
            return weak.expired();
        });
        for (const auto& weak : clients) {
            if (const auto client = weak.lock()) {
                std::lock_guard clientLock(client->mutex);
                if (client->status.backend != status.backend || client->status.error != status.error) {
                    client->status = status;
                    notify(*client);
                }
            }
        }
    }

    void begin() {
        if (started || stopping) {
            return;
        }
        started = true;
        watcher = std::make_unique<native::ServiceWatcher>([weak = weak_from_this()](bool database) {
            if (const auto self = weak.lock()) {
                asio::post(self->io, [self, database] {
                    if (!self->stopping && self->watcher && self->watcher->refresh(database) && !self->pipe.is_open()) {
                        self->retry.cancel();
                        self->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                        self->delay = std::chrono::milliseconds(100);
                        self->open();
                    }
                });
            }
        });
        watcher->refresh(true);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        request.operation = native::HostOperation::Acquire;
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&request.lease), sizeof(request.lease),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
            !request.lease) {
            publish({{}, ERROR_GEN_FAILURE});
            return;
        }
        open();
    }

    void failed(uint32_t error) {
        Error ignored;
        pipe.close(ignored);
        publish({{}, error});
        if (stopping || std::chrono::steady_clock::now() >= deadline) {
            return;
        }
        retry.expires_after(delay);
        delay = std::min(delay * 2, std::chrono::milliseconds(1000));
        retry.async_wait([self = shared_from_this()](Error code) {
            if (!code) {
                self->open();
            }
        });
    }

    void open() {
        if (stopping) {
            return;
        }
        wil::unique_handle handle(
            CreateFileW(native::kServicePipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
        if (!handle || handle.get() == INVALID_HANDLE_VALUE) {
            failed(GetLastError());
            return;
        }
        if (!trustedServer(handle.get())) {
            failed(ERROR_ACCESS_DENIED);
            return;
        }
        pipe.assign(handle.release());
        asio::async_write(pipe, asio::buffer(&request, sizeof(request)),
                          [self = shared_from_this()](Error error, size_t) {
                              if (error) {
                                  self->failed(error.value());
                              } else {
                                  self->read();
                              }
                          });
    }

    void read() {
        asio::async_read(pipe, asio::buffer(&response, sizeof(response)),
                         [self = shared_from_this()](Error error, size_t) {
                             if (error) {
                                 // Reconnect only following a real transport failure; a healthy
                                 // idle session has one pending overlapped read and no timer.
                                 self->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                                 self->failed(error.value());
                                 return;
                             }
                             const auto& response = self->response;
                             if (response.magic != native::kHostMagic || response.version != 1 ||
                                 response.watchHandle || bool(response.process) != bool(response.generation)) {
                                 self->failed(ERROR_INVALID_DATA);
                                 return;
                             }
                             self->delay = std::chrono::milliseconds(100);
                             self->publish({{response.process, response.generation}, response.error});
                             self->read();
                         });
    }

    void stop() {
        stopping = true;
        watcher.reset();
        retry.cancel();
        Error ignored;
        pipe.close(ignored);
    }
};
}  // namespace

struct NativeServiceConnection::Impl {
    std::shared_ptr<Values> values = std::make_shared<Values>();
};

NativeServiceConnection::NativeServiceConnection(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

NativeServiceConnection::~NativeServiceConnection() = default;

NativeServiceStatus NativeServiceConnection::status() const {
    std::lock_guard lock(m_impl->values->mutex);
    return m_impl->values->status;
}

void NativeServiceConnection::observeWindow(uintptr_t window, uint32_t cookie) {
    auto& values = *m_impl->values;
    std::lock_guard lock(values.mutex);
    const auto entry = std::pair(window, cookie);
    if (std::find(values.windows.begin(), values.windows.end(), entry) == values.windows.end()) {
        values.windows.push_back(entry);
        notify(values);
    }
}

void NativeServiceConnection::forgetWindow(uintptr_t window, uint32_t cookie) {
    std::lock_guard lock(m_impl->values->mutex);
    std::erase(m_impl->values->windows, std::pair(window, cookie));
}

struct NativeServiceRuntime::Impl {
    std::shared_ptr<Transport> transport = std::make_shared<Transport>();
    asio::executor_work_guard<asio::io_context::executor_type> work{transport->io.get_executor()};
    std::thread thread;
};

NativeServiceRuntime::NativeServiceRuntime() : m_impl(std::make_unique<Impl>()) {
    m_impl->thread = std::thread([transport = m_impl->transport] {
        transport->io.run();
    });
}

NativeServiceRuntime::~NativeServiceRuntime() {
    asio::post(m_impl->transport->io, [transport = m_impl->transport] {
        transport->stop();
    });
    m_impl->work.reset();
    m_impl->thread.join();
}

std::shared_ptr<NativeServiceConnection> NativeServiceRuntime::connect() {
    auto connection = std::shared_ptr<NativeServiceConnection>(
        new NativeServiceConnection(std::make_unique<NativeServiceConnection::Impl>()));
    auto transport = m_impl->transport;
    {
        std::lock_guard lock(transport->mutex);
        connection->m_impl->values->status = transport->last;
        std::erase_if(transport->clients, [](const auto& value) {
            return value.expired();
        });
        transport->clients.push_back(connection->m_impl->values);
    }
    asio::post(transport->io, [transport] {
        transport->begin();
    });
    return connection;
}

}  // namespace liquidglass
