#include "CyberLyrics/Parser.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringDecoder>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <cmath>

#include "CyberLyrics/Runtime.h"

namespace cyberlyrics {
namespace {
Document failed(const Track& track, const QString& error) {
    Document result;
    result.track = track;
    result.error = error;
    result.state = LoadState::Failed;
    return result;
}
}  // namespace

Document parseLrc(const QByteArray& utf8, const Track& track) {
    if (utf8.size() > 4 * 1024 * 1024) {
        return failed(track, QStringLiteral("Lyrics exceed 4 MiB"));
    }
    QStringDecoder decoder(QStringConverter::Utf8);
    QString text = decoder.decode(utf8);
    if (decoder.hasError()) {
        return failed(track, QStringLiteral("Invalid UTF-8 lyrics"));
    }
    // Preserve the existing FFmpeg parser and its metadata-unit correction.
    // Its pinned LRC demuxer subtracts offset directly from microsecond PTS.
    static const QRegularExpression offset(QStringLiteral("^\\s*\\[offset:([+-]?\\d+)\\]\\s*$"),
                                           QRegularExpression::MultilineOption);
    auto matches = offset.globalMatch(text);
    qint64 milliseconds = 0;
    bool hasOffset = false;
    while (matches.hasNext()) {
        bool ok = false;
        const auto value = matches.next().captured(1).toLongLong(&ok);
        if (ok) {
            milliseconds = qBound<qint64>(-86400000LL, value, 86400000LL);
            hasOffset = true;
        }
    }
    text.remove(QChar(0xfeff));
    if (hasOffset) {
        text.remove(offset);
        text.prepend(QStringLiteral("[offset:%1]\n").arg(milliseconds * 1000));
    }
    QTemporaryFile normalized;
    if (!normalized.open()) {
        return failed(track, normalized.errorString());
    }
    const auto bytes = text.toUtf8();
    if (normalized.write(bytes) != bytes.size() || !normalized.flush()) {
        return failed(track, normalized.errorString());
    }
    normalized.close();
    Document result;
    result.track = track;
    char error[256]{};
    const auto count = cyberlyrics_read_lrc(
        normalized.fileName().toUtf8().constData(),
        [](void* context, double seconds, const char* data, int length) {
            auto& document = *static_cast<Document*>(context);
            // FFmpeg can include the terminating NUL in its final LRC packet.
            // It is a packet delimiter, not part of the user's lyric text.
            while (length > 0 && data[length - 1] == 0) {
                --length;
            }
            document.lines.push_back({seconds, 0, QString::fromUtf8(data, length).trimmed(), {}, {}});
        },
        &result, error, sizeof(error));
    if (count < 0) {
        return failed(track, QString::fromUtf8(error));
    }
    result.state = LoadState::NotFound;
    result.normalize();
    return result;
}

Document readFile(const QString& path, const Track& track) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failed(track, file.errorString());
    }
    if (file.size() > 4 * 1024 * 1024) {
        return failed(track, QStringLiteral("Lyrics exceed 4 MiB"));
    }
    const auto bytes = file.readAll();
    QStringDecoder decoder(QStringConverter::Utf8);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) {
#ifdef _WIN32
        const int length =
            MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.constData(), int(bytes.size()), nullptr, 0);
        if (length <= 0) {
            return failed(track, QStringLiteral("Lyrics are neither UTF-8 nor GB18030"));
        }
        text.resize(length);
        MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.constData(), int(bytes.size()),
                            reinterpret_cast<wchar_t*>(text.data()), length);
#else
        return failed(track, QStringLiteral("Invalid UTF-8 lyrics"));
#endif
    }
    auto document = parseWords(text, track);
    if (document.state == LoadState::Ready) {
        return document;
    }
    return parseLrc(text.toUtf8(), track);
}

Document parseWords(const QString& input, const Track& track) {
    if (input.size() > 4 * 1024 * 1024) {
        return failed(track, QStringLiteral("Lyrics exceed 4 MiB"));
    }
    QString text = input;
    if (text.trimmed().startsWith('<')) {
        QXmlStreamReader xml(text);
        text.clear();
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.attributes().hasAttribute("LyricContent")) {
                text = xml.attributes().value("LyricContent").toString();
            }
        }
        if (xml.hasError()) {
            return failed(track, xml.errorString());
        }
    }
    static const QRegularExpression linePattern(QStringLiteral("^\\[(\\d+),(\\d+)(?:,[^\\]]*)?\\](.*)$"));
    static const QRegularExpression timing(QStringLiteral("\\((\\d+),(\\d+)(?:,\\d+)?\\)"));
    Document result;
    result.track = track;
    result.state = LoadState::NotFound;
    for (const auto& raw : text.split('\n')) {
        const auto match = linePattern.match(raw.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        Line line;
        line.start = match.captured(1).toDouble() * .001;
        line.end = line.start + match.captured(2).toDouble() * .001;
        const auto body = match.captured(3);
        auto timestamps = timing.globalMatch(body);
        if (!timestamps.hasNext()) {
            continue;
        }
        auto stamp = timestamps.next();
        const bool timingFirst = body.left(stamp.capturedStart()).trimmed().isEmpty();
        line.text = body;
        line.text.remove(timing);
        if (timingFirst && stamp.capturedStart() > 0) {
            line.text.remove(0, stamp.capturedStart());
        }
        qsizetype previousEnd = 0;
        while (stamp.hasMatch()) {
            const auto next = timestamps.hasNext() ? timestamps.next() : QRegularExpressionMatch();
            const auto content =
                timingFirst
                    ? body.mid(stamp.capturedEnd(), next.hasMatch() ? next.capturedStart() - stamp.capturedEnd() : -1)
                    : body.mid(previousEnd, stamp.capturedStart() - previousEnd);
            const double start = stamp.captured(1).toDouble() * .001;
            const double duration = stamp.captured(2).toDouble() * .001;
            if (!content.isEmpty()) {
                line.words.push_back({content, start, start + duration});
            }
            previousEnd = stamp.capturedEnd();
            stamp = next;
        }
        if (!line.words.isEmpty()) {
            result.lines.push_back(std::move(line));
        }
        if (result.lines.size() > 20000) {
            return failed(track, QStringLiteral("Lyrics exceed 20000 lines"));
        }
    }
    result.normalize();
    return result;
}

void mergeTranslation(Document& document, const Document& translation) {
    if (!matches(document.track, translation.track)) {
        return;
    }
    for (auto& line : document.lines) {
        const int index = translation.indexAt(line.start + .05);
        if (index >= 0 && std::abs(translation.lines[index].start - line.start) <= .1) {
            line.translation = translation.lines[index].text;
        }
    }
}
}  // namespace cyberlyrics
