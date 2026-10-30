# Host data services (SDK 1.1.0)

The application owns all producers. A plugin subscribes to snapshots; it does not open Raw Input, WASAPI, media sessions or HTTP clients. Matching subscriptions share a producer and network request. Official cards and third-party plugins use the same interfaces. The public SDK contains contracts and synthetic data only; private providers, credentials and desktop sources are excluded.

## Discover and declare

Include `sdk/services/DataServices.h` from C or `sdk/cpp/Services.h` from C++20. Check ABI 1 and the 88-byte `CdServiceHostApi` prefix before casting. `query_service(context, id, version, caller_table, sizeof(table))` returns a caller-owned function table. All current services have version 1. Unknown IDs, undeclared capabilities, unknown versions and short buffers return errors without creating a subscription.

| ID | Capability | Table / payload |
| --- | --- | --- |
| `input.stats` | `INPUT_STATS` | `CdInputServiceApi` / `CdInputStats` |
| `system.telemetry` | `SYSTEM_INFO` | `CdSystemServiceApi` / `CdSystemInfoEx` |
| `media.playback` | `MEDIA_CONTROL` | `CdMediaServiceApi` / `CdMediaInfoEx` |
| `media.sessions` | `MEDIA_CONTROL` | `CdSessionsServiceApi` / borrowed array |
| `audio.spectrum` | `AUDIO_FFT` | `CdAudioServiceApi` / float bands, `CdAudioStatus` |
| `audio.pcm` | `AUDIO_PCM` | `CdAudioServiceApi` / stereo PCM, `CdAudioStatus` |
| `audio.ambient` | `MEDIA_CONTROL` | `CdAmbientApi` / `CdAmbientSnapshot` (include `sdk/services/AmbientAudio.h`) |
| `location.current` | `LOCATION` | `CdLocationServiceApi` / `CdLocationInfo` |
| `weather.forecast` | `WEATHER` | `CdWeatherServiceApi` / `CdWeatherInfo` |
| `air.quality` | `AIR_QUALITY` | `CdAirServiceApi` / `CdAirInfo` |

Declare capabilities and indispensable services in your protobuf descriptor:

```protobuf
capabilities: WEATHER
capabilities: AIR_QUALITY
required_services { id: "weather.forecast" version: 1 }
required_services { id: "air.quality" version: 1 }
```

The host checks requirements before calling your factory. An optional service should omit `required_services` and handle `CD_ERROR_UNSUPPORTED` with an appropriate display. The minimum matching application is the 2026-10-14 data-services build with this extension; an older ABI 1 host can still load older prefix-only plugins, but cannot satisfy the new requirements. SDK version, C ABI, package format and individual service version are separate numbers.

## Ownership and lifecycle

Registration, refresh, commands and unsubscription run on the UI thread. Callbacks run on that thread and never wait for network or capture work. All pointers in a callback are borrowed until it returns. Copy strings, arrays and cover pixels you retain. Never call a service from a worker thread. `request_redraw` marks only your instance dirty; call it when visible content actually changes.

Initial callbacks are deferred. A callback may unregister itself or another subscription. Unregistration is idempotent and suppresses pending callbacks. Unregister on pause and before destroy; resubscribe on resume. Producers stop when unused, except input aggregate recording, which continues until application exit. The host cleans up remaining instance tokens, but explicit release remains required for reusable instances.

`cdsdk::Services` offers `input`, `system`, `media`, `sessions`, `audio`, `audioStatus`, `location`, `weather`, `air` and `command`. Each `Watch` owns a move-only RAII subscription and callback state. Its callback receives an owned snapshot, including copied arrays. Check `.result()` and release with `.reset()`; scope destruction unregisters automatically. Move assignment releases the previous token. Empty callbacks are rejected; exceptions inside a C++ callback become `CD_ERROR_INTERNAL` and do not cross the C boundary.

