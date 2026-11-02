#include "CyberLyrics/Engine.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "CyberLyrics/Parser.h"

namespace cyberlyrics {
namespace {
constexpr size_t kDocumentBudget = 16 * 1024 * 1024;

size_t documentBytes(const Document& document) {
    size_t bytes = sizeof(Document) + size_t(document.lines.capacity()) * sizeof(Line);
    const auto text = [&bytes](const QString& value) {
        bytes += size_t(value.capacity()) * sizeof(QChar);
    };
    for (const auto* value :
         {&document.track.source, &document.track.title, &document.track.artist, &document.track.album,
          &document.track.version, &document.provider, &document.attribution, &document.error}) {
        text(*value);
    }
    for (const auto& line : document.lines) {
        text(line.text);
        text(line.translation);
        bytes += size_t(line.words.capacity()) * sizeof(Word);
        for (const auto& word : line.words) {
            text(word.text);
        }
        if (bytes > kDocumentBudget) {
            break;
        }
    }
    return bytes;
}

Document bounded(Document result, const Track& track) {
    if (documentBytes(result) > kDocumentBudget) {
        result = {};
        result.track = track;
        result.state = LoadState::Failed;
        result.error = QStringLiteral("Lyrics document exceeds 16 MiB memory budget");
    }
    return result;
}

struct DeliveryBudget {
    std::atomic_size_t bytes{0};
    std::mutex mutex;
    std::condition_variable changed;

    void wake() {
        // Synchronize with the predicate-to-wait transition, including stop.
        { std::lock_guard lock(mutex); }
        changed.notify_all();
    }
};

struct DeliveryReservation {
    std::shared_ptr<DeliveryBudget> budget;
    size_t bytes = 0;

    ~DeliveryReservation() {
        release();
    }

    void release() {
        if (bytes) {
            {
                std::lock_guard lock(budget->mutex);
                budget->bytes.fetch_sub(bytes);
            }
            bytes = 0;
            budget->changed.notify_all();
        }
    }
};
}  // namespace

Provider::~Provider() = default;
Store::~Store() = default;

struct Engine::Impl {
    struct Job {
        Track track;
        QString key, localFile;
        bool retry = false;
        std::atomic_bool cancelled{false};
        std::map<uint64_t, Callback> callbacks;
    };

    struct Cached {
        std::shared_ptr<const Document> document;
        std::chrono::steady_clock::time_point expires;
        size_t bytes = 0;
    };

    QPointer<Engine> owner;
    std::vector<std::shared_ptr<Provider>> providers;
    std::shared_ptr<Store> store;
    std::map<QString, std::shared_ptr<Job>> active;
    std::map<QString, Cached> cache;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Job>> queue;
    uint64_t nextToken = 1, revision = 0;
    size_t cacheBytes = 0;
    std::map<size_t, std::chrono::steady_clock::time_point> sourceRetry;
    bool storeHealthy = true;
    std::atomic_bool networkEnabled{true};
    std::shared_ptr<DeliveryBudget> deliveryBudget = std::make_shared<DeliveryBudget>();
    std::jthread worker;

    Impl(Engine* engine, std::vector<std::shared_ptr<Provider>> sources, std::shared_ptr<Store> storage)
        : owner(engine), providers(std::move(sources)), store(std::move(storage)), worker([this](std::stop_token stop) {
              run(stop);
          }) {}

    ~Impl() {
        worker.request_stop();
        changed.notify_all();
        deliveryBudget->wake();
        worker.join();
    }

