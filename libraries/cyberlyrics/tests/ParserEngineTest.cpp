#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QThread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "CyberLyrics/Engine.h"
#include "CyberLyrics/Parser.h"
#include "CyberLyrics/Runtime.h"

namespace {
int64_t monotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class Source final : public cyberlyrics::Provider {
public:
    std::atomic_int calls{0}, cancelled{0};
    std::atomic_int64_t returnedAt{0};

    cyberlyrics::Document fetch(const cyberlyrics::Track& track, const cyberlyrics::RequestContext& context) override {
        ++calls;
        for (int index = 0; index < 30; ++index) {
            if (context.cancelled()) {
                ++cancelled;
                return {};
            }
            QThread::msleep(2);
        }
        auto result = cyberlyrics::parseLrc("[00:01.00]line\n[00:02.00]next", track);
        result.provider = "fixture";
        returnedAt = monotonicNanoseconds();
        return result;
    }
};

class FunctionSource final : public cyberlyrics::Provider {
public:
    std::atomic_int calls{0};
    std::function<cyberlyrics::Document(const cyberlyrics::Track&)> implementation;

    cyberlyrics::Document fetch(const cyberlyrics::Track& track, const cyberlyrics::RequestContext&) override {
        ++calls;
        return implementation(track);
    }
};

class FaultStore final : public cyberlyrics::Store {
public:
    std::atomic_int reads{0}, writes{0};

    std::optional<cyberlyrics::Document> load(const QString&) override {
        ++reads;
        throw std::runtime_error("injected store read failure");
    }