```cpp
cdsdk::Services services(context->host, context->host_context);
CdLocationQuery location{sizeof(location), CD_LOCATION_DEFAULT, 0, 0,
  nullptr, nullptr, "default", "en", 900, 5, 24, 0, nullptr};
auto weather = services.weather(location, [&](const cdsdk::WeatherSnapshot& value) {
  // Retain a copy in your instance, compare visible fields, then request_redraw.
  if (value.valid()) { /* display temperatureCelsius and hours */ }
});
auto air = services.air(location, [&](const cdsdk::AirSnapshot& value) {
  if (value.valid()) { /* display aqi, respecting pollutantValidFields */ }
});
```

Store these watches as instance members, with the host pointer alive until their destructors finish. The three complete examples demonstrate subscriptions, standard per-instance settings, font selection, zh-CN / en labels, local redraw, pause/resume and destruction. The host persists settings separately from opaque plugin business state.

## States, time and units

Network payloads begin with `CdDataInfo`. Test `CD_DATA_VALID` independently from status: `READY` is a good snapshot, `STALE` retains prior valid data after a failed refresh, `LOADING` has no completed data yet, `UNAVAILABLE` lacks a resource, and `ERROR` has no valid values. Errors distinguish network, location, rate limit, credentials, authorization, malformed response and no data. An absent measurement must be shown as unknown, not measured zero.

`observed_at_utc_ms` and `fetched_at_utc_ms` are Unix UTC milliseconds. Zero observation time means the provider did not supply a usable timestamp; it is not replaced with download time. `sequence` is monotonic per resource. Local display strings include the provider's local time. System, media and audio sample timestamps are monotonic Windows milliseconds, not Unix time. Daily forecast dates are local calendar dates; hourly forecast timestamps are UTC.

Weather values use Celsius, humidity and precipitation probability percent, wind **m/s**, WMO weather codes and degrees latitude/longitude. Daily / hourly precipitation `-1` means unavailable. Requests support 1–14 daily entries (zero selects 5) and 0–168 hourly entries. Returned counts can be shorter; do not assume fixed array sizes. Current weather validity is independent from how many forecast entries exist.

AQI and the six pollutant fields are WAQI US-AQI **sub-indices**, never concentrations. Pollutant order: PM2.5, PM10, O3, NO2, SO2, CO. Check each bit in `pollutant_valid_fields`; missing pollutants remain unknown. Display the station, its URL and attribution sources where your layout permits. Forecast values are indices as well. AQI is a station measurement, not a whole-city average.

System telemetry preserves CPU / memory / GPU / CPU-temperature validity bits. CPU / GPU utilization is percent, memory is bytes, network rates are bytes/second and CPU temperature is Celsius. Missing CPU temperature may be shown as a labeled placeholder with its validity bit clear.

Input data contains only today / total / current-process keyboard and mouse counts. Keyboard auto-repeat is suppressed; five mouse buttons count on down, and a double click counts twice. Movement, scrolling, key identities and text are not exposed. Today rolls over at local midnight; totals are persisted privately by the host.

## Location, cache and credentials

`DEFAULT` follows the application's shared default location; `AUTO` explicitly uses approximate IP location; `CITY` uses a name and optional two-letter country filter; `COORDINATES` uses numeric latitude/longitude. `STATION` is accepted only by air quality. Queries are copied during registration. IP results can reflect a VPN/proxy; failures never silently select Beijing. Display language and temperature units do not duplicate numeric HTTP requests; provider place names are canonical, while your labels are localized per instance.

Refresh intervals clamp to 300–10800 seconds. Equivalent locations, service and credential profile share resources; the smallest active interval and largest forecast range are used. Identical in-flight requests merge. Manual refresh has a 30-second resource cooldown and may return `CD_ERROR_BUSY`; do not loop on that result. Network calls use 20-second connect, 60-second request and 45-second slow-transfer protection, with at most 20 retries for temporary failures and cancellable backoff. Parameter, authentication and rate-limit errors do not receive those temporary-error retries.

Weather may use bounded host disk cache; AQI uses host memory cache. Canceling the last subscription never waits for HTTP on the UI thread. Changing the default rebinds followers and discards obsolete results; explicitly selected locations remain independent.

