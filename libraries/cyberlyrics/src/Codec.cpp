#include "CyberLyrics/Codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "CyberLyrics/Document.h"

namespace cyberlyrics {
QByteArray encode(const Document& document) {
    const auto& track = document.track;
    QJsonArray lines;
    for (const auto& line : document.lines) {
        QJsonArray words;
        for (const auto& word : line.words) {
            words.append(QJsonObject{{"text", word.text}, {"start", word.start}, {"end", word.end}});
        }
        lines.append(QJsonObject{{"start", line.start},
                                 {"end", line.end},
                                 {"text", line.text},
                                 {"translation", line.translation},
                                 {"words", words}});
    }
    return QJsonDocument(QJsonObject{{"schema", 1},
                                     {"source", track.source},
                                     {"title", track.title},
                                     {"artist", track.artist},
                                     {"album", track.album},
                                     {"version", track.version},
                                     {"duration", track.duration},
                                     {"provider", document.provider},
                                     {"attribution", document.attribution},
                                     {"state", int(document.state)},
                                     {"lines", lines}})
        .toJson(QJsonDocument::Compact);
}

Document decode(const QByteArray& bytes) {
    Document result;
    result.state = LoadState::Failed;
    if (bytes.size() > 4 * 1024 * 1024) {
        result.error = QStringLiteral("Cached lyrics exceed 4 MiB");
        return result;
    }
    QJsonParseError error;
    const auto object = QJsonDocument::fromJson(bytes, &error).object();
    if (error.error != QJsonParseError::NoError || object.value("schema").toInt() != 1) {
        result.error = QStringLiteral("Invalid lyrics cache schema");
        return result;
    }
    result.track = {object.value("source").toString(),  object.value("title").toString(),
                    object.value("artist").toString(),  object.value("album").toString(),
                    object.value("version").toString(), object.value("duration").toDouble()};
    result.provider = object.value("provider").toString();
    result.attribution = object.value("attribution").toString();
    const auto state = object.value("state").toInt();
    if (state != int(LoadState::Ready) && state != int(LoadState::Instrumental)) {
        result.error = QStringLiteral("Cache contains a nonpersistent lyric state");
        return result;
    }
    result.state = LoadState(state);
    const auto lines = object.value("lines").toArray();
    if (lines.size() > 20000) {
        result.state = LoadState::Failed;
        result.error = QStringLiteral("Cached lyrics exceed 20000 lines");
        return result;
    }
    for (const auto& value : lines) {
        const auto saved = value.toObject();
        Line line{saved.value("start").toDouble(),
                  saved.value("end").toDouble(),
                  saved.value("text").toString(),
                  saved.value("translation").toString(),
                  {}};
        for (const auto& savedWord : saved.value("words").toArray()) {
            const auto word = savedWord.toObject();
            line.words.push_back(
                {word.value("text").toString(), word.value("start").toDouble(), word.value("end").toDouble()});
        }
        result.lines.push_back(std::move(line));
    }
    result.normalize();
    if ((state == int(LoadState::Ready) && result.lines.isEmpty()) ||
        (state == int(LoadState::Instrumental) && !result.lines.isEmpty())) {
        result.state = LoadState::Failed;
        result.error = QStringLiteral("Cached lyric content contradicts its persistent state");
    }
    return result;
}
}  // namespace cyberlyrics
