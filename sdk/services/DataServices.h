#ifndef CYBERDESKTOP_DATA_SERVICES_H
#define CYBERDESKTOP_DATA_SERVICES_H

#include "sdk/PluginApi.h"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

#define CD_SERVICE_VERSION 1u
#define CD_SERVICE_INPUT "input.stats"
#define CD_SERVICE_SYSTEM "system.telemetry"
#define CD_SERVICE_MEDIA "media.playback"
#define CD_SERVICE_SESSIONS "media.sessions"
#define CD_SERVICE_FFT "audio.spectrum"
#define CD_SERVICE_PCM "audio.pcm"
#define CD_SERVICE_LOCATION "location.current"
#define CD_SERVICE_WEATHER "weather.forecast"
#define CD_SERVICE_AIR "air.quality"

#define CD_DATA_LOADING 0u
#define CD_DATA_READY 1u
#define CD_DATA_STALE 2u
#define CD_DATA_UNAVAILABLE 3u
#define CD_DATA_ERROR 4u
#define CD_DATA_VALID 1u
#define CD_DATA_NETWORK 1u
#define CD_DATA_NO_LOCATION 2u
#define CD_DATA_RATE_LIMITED 3u
#define CD_DATA_CREDENTIALS_MISSING 4u
#define CD_DATA_UNAUTHORIZED 5u
#define CD_DATA_INVALID_RESPONSE 6u
#define CD_DATA_NO_DATA 7u

/* All strings/arrays/pixels are borrowed until the UI callback returns.
   UTC times are milliseconds; zero means unavailable. sequence is monotonic
   per resource. Status and data validity are independent. */
typedef struct CdDataInfo {
    uint32_t struct_size, status, valid_fields, error;
    uint64_t observed_at_utc_ms, fetched_at_utc_ms, sequence;
    const char *source_id, *source_url;
} CdDataInfo;

#define CD_LOCATION_DEFAULT 0u
#define CD_LOCATION_AUTO 1u
#define CD_LOCATION_CITY 2u
#define CD_LOCATION_COORDINATES 3u
#define CD_LOCATION_STATION 4u

/* DEFAULT follows the application's default location. Display units and
   language do not create separate numerical forecast resources. credential_id
   references a private host profile, never a token or credentials file. */
typedef struct CdLocationQuery {
    uint32_t struct_size, mode;
    double latitude, longitude;
    const char *city, *station, *credential_id, *language;
    uint32_t refresh_seconds, forecast_days, forecast_hours, reserved;
    const char* country_code; /* optional ISO country filter for city searches */
} CdLocationQuery;

typedef struct CdLocationInfo {
    CdDataInfo info;
    double latitude, longitude;
    const char *city, *region, *timezone;
    uint32_t mode, reserved;
} CdLocationInfo;

typedef struct CdWeatherDay {
    const char* date;
    int32_t condition_code;
    uint32_t reserved;
    double high_celsius, low_celsius, precipitation_probability;
} CdWeatherDay;

typedef struct CdWeatherHour {
    uint64_t time_utc_ms;
    double temperature_celsius, precipitation_probability;
    int32_t condition_code;
    uint32_t reserved;
} CdWeatherHour;

typedef struct CdWeatherInfo {
    CdDataInfo info;
    const char *city, *region, *local_time;
    double latitude, longitude, temperature_celsius, feels_celsius, humidity_percent, wind_mps;
    int32_t condition_code;
    uint32_t is_day, day_count, hour_count;
    const CdWeatherDay* days;
    const CdWeatherHour* hours;
} CdWeatherInfo;

typedef struct CdDataSource {
    const char *name, *url;
} CdDataSource;

typedef struct CdAirForecast {
    const char* date;
    double average, low, high;
} CdAirForecast;

/* pollutant_indices: PM2.5, PM10, O3, NO2, SO2, CO, as WAQI sub-indices,
   NOT concentrations. Test pollutant_valid_fields before displaying values. */
typedef struct CdAirInfo {
    CdDataInfo info;
    const char *city, *station, *station_url, *local_time, *dominant;
    double latitude, longitude, aqi, pollutant_indices[6];
    uint32_t pollutant_valid_fields, source_count, forecast_count, reserved;
    const CdDataSource* sources;
    const CdAirForecast* forecast;
} CdAirInfo;

typedef void(CD_CALL* CdLocationCallback)(void*, const CdLocationInfo*);
typedef void(CD_CALL* CdWeatherCallback)(void*, const CdWeatherInfo*);
typedef void(CD_CALL* CdAirCallback)(void*, const CdAirInfo*);

typedef struct CdInputServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, CdInputStatsCallback, void*, uint64_t*);
} CdInputServiceApi;

typedef struct CdSystemServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, uint32_t interval_ms, CdSystemCallback, void*, uint64_t*);
} CdSystemServiceApi;

typedef struct CdMediaServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const char* source_filter, CdMediaCallback, void*, uint64_t*);
    CdResult(CD_CALL* command)(void*, uint64_t subscription, uint32_t command, double value);
    CdResult(CD_CALL* command_request)(void*, uint64_t subscription, uint32_t command, double value,
                                       uint64_t* request_id);
} CdMediaServiceApi;

/* The existing 112-byte payload remains the prefix. A request ID identifies
   an accepted asynchronous command; zero means no command for this session. */
typedef struct CdMediaInfoEx {
    CdMediaInfo base;
    uint64_t command_request_id, session_generation;
} CdMediaInfoEx;

typedef struct CdMediaSession {
    const char *source_id, *name;
    uint32_t controls, reserved;
} CdMediaSession;

typedef void(CD_CALL* CdSessionsCallback)(void*, const CdMediaSession*, uint32_t count);

typedef struct CdSessionsServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, CdSessionsCallback, void*, uint64_t*);
} CdSessionsServiceApi;

typedef struct CdAudioOptions {
    uint32_t struct_size, fft_bands, max_fps, reserved;
    const char* output_device_id; /* empty: follow default output device */
} CdAudioOptions;

typedef struct CdAudioStatus {
    uint32_t struct_size, status, platform_error, reserved;
    uint64_t sampled_at_ms, dropped_pcm_frames, dropped_analysis_frames;
} CdAudioStatus;

typedef void(CD_CALL* CdAudioStatusCallback)(void*, const CdAudioStatus*);

typedef struct CdAudioServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const CdAudioOptions*, CdAudioCallback, void*, uint64_t*);
    CdResult(CD_CALL* subscribe_status)(void*, const CdAudioOptions*, CdAudioStatusCallback, void*, uint64_t*);
} CdAudioServiceApi;

typedef struct CdLocationServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const CdLocationQuery*, CdLocationCallback, void*, uint64_t*);
    CdResult(CD_CALL* refresh)(void*, uint64_t subscription);
} CdLocationServiceApi;

typedef struct CdWeatherServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const CdLocationQuery*, CdWeatherCallback, void*, uint64_t*);
    CdResult(CD_CALL* refresh)(void*, uint64_t subscription);
} CdWeatherServiceApi;

typedef struct CdAirServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const CdLocationQuery*, CdAirCallback, void*, uint64_t*);
    CdResult(CD_CALL* refresh)(void*, uint64_t subscription);
} CdAirServiceApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