`credential_id` is a private host profile ID, such as `default`. The application Settings → Data services page maps it to a local credentials file. Tokens and private paths are not supplied to plugins, snapshots, SDK mocks or packages. Missing profiles return `CD_DATA_CREDENTIALS_MISSING`. Service availability does not grant commercial API rights; retain Open-Meteo / WAQI attribution and follow the provider's terms for your product.

## Media and audio

The 2026-10-15 host adds optional `audio.ambient` version 1 for natural-sound playback. Query `CdAmbientApi` from `sdk/services/AmbientAudio.h`, declare `MEDIA_CONTROL`, and require the service if sound is indispensable. `open` returns an instance-owned token: an empty key is private, while `rain`, `fire` and `stars` join shared scenes. Track order is rain, ocean, stream, birds, fireplace, wind. Configure finite 0–1 master/track levels, at most four nonzero tracks, and a 0–240 minute timer (0 is unlimited). Invalid configurations leave all settings unchanged. Commands are play, pause and stop; errors are visible in snapshots.

Playback defaults to stopped and 25% master. Decode and device work runs in the host background thread using miniaudio; UI calls only update control state. Fades smooth playback and volume changes, and the last ten seconds of a timer fade out. Snapshot deadlines use UTC epoch seconds and keep counting while paused or hidden. Keep the audio token when display animation pauses; close it at destruction. Closing the last scene owner stops playback, and application restart never resumes sound automatically. SDK tools provide silent controls and never play real audio. Sound assets and attribution ship with the host in `assets/ambient`; they are not part of the public SDK.

Media commands target the subscription's last delivered session and generation. Check control bits before sending a command. `command_request` / `Services.command` returns a unique nonzero request ID for an accepted request. The extended payload reports that ID with `CD_ERROR_BUSY` while queued and then a completion result. A changed source/generation invalidates a previous command. Acceptance does not guarantee that the player executes it. The unchanged 112-byte media prefix and legacy command entry remain readable by existing ABI 1 consumers.

FFT and PCM describe the **system output mix**, not the media subscription's selected player. Audio options select default or explicit WASAPI output device ID and 1–30 maximum callbacks per second. FFT requests use 1–128 bands; PCM uses zero bands. FFT floats are normalized display magnitudes; PCM is interleaved stereo floating point, with callback count measured in frames and sample rate supplied separately. The audio callback's pointers are borrowed; `AudioWatch` copies them for retention. `AudioView` itself is only a borrowed view.

`subscribe_status` / `audioStatus` reports capture loading, ready or unavailable, platform error, monotonic sample time and cumulative dropped PCM / analysis frames. Silence remains valid zero samples and decaying zero FFT, while unavailable devices are identified by status. Capture retains at most two seconds of PCM; analysis has one pending input and one latest result. Equal device/band requests share capture and analysis. There is no unbounded callback queue. Unused capture stops; default-device changes and transient failures rebuild the capture session.

SDK CLI and preview never monitor actual input, call HTTP, require a token or control real music players. Their virtual-time fixtures cover errors and unsupported commands. Production integration must use a matching host separately.

## Weather presentation helpers

Optional C++ helpers in `sdk/WeatherPresentation.h` are linked through `CyberDesktopSDK::Core`
(or the SDK umbrella target). `cdsdk::weather::iconName(code, day)` maps WMO condition codes to
Meteocons asset basenames; `condition(code, english)` returns the existing Chinese or English label.
Unknown codes return `not-available` and the unknown-weather label. These functions do not fetch data,
load images or embed icon assets. Consumers keep their own licensed resources and localization.
Provider HTTP clients, JSON parsing, credentials and network tests belong to the private host;
plugins subscribe to `weather.forecast` / `air.quality` and only maintain presentation state.

## Host callback table lifetime

`cdsdk::Services` copies the validated host callbacks instead of retaining the caller's API table address.
A `Subscription` keeps the cancellation callback and cancels its token at most once, including during
reentrant cancellation. The opaque host context remains borrowed: it and the callback code must stay
alive until all associated subscriptions are destroyed. Copying callbacks does not extend host lifetime.


## Shared local volume capacity

