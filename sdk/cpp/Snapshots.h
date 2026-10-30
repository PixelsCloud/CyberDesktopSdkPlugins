#ifndef CYBERDESKTOP_CPP_SNAPSHOTS_H
#define CYBERDESKTOP_CPP_SNAPSHOTS_H

#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "sdk/services/Content.h"
#include "sdk/services/DataServices.h"
#include "sdk/services/Storage.h"

namespace cdsdk {
std::string copyText(const char* value);

struct DataStatus {
    uint32_t status = CD_DATA_LOADING, validFields = 0, error = 0;
    uint64_t observedAtUtc = 0, fetchedAtUtc = 0, sequence = 0;
    std::string sourceId, sourceUrl;

    explicit DataStatus(const CdDataInfo& value);

    bool valid() const;
};

struct ContentItem {
    std::string title, url, summary, source, language, stars, growth, discussion;
    int64_t published = 0;
    int score = 0, comments = 0;
    std::string date;
    bool operator==(const ContentItem&) const = default;
};

struct ContentSnapshot : DataStatus {
    std::string title, errorCode;
    std::vector<ContentItem> items;
    bool partial = false;
    explicit ContentSnapshot(const CdContentInfo& value);
};

struct VolumeSnapshot : DataStatus {
    std::string root;
    uint64_t available = 0, total = 0, free = 0;

    explicit VolumeSnapshot(const CdVolumeInfo& value);
};

struct LocationSnapshot : DataStatus {
    double latitude = 0, longitude = 0;
    std::string city, region, timezone;
    uint32_t mode = 0;

    explicit LocationSnapshot(const CdLocationInfo& value);
};

struct WeatherDay {
    std::string date;
    int32_t code = 0;
    double high = 0, low = 0, precipitationProbability = 0;
};

struct WeatherSnapshot : DataStatus {
    std::string city, region, localTime;
    double latitude = 0, longitude = 0, temperatureCelsius = 0, feelsCelsius = 0, humidityPercent = 0, windMps = 0;
    int32_t conditionCode = 0;
    bool isDay = true;
    std::vector<WeatherDay> days;
    std::vector<CdWeatherHour> hours;

    explicit WeatherSnapshot(const CdWeatherInfo& value);
};

struct DataSource {
    std::string name, url;
};

struct AirForecast {
    std::string date;
    double average = 0, low = 0, high = 0;
};

struct AirSnapshot : DataStatus {
    std::string city, station, stationUrl, localTime, dominant;
    double latitude = 0, longitude = 0, aqi = 0;
    double pollutantIndices[6]{};
    uint32_t pollutantValidFields = 0;
    std::vector<DataSource> sources;
    std::vector<AirForecast> forecast;

    explicit AirSnapshot(const CdAirInfo& value);
};

struct SystemSnapshot {
    CdSystemInfoEx data{};

    explicit SystemSnapshot(const CdSystemInfo& value);
};

struct MediaSnapshot {
    std::string sourceId, title, artist, album;
    std::vector<uint8_t> cover;
    uint32_t status = 0, controls = 0, coverWidth = 0, coverHeight = 0, coverStride = 0;
    uint64_t coverGeneration = 0, sampledAtMs = 0, commandRequestId = 0, sessionGeneration = 0;
    double positionSeconds = 0, durationSeconds = 0, playbackRate = 1;
    CdResult commandResult = CD_OK;

    explicit MediaSnapshot(const CdMediaInfo& value);
};

// Audio views are borrowed only for the callback. Copy samples before retaining.
struct AudioView {
    std::span<const float> samples;
    uint32_t count, sampleRate, channels;

    AudioView(const float* data, uint32_t count, uint32_t rate, uint32_t channels);
};

struct AudioSnapshot {
    std::vector<float> samples;
    uint32_t count = 0, sampleRate = 0, channels = 0;

    explicit AudioSnapshot(const AudioView& value);
};

struct SessionsView {
    uint32_t struct_size = sizeof(SessionsView);
    const CdMediaSession* entries;
    uint32_t count;
};

struct SessionSnapshot {
    struct Entry {
        std::string sourceId, name;
        uint32_t controls;
    };

    std::vector<Entry> entries;

    explicit SessionSnapshot(const SessionsView& value);
};
}  // namespace cdsdk
#endif