    Document fetch(const std::shared_ptr<Job>& job, const RequestContext& context) {
        if (!job->localFile.isEmpty()) {
            auto result = readFile(job->localFile, job->track);
            result.provider = QStringLiteral("local");
            return result;
        }
        if (job->retry) {
            storeHealthy = true;
        }
        if (store && storeHealthy && !job->retry) {
            try {
                if (auto saved = store->load(job->track.key()); saved && matches(job->track, saved->track)) {
                    return bounded(std::move(*saved), job->track);
                }
            } catch (const std::exception& error) {
                storeHealthy = false;
                qWarning() << "Lyrics cache read unavailable:" << error.what();
            }
        }
        Document result;
        result.track = job->track;
        result.state = LoadState::NotFound;
        if (job->track.title.isEmpty() || job->track.artist.isEmpty()) {
            return result;
        }
        for (size_t index = 0; index < providers.size(); ++index) {
            if (context.cancelled() || !networkEnabled.load() || std::chrono::steady_clock::now() >= context.deadline) {
                break;
            }
            Document candidate;
            if (!job->retry && sourceRetry[index] > std::chrono::steady_clock::now()) {
                result.state = LoadState::Failed;
                result.error = QStringLiteral("Lyrics sources are temporarily backing off");
                continue;
            }
            try {
                candidate = providers[index]->fetch(job->track, context);
            } catch (const std::exception& error) {
                candidate.state = LoadState::Failed;
                candidate.error = QString::fromUtf8(error.what());
            }
            candidate = bounded(std::move(candidate), job->track);
            if (candidate.state == LoadState::Ready || candidate.state == LoadState::Instrumental) {
                if (matches(job->track, candidate.track)) {
                    sourceRetry.erase(index);
                    result = std::move(candidate);
                    break;
                }
            } else if (candidate.state == LoadState::Failed) {
                sourceRetry[index] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                result.error = candidate.error;
                result.state = LoadState::Failed;
            }
        }
        if (store && storeHealthy && !context.cancelled() &&
            (result.state == LoadState::Ready || result.state == LoadState::Instrumental)) {
            try {
                store->save(job->track.key(), result);
            } catch (const std::exception& error) {
                storeHealthy = false;
                qWarning() << "Lyrics cache write unavailable:" << error.what();
            }
        }
        return result;
    }

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] {
                    return stop.stop_requested() || !queue.empty();
                });
                if (stop.stop_requested()) {
                    return;
                }
                job = queue.front();
                queue.pop_front();
            }
            if (job->cancelled.load()) {
                continue;
            }
            const RequestContext context{[job, stop] {
                                             return stop.stop_requested() || job->cancelled.load();
                                         },
                                         std::chrono::steady_clock::now() + std::chrono::seconds(15)};
            Document result;
            try {
                result = fetch(job, context);
            } catch (const std::exception& error) {
                result.track = job->track;
                result.state = LoadState::Failed;
                result.error = QString::fromUtf8(error.what());
            }
            if (context.cancelled()) {
                continue;
            }
            result = bounded(std::move(result), job->track);
            const size_t bytes = documentBytes(result);
            const auto reservation = std::make_shared<DeliveryReservation>();
            reservation->budget = deliveryBudget;
            {
                std::unique_lock lock(deliveryBudget->mutex);
                deliveryBudget->changed.wait(lock, [&] {
                    return context.cancelled() || deliveryBudget->bytes.load() + bytes <= kDocumentBudget;
                });
                if (context.cancelled()) {
                    continue;
                }
                deliveryBudget->bytes.fetch_add(bytes);
                reservation->bytes = bytes;
            }
            const auto guard = owner;
            auto snapshot = std::make_shared<Document>(std::move(result));
            QMetaObject::invokeMethod(
                owner.data(),
                [guard, job, snapshot, bytes, reservation] {
                    reservation->release();
                    if (!guard || job->cancelled.load()) {
                        return;
                    }
                    auto& self = *guard->m_impl;
                    const auto found = self.active.find(job->key);
                    if (found == self.active.end() || found->second != job) {
                        return;
                    }
                    snapshot->revision = ++self.revision;
                    if (const auto previous = self.cache.find(job->key); previous != self.cache.end()) {
                        self.cacheBytes -= previous->second.bytes;
                        self.cache.erase(previous);
                    }
                    while (!self.cache.empty() &&
                           (self.cache.size() >= 64 || self.cacheBytes + bytes > kDocumentBudget)) {
                        self.cacheBytes -= self.cache.begin()->second.bytes;
                        self.cache.erase(self.cache.begin());
                    }
                    const auto lifetime = snapshot->state == LoadState::Failed     ? 30000
                                          : snapshot->state == LoadState::NotFound ? 300000
                                                                                   : 86400000;
                    if (bytes <= kDocumentBudget) {
                        self.cache[job->key] = {
                            snapshot, std::chrono::steady_clock::now() + std::chrono::milliseconds(lifetime), bytes};
                        self.cacheBytes += bytes;
                    }
                    // Reentrant retry must start a new job while cancellation can
                    // still find and suppress the remaining delivery callbacks.
                    auto node = self.active.extract(found);
                    job->key += QStringLiteral(":delivery:") + QString::number(snapshot->revision);
                    node.key() = job->key;
                    self.active.insert(std::move(node));
                    // Cancellation during one callback must suppress other callbacks.
                    // Keep this delivery active until all callbacks have been visited.
                    const auto callbacks = job->callbacks;
                    for (const auto& [token, callback] : callbacks) {
                        if (!guard || job->cancelled.load()) {
                            return;
                        }
                        if (job->callbacks.erase(token) && callback) {
                            callback(snapshot);
                        }
                    }
                    if (guard) {
                        const auto current = guard->m_impl->active.find(job->key);
                        if (current != guard->m_impl->active.end() && current->second == job) {
                            guard->m_impl->active.erase(current);
                        }
                    }
                },
                Qt::QueuedConnection);
        }
    }
};