Include `sdk/services/Storage.h` for `storage.volume` version 1, or use
`cdsdk::Services::volume(query, callback)` and the owning `VolumeWatch` C++ wrapper.
Declare `SYSTEM_INFO` capability. This is an additive service; the base plugin ABI
and existing data tables do not change.

`CdVolumeQuery` accepts a Windows local drive root (`C:\` or `c:/`) and a refresh
period. Roots are normalized to uppercase/backslash; other paths and UNC paths
are rejected. Zero seconds selects 30; other periods are clamped to 10–3600.
Mapped network drives return `CD_VOLUME_REMOTE` without requesting their capacity.

The host copies the query, queues the first callback, combines subscriptions for
the same drive and schedules at the fastest requested interval. Capacity probing
runs outside the UI thread, with at most two probes in flight for the process.
Successful values expose total, free, and quota-aware available bytes separately.
An unavailable first sample has no valid data. A failed refresh returns STALE with
last successful values; `observed_at_utc_ms` remains that sample's time and
`fetched_at_utc_ms` records the failed attempt. Error codes are `CD_VOLUME_UNAVAILABLE`
and `CD_VOLUME_REMOTE`. `info.struct_size` is `sizeof(CdVolumeInfo)`.

Callbacks and unsubscribe run on the UI thread. Strings are borrowed for the
callback only; `VolumeSnapshot` owns its root string. Cancel on pause/destruction. Last valid values are retained for at most the 26
drive-letter roots, without idle polling, so resume can reuse them; no callback may use its user pointer after unsubscribe returns. No disk worker,
polling timer or fallback system call belongs in a plugin. Unsupported hosts return
`CD_ERROR_UNSUPPORTED`, so consumers can display unavailable data.

SDK preview uses deterministic 512 GiB / 220 GiB free / 200 GiB available data,
virtual-time refresh, and Normal/Loading/Empty/Error scenarios. It does not read
the developer's disks. The private host native provider is tested separately.

## Shared RSS, news and trending content

Include `sdk/services/Content.h`, declare `CONTENT`, and discover `content.feeds`
version 1. `Services::content` returns a `ContentWatch`; `ContentSnapshot` owns all
strings and items. The existing host ABI remains unchanged. Hosts without this
service return `CD_ERROR_UNSUPPORTED`; plugins show unavailable content without
starting a fallback HTTP worker.

`CdContentQuery.kind` selects RSS/Atom feeds, Hacker News, or GitHub Trending.
Feeds accept 1–6 HTTP(S) URLs, each at most 2048 input bytes, without credentials.
The host normalizes scheme/host, default ports, path segments and fragments,
deduplicates URLs, and preserves their ordering for interleaving. GitHub accepts
`daily`/`weekly`/`monthly` and the plugin's existing 14 language filters or `all`;
Hacker News accepts `top`/`new`/`best`. Parameters are copied before subscribe
returns. Zero refresh seconds selects 900; the range is 300–10800 seconds.

Subscriptions with the same normalized query share a producer and the shortest
requested period. This includes a single-feed RSS card and a news card pointing
to that same feed. Different ordered multi-feed combinations remain distinct
queries; overlapping subsets are not an endpoint-level response cache. The host
retains at most 64 query snapshots (30 bounded items each), evicts unused entries
by last use, and limits subscriptions to 1024 and concurrent fetch jobs to two.
No per-plugin network threads, polling timers, or startup source enumeration are
needed. The cache is bounded process memory, not a new database; per-card saved
items/read markers remain plugin state owned by the existing Desktop persistence.

Initial delivery is queued on the UI thread. Each callback borrows its data until
return; `info.struct_size` is `sizeof(CdContentInfo)`. An empty successful feed is
READY and VALID. Failed refreshes preserve the last successful payload as STALE;
`observed_at_utc_ms` is the last successful observation and `fetched_at_utc_ms`
records the latest completed attempt. `partial` means some sources failed while
others supplied usable content. The stable `error_code` is one of `network`,
`rate_limited`, `response_too_large`, `cancelled`, `no_feeds`, `invalid_feed`, or
`trending_unavailable`. Unchanged content/status does not deliver another callback.

`refresh` takes this instance's subscription token. Refresh throttling is shared:
at least 15 seconds between attempts, or 15 minutes after HTTP 429. Automatic
failures use bounded exponential backoff. Last unsubscribe cancels pending work
and stops scheduling; a callback may unsubscribe itself safely. A cancelled
background request cannot call a destroyed plugin. Resume can reuse the cached
snapshot before its next refresh is due.

`open_url` is a separate explicit user action, never a side effect of receiving a
snapshot. It accepts only validated HTTP(S) URLs and queues at most eight commands,
including the active command, independently of fetch jobs. It executes in the
host background; success acknowledges queueing rather than browser completion.
SDK preview records the command without launching a browser and provides fixed
Normal/Loading/Empty/Error content, with no external requests.

The Desktop provider reuses static CPR/libcurl, TinyXML2 and Lexbor. Each decoded
response is capped at 4 MiB, redirects stay HTTP(S) and are capped at four, each
request has a 10-second timeout, and a multi-request query has a 30-second budget
checked between requests and by the cancellation callback. DTDs are rejected;
HTML excerpts become plain text. Tests use injected responses, not public sites.


## Photo library

`media.library` v1 (`sdk/services/Library.h`) requires `MEDIA_LIBRARY`. Pass an
absolute UTF-8 directory (at most 4096 bytes), or an empty string for Pictures,
a recursive flag, and a physical-pixel longest edge in 1–2048. Compute that edge
from the actual visible photo region and DPI, after the first frame is known.
The host copies the query before returning. All API calls and queued callbacks
use the UI thread; directory enumeration and EXIF-aware decoding use workers.

Each callback delivers one selected-photo page, not an entire file list or
full-resolution album. Discovery completes its bounded scan before publishing
the first page; this is not streaming enumeration. `index` and `count` describe
the current directory snapshot; they are not stable file identifiers. `select`
changes only this subscription's index and dimensions, without rescanning.
Paths and tightly packed premultiplied BGRA8 pixels are borrowed until callback
return. The current adapter copies changed pixels into a rendering-budget lease;
it does not claim zero-copy sharing across plugin DLLs.

Empty READY is valid. Loading waits for a result; errors preserve the previous
usable image as STALE where available. `partial` means discovery hit a limit or
could only read part of the directory. Failures include a stable code and a
locatable directory/photo path. These local snapshots currently leave the common
UTC observation fields zero (unavailable); `sequence` orders callbacks and
`revision` reflects file modification time and size. Metadata-preserving edits
cannot be distinguished by this revision; no file-content hashing is performed.

The Desktop provider shares normalized directory/recursive queries, with eight
cached directory snapshots, 64 subscriptions and two concurrent scans. Each scan
is limited to 50,000 examined entries, 8,192 photos, 4 MiB of stored path text,
recursive depth four, and a ten-second cooperative budget. Reparse points are
not traversed. Native filesystem calls themselves may block a worker beyond
that budget; the UI never joins that worker during cancellation or destruction.
Manual refresh has a shared 15-second floor; automatic discovery runs at most
once per minute while subscribed. Last unsubscribe stops scheduling and cancels
in-flight discovery. No new database is created or opened across processes.

Decoding reuses CyberCanvas's shared 64 MiB/128-entry image cache and single
worker. Photo requests apply EXIF orientation before proportional resizing and
reject sources above 100 megapixels; skin/atlas decoding is unchanged. Failed
photos are skipped, bounded to 32 attempts per selection. Refresh can retry a
failed decode. The plugin's current/transition copies participate in the host
render-memory budget; copy-budget failures retry with 1–30-second backoff.
Animation and slideshow position remain per instance. Static/paused albums have
no 33 ms result polling; only active transitions or photo motion use that cadence.

SDK preview supplies deterministic Normal/Loading/Empty/Error pixels, with no
real directory access. Opening the original image remains an explicit
`app.image_viewer` action. Public plugin tests use this mock; private host tests
exercise enumeration, shared decode, cancellation, EXIF and scan concurrency.
