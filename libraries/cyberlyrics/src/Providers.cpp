#include <cpr/cpr.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>
#include <algorithm>
#include <stdexcept>

#include "CyberLyrics/Engine.h"
#include "CyberLyrics/Parser.h"

namespace cyberlyrics {
namespace {
QJsonDocument request(const std::string& url, const cpr::Parameters& parameters, const RequestContext& context,
                      const cpr::Payload* payload = nullptr) {
    const auto began = std::chrono::steady_clock::now();
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(context.deadline - std::chrono::steady_clock::now())
            .count();
    if (context.cancelled() || remaining <= 0) {
        throw std::runtime_error("Lyrics request cancelled or deadline reached");
    }
    cpr::Session session;
    session.SetUrl(cpr::Url{url});
    session.SetParameters(parameters);
    session.SetHeader(cpr::Header{{"User-Agent", "CyberLyrics/0.1 (CyberDesktop)"}, {"Accept", "application/json"}});
    session.SetRedirect(cpr::Redirect{false});
    session.SetConnectTimeout(cpr::ConnectTimeout{2000});
    session.SetTimeout(cpr::Timeout{int(std::min<int64_t>(5000, remaining))});
    size_t headerBytes = 0;
    std::string bytes;
    session.SetHeaderCallback(cpr::HeaderCallback{[&](std::string_view chunk, intptr_t) {
        headerBytes += chunk.size();
        return headerBytes <= 32768;
    }});
    session.SetProgressCallback(cpr::ProgressCallback{[&](auto, auto, auto, auto, intptr_t) {
        return !context.cancelled() && std::chrono::steady_clock::now() < context.deadline;
    }});
    session.SetWriteCallback(cpr::WriteCallback{[&](std::string_view chunk, intptr_t) {
        if (context.cancelled() || bytes.size() + chunk.size() > 4 * 1024 * 1024) {
            return false;
        }
        bytes.append(chunk);
        return true;
    }});
    if (payload) {
        session.SetPayload(*payload);
    }
    const auto response = payload ? session.Post() : session.Get();
    if (context.report) {
        context.report(
            {QString::fromStdString(url), QString::fromStdString(response.error.message), int(response.status_code),
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count(),
             int64_t(bytes.size())});
    }
    if (response.status_code == 404) {
        return {};
    }
    if (response.error.code != cpr::ErrorCode::OK || response.status_code != 200) {
        throw std::runtime_error("Lyrics HTTP request failed: " + std::to_string(response.status_code) + " " +
                                 response.error.message);
    }
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(QByteArray::fromStdString(bytes), &error);
    if (error.error != QJsonParseError::NoError) {
        throw std::runtime_error("Lyrics response contains invalid JSON");
    }
    return json;
}

class Lrclib final : public Provider {
public:
    Document fetch(const Track& track, const RequestContext& context) override {
        cpr::Parameters parameters{{"track_name", track.title.toStdString()},
                                   {"artist_name", track.artist.toStdString()}};
        if (!track.album.isEmpty()) {
            parameters.Add({"album_name", track.album.toStdString()});
        }
        if (track.duration > 0) {
            parameters.Add({"duration", std::to_string(qRound64(track.duration))});
        }
        const auto object = request("https://lrclib.net/api/get", parameters, context).object();
        Document result;
        result.track = {track.source,
                        object.value("trackName").toString(),
                        object.value("artistName").toString(),
                        object.value("albumName").toString(),
                        {},
                        object.value("duration").toDouble()};
        result.state = LoadState::NotFound;
        if (!matches(track, result.track)) {
            return result;
        }
        if (object.value("instrumental").toBool()) {
            result.state = LoadState::Instrumental;
        } else {
            result = parseLrc(object.value("syncedLyrics").toString().toUtf8(), result.track);
        }
        result.provider = QStringLiteral("LRCLIB");
        result.attribution = QStringLiteral("https://lrclib.net");
        return result;
    }
};

class Netease final : public Provider {
public:
    Document fetch(const Track& track, const RequestContext& context) override {
        const cpr::Payload payload{
            {"s", (track.title + ' ' + track.artist).toStdString()}, {"type", "1"}, {"limit", "12"}, {"offset", "0"}};
        const auto songs = request("https://music.163.com/api/search/get/web", {}, context, &payload)
                               .object()
                               .value("result")
                               .toObject()
                               .value("songs")
                               .toArray();
        for (const auto& value : songs) {
            if (context.cancelled()) {
                break;
            }
            const auto song = value.toObject();
            QStringList artists;
            for (const auto& artist : song.value("artists").toArray()) {
                artists.push_back(artist.toObject().value("name").toString());
            }
            Track candidate{track.source,
                            song.value("name").toString(),
                            artists.join('/'),
                            song.value("album").toObject().value("name").toString(),
                            {},
                            song.value("duration").toDouble() * .001};
            if (!matches(track, candidate)) {
                continue;
            }
            const auto id = QString::number(song.value("id").toInteger());
            const auto lyrics = request("https://music.163.com/api/song/lyric",
                                        {{"id", id.toStdString()}, {"lv", "-1"}, {"kv", "-1"}, {"tv", "-1"}}, context)
                                    .object();
            auto result = parseLrc(lyrics.value("lrc").toObject().value("lyric").toString().toUtf8(), candidate);
            const auto words = parseWords(lyrics.value("yrc").toObject().value("lyric").toString(), candidate);
            if (words.quality == TimingQuality::Word && words.state == LoadState::Ready) {
                result = words;
            }
            const auto translation =
                parseLrc(lyrics.value("tlyric").toObject().value("lyric").toString().toUtf8(), candidate);
            mergeTranslation(result, translation);
            result.provider = QStringLiteral("NetEase");
            result.attribution = QStringLiteral("https://music.163.com/song?id=") + id;
            return result;
        }
        Document result;
        result.track = track;
        result.state = LoadState::NotFound;
        return result;
    }
};

double catalogDuration(const QJsonValue& value) {
    if (value.isDouble()) {
        return value.toDouble();
    }
    const auto text = value.toString();
    const auto time = QTime::fromString(text, QStringLiteral("m:ss"));
    if (time.isValid()) {
        return QTime(0, 0).secsTo(time);
    }
    static const QRegularExpression chinese(QStringLiteral("^(?:(\\d+)分)?(\\d+)秒$"));
    const auto match = chinese.match(text);
    return match.hasMatch() ? match.captured(1).toDouble() * 60 + match.captured(2).toDouble() : text.toDouble();
}

class Catalog final : public Provider {
public:
    explicit Catalog(bool netease) : m_netease(netease) {}

