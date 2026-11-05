#include "CyberLyrics/WindowsMedia.h"

#include <Windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "CyberLyrics/MediaTimelineClock.h"

namespace cyberlyrics::windowsmedia {
namespace {
using namespace winrt::Windows::Media::Control;
using Session = GlobalSystemMediaTransportControlsSession;
using Manager = GlobalSystemMediaTransportControlsSessionManager;

template <class Operation>
auto complete(Operation operation, std::stop_token stop) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
            operation.Cancel();
            throw std::runtime_error("Media operation cancelled or timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return operation.GetResults();
}

QString string(const winrt::hstring& value) {
    return QString::fromWCharArray(value.c_str(), int(value.size()));
}

uint32_t capabilities(const Session& session) {
    const auto info = session.GetPlaybackInfo();
    const auto controls = info.Controls();
    uint32_t flags = 0;
    if (controls.IsPlayPauseToggleEnabled() ||
        (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing
             ? controls.IsPauseEnabled()
             : controls.IsPlayEnabled()))
        flags |= CanToggle;
    if (controls.IsPreviousEnabled())
        flags |= CanPrevious;
    if (controls.IsNextEnabled())
        flags |= CanNext;
    if (controls.IsPlaybackPositionEnabled())
        flags |= CanSeek;
    return flags;
}

int32_t control(const Session& session, uint32_t command, double value, std::stop_token stop) {
    const uint32_t flag = command == Toggle     ? CanToggle
                          : command == Previous ? CanPrevious
                          : command == Next     ? CanNext
                                                : CanSeek;
    if (!(capabilities(session) & flag))
        return MediaUnsupported;
    bool accepted = false;
    if (command == Toggle) {
        const auto info = session.GetPlaybackInfo();
        if (info.Controls().IsPlayPauseToggleEnabled())
            accepted = complete(session.TryTogglePlayPauseAsync(), stop);
        else if (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
            accepted = complete(session.TryPauseAsync(), stop);
        else
            accepted = complete(session.TryPlayAsync(), stop);
    } else if (command == Previous)
        accepted = complete(session.TrySkipPreviousAsync(), stop);
    else if (command == Next)
        accepted = complete(session.TrySkipNextAsync(), stop);
    else if (command == Seek) {
        const auto timeline = session.GetTimelineProperties();
        const double duration = std::max(0.0, (timeline.EndTime() - timeline.StartTime()).count() / 10000000.0);
        const auto position = timeline.StartTime().count() + int64_t(std::clamp(value, 0.0, duration) * 10000000);
        accepted = complete(session.TryChangePlaybackPositionAsync(position), stop);
    }
    return accepted ? MediaOk : MediaInternal;
}
}  // namespace

struct MediaService::Impl {
    struct Subscriber {
        QString filter, selected;
        std::function<void(const MediaSnapshot&)> callback;
        int32_t result = MediaOk;
        uint64_t generation = 0, requestId = 0;
        uint32_t controls = 0;
        SourceMatch match = SourceMatch::Contains;
    };

    struct Command {
        uint64_t token;
        QString source;
        uint32_t command;
        double value;
        uint64_t generation;
        uint64_t requestId;
    };

    struct Shared {
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<Command> commands;
        std::set<QString> dirty;
        size_t subscribers = 0;
        bool wake = true;
    };

    struct Node {
        Session session{nullptr};
        Session::MediaPropertiesChanged_revoker properties;
        Session::PlaybackInfoChanged_revoker playback;
        Session::TimelinePropertiesChanged_revoker timeline;
        MediaSnapshot snapshot;
        MediaTimelineClock clock;
        bool metadata = true, resetTimeline = false;
        uint64_t coverRetryAt = 0;
        unsigned coverRetries = 0;
    };

    QPointer<MediaService> owner;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::jthread worker;
    std::map<uint64_t, Subscriber> subscribers;
    std::map<uint64_t, std::function<void(const std::vector<MediaSessionInfo>&)>> sessionSubscribers;
    std::vector<MediaSessionInfo> sessionList;
    std::mutex pendingMutex;
    std::vector<MediaSnapshot> pendingSnapshots;
    QString pendingCurrent;
    bool pendingDelivery = false;
    std::vector<MediaSnapshot> snapshots;
    QString current;
    uint64_t nextToken = 1, nextRequest = 1;

