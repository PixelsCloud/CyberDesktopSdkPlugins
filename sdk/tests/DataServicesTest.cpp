#include <iostream>
#include <optional>
#include <stdexcept>

#include "sdk/cpp/Services.h"
#include "tools/Runtime.h"

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
}  // namespace

int main() {
    try {
        cdsdk::MockHost host;
        cdsdk::Services services(&host.api, &host);
        CdInputServiceApi api{};
        check(services.query(CD_SERVICE_INPUT, api) == CD_OK && api.struct_size == sizeof(api) && api.subscribe,
              "input service discovery");
        check(services.query("unknown", api) == CD_ERROR_UNSUPPORTED, "unknown service");
        check(services.query(CD_SERVICE_INPUT, api, 99) == CD_ERROR_VERSION, "unsupported service version");
        check(host.extended.query_service(&host, CD_SERVICE_INPUT, 1, &api, sizeof(api) - 1) == CD_ERROR_BUFFER,
              "short table is rejected");
        check(host.extended.query_service(&host, nullptr, 1, &api, sizeof(api)) == CD_ERROR_ARGUMENT, "null ID");
        CdHostApi old = host.api;
        old.struct_size = sizeof(CdInputStatsHostApi);
        check(cdsdk::Services(&old, &host).query(CD_SERVICE_INPUT, api) == CD_ERROR_UNSUPPORTED, "old host size");

        int deliveries = 0;
        {
            auto first = services.input([&](const CdInputStats& value) {
                check(value.status & CD_INPUT_STATS_ACTIVE, "active stats");
                ++deliveries;
            });
            check(first.result() == CD_OK, "typed subscription result");
            cdsdk::InputWatch moved(std::move(first));
            host.tick(0);
            check(deliveries == 1 && host.subscriptions.size() == 1, "move preserves callback and token");
            host.paused = true;
            host.tick(1);
            check(deliveries == 1, "paused mock does not deliver");
            host.paused = false;
            host.tick(2);
            check(deliveries == 2, "resumed mock delivers");
        }
        host.tick(3);
        check(deliveries == 2 && host.subscriptions.empty(), "RAII removes pending callback");

        CdSystemServiceApi system{};
        CdMediaServiceApi media{};
        CdAudioServiceApi audio{};
        check(services.query(CD_SERVICE_SYSTEM, system) == CD_OK && services.query(CD_SERVICE_MEDIA, media) == CD_OK &&
                  services.query(CD_SERVICE_FFT, audio) == CD_OK,
              "local service tables");

        uint64_t unused = 0;
        check(services.query(CD_SERVICE_INPUT, api) == CD_OK, "restore valid input table");
        check(api.subscribe(&host, nullptr, nullptr, &unused) == CD_ERROR_ARGUMENT, "null callback rejected");
        host.capabilities.erase("INPUT_STATS");
        check(services.query(CD_SERVICE_INPUT, api) == CD_ERROR_UNSUPPORTED, "discovery capability gate");
        check(cdsdk::MockHost::inputStats(
                  &host,
                  [](void*, const CdInputStats*) {
                  },
                  nullptr, &unused) == CD_ERROR_UNSUPPORTED,
              "direct function capability gate");
        host.capabilities.insert("INPUT_STATS");
        auto empty = services.input({});
        check(empty.result() == CD_ERROR_ARGUMENT, "empty C++ callback rejected");
        cdsdk::InputWatch selfRemoving;
        selfRemoving = services.input([&](const auto&) {
            selfRemoving.reset();
        });
        host.tick(5);
        check(host.subscriptions.empty(), "RAII reset during callback keeps state alive");

        CdLocationQuery query{
            sizeof(query), CD_LOCATION_CITY, 0, 0, "Borrowed City", nullptr, "default", "en", 900, 3, 24, 0, "GB"};
        std::optional<cdsdk::WeatherSnapshot> weatherValue;
        std::optional<cdsdk::AirSnapshot> airValue;
        auto weatherWatch = services.weather(query, [&](const auto& value) {
            weatherValue = value;
        });
        auto airWatch = services.air(query, [&](const auto& value) {
            airValue = value;
        });
        auto locationWatch = services.location(query, [](const auto& value) {
            check(value.city == "Borrowed City" && value.valid(), "owned location snapshot");
        });
        check(weatherWatch.result() == CD_OK && airWatch.result() == CD_OK && locationWatch.result() == CD_OK,
              "all network tables subscribe");
        host.tick(6);
        check(weatherValue && weatherValue->valid() && weatherValue->city == "Borrowed City" &&
                  weatherValue->days.size() == 3 && weatherValue->hours.size() == 24 && airValue &&
                  airValue->sources.front().name == "Demo Observatory",
              "owned arrays survive callback");
        CdWeatherServiceApi weatherApi{};
        check(services.query(CD_SERVICE_WEATHER, weatherApi) == CD_OK &&
                  weatherApi.refresh(&host, weatherWatch.token()) == CD_ERROR_BUSY,
              "mock refresh cooldown");
        host.tick(40);
        host.dataError = CD_DATA_NETWORK;
        check(weatherApi.refresh(&host, weatherWatch.token()) == CD_OK, "refresh after cooldown");
        host.tick(41);
        check(weatherValue->status == CD_DATA_STALE && weatherValue->valid() && weatherValue->error == CD_DATA_NETWORK,
              "network failure retains stale data");
        host.hasNetworkData = false;
        host.tick(80);
        check(weatherApi.refresh(&host, weatherWatch.token()) == CD_OK, "second refresh after cooldown");
        host.tick(81);
        check(weatherValue->status == CD_DATA_ERROR && !weatherValue->valid(), "failure without cached values");
        weatherWatch.reset();
        airWatch.reset();
        locationWatch.reset();
        query.mode = CD_LOCATION_STATION;
        query.station = "@100001";
        check(services.weather(query,
                               [](const auto&) {
                               })
                      .result() == CD_ERROR_ARGUMENT,
              "station is exclusive to air quality");

        int audioDeliveries = 0, sessionDeliveries = 0;
        const CdAudioOptions options{sizeof(options), 32, 10, 0, nullptr};
        auto spectrumWatch = services.audio(CD_SERVICE_FFT, options, [&](const auto& value) {
            check(value.count == 32 && value.channels == 0 && value.samples.size() == 32, "owned spectrum");
            ++audioDeliveries;
        });
        auto sessionsWatch = services.sessions([&](const auto& value) {
            check(value.entries.size() == 2 && value.entries[0].sourceId == "QQMusic.exe", "owned session list");
            ++sessionDeliveries;
        });
        check(spectrumWatch.result() == CD_OK && sessionsWatch.result() == CD_OK, "C++ audio and sessions");
        host.tick(82);
        host.tick(82.05);
        host.tick(82.11);
        check(audioDeliveries == 2 && sessionDeliveries == 1, "individual delivery frequency");
        spectrumWatch.reset();
        sessionsWatch.reset();
        uint32_t audioState = CD_DATA_LOADING;
        auto statusWatch = services.audioStatus(CD_SERVICE_FFT, options, [&](const auto& value) {
            audioState = value.status;
        });
        host.tick(83);
        check(statusWatch.result() == CD_OK && audioState == CD_DATA_READY, "audio status ready");
        host.hasAudioDevice = false;
        host.tick(84);
        check(audioState == CD_DATA_UNAVAILABLE, "audio device unavailable is distinct from silence");
        statusWatch.reset();

        std::optional<cdsdk::MediaSnapshot> mediaValue;
        auto mediaWatch = services.media("auto", [&](const auto& value) {
            mediaValue = value;
        });
        host.tick(83);
        uint64_t request = 0;
        check(services.command(mediaWatch.token(), CD_MEDIA_SEEK, 20, &request) == CD_ERROR_UNSUPPORTED && !request,
              "unsupported command is rejected");
        check(services.command(mediaWatch.token(), CD_MEDIA_TOGGLE, 0, &request) == CD_OK && request,
              "asynchronous command has request ID");
        host.tick(84);
        check(mediaValue->commandRequestId == request && mediaValue->commandResult == CD_ERROR_BUSY,
              "queued command correlation");
        host.tick(85);
        check(mediaValue->commandRequestId == request && mediaValue->commandResult == CD_OK,
              "command completion correlation");
        mediaWatch.reset();
        auto specific = services.media("sPoTiFy", [&](const auto& value) {
            mediaValue = value;
        });
        host.tick(86);
        check(mediaValue->sourceId == "Spotify.exe" && (mediaValue->controls & CD_MEDIA_CAN_SEEK),
              "case-insensitive source selection");
        check(services.command(specific.token(), CD_MEDIA_SEEK, 20, &request) == CD_OK, "supported seek command");
        ++host.mediaGeneration;
        host.tick(87);
        check(mediaValue->commandRequestId == 0 && mediaValue->sessionGeneration == host.mediaGeneration,
              "old command is invalidated when session generation changes");
        host.hasMediaSession = false;
        host.tick(88);
        check(mediaValue->sourceId.empty() && !mediaValue->controls && mediaValue->title.empty(),
              "missing session clears old data");
        specific.reset();
        check(host.subscriptions.empty(), "all services release cleanly");
        std::cout << "PASS: all service contracts, capability gates, owned arrays, RAII self removal, failures and "
                     "commands\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