    Document fetch(const Track& track, const RequestContext& context) override {
        const std::string base = "https://api.vkeys.cn/v2/music/";
        const auto searchPath = m_netease ? "netease" : "tencent/search/song";
        const auto root =
            request(base + searchPath, {{"word", (track.title + ' ' + track.artist).toStdString()}}, context).object();
        if (root.value("code").toInt() != 200) {
            throw std::runtime_error("Lyrics catalog search returned an unsuccessful API status");
        }
        auto candidates = root.value("data").toArray();
        // QQ groups releases of the same title under grp. Inspect a bounded set
        // so an exact album/version can win over the first search hit.
        const auto originalCount = std::min<qsizetype>(candidates.size(), 32);
        for (qsizetype index = 0; index < originalCount && candidates.size() < 64; ++index) {
            for (const auto& release : candidates[index].toObject().value("grp").toArray()) {
                if (candidates.size() >= 64) {
                    break;
                }
                candidates.append(release);
            }
        }
        Document empty;
        empty.track = track;
        empty.state = LoadState::NotFound;
        for (qsizetype index = 0; index < std::min<qsizetype>(64, candidates.size()); ++index) {
            if (context.cancelled() || std::chrono::steady_clock::now() >= context.deadline) {
                break;
            }
            const auto song = candidates[index].toObject();
            Track candidate{track.source,
                            song.value("song").toString(),
                            song.value("singer").toString(),
                            song.value("album").toString(),
                            song.value("subtitle").toString(),
                            catalogDuration(song.value("interval"))};
            const auto id = song.value("id").toInteger();
            if (id <= 0 || !matches(track, candidate)) {
                continue;
            }
            const auto lyricPath = m_netease ? "netease/lyric" : "tencent/lyric";
            const auto lyricRoot = request(base + lyricPath, {{"id", std::to_string(id)}}, context).object();
            if (lyricRoot.value("code").toInt() != 200) {
                throw std::runtime_error("Lyrics catalog lookup returned an unsuccessful API status");
            }
            const auto data = lyricRoot.value("data").toObject();
            auto lrc = data.value("lrc").toString();
            if (lrc.isEmpty()) {
                lrc = data.value("rc").toString();
            }
            auto result = parseLrc(lrc.toUtf8(), candidate);
            auto words = parseWords(data.value("yrc").toString(), candidate);
            if (words.state == LoadState::Ready && words.quality == TimingQuality::Word) {
                result = std::move(words);
            }
            const auto translation = parseLrc(data.value("trans").toString().toUtf8(), candidate);
            mergeTranslation(result, translation);
            result.provider = m_netease ? QStringLiteral("LuoYue/NetEase") : QStringLiteral("LuoYue/QQ");
            result.attribution =
                QString::fromStdString(base + lyricPath) + QStringLiteral("?id=") + QString::number(id);
            return result;
        }
        return empty;
    }

private:
    bool m_netease;
};

class PreferredCatalog final : public Provider {
public:
    explicit PreferredCatalog(bool preferred) : m_preferred(preferred) {}

    Document fetch(const Track& track, const RequestContext& context) override {
        const bool netease = track.source.contains("cloudmusic", Qt::CaseInsensitive) ||
                             track.source.contains("netease", Qt::CaseInsensitive);
        return Catalog(m_preferred ? netease : !netease).fetch(track, context);
    }

private:
    bool m_preferred;
};
}  // namespace

std::shared_ptr<Provider> createNetworkProvider(NetworkSource source) {
    switch (source) {
        case NetworkSource::LuoYueQQ:
            return std::make_shared<Catalog>(false);
        case NetworkSource::LuoYueNetEase:
            return std::make_shared<Catalog>(true);
        case NetworkSource::NetEase:
            return std::make_shared<Netease>();
        case NetworkSource::LRCLIB:
            return std::make_shared<Lrclib>();
    }
    return {};
}

std::vector<std::shared_ptr<Provider>> networkProviders() {
    return {std::make_shared<PreferredCatalog>(true), std::make_shared<PreferredCatalog>(false),
            createNetworkProvider(NetworkSource::NetEase), createNetworkProvider(NetworkSource::LRCLIB)};
}
}  // namespace cyberlyrics
