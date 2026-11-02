#include "CyberLyrics/Document.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>
#include <utility>

namespace cyberlyrics {
namespace {
QString canonical(QString value) {
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
    static const QRegularExpression punctuation(QStringLiteral("[\\p{P}\\p{Z}\\s]+"));
    return value.remove(punctuation);
}

QString variant(const Track& track) {
    const auto text = (track.title + " " + track.version).toCaseFolded();
    QStringList found;
    for (const auto* token : {"live", "remix", "acoustic", "instrumental", "伴奏", "现场", "翻唱", "加速", "慢速"}) {
        if (text.contains(QString::fromUtf8(token))) {
            found.push_back(QString::fromUtf8(token));
        }
    }
    return found.join('/');
}
}  // namespace

QString Track::key() const {
    QByteArray bytes;
    for (const auto& field : {source, title, artist, album, version}) {
        const auto encoded = canonical(field).toUtf8();
        bytes += QByteArray::number(encoded.size()) + ':' + encoded;
    }
    bytes += ':' + QByteArray::number(std::isfinite(duration) ? qRound64(duration) : 0);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

int Document::indexAt(double position, double delay) const {
    if (!std::isfinite(position) || !std::isfinite(delay)) {
        return -1;
    }
    const auto it = std::upper_bound(lines.begin(), lines.end(), position - delay, [](double time, const Line& line) {
        return time < line.start;
    });
    return int(it - lines.begin()) - 1;
}

double Document::progressAt(int index, double position, double delay) const {
    if (index < 0 || index >= lines.size() || !std::isfinite(position) || !std::isfinite(delay)) {
        return 0;
    }
    const auto& line = lines[index];
    const double time = position - delay;
    if (line.words.isEmpty()) {
        return line.end > line.start ? std::clamp((time - line.start) / (line.end - line.start), 0.0, 1.0) : 0;
    }
    // Return a word coordinate. Views measure actual shaped text advances;
    // this coordinate is never interpreted as a UTF-16 character width.
    double completed = 0;
    for (const auto& word : line.words) {
        if (time >= word.end) {
            completed += 1;
        } else if (time > word.start && word.end > word.start) {
            completed += (time - word.start) / (word.end - word.start);
        }
    }
    return completed / line.words.size();
}

void Document::normalize() {
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [](const Line& line) {
                                   return !std::isfinite(line.start) || line.text.isEmpty();
                               }),
                lines.end());
    std::stable_sort(lines.begin(), lines.end(), [](const Line& left, const Line& right) {
        return left.start < right.start;
    });
    quality = TimingQuality::Line;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        auto& line = lines[index];
        const double boundary = index + 1 < lines.size() ? lines[index + 1].start : track.duration;
        if (!std::isfinite(line.end) || line.end <= line.start) {
            line.end = boundary > line.start ? boundary : line.start + 5;
        }
        QString text;
        double previous = line.start;
        bool valid = !line.words.isEmpty();
        for (const auto& word : line.words) {
            valid &= std::isfinite(word.start) && std::isfinite(word.end) && word.start >= previous &&
                     word.end >= word.start && word.end <= line.end + .05;
            previous = word.end;
            text += word.text;
        }
        if (!valid || text != line.text) {
            line.words.clear();
        } else {
            QVector<Word> clusters;
            QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, line.text);
            int offset = 0;
            for (const auto& word : line.words) {
                boundaries.setPosition(offset);
                if (!clusters.isEmpty() && !boundaries.isAtBoundary()) {
                    clusters.back().text += word.text;
                    clusters.back().end = word.end;
                } else {
                    clusters.push_back(word);
                }
                offset += int(word.text.size());
            }
            line.words = std::move(clusters);
            quality = TimingQuality::Word;
        }
    }
    if (!lines.isEmpty()) {
        state = LoadState::Ready;
    }
}

void Timeline::update(const Playback& playback) {
    m_playback = playback;
    m_playback.positionValid &= std::isfinite(playback.position) && playback.position >= 0;
    if (!std::isfinite(m_playback.rate) || m_playback.rate <= 0 || m_playback.rate > 16) {
        m_playback.rate = 1;
    }
}

double Timeline::positionAt(int64_t monotonicNow) const {
    if (!valid()) {
        return 0;
    }
    double position = m_playback.position;
    if (m_playback.playing && monotonicNow >= m_playback.sampledAt) {
        position += double(monotonicNow - m_playback.sampledAt) * .001 * m_playback.rate;
    }
    position = std::max(0.0, position);
    if (std::isfinite(m_playback.duration) && m_playback.duration > 0) {
        position = std::min(position, m_playback.duration);
    }
    return position;
}

bool Timeline::valid() const {
    return m_playback.positionValid;
}

bool matches(const Track& requested, const Track& candidate) {
    if (canonical(requested.title).isEmpty() || canonical(requested.title) != canonical(candidate.title) ||
        canonical(requested.artist).isEmpty() || canonical(requested.artist) != canonical(candidate.artist) ||
        variant(requested) != variant(candidate)) {
        return false;
    }
    if (!requested.album.isEmpty() && !candidate.album.isEmpty() &&
        canonical(requested.album) != canonical(candidate.album)) {
        return false;
    }
    return requested.duration <= 0 || candidate.duration <= 0 || std::abs(requested.duration - candidate.duration) <= 3;
}
}  // namespace cyberlyrics