    void save(const QString&, const cyberlyrics::Document&) override {
        ++writes;
        throw std::runtime_error("injected store write failure");
    }
};

cyberlyrics::Document ready(const cyberlyrics::Track& track) {
    return cyberlyrics::parseLrc("[00:01.00]line\n[00:02.00]next", track);
}

bool until(const std::function<bool()>& ready, int milliseconds = 1500) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return ready();
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        failures += !ok;
    };
    const cyberlyrics::Track track{"fixture", "Track", "Artist", {}, {}, 180};
    auto document = cyberlyrics::parseLrc("[00:02.00]世界\n[00:01.00]hello\n[offset:500]", track);
    check(document.lines.size() == 2 && document.lines[0].text == "hello" &&
              std::abs(document.lines[0].start - .5) < .001,
          "FFmpeg parser preserves global millisecond offset and Unicode");
    document = cyberlyrics::parseLrc("[00:01.00][00:03.00]repeat", track);
    check(document.lines.size() == 2, "multiple LRC timestamps preserved");
    check(cyberlyrics::parseLrc(QByteArray(4 * 1024 * 1024 + 1, 'x')).state == cyberlyrics::LoadState::Failed,
          "oversize input rejected");
    check(cyberlyrics::parseLrc(QByteArray("\xff")).state == cyberlyrics::LoadState::Failed,
          "malformed UTF-8 rejected");
    document = cyberlyrics::parseWords("[1000,3000](1000,600,0)你(1800,1000,0)好🌍", track);
    check(document.lines.size() == 1 && document.lines[0].words.size() == 2 && document.lines[0].text == "你好🌍" &&
              document.quality == cyberlyrics::TimingQuality::Word,
          "YRC genuine word timestamps and emoji preserved");
    document = cyberlyrics::parseWords("[1000,3000,0](1000,600)你(1800,1000)好", track);
    check(document.lines.size() == 1 && document.lines[0].words.size() == 2 && document.lines[0].text == "你好" &&
              document.quality == cyberlyrics::TimingQuality::Word,
          "timing-first API variant accepts two-field words and extended line headers");
    document = cyberlyrics::parseWords("[1000,3000]你(1000,600,0)好🌍(1800,1000,0)", track);
    check(document.lines.size() == 1 && document.lines[0].words.size() == 2 && document.lines[0].text == "你好🌍" &&
              document.quality == cyberlyrics::TimingQuality::Word,
          "text-first API variant preserves genuine word times and emoji");
    document = cyberlyrics::parseWords("[1000,3000](1000,600)(hello)(1800,1000,0) world", track);
    check(document.lines.size() == 1 && document.lines[0].text == "(hello) world" &&
              document.lines[0].words.size() == 2 && document.quality == cyberlyrics::TimingQuality::Word,
          "literal parentheses remain part of timing-first lyric text");
    document = cyberlyrics::parseWords("[1000,3000](hello)(1000,600) world(1800,1000)", track);
    check(document.lines.size() == 1 && document.lines[0].text == "(hello) world" &&
              document.lines[0].words.size() == 2 && document.quality == cyberlyrics::TimingQuality::Word,
          "literal parentheses remain part of text-first lyric text");
    document = cyberlyrics::parseWords("[1000,3000]你(1800,1000)好(1200,600)", track);
    check(document.quality == cyberlyrics::TimingQuality::Line && document.lines.size() == 1 &&
              document.lines[0].words.isEmpty(),
          "out-of-order word timestamps cannot masquerade as genuine word timing");
    document = cyberlyrics::parseWords(
        "<QrcInfos><LyricInfo><Lyric_1 LyricContent=\"[1000,3000]你(1000,600)好(1800,1000)\"/></LyricInfo></QrcInfos>",
        track);
    check(document.lines.size() == 1 && document.quality == cyberlyrics::TimingQuality::Word,
          "QRC XML parsed with Qt XML reader");
    document = cyberlyrics::parseLrc("[00:01]original\n[00:02]second", track);
    auto translation = cyberlyrics::parseLrc("[00:01]译文", track);
    cyberlyrics::mergeTranslation(document, translation);
    check(document.lines.size() == 2 && document.lines[0].translation == "译文" &&
              document.lines[1].translation.isEmpty(),
          "translation does not leak into following line");
    translation.track.version = "live";
    document.lines[0].translation.clear();
    cyberlyrics::mergeTranslation(document, translation);
    check(document.lines[0].translation.isEmpty(), "translation from different recording rejected");
    check(QString::fromUtf8(cyberlyrics_runtime_license()).startsWith("LGPL"),
          "replaceable runtime uses audited LGPL FFmpeg");

    auto source = std::make_shared<Source>();
    cyberlyrics::Engine engine({source});
    int received = 0;
    const auto firstStartedAt = monotonicNanoseconds();
    double firstReadyMs = 0, deliveryMs = 0, warmReadyMs = 0;
    const auto first = engine.request(track, [&](auto) {
        ++received;
    });
    engine.request(track, [&](auto result) {
        const auto now = monotonicNanoseconds();
        firstReadyMs = (now - firstStartedAt) / 1e6;
        deliveryMs = (now - source->returnedAt.load()) / 1e6;
        received += result->state == cyberlyrics::LoadState::Ready;
    });
    engine.cancel(first);
    check(until([&] {
              return received == 1;
          }) &&
              source->calls.load() == 1,
          "same-process consumers share one request and cancellation preserves remaining consumer");
    const auto cached = engine.request(track, [&](auto) {
        ++received;
    });
    engine.cancel(cached);
    QCoreApplication::processEvents();
    check(received == 1, "cancellation suppresses queued cache delivery");
    const auto warmStartedAt = monotonicNanoseconds();
    engine.request(track, [&](auto) {
        warmReadyMs = (monotonicNanoseconds() - warmStartedAt) / 1e6;
        ++received;
    });
    check(until([&] {
              return received == 2;
          }) &&
              source->calls.load() == 1,
          "warm cache avoids duplicate source request");
    auto other = track;
    other.title = "Other";
    const auto cancelled = engine.request(other, [&](auto) {
        received += 100;
    });
    check(until([&] {
              return source->calls.load() == 2;
          }),
          "second recording begins independently");
    engine.cancel(cancelled);
    check(until([&] {
              return source->cancelled.load() == 1;
          }) &&
              received == 2 && engine.pendingCount() == 0,
          "last consumer cancels in-flight work and stale result cannot publish");
    // A callback can cancel another consumer of the same completed request.
    uint64_t second = 0;
    engine.request(other, [&](auto) {
        ++received;
        engine.cancel(second);
    });
    second = engine.request(other, [&](auto) {
        received += 100;
    });
    check(until([&] {
              return received == 3;
          }) &&
              engine.pendingCount() == 0,
          "cancellation during callback delivery suppresses later consumers");
    const auto callsBeforeOffline = source->calls.load();
    engine.setNetworkEnabled(false);
    bool offlineMiss = false, offlineHit = false;
    auto unavailable = track;
    unavailable.title = "Not cached offline";
    engine.request(unavailable, [&](auto result) {
        offlineMiss = result->state == cyberlyrics::LoadState::NotFound;
    });
    engine.request(track, [&](auto result) {
        offlineHit = result->state == cyberlyrics::LoadState::Ready;
    });
    check(until([&] {
              return offlineMiss && offlineHit;
          }) &&
              source->calls.load() == callsBeforeOffline,
          "network disabled preserves cached lyrics without contacting providers for a cache miss");
    {
        auto provider = std::make_shared<FunctionSource>();
        provider->implementation = ready;
        cyberlyrics::Engine reentrant({provider});
        int delivered = 0;
        reentrant.request(track, [&](auto) {
            ++delivered;
            reentrant.request(
                track,
                [&](auto) {
                    ++delivered;
                },
                {}, true);
        });
        reentrant.request(track, [&](auto) {
            ++delivered;
        });
        check(until([&] {
                  return delivered == 3;
              }) &&
                  provider->calls == 2 && reentrant.pendingCount() == 0,
              "retry requested inside a completion gets its own delivery without losing existing consumers");
        int accepted = 0;
        for (int index = 0; index < 33; ++index) {
            accepted += reentrant.request(track, [&](auto) {
                ++delivered;
            }) != 0;
        }
        check(accepted == 32 && reentrant.pendingCount() == 32,
              "queued cache hits obey the same 32-job bound as network requests");
        reentrant.cancelAll();
        QCoreApplication::processEvents();
        check(delivered == 3 && reentrant.pendingCount() == 0, "cancel-all suppresses every queued cache hit");
        auto invalid = track;
        invalid.title = QString(4097, 'x');
        check(reentrant.request(invalid, {}) == 0, "oversized metadata is rejected before key and job allocation");
    }
    {
        auto primary = std::make_shared<FunctionSource>(), fallback = std::make_shared<FunctionSource>();
        bool recover = false;
        primary->implementation = [&](const auto& value) {
            if (!recover) {
                throw std::runtime_error("injected source failure");
            }
            return ready(value);
        };
        fallback->implementation = ready;
        cyberlyrics::Engine health({primary, fallback});
        int delivered = 0;
        const auto request = [&](const char* title, bool retry) {
            auto value = track;
            value.title = title;
            const int expected = delivered + 1;
            health.request(
                value,
                [&](auto result) {
                    delivered += result->state == cyberlyrics::LoadState::Ready;
                },
                {}, retry);
            return until([&] {
                return delivered == expected;
            });
        };
        const bool backedOff =
            request("first", false) && request("second", false) && primary->calls == 1 && fallback->calls == 2;
        recover = true;
        check(backedOff && request("retry", true) && request("after retry", false) && primary->calls == 3 &&
                  fallback->calls == 2,
              "failed source backs off while fallback works; successful explicit retry clears its cooldown");
    }
    {
        auto provider = std::make_shared<FunctionSource>();
        provider->implementation = ready;
        auto store = std::make_shared<FaultStore>();
        cyberlyrics::Engine faults({provider}, store);
        int delivered = 0;
        bool success = true;
        for (int index = 0; index < 4; ++index) {
            auto value = track;
            value.title = QString::number(index);
            faults.request(
                value,
                [&](auto result) {
                    delivered += result->state == cyberlyrics::LoadState::Ready;
                },
                {}, index == 2);
            success &= until([&] {
                return delivered == index + 1;
            });
        }
        check(success && store->reads == 1 && store->writes == 1 && provider->calls == 4,
              "store read/write exceptions preserve lyrics, disable repeated broken I/O and allow explicit recovery");
    }
    {
        auto provider = std::make_shared<FunctionSource>();
        provider->implementation = ready;
        auto disposable =
            std::make_unique<cyberlyrics::Engine>(std::vector<std::shared_ptr<cyberlyrics::Provider>>{provider});
        int delivered = 0;
        disposable->request(track, [&](auto) {
            ++delivered;
            disposable.reset();
        });
        disposable->request(track, [&](auto) {
            delivered += 100;
        });
        check(until([&] {
                  return !disposable;
              }) &&
                  delivered == 1,
              "destroying the engine from its callback safely suppresses remaining deliveries");
    }
    {
        auto provider = std::make_shared<FunctionSource>();
        provider->implementation = [](const auto& value) {
            cyberlyrics::Document result;
            result.track = value;
            result.state = cyberlyrics::LoadState::Ready;
            result.lines.push_back({0, 180, QString(3 * 1024 * 1024, 'x'), {}, {}});
            return result;
        };
        cyberlyrics::Engine budget({provider});
        int delivered = 0;
        for (int index = 0; index < 6; ++index) {
            auto value = track;
            value.title = QString::number(index);
            budget.request(value, [&](auto result) {
                delivered += result->state == cyberlyrics::LoadState::Ready;
            });
        }
        // Deliberately stop processing GUI events: two 6 MiB results fit, the
        // worker must wait with its third result rather than queue all six.
        QElapsedTimer blocked;
        blocked.start();
        while (provider->calls < 3 && blocked.elapsed() < 1500) {
            QThread::msleep(1);
        }
        QThread::msleep(25);
        const auto queued = budget.usage();
        check(provider->calls == 3 && queued.queuedDeliveryBytes >= 12 * 1024 * 1024 &&
                  queued.queuedDeliveryBytes <= 16 * 1024 * 1024,
              "a stalled UI cannot queue more than 16 MiB of lyric documents");
        check(until([&] {
                  return delivered == 6;
              }) &&
                  budget.usage().cachedBytes <= 16 * 1024 * 1024 && budget.usage().queuedDeliveryBytes == 0 &&
                  budget.usage().cachedDocuments <= 2,
              "resuming delivery releases reservations and evicts cache entries within its independent 16 MiB budget");
        for (int index = 6; index < 12; ++index) {
            auto value = track;
            value.title = QString::number(index);
            budget.request(value, [&](auto) {
                delivered += 100;
            });
        }
        QThread::msleep(75);
        budget.cancelAll();
        QCoreApplication::processEvents();
        check(until([&] {
                  return budget.usage().queuedDeliveryBytes == 0;
              }) &&
                  delivered == 6,
              "cancellation wakes a worker waiting for the delivery budget and suppresses queued results");
    }
    {
        auto provider = std::make_shared<FunctionSource>();
        provider->implementation = [](const auto& value) {
            auto result = ready(value);
            result.lines[0].text = QString(9 * 1024 * 1024, 'x');
            return result;
        };
        cyberlyrics::Engine oversized({provider});
        bool rejected = false;
        oversized.request(track, [&](auto result) {
            rejected = result->state == cyberlyrics::LoadState::Failed && result->lines.isEmpty() &&
                       result->error.contains("16 MiB");
        });
        check(until([&] {
                  return rejected;
              }) &&
                  oversized.usage().cachedBytes < 4096,
              "oversized provider documents become bounded failures before caching or delivery");
    }
    if (app.arguments().contains("--network")) {
        cyberlyrics::Engine live;
        bool complete = false;
        live.request({"test", "Bohemian Rhapsody", "Queen", "A Night at the Opera", {}, 354}, [&](auto result) {
            std::cout << "Live provider " << result->provider.toStdString() << " lines " << result->lines.size()
                      << " error " << result->error.toStdString() << '\n';
            complete = true;
            check(result->state == cyberlyrics::LoadState::Ready, "live bounded HTTPS provider lookup");
        });
        check(until(
                  [&] {
                      return complete;
                  },
                  18000),
              "live provider deadline");
    }
    const auto reportIndex = app.arguments().indexOf("--report");
    if (reportIndex >= 0) {
        if (reportIndex + 1 >= app.arguments().size()) {
            return 2;
        }
        const auto usage = engine.usage();
        const QJsonObject metrics{
            {"passed", failures == 0},
            {"firstLineReadyMs", firstReadyMs},
            {"completeDocumentReadyMs", firstReadyMs},
            {"atomicDocumentDelivery", true},
            {"providerReturnToUiEventMs", deliveryMs},
            {"warmCacheDeliveryMs", warmReadyMs},
            {"controlledProviderCalls", source->calls.load()},
            {"controlledProviderCancellations", source->cancelled.load()},
            {"cachedDocumentBytesEstimate", double(usage.cachedBytes)},
            {"queuedDocumentBytesEstimate", double(usage.queuedDeliveryBytes)},
            {"pendingJobs", double(engine.pendingCount())},
            {"scope",
             "One controlled in-process provider and a warm-cache sample; includes Qt event delivery. "
             "The engine delivers a complete immutable document atomically, so first line and complete "
             "document become readable together. Not live HTTP latency or whole-process memory."}};
        QFile report(app.arguments()[reportIndex + 1]);
        if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(metrics).toJson()) < 0) {
            return 2;
        }
    }
    return failures ? 1 : 0;
}
