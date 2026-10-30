#include "sdk/cpp/Snapshots.h"

#include <stdexcept>

namespace cdsdk {
std::string copyText(const char* value) {
    return value ? value : "";
}

DataStatus::DataStatus(const CdDataInfo& value)
    : status(value.status),
      validFields(value.valid_fields),
      error(value.error),
      observedAtUtc(value.observed_at_utc_ms),
      fetchedAtUtc(value.fetched_at_utc_ms),
      sequence(value.sequence),
      sourceId(copyText(value.source_id)),
      sourceUrl(copyText(value.source_url)) {}

auto DataStatus::valid() const -> bool {
    return (validFields & CD_DATA_VALID) != 0;
}

ContentSnapshot::ContentSnapshot(const CdContentInfo& value)
    : DataStatus(value.info),
      title(copyText(value.title)),
      errorCode(copyText(value.error_code)),
      partial(value.partial != 0) {
    if (value.item_count > 30 || (value.item_count && !value.items)) {
        throw std::invalid_argument("Invalid content snapshot");
    }
    items.reserve(value.item_count);
    for (uint32_t i = 0; i < value.item_count; ++i) {
        const auto& item = value.items[i];
        items.push_back({copyText(item.title), copyText(item.url), copyText(item.summary), copyText(item.source),
                         copyText(item.language), copyText(item.stars), copyText(item.growth),
                         copyText(item.discussion), item.published, item.score, item.comments, copyText(item.date)});
    }
}

VolumeSnapshot::VolumeSnapshot(const CdVolumeInfo& value)
    : DataStatus(value.info),
      root(copyText(value.root)),
      available(value.available_bytes),
      total(value.total_bytes),
      free(value.free_bytes) {}

LocationSnapshot::LocationSnapshot(const CdLocationInfo& value)
    : DataStatus(value.info),
      latitude(value.latitude),
      longitude(value.longitude),
      city(copyText(value.city)),
      region(copyText(value.region)),
      timezone(copyText(value.timezone)),
      mode(value.mode) {}

WeatherSnapshot::WeatherSnapshot(const CdWeatherInfo& value)
    : DataStatus(value.info),
      city(copyText(value.city)),
      region(copyText(value.region)),
      localTime(copyText(value.local_time)),
      latitude(value.latitude),
      longitude(value.longitude),
      temperatureCelsius(value.temperature_celsius),
      feelsCelsius(value.feels_celsius),
      humidityPercent(value.humidity_percent),
      windMps(value.wind_mps),
      conditionCode(value.condition_code),
      isDay(value.is_day != 0) {
    if (value.days && value.day_count <= 14)
        for (uint32_t i = 0; i < value.day_count; ++i) {
            const auto& day = value.days[i];
            days.push_back({copyText(day.date), day.condition_code, day.high_celsius, day.low_celsius,
                            day.precipitation_probability});
        }
    if (value.hours && value.hour_count <= 168)
        hours.assign(value.hours, value.hours + value.hour_count);
}

AirSnapshot::AirSnapshot(const CdAirInfo& value)
    : DataStatus(value.info),
      city(copyText(value.city)),
      station(copyText(value.station)),
      stationUrl(copyText(value.station_url)),
      localTime(copyText(value.local_time)),
      dominant(copyText(value.dominant)),
      latitude(value.latitude),
      longitude(value.longitude),
      aqi(value.aqi),
      pollutantValidFields(value.pollutant_valid_fields) {
    std::copy(std::begin(value.pollutant_indices), std::end(value.pollutant_indices), std::begin(pollutantIndices));
    if (value.sources && value.source_count <= 64)
        for (uint32_t i = 0; i < value.source_count; ++i)
            sources.push_back({copyText(value.sources[i].name), copyText(value.sources[i].url)});
    if (value.forecast && value.forecast_count <= 64)
        for (uint32_t i = 0; i < value.forecast_count; ++i) {
            const auto& day = value.forecast[i];
            forecast.push_back({copyText(day.date), day.average, day.low, day.high});
        }
}

SystemSnapshot::SystemSnapshot(const CdSystemInfo& value) {
    if (value.struct_size >= sizeof(CdSystemInfoEx))
        data = *reinterpret_cast<const CdSystemInfoEx*>(&value);
    else
        data.base = value;
}

MediaSnapshot::MediaSnapshot(const CdMediaInfo& value)
    : sourceId(copyText(value.source_id)),
      title(copyText(value.title)),
      artist(copyText(value.artist)),
      album(copyText(value.album)),
      status(value.status),
      controls(value.controls),
      coverWidth(value.cover_width),
      coverHeight(value.cover_height),
      coverStride(value.cover_stride),
      coverGeneration(value.cover_generation),
      sampledAtMs(value.sampled_at_ms),
      positionSeconds(value.position_seconds),
      durationSeconds(value.duration_seconds),
      playbackRate(value.playback_rate),
      commandResult(value.command_result) {
    if (value.struct_size >= sizeof(CdMediaInfoEx)) {
        const auto& extended = reinterpret_cast<const CdMediaInfoEx&>(value);
        commandRequestId = extended.command_request_id;
        sessionGeneration = extended.session_generation;
    }
    if (value.cover_rgba && coverWidth <= 256 && coverHeight <= 256 && coverStride >= coverWidth * 4 &&
        coverStride <= 4096)
        cover.assign(value.cover_rgba, value.cover_rgba + uint64_t(coverStride) * coverHeight);
}

AudioView::AudioView(const float* data, uint32_t count, uint32_t rate, uint32_t channels)
    : samples(data, size_t(count) * (channels ? channels : 1)), count(count), sampleRate(rate), channels(channels) {}

AudioSnapshot::AudioSnapshot(const AudioView& value)
    : samples(value.samples.begin(), value.samples.end()),
      count(value.count),
      sampleRate(value.sampleRate),
      channels(value.channels) {}

SessionSnapshot::SessionSnapshot(const SessionsView& value) {
    if (value.count > 256 || (value.count && !value.entries))
        throw std::invalid_argument("Invalid media session array");
    for (uint32_t index = 0; index < value.count; ++index) {
        const auto& entry = value.entries[index];
        entries.push_back({copyText(entry.source_id), copyText(entry.name), entry.controls});
    }
}
}  // namespace cdsdk
