#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>
#include <iostream>

#include "CyberLyrics/Engine.h"
#include "CyberLyrics/WindowsMedia.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    const auto option = [&arguments](const QString& name, const QString& fallback = {}) {
        const auto index = arguments.indexOf(name);
        return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1] : fallback;
    };
    QJsonArray sessions, samples, lyrics;
    QElapsedTimer elapsed;
    elapsed.start();
    cyberlyrics::windowsmedia::MediaService media;
    cyberlyrics::Engine engine;
    QString lastTrack;
    uint64_t request = 0;
    const auto selected = option("--source");
    const auto sessionsToken = media.subscribeSessions([&](const auto& values) {
        sessions = {};
        for (const auto& value : values) {
            sessions.append(QJsonObject{{"source", value.source}, {"controls", int(value.controls)}});
        }
    });
    const auto mediaToken = media.subscribe(selected, [&](const auto& value) {
        if (samples.size() < 512) {
            samples.append(QJsonObject{{"elapsedMs", elapsed.elapsed()},
                                       {"source", value.source},
                                       {"title", value.title},
                                       {"artist", value.artist},
                                       {"album", value.album},
                                       {"status", int(value.status)},
                                       {"controls", int(value.controls)},
                                       {"position", value.position},
                                       {"positionValid", value.positionValid},
                                       {"duration", value.duration},
                                       {"rate", value.rate},
                                       {"sampledAt", qint64(value.sampledAt)}});
        }
        const cyberlyrics::Track track{value.source, value.title, value.artist, value.album, {}, value.duration};
        if (arguments.contains("--lyrics") && !value.title.isEmpty() && !value.artist.isEmpty() &&
            track.key() != lastTrack) {
            lastTrack = track.key();
            if (request) {
                engine.cancel(request);
            }
            const auto started = elapsed.elapsed();
            request = engine.request(track, [&, started](auto document) {
                request = 0;
                lyrics.append(QJsonObject{{"state", int(document->state)},
                                          {"provider", document->provider},
                                          {"title", document->track.title},
                                          {"lines", document->lines.size()},
                                          {"readyMilliseconds", elapsed.elapsed() - started},
                                          {"error", document->error}});
            });
        }
    });
    QTimer::singleShot(std::clamp(option("--seconds", "8").toInt(), 1, 120) * 1000, &app, &QCoreApplication::quit);
    app.exec();
    media.unsubscribe(mediaToken);
    media.unsubscribe(sessionsToken);
    engine.cancelAll();
    bool found = false, advances = false;
    double previous = -1;
    bool previousValid = false;
    QString previousSource;
    for (const auto& value : samples) {
        const auto sample = value.toObject();
        found |= !sample.value("source").toString().isEmpty();
        const auto position = sample.value("position").toDouble();
        const bool valid = sample.value("positionValid").toBool();
        const auto source = sample.value("source").toString();
        advances |= previousValid && valid && source == previousSource && previous >= 0 && position > previous + .1 &&
                    sample.value("status").toInt() == 1;
        previous = position;
        previousValid = valid;
        previousSource = source;
    }
    const QJsonObject report{
        {"sessions", sessions},
        {"samples", samples},
        {"lyrics", lyrics},
        {"sessionFound", found},
        {"playingPositionAdvanced", advances},
        {"scope", "independent in-process SMTC and optional lyric lookup; no Desktop or Player dependency"}};
    const auto bytes = QJsonDocument(report).toJson();
    const auto path = option("--output");
    if (!path.isEmpty()) {
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
            std::cerr << "Cannot write media probe report\n";
            return 2;
        }
    }
    std::cout << bytes.constData();
    return arguments.contains("--require-session") && !found ? 1 : 0;
}
