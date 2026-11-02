#include "CyberLyrics/MediaTimelineClock.h"

#include <algorithm>

namespace cyberlyrics::windowsmedia {
double MediaTimelineClock::update(const MediaSnapshot& sample, int64_t updatedAt, int64_t utcNow, bool trackChanged) {
    const bool timelineChanged = !m_initialized || trackChanged || sample.position != m_rawPosition ||
                                 sample.duration != m_duration || updatedAt != m_updatedAt;
    double position = sample.position;
    if (timelineChanged) {
        if (sample.status == MediaPlaying && updatedAt > 0 && utcNow >= updatedAt) {
            position += double(utcNow - updatedAt) / 10000000.0 * sample.rate;
        }
    } else {
        position = m_position;
        // Use the preceding playback state/rate for time since the last sample.
        // Paused time is never added when playback resumes without a timeline event.
        if (m_status == MediaPlaying && sample.sampledAt >= m_sampledAt) {
            position += double(sample.sampledAt - m_sampledAt) * .001 * m_rate;
        }
    }

    m_rawPosition = sample.position;
    m_duration = sample.duration;
    m_updatedAt = updatedAt;
    m_status = sample.status;
    m_rate = sample.rate;
    m_sampledAt = sample.sampledAt;
    m_position = std::max(0.0, position);
    if (sample.duration > 0) {
        m_position = std::min(m_position, sample.duration);
    }
    m_initialized = true;
    return m_position;
}
}  // namespace cyberlyrics::windowsmedia
