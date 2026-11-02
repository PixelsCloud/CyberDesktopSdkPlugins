#pragma once

#include "CyberLyrics/WindowsMedia.h"

namespace cyberlyrics::windowsmedia {
// SMTC positions describe LastUpdatedTime, rather than the time of each poll.
class MediaTimelineClock {
public:
    double update(const MediaSnapshot& sample, int64_t updatedAt, int64_t utcNow, bool trackChanged = false);

private:
    bool m_initialized = false;
    double m_rawPosition = 0, m_duration = 0, m_position = 0, m_rate = 1;
    uint32_t m_status = 0;
    uint64_t m_sampledAt = 0;
    int64_t m_updatedAt = 0;
};
}  // namespace cyberlyrics::windowsmedia
