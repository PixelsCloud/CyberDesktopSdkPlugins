#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace cyberlyrics {
enum class TimingQuality { Line, Word };
enum class LoadState { Empty, Loading, Ready, Instrumental, NotFound, Failed, Cancelled };

struct Track {
    QString source, title, artist, album, version;
    double duration = 0;
    QString key() const;
};

struct Word {
    QString text;
    double start = 0, end = 0;
};

struct Line {
    double start = 0, end = 0;
    QString text, translation;
    QVector<Word> words;
};

struct Document {
    Track track;
    QString provider, attribution, error;
    QVector<Line> lines;
    TimingQuality quality = TimingQuality::Line;
    LoadState state = LoadState::Empty;
    uint64_t revision = 0;
    int indexAt(double position, double delay = 0) const;
    double progressAt(int index, double position, double delay = 0) const;
    void normalize();
};

// Positive delay always means display later. The authoritative position already
// includes playback speed; speed applies only to elapsed monotonic time.
struct Playback {
    double position = 0, duration = 0, rate = 1;
    int64_t sampledAt = 0;
    uint64_t generation = 0;
    bool playing = false, positionValid = false;
};

class Timeline final {
public:
    void update(const Playback& playback);
    double positionAt(int64_t monotonicNow) const;
    bool valid() const;

private:
    Playback m_playback;
};

// Conservative cross-source matching: known live/remix/instrumental variants
// and trusted duration must agree. A title alone never proves a match.
bool matches(const Track& requested, const Track& candidate);
}  // namespace cyberlyrics
