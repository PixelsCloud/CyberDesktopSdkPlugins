#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "CyberLyrics/Document.h"
#include "CyberLyrics/Engine.h"

namespace {
using Clock = std::chrono::steady_clock;

struct Source {
    const char* name;
    cyberlyrics::NetworkSource id;
};

constexpr std::array sources{Source{"luoyue-qq", cyberlyrics::NetworkSource::LuoYueQQ},
                             Source{"luoyue-netease", cyberlyrics::NetworkSource::LuoYueNetEase},
                             Source{"netease", cyberlyrics::NetworkSource::NetEase},
                             Source{"lrclib", cyberlyrics::NetworkSource::LRCLIB}};

QString stateName(cyberlyrics::LoadState state) {
    switch (state) {
        case cyberlyrics::LoadState::Ready:
            return QStringLiteral("ready");
        case cyberlyrics::LoadState::Instrumental:
            return QStringLiteral("instrumental");
        case cyberlyrics::LoadState::NotFound:
            return QStringLiteral("not-found");
        case cyberlyrics::LoadState::Failed:
            return QStringLiteral("failed");
        case cyberlyrics::LoadState::Cancelled:
            return QStringLiteral("cancelled");
        default:
            return QStringLiteral("empty");
    }
}

bool validTimeline(const cyberlyrics::Document& document) {
    double previous = -1;
    for (const auto& line : document.lines) {
        if (!std::isfinite(line.start) || !std::isfinite(line.end) || line.start < 0 || line.start < previous ||
            line.end <= line.start || line.text.isEmpty()) {
            return false;
        }
        previous = line.start;
        double wordEnd = line.start;
        QString text;
        for (const auto& word : line.words) {
            if (!std::isfinite(word.start) || !std::isfinite(word.end) || word.start < wordEnd ||
                word.end < word.start || word.end > line.end + .05) {
                return false;
            }
            wordEnd = word.end;
            text += word.text;
        }
        if (!line.words.isEmpty() && text != line.text) {
            return false;
        }
    }
    return !document.lines.isEmpty();
}

QJsonObject probe(const Source& source, const cyberlyrics::Track& track, int timeout, bool requireWords,
                  bool requireTranslation) {
    QJsonArray requests;
    const auto began = Clock::now();
    cyberlyrics::RequestContext context{[] {
                                            return false;
                                        },
                                        began + std::chrono::milliseconds(timeout),
                                        [&requests](const cyberlyrics::RequestEvent& event) {
                                            requests.append(QJsonObject{{"endpoint", event.endpoint},
                                                                        {"httpStatus", event.httpStatus},
                                                                        {"elapsedMs", event.elapsedMs},
                                                                        {"responseBytes", event.responseBytes},
                                                                        {"error", event.error}});
                                        }};
    cyberlyrics::Document document;
    document.track = track;
    try {
        document = cyberlyrics::createNetworkProvider(source.id)->fetch(track, context);
    } catch (const std::exception& error) {
        document.state = cyberlyrics::LoadState::Failed;
        document.error = QString::fromUtf8(error.what());
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count();
    qsizetype words = 0, translated = 0;
    for (const auto& line : document.lines) {
        words += line.words.size();
        translated += !line.translation.isEmpty();
    }
    const bool matched = cyberlyrics::matches(track, document.track);
    const bool timed = validTimeline(document);
    const bool inBudget = elapsed <= timeout + 250;
    const bool passed = document.state == cyberlyrics::LoadState::Ready && matched && timed && inBudget &&
                        (!requireWords || words > 0) && (!requireTranslation || translated > 0);
    QStringList failures;
    if (!inBudget) {
        failures.push_back(QStringLiteral("Source exceeded its time budget plus 250 ms scheduling tolerance"));
    }
    if (document.state != cyberlyrics::LoadState::Ready) {
        failures.push_back(QStringLiteral("No ready lyrics returned; state=") + stateName(document.state));
    }
    if (!matched) {
        failures.push_back(QStringLiteral("Returned recording does not match the requested metadata"));
    }
    if (!timed) {
        failures.push_back(QStringLiteral("No valid timed lyric lines"));
    }
    if (requireWords && !words) {
        failures.push_back(QStringLiteral("Real word timing was required but is unavailable"));
    }
    if (requireTranslation && !translated) {
        failures.push_back(QStringLiteral("Translation was required but is unavailable"));
    }
    std::cout << (passed ? "PASS " : "FAIL ") << source.name << " elapsed=" << elapsed
              << "ms lines=" << document.lines.size() << " words=" << words << " translated=" << translated << '\n';
    if (!document.error.isEmpty()) {
        std::cout << "  " << document.error.toStdString() << '\n';
    }
    return {{"source", source.name},
            {"passed", passed},
            {"state", stateName(document.state)},
            {"elapsedMs", qint64(elapsed)},
            {"lineCount", document.lines.size()},
            {"wordCount", words},
            {"translatedLineCount", translated},
            {"timingQuality", document.quality == cyberlyrics::TimingQuality::Word ? "word" : "line"},
            {"metadataMatched", matched},
            {"timelineValid", timed},
            {"withinTimeBudget", inBudget},
            {"provider", document.provider},
            {"attribution", document.attribution},
            {"matchedTrack", QJsonObject{{"title", document.track.title},
                                         {"artist", document.track.artist},
                                         {"album", document.track.album},
                                         {"duration", document.track.duration}}},
            {"failures", QJsonArray::fromStringList(failures)},
            {"error", document.error},
            {"requests", requests}};
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("CyberLyrics API feasibility probe"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Read-only HTTPS lyric checks; no music client or media session required."));
    parser.addHelpOption();
    parser.addOptions(
        {QCommandLineOption("provider", "all, luoyue-qq, luoyue-netease, netease, lrclib", "provider", "all"),
         QCommandLineOption("title", "Track title", "title", "Bohemian Rhapsody"),
         QCommandLineOption("artist", "Artist", "artist", "Queen"),
         QCommandLineOption("album", "Album; optional exact match", "album"),
         QCommandLineOption("duration", "Duration in seconds; zero means unknown", "seconds", "0"),
         QCommandLineOption("timeout-ms", "Total time budget per source (250..30000 ms)", "milliseconds", "12000"),
         QCommandLineOption("output", "Overwrite this JSON report", "path"),
         QCommandLineOption("require-words", "Require genuine word timestamps"),
         QCommandLineOption("require-translation", "Require at least one aligned translation"),
         QCommandLineOption("require-any", "Succeed when any selected source passes; default requires every source")});
    parser.process(app);
    bool timeoutValid = false, durationValid = false;
    const int timeout = parser.value("timeout-ms").toInt(&timeoutValid);
    const double duration = parser.value("duration").toDouble(&durationValid);
    const auto title = parser.value("title").trimmed(), artist = parser.value("artist").trimmed();
    const auto selected = parser.value("provider");
    const bool sourceValid = selected == "all" || std::any_of(sources.begin(), sources.end(), [&](const auto& source) {
                                 return selected == QLatin1StringView(source.name);
                             });
    if (!timeoutValid || timeout < 250 || timeout > 30000 || !durationValid || !std::isfinite(duration) ||
        duration < 0 || duration > 86400 || title.isEmpty() || artist.isEmpty() || title.size() > 512 ||
        artist.size() > 512 || parser.value("album").size() > 512 || !sourceValid ||
        !parser.positionalArguments().isEmpty()) {
        std::cerr << "Invalid parameters; run --help.\n";
        return 2;
    }
    const cyberlyrics::Track track{QStringLiteral("api-probe"), title, artist, parser.value("album"), {}, duration};
    QJsonArray results;
    int passed = 0;
    for (const auto& source : sources) {
        if (selected != "all" && selected != QLatin1StringView(source.name)) {
            continue;
        }
        const auto result =
            probe(source, track, timeout, parser.isSet("require-words"), parser.isSet("require-translation"));
        passed += result.value("passed").toBool();
        results.append(result);
    }
    const bool success = parser.isSet("require-any") ? passed > 0 : passed == results.size();
    const QJsonObject report{
        {"schema", 1},
        {"checkedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"passed", success},
        {"policy", parser.isSet("require-any") ? "any" : "all"},
        {"requiredWords", parser.isSet("require-words")},
        {"requiredTranslation", parser.isSet("require-translation")},
        {"perSourceBudgetMs", timeout},
        {"tlsVerification", "enabled"},
        {"scope", "direct lyric APIs only; no client operations; no playback synchronization claim"},
        {"requestedTrack",
         QJsonObject{{"title", title}, {"artist", artist}, {"album", track.album}, {"duration", duration}}},
        {"results", results}};
    if (parser.isSet("output")) {
        const auto path = parser.value("output");
        QSaveFile file(path);
        const auto bytes = QJsonDocument(report).toJson();
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
            file.write(bytes) != bytes.size() || !file.commit()) {
            std::cerr << "Cannot write report: " << file.errorString().toStdString() << '\n';
            return 2;
        }
        std::cout << "Report: " << QFileInfo(path).absoluteFilePath().toStdString() << '\n';
    }
    std::cout << passed << '/' << results.size()
              << " sources passed (policy=" << (parser.isSet("require-any") ? "any" : "all") << ").\n";
    return success ? 0 : 1;
}
