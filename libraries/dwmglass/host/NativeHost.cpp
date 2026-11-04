// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/host/NativeHost.h"

#include <Windows.h>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <wil/resource.h>
#include <winrt/base.h>

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/windows/object_handle.hpp>
#include <boost/asio/windows/overlapped_ptr.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "libraries/dwmglass/host/DwmAttachment.h"
#include "libraries/liquidglass/src/NativeServiceProtocol_win.h"

namespace cyberglass::dwm::host {
namespace {
namespace asio = boost::asio;
using Error = boost::system::error_code;
using liquidglass::native::HostOperation;
using liquidglass::native::HostRequest;
using liquidglass::native::HostResponse;
using Clock = std::chrono::steady_clock;

uint32_t exceptionCode() {
    try {
        throw;
    } catch (const winrt::hresult_error& error) {
        const auto code = uint32_t(error.code());
        return (code & 0xffff0000u) == 0x80070000u ? code & 0xffffu : code;
    } catch (const std::exception& error) {
        spdlog::error("Native host exception: {}", error.what());
        return ERROR_INVALID_DATA;
    }
}

class Peer;

struct Session final {
    explicit Session(asio::io_context& io) : process(io), retry(io) {}

    asio::windows::object_handle process;
    asio::steady_timer retry;
    std::shared_ptr<DwmAttachment> attachment;
    std::set<std::shared_ptr<Peer>> clients;
    HostResponse status;
    Clock::time_point deadline;
    std::chrono::milliseconds delay{100};
    bool working = false;
};

class Peer final : public std::enable_shared_from_this<Peer> {
public:
    Peer(asio::io_context& io, std::function<void(std::shared_ptr<Peer>, const HostRequest*)> dispatch)
        : pipe(io), timeout(io), m_dispatch(std::move(dispatch)) {}

    asio::windows::stream_handle pipe;
    asio::steady_timer timeout;
    uint32_t session = 0;
    uint64_t lease = 0;

    void read() {
        asio::async_read(pipe, asio::buffer(&m_request, sizeof(m_request)),
                         [self = shared_from_this()](Error error, size_t) {
                             if (error) {
                                 self->close();
                             } else {
                                 self->timeout.cancel();
                                 self->m_dispatch(self, &self->m_request);
                                 if (self->pipe.is_open()) {
                                     self->read();
                                 }
                             }
                         });
    }

    void send(HostResponse response) {
        if (!pipe.is_open()) {
            return;
        }
        // Slow/unresponsive clients must not retain unbounded transition queues.
        if (m_output.size() >= 16) {
            close();
            return;
        }
        m_output.push_back(response);
        if (m_output.size() == 1) {
            write();
        }
    }

    void close() {
        if (m_closed) {
            return;
        }
        m_closed = true;
        Error ignored;
        timeout.cancel();
        pipe.close(ignored);
        m_dispatch(shared_from_this(), nullptr);
    }

private:
    void write() {
        asio::async_write(pipe, asio::buffer(&m_output.front(), sizeof(HostResponse)),
                          [self = shared_from_this()](Error error, size_t) {
                              if (error) {
                                  self->close();
                              } else {
                                  self->m_output.pop_front();
                                  if (!self->m_output.empty()) {
                                      self->write();
                                  }
                              }
                          });
    }

    HostRequest m_request;
    std::deque<HostResponse> m_output;
    std::function<void(std::shared_ptr<Peer>, const HostRequest*)> m_dispatch;
    bool m_closed = false;
};
}  // namespace

struct NativeHost::Impl {
    asio::io_context io;
    asio::thread_pool worker{1};
    std::filesystem::path library, symbols;
    std::map<uint32_t, std::shared_ptr<Session>> sessions;
    std::set<std::shared_ptr<Peer>> peers;
    std::shared_ptr<Peer> listener;
    bool stopping = false;