Engine::Engine(std::vector<std::shared_ptr<Provider>> providers, std::shared_ptr<Store> store, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this, std::move(providers), std::move(store))) {}

Engine::~Engine() {
    cancelAll();
}

void Engine::setNetworkEnabled(bool enabled) {
    if (m_impl->networkEnabled.exchange(enabled) != enabled && !enabled) {
        cancelAll();
    }
}

uint64_t Engine::request(const Track& track, Callback callback, const QString& localFile, bool retry) {
    auto& self = *m_impl;
    for (const auto* value : {&track.source, &track.title, &track.artist, &track.album, &track.version}) {
        if (value->size() > 4096) {
            return 0;
        }
    }
    if (localFile.size() > 32768) {
        return 0;
    }
    const auto token = self.nextToken++;
    const QString key = track.key() + ':' + localFile;
    if (!retry && localFile.isEmpty()) {
        const auto cached = self.cache.find(key);
        if (cached != self.cache.end() && cached->second.expires > std::chrono::steady_clock::now()) {
            if (self.active.size() >= 32) {
                return 0;
            }
            auto job = std::make_shared<Impl::Job>();
            job->key = key + ':' + QString::number(token);
            job->callbacks[token] = std::move(callback);
            self.active[job->key] = job;
            const auto snapshot = cached->second.document;
            QTimer::singleShot(0, this, [this, job, token, snapshot] {
                if (!job->cancelled.load()) {
                    m_impl->active.erase(job->key);
                    if (auto callback = job->callbacks[token]) {
                        callback(snapshot);
                    }
                }
            });
            return token;
        }
    }
    auto found = self.active.find(key);
    if (found != self.active.end()) {
        if (found->second->callbacks.size() >= 128) {
            return 0;
        }
        found->second->callbacks[token] = std::move(callback);
        return token;
    }
    if (self.active.size() >= 32) {
        return 0;
    }
    auto job = std::make_shared<Impl::Job>();
    job->track = track;
    job->key = key;
    job->localFile = localFile;
    job->retry = retry;
    job->callbacks[token] = std::move(callback);
    self.active[key] = job;
    {
        std::lock_guard lock(self.mutex);
        std::erase_if(self.queue, [](const auto& queued) {
            return queued->cancelled.load();
        });
        self.queue.push_back(job);
    }
    self.changed.notify_one();
    return token;
}

void Engine::cancel(uint64_t token) {
    auto& active = m_impl->active;
    for (auto it = active.begin(); it != active.end(); ++it) {
        if (it->second->callbacks.erase(token)) {
            if (it->second->callbacks.empty()) {
                it->second->cancelled.store(true);
                active.erase(it);
                m_impl->deliveryBudget->wake();
            }
            return;
        }
    }
}

void Engine::cancelAll() {
    for (const auto& [key, job] : m_impl->active) {
        job->cancelled.store(true);
    }
    m_impl->active.clear();
    m_impl->deliveryBudget->wake();
}

size_t Engine::pendingCount() const {
    return m_impl->active.size();
}

EngineUsage Engine::usage() const {
    return {m_impl->cache.size(), m_impl->cacheBytes, m_impl->deliveryBudget->bytes.load()};
}
}  // namespace cyberlyrics