    explicit Impl(MediaService* value)
        : owner(value), worker([this](std::stop_token stop) {
              run(stop);
          }) {}

    ~Impl() {
        worker.request_stop();
        shared->condition.notify_all();
        worker.join();
    }

    void deliver(uint64_t token) {
        auto it = subscribers.find(token);
        if (it == subscribers.end())
            return;
        const auto selected = selectMediaSessionIndex(snapshots, current, it->second.filter, it->second.match);
        auto snapshot = selected ? snapshots[*selected] : MediaSnapshot{};
        if (it->second.selected != snapshot.source || it->second.generation != snapshot.sessionGeneration) {
            it->second.selected = snapshot.source;
            it->second.generation = snapshot.sessionGeneration;
            it->second.result = MediaOk;
            it->second.requestId = 0;
        }
        it->second.controls = snapshot.controls;
        snapshot.commandResult = it->second.result;
        snapshot.commandRequestId = it->second.requestId;
        auto callback = it->second.callback;
        callback(snapshot);
    }

    void publish(std::vector<MediaSnapshot> values, QString focused) {
        {
            std::lock_guard lock(pendingMutex);
            pendingSnapshots = std::move(values);
            pendingCurrent = std::move(focused);
            if (pendingDelivery)
                return;
            pendingDelivery = true;
        }
        const QPointer<MediaService> guard = owner;
        QMetaObject::invokeMethod(
            owner.data(),
            [guard]() mutable {
                if (!guard)
                    return;
                auto& self = *guard->m_impl;
                {
                    std::lock_guard lock(self.pendingMutex);
                    self.snapshots = std::move(self.pendingSnapshots);
                    self.current = std::move(self.pendingCurrent);
                    self.pendingDelivery = false;
                }
                std::vector<uint64_t> tokens;
                for (const auto& [token, subscriber] : self.subscribers)
                    tokens.push_back(token);
                for (const auto token : tokens)
                    self.deliver(token);
                std::vector<MediaSessionInfo> sessions;
                for (const auto& snapshot : self.snapshots)
                    sessions.push_back({snapshot.source, snapshot.source, snapshot.controls});
                if (sessions != self.sessionList) {
                    self.sessionList = std::move(sessions);
                    const auto saved = self.sessionSubscribers;
                    for (const auto& [token, callback] : saved)
                        if (self.sessionSubscribers.contains(token))
                            callback(self.sessionList);
                }
            },
            Qt::QueuedConnection);
    }

    void commandDone(const Command& command, int32_t result) {
        const QPointer<MediaService> guard = owner;
        QMetaObject::invokeMethod(
            owner.data(),
            [guard, command, result] {
                if (!guard)
                    return;
                auto& self = *guard->m_impl;
                const auto it = self.subscribers.find(command.token);
                if (it != self.subscribers.end() && it->second.selected == command.source &&
                    it->second.generation == command.generation && it->second.requestId == command.requestId) {
                    it->second.result = result;
                    self.deliver(command.token);
                }
            },
            Qt::QueuedConnection);
    }