    void accept() {
        if (stopping) {
            return;
        }
        auto peer = std::make_shared<Peer>(io, [this](auto client, const HostRequest* request) {
            dispatch(std::move(client), request);
        });
        PSECURITY_DESCRIPTOR raw = nullptr;
        winrt::check_bool(ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)", SDDL_REVISION_1, &raw, nullptr));
        const wil::unique_hlocal_security_descriptor security(raw);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), security.get(), FALSE};
        wil::unique_handle handle(
            CreateNamedPipeW(liquidglass::native::kServicePipe, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 64, 4096,
                             4096, 0, &attributes));
        winrt::check_bool(handle.get() != INVALID_HANDLE_VALUE);
        peer->pipe.assign(handle.release());
        listener = peer;
        asio::windows::overlapped_ptr operation(io, [this, peer](Error error, size_t) {
            if (stopping) {
                return;
            }
            listener.reset();
            if (!error) {
                ULONG process = 0;
                DWORD session = 0;
                if (GetNamedPipeClientProcessId(peer->pipe.native_handle(), &process) &&
                    ProcessIdToSessionId(process, &session) && session != 0 && peers.size() < 128) {
                    peer->session = session;
                    peers.insert(peer);
                    peer->timeout.expires_after(std::chrono::seconds(5));
                    peer->timeout.async_wait([peer](Error code) {
                        if (!code) {
                            peer->close();
                        }
                    });
                    peer->read();
                }
            }
            accept();
        });
        const auto connected = ConnectNamedPipe(peer->pipe.native_handle(), operation.get());
        const auto error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_PIPE_CONNECTED || error == ERROR_SUCCESS) {
            operation.complete({}, 0);
        } else if (error == ERROR_IO_PENDING) {
            operation.release();
        } else {
            operation.complete(Error(error, asio::error::get_system_category()), 0);
        }
    }

    void broadcast(const std::shared_ptr<Session>& session) {
        const std::vector clients(session->clients.begin(), session->clients.end());
        for (const auto& peer : clients) {
            peer->send(session->status);
        }
    }

    void attach(uint32_t id, const std::shared_ptr<Session>& session) {
        if (stopping || session->working || session->clients.empty()) {
            return;
        }
        session->working = true;
        asio::post(worker, [this, id, session, attachment = session->attachment]() mutable {
            uint32_t error = 0;
            try {
                if (!attachment || !attachment->alive()) {
                    attachment = std::make_shared<DwmAttachment>(id);
                }
                attachment->attach(library, symbols);
            } catch (...) {
                error = exceptionCode();
                spdlog::warn("Native session {} attach failed: 0x{:08x}", id, error);
            }
            asio::post(io, [this, id, session, attachment = std::move(attachment), error] {
                session->working = false;
                session->status = {};
                session->status.error = error;
                session->attachment = attachment;
                if (attachment && !error) {
                    session->status.process = attachment->process();
                    session->status.generation = attachment->generation();
                    if (!stopping && !session->process.is_open()) {
                        wil::unique_handle watch;
                        winrt::check_bool(DuplicateHandle(GetCurrentProcess(), attachment->processHandle(),
                                                          GetCurrentProcess(), watch.put(), SYNCHRONIZE, FALSE, 0));
                        session->process.assign(watch.release());
                        session->process.async_wait([this, id, session](Error code) {
                            if (code || stopping) {
                                return;
                            }
                            session->process.close();
                            session->attachment.reset();
                            session->status = {};
                            session->status.error = ERROR_NOT_READY;
                            broadcast(session);
                            session->deadline = Clock::now() + std::chrono::seconds(30);
                            session->delay = std::chrono::milliseconds(100);
                            attach(id, session);
                        });
                    }
                }
                broadcast(session);
                if (stopping) {
                    finishStop();
                } else if (error && !session->clients.empty() && Clock::now() < session->deadline) {
                    session->retry.expires_after(session->delay);
                    session->delay = std::min(session->delay * 2, std::chrono::milliseconds(1000));
                    session->retry.async_wait([this, id, session](Error code) {
                        if (!code) {
                            attach(id, session);
                        }
                    });
                }
            });
        });
    }

    void dispatch(std::shared_ptr<Peer> peer, const HostRequest* request) {
        if (!request) {
            if (const auto found = sessions.find(peer->session); found != sessions.end()) {
                found->second->clients.erase(peer);
            }
            peers.erase(peer);
            return;
        }
        if (request->magic != liquidglass::native::kHostMagic || request->version != 1 || request->reserved ||
            !request->lease || (peer->lease && peer->lease != request->lease)) {
            peer->close();
            return;
        }
        if (request->operation == HostOperation::Release) {
            peer->close();
            return;
        }
        if (request->operation != HostOperation::Acquire && request->operation != HostOperation::Status) {
            peer->close();
            return;
        }
        auto& slot = sessions[peer->session];
        if (!slot) {
            slot = std::make_shared<Session>(io);
            slot->status.error = ERROR_NOT_READY;
        }
        peer->lease = request->lease;
        slot->clients.insert(peer);
        peer->send(slot->status);
        if (!slot->status.process && !slot->working) {
            slot->deadline = Clock::now() + std::chrono::seconds(30);
            attach(peer->session, slot);
        }
    }

    void finishStop() {
        if (std::any_of(sessions.begin(), sessions.end(), [](const auto& entry) {
                return entry.second->working;
            })) {
            return;
        }
        // Registered client windows retire their own surfaces. Do not make a
        // remote stop/unload call while DWM may still execute hooked callbacks.
        // A later service instance can reconnect to the resident engine.
        spdlog::info("Native host stopping; DWM modules retain compositor lifetime");
        io.stop();
    }

    void stop() {
        if (stopping) {
            return;
        }
        stopping = true;
        if (listener) {
            listener->close();
            listener.reset();
        }
        const std::vector connected(peers.begin(), peers.end());
        for (const auto& peer : connected) {
            peer->close();
        }
        for (const auto& [id, session] : sessions) {
            Error ignored;
            session->retry.cancel();
            session->process.close(ignored);
        }
        finishStop();
    }
};

NativeHost::NativeHost(std::filesystem::path library, std::filesystem::path symbols)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->library = std::move(library);
    m_impl->symbols = std::move(symbols);
}

NativeHost::~NativeHost() {
    m_impl->worker.join();
}

void NativeHost::run() {
    m_impl->accept();
    auto guard = asio::make_work_guard(m_impl->io);
    m_impl->io.run();
    m_impl->worker.join();
}

void NativeHost::requestStop() {
    asio::post(m_impl->io, [impl = m_impl.get()] {
        impl->stop();
    });
}

}  // namespace cyberglass::dwm::host
