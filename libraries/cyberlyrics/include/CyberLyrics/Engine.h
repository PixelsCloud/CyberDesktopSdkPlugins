#pragma once

#include <QObject>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "CyberLyrics/Document.h"

namespace cyberlyrics {
struct RequestEvent {
    QString endpoint, error;
    int httpStatus = 0;
    int64_t elapsedMs = 0, responseBytes = 0;
};

struct RequestContext {
    std::function<bool()> cancelled;
    std::chrono::steady_clock::time_point deadline;
    // Optional bounded request diagnostics. Called on the fetching thread;
    // observers must not throw and must outlive this request context.
    std::function<void(const RequestEvent&)> report;
};

class Provider {
public:
    virtual ~Provider();
    virtual Document fetch(const Track& track, const RequestContext& context) = 0;
};

// Invoked exclusively on the engine worker. Each application supplies storage
// owned by its own process, or uses the bounded in-memory cache alone.
class Store {
public:
    virtual ~Store();
    virtual std::optional<Document> load(const QString& key) = 0;
    virtual void save(const QString& key, const Document& document) = 0;
};

enum class NetworkSource { LuoYueQQ, LuoYueNetEase, NetEase, LRCLIB };
std::shared_ptr<Provider> createNetworkProvider(NetworkSource source);
std::vector<std::shared_ptr<Provider>> networkProviders();

// On-demand estimates for engine-owned documents, not process or GPU memory.
struct EngineUsage {
    size_t cachedDocuments = 0, cachedBytes = 0, queuedDeliveryBytes = 0;
};

// Requests, cancellation and callbacks belong to the QObject's owning thread.
// Providers must honor cancellation/deadlines; callbacks must not throw.
class Engine final : public QObject {
public:
    using Callback = std::function<void(std::shared_ptr<const Document>)>;
    explicit Engine(std::vector<std::shared_ptr<Provider>> providers = networkProviders(),
                    std::shared_ptr<Store> store = {}, QObject* parent = nullptr);
    ~Engine() override;
    uint64_t request(const Track& track, Callback callback, const QString& localFile = {}, bool retry = false);
    void cancel(uint64_t token);
    void cancelAll();
    void setNetworkEnabled(bool enabled);
    size_t pendingCount() const;
    EngineUsage usage() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace cyberlyrics