    void run(std::stop_token stop) noexcept {
        bool initialized = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            initialized = true;
            Manager manager{nullptr};
            Manager::SessionsChanged_revoker sessionsEvent;
            Manager::CurrentSessionChanged_revoker currentEvent;
            std::map<QString, std::unique_ptr<Node>> nodes;
            uint64_t coverGeneration = 0, sessionGeneration = 0;
            const auto wake = [state = shared] {
                std::lock_guard lock(state->mutex);
                state->wake = true;
                state->condition.notify_one();
            };
            while (!stop.stop_requested()) {
                std::deque<Command> commands;
                std::set<QString> dirty;
                {
                    std::unique_lock lock(shared->mutex);
                    if (!shared->subscribers) {
                        lock.unlock();
                        nodes.clear();
                        lock.lock();
                        shared->condition.wait(lock, [&] {
                            return stop.stop_requested() || shared->subscribers;
                        });
                    } else {
                        shared->condition.wait_for(lock, std::chrono::seconds(1), [&] {
                            return stop.stop_requested() || !shared->subscribers || shared->wake;
                        });
                    }
                    if (stop.stop_requested())
                        break;
                    if (!shared->subscribers) {
                        continue;
                    }
                    // Coalesce bursts of SMTC events without losing metadata changes.
                    shared->condition.wait_for(lock, std::chrono::milliseconds(100), [&] {
                        return stop.stop_requested();
                    });
                    if (stop.stop_requested())
                        break;
                    shared->wake = false;
                    commands.swap(shared->commands);
                    dirty.swap(shared->dirty);
                }
                try {
                    if (!manager) {
                        manager = complete(Manager::RequestAsync(), stop);
                        sessionsEvent = manager.SessionsChanged(winrt::auto_revoke, [wake](auto&&, auto&&) {
                            wake();
                        });
                        currentEvent = manager.CurrentSessionChanged(winrt::auto_revoke, [wake](auto&&, auto&&) {
                            wake();
                        });
                    }
                    const auto active = manager.GetCurrentSession();
                    const auto focused = active ? string(active.SourceAppUserModelId()) : QString{};
                    std::set<QString> present;
                    for (const auto& session : manager.GetSessions()) {
                        if (present.size() >= 32)
                            break;
                        const auto id = string(session.SourceAppUserModelId());
                        present.insert(id);
                        auto& node = nodes[id];
                        if (!node || node->session != session) {
                            node = std::make_unique<Node>();
                            node->session = session;
                            node->snapshot.source = id;
                            node->snapshot.sessionGeneration = ++sessionGeneration;
                            const auto changed = [state = shared, id](bool metadata) {
                                std::lock_guard lock(state->mutex);
                                if (metadata)
                                    state->dirty.insert(id);
                                state->wake = true;
                                state->condition.notify_one();
                            };
                            node->properties =
                                session.MediaPropertiesChanged(winrt::auto_revoke, [changed](auto&&, auto&&) {
                                    changed(true);
                                });
                            node->playback = session.PlaybackInfoChanged(winrt::auto_revoke, [changed](auto&&, auto&&) {
                                changed(false);
                            });
                            node->timeline =
                                session.TimelinePropertiesChanged(winrt::auto_revoke, [changed](auto&&, auto&&) {
                                    changed(false);
                                });
                        }
                        if (dirty.contains(id)) {
                            node->metadata = true;
                            node->coverRetries = 0;
                            node->coverRetryAt = 0;
                        }
                    }
                    std::erase_if(nodes, [&](const auto& entry) {
                        return !present.contains(entry.first);
                    });
                    for (const auto& command : commands) {
                        int32_t result = MediaUnsupported;
                        try {
                            const auto it = nodes.find(command.source);
                            if (it != nodes.end() && it->second->snapshot.sessionGeneration == command.generation)
                                result = control(it->second->session, command.command, command.value, stop);
                        } catch (...) {
                            result = MediaInternal;
                        }
                        commandDone(command, result);
                    }
                    std::vector<MediaSnapshot> values;
                    for (auto& [id, node] : nodes) {
                        try {
                            auto& snapshot = node->snapshot;
                            if (node->metadata || (node->coverRetryAt && GetTickCount64() >= node->coverRetryAt)) {
                                const auto properties = complete(node->session.TryGetMediaPropertiesAsync(), stop);
                                const auto title = string(properties.Title()).left(1024);
                                const auto artist = string(properties.Artist()).left(1024);
                                const auto album = string(properties.AlbumTitle()).left(1024);
                                const bool trackChanged =
                                    snapshot.title != title || snapshot.artist != artist || snapshot.album != album;
                                node->resetTimeline |= trackChanged;
                                if (trackChanged) {
                                    snapshot.cover = {};
                                    snapshot.coverGeneration = ++coverGeneration;
                                    node->coverRetries = 0;
                                }
                                snapshot.title = title;
                                snapshot.artist = artist;
                                snapshot.album = album;
                                node->metadata = false;
                                node->coverRetryAt = 0;
                                QImage cover;
                                if (const auto reference = properties.Thumbnail()) {
                                    try {
                                        const auto stream = complete(reference.OpenReadAsync(), stop);
                                        if (stream.Size() > 0 && stream.Size() <= 2 * 1024 * 1024) {
                                            auto reader = winrt::Windows::Storage::Streams::DataReader(stream);
                                            const auto size = uint32_t(stream.Size());
                                            if (complete(reader.LoadAsync(size), stop) == size) {
                                                QByteArray bytes(int(size), Qt::Uninitialized);
                                                reader.ReadBytes(winrt::array_view<uint8_t>(
                                                    reinterpret_cast<uint8_t*>(bytes.data()),
                                                    reinterpret_cast<uint8_t*>(bytes.data()) + size));
                                                QBuffer buffer(&bytes);
                                                buffer.open(QIODevice::ReadOnly);
                                                QImageReader decoder(&buffer);
                                                const auto size = decoder.size();
                                                if (size.isValid() && size.width() <= 8192 && size.height() <= 8192) {
                                                    decoder.setScaledSize(size.scaled(256, 256, Qt::KeepAspectRatio));
                                                    cover = decoder.read().convertToFormat(
                                                        QImage::Format_RGBA8888_Premultiplied);
                                                }
                                            }
                                        }
                                    } catch (...) {
                                        // Keep the last cover for this track while its thumbnail is unavailable.
                                    }
                                }
                                if (!cover.isNull()) {
                                    if (snapshot.cover != cover) {
                                        snapshot.cover = std::move(cover);
                                        snapshot.coverGeneration = ++coverGeneration;
                                    }
                                    node->coverRetries = 0;
                                } else if (node->coverRetries < 3) {
                                    node->coverRetryAt = GetTickCount64() + 1000 * ++node->coverRetries;
                                }
                            }
                            const auto playback = node->session.GetPlaybackInfo();
                            snapshot.status =
                                playback.PlaybackStatus() ==
                                        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing
                                    ? MediaPlaying
                                    : (playback.PlaybackStatus() ==
                                               GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused
                                           ? MediaPaused
                                           : 0);
                            snapshot.controls = capabilities(node->session);
                            snapshot.rate = playback.PlaybackRate() ? playback.PlaybackRate().Value() : 1;
                            if (!std::isfinite(snapshot.rate) || snapshot.rate < 0 || snapshot.rate > 16)
                                snapshot.rate = 1;
                            const auto timeline = node->session.GetTimelineProperties();
                            snapshot.duration =
                                std::max(0.0, (timeline.EndTime() - timeline.StartTime()).count() / 10000000.0);
                            snapshot.position =
                                std::max(0.0, (timeline.Position() - timeline.StartTime()).count() / 10000000.0);
                            if (snapshot.duration > 0) {
                                snapshot.position = std::min(snapshot.position, snapshot.duration);
                            }
                            snapshot.positionValid =
                                timeline.LastUpdatedTime().time_since_epoch().count() > 0 || snapshot.position > 0;
                            snapshot.sampledAt = GetTickCount64();
                            snapshot.position =
                                snapshot.positionValid
                                    ? node->clock.update(
                                          snapshot, timeline.LastUpdatedTime().time_since_epoch().count(),
                                          winrt::clock::now().time_since_epoch().count(), node->resetTimeline)
                                    : 0;
                            node->resetTimeline = false;
                            values.push_back(snapshot);
                        } catch (...) {
                            // Retry transient metadata failures, never block the UI thread.
                            if (!node->snapshot.title.isEmpty()) {
                                values.push_back(node->snapshot);
                            }
                        }
                    }
                    publish(std::move(values), focused);
                } catch (...) {
                    for (const auto& command : commands)
                        commandDone(command, MediaInternal);
                    publish({}, {});
                    manager = nullptr;
                }
            }
        } catch (...) {
            publish({}, {});
        }
        if (initialized)
            winrt::uninit_apartment();
    }
};

std::optional<size_t> selectMediaSessionIndex(std::span<const MediaSnapshot> snapshots, const QString& current,
                                              const QString& filter, SourceMatch match) {
    if (filter.isEmpty() || filter.compare("auto", Qt::CaseInsensitive) == 0) {
        for (size_t index = 0; index < snapshots.size(); ++index) {
            if (!snapshots[index].source.isEmpty() &&
                snapshots[index].source.compare(current, Qt::CaseInsensitive) == 0) {
                return index;
            }
        }
        for (size_t index = 0; index < snapshots.size(); ++index) {
            if (!snapshots[index].source.isEmpty()) {
                return index;
            }
        }
        return {};
    }
    for (size_t index = 0; index < snapshots.size(); ++index) {
        if (snapshots[index].source.compare(filter, Qt::CaseInsensitive) == 0) {
            return index;
        }
    }
    if (match == SourceMatch::Contains) {
        for (size_t index = 0; index < snapshots.size(); ++index) {
            if (snapshots[index].source.contains(filter, Qt::CaseInsensitive)) {
                return index;
            }
        }
    }
    return {};
}

MediaService::MediaService(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {}

MediaService::~MediaService() = default;

uint64_t MediaService::subscribe(QString filter, std::function<void(const MediaSnapshot&)> callback,
                                 SourceMatch match) {
    const auto token = m_impl->nextToken++;
    m_impl->subscribers.emplace(token, Impl::Subscriber{filter.toLower(), {}, std::move(callback)});
    m_impl->subscribers.at(token).match = match;
    {
        std::lock_guard lock(m_impl->shared->mutex);
        m_impl->shared->subscribers = m_impl->subscribers.size() + m_impl->sessionSubscribers.size();
        m_impl->shared->wake = true;
        m_impl->shared->condition.notify_one();
    }
    QMetaObject::invokeMethod(
        this,
        [this, token] {
            m_impl->deliver(token);
        },
        Qt::QueuedConnection);
    return token;
}

void MediaService::unsubscribe(uint64_t token) {
    m_impl->subscribers.erase(token);
    m_impl->sessionSubscribers.erase(token);
    std::lock_guard lock(m_impl->shared->mutex);
    m_impl->shared->subscribers = m_impl->subscribers.size() + m_impl->sessionSubscribers.size();
    std::erase_if(m_impl->shared->commands, [token](const auto& command) {
        return command.token == token;
    });
    m_impl->shared->condition.notify_one();
}

uint64_t MediaService::subscribeSessions(std::function<void(const std::vector<MediaSessionInfo>&)> callback) {
    const auto token = m_impl->nextToken++;
    m_impl->sessionSubscribers.emplace(token, std::move(callback));
    {
        std::lock_guard lock(m_impl->shared->mutex);
        m_impl->shared->subscribers = m_impl->subscribers.size() + m_impl->sessionSubscribers.size();
        m_impl->shared->wake = true;
        m_impl->shared->condition.notify_one();
    }
    QMetaObject::invokeMethod(
        this,
        [this, token] {
            const auto it = m_impl->sessionSubscribers.find(token);
            if (it != m_impl->sessionSubscribers.end()) {
                const auto callback = it->second;
                callback(m_impl->sessionList);
            }
        },
        Qt::QueuedConnection);
    return token;
}

int32_t MediaService::command(uint64_t token, uint32_t command, double value, uint64_t* requestId) {
    if (requestId)
        *requestId = 0;
    auto it = m_impl->subscribers.find(token);
    if (it == m_impl->subscribers.end() || command < Toggle || command > Seek || !std::isfinite(value))
        return MediaArgument;
    if (it->second.selected.isEmpty())
        return MediaUnsupported;
    const auto flag = command == Toggle     ? CanToggle
                      : command == Previous ? CanPrevious
                      : command == Next     ? CanNext
                                            : CanSeek;
    if (!(it->second.controls & flag))
        return MediaUnsupported;
    {
        std::lock_guard lock(m_impl->shared->mutex);
        if (m_impl->shared->commands.size() >= 16 || it->second.result == MediaBusy)
            return MediaBusy;
        const auto id = m_impl->nextRequest++;
        m_impl->shared->commands.push_back({token, it->second.selected, command, value, it->second.generation, id});
        it->second.requestId = id;
        if (requestId)
            *requestId = id;
        m_impl->shared->wake = true;
        m_impl->shared->condition.notify_one();
    }
    it->second.result = MediaBusy;
    m_impl->deliver(token);
    return MediaOk;
}
}  // namespace cyberlyrics::windowsmedia
