#include "tools/Runtime.h"

#include <string_view>

#include "tools/PreviewServices.h"
#include "tools/WindowsApartment.h"

namespace cdsdk {
MockHost::MockHost()
    : lifetime(std::make_shared<const bool>(true)),
      apartment(std::make_unique<WindowsApartment>()),
      preview(std::make_unique<PreviewServices>(*this)) {}

MockHost::~MockHost() = default;

CdResult CD_CALL MockHost::environment(void* context, CdEnvironmentSnapshot* output) {
    if (!context || !output || output->struct_size < sizeof(*output)) {
        return CD_ERROR_ARGUMENT;
    }
    const auto& host = *static_cast<MockHost*>(context);
    CdEnvironmentSnapshot result{sizeof(result), 1};
    result.monotonic_ms = uint64_t(std::max(0., host.virtualTime) * 1000);
    result.unix_ms = host.fixedUnixMs.value_or(host.baseUnixMs + result.monotonic_ms);
    result.utc_offset_minutes = host.utcOffsetMinutes;
    result.theme = host.theme;
    std::memcpy(result.locale, host.locale.data(), std::min(host.locale.size(), sizeof(result.locale) - 1));
    *output = result;
    return CD_OK;
}

void CD_CALL MockHost::unregisterDiagnostic(void* context, uint64_t token) {
    if (context) {
        static_cast<MockHost*>(context)->diagnostics.detach(token);
    }
}

auto MockHost::uiFocus(bool focused) -> void {
    focused &= !paused;
    if (uiGranted == focused) {
        return;
    }
    uiGranted = focused;
    if (uiCallback) {
        const CdUiEvent event{sizeof(event), CD_UI_FOCUS, 0, 0, 0, 0, focused ? 1. : 0.};
        uiCallback(uiUser, &event);
    }
}

auto MockHost::uiKey(uint32_t key, uint32_t modifiers) -> bool {
    const CdUiEvent event{sizeof(event), CD_UI_KEY, key, modifiers};
    return !paused && uiGranted && uiCallback && uiCallback(uiUser, &event) == CD_OK;
}

CdResult CD_CALL MockHost::uiConnect(void* context, CdUiCallback callback, void* user) {
    if (!context) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    host.uiFocus(false);
    host.uiCallback = callback;
    host.uiUser = user;
    host.semantics.clear();
    host.semanticStrings.clear();
    return CD_OK;
}

CdResult CD_CALL MockHost::uiRequest(void* context, uint32_t focused) {
    if (!context || focused > 1) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    if (!host.uiCallback || host.paused) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (host.requestKeyboardFocus) {
        host.requestKeyboardFocus(focused != 0);
    } else {
        host.uiFocus(focused != 0);
    }
    return CD_OK;
}

auto MockHost::selectedMedia(const Subscription& subscription) const -> std::string {
    if (!hasMediaSession || scenario == MockScenario::Empty)
        return {};
    auto filter = subscription.sourceFilter;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char value) {
        return char(std::tolower(value));
    });
    if (filter.empty() || filter == "auto")
        return currentMediaSource;
    for (const auto& source : {std::string("QQMusic.exe"), std::string("Spotify.exe")}) {
        auto normalized = source;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) {
            return char(std::tolower(value));
        });
        if (normalized.find(filter) != std::string::npos)
            return source;
    }
    return {};
}

void CD_CALL MockHost::redraw(void* context) {
    static_cast<MockHost*>(context)->dirty = true;
}

void CD_CALL MockHost::log(void*, uint32_t, const char*, uint32_t) {}

void CD_CALL MockHost::unsubscribe(void* context, uint64_t token) {
    if (context) {
        static_cast<MockHost*>(context)->subscriptions.erase(token);
        static_cast<MockHost*>(context)->preview->unsubscribe(token);
    }
}

CdResult CD_CALL MockHost::command(void* context, uint64_t token, uint32_t command, double value) {
    return commandRequest(context, token, command, value, nullptr);
}

CdResult CD_CALL MockHost::commandRequest(void* context, uint64_t token, uint32_t command, double value,
                                          uint64_t* requestId) {
    if (requestId)
        *requestId = 0;
    if (!context || !std::isfinite(value) || command < CD_MEDIA_TOGGLE || command > CD_MEDIA_SEEK)
        return CD_ERROR_ARGUMENT;
    auto& host = *static_cast<MockHost*>(context);
    auto& subscriptions = host.subscriptions;
    const auto it = subscriptions.find(token);
    if (it == subscriptions.end() || !it->second.media || host.paused)
        return CD_ERROR_ARGUMENT;
    if (it->second.selectedSource.empty() || it->second.selectedSource != host.selectedMedia(it->second) ||
        it->second.selectedGeneration != host.mediaGeneration ||
        (command == CD_MEDIA_SEEK && it->second.selectedSource != "Spotify.exe"))
        return CD_ERROR_UNSUPPORTED;
    if (it->second.commandResult == CD_ERROR_BUSY)
        return CD_ERROR_BUSY;
    it->second.requestId = host.nextRequest++;
    it->second.commandResult = CD_ERROR_BUSY;
    it->second.nextTime = 0;
    if (requestId)
        *requestId = it->second.requestId;
    return CD_OK;
}

CdResult CD_CALL MockHost::audioService(void* context, const CdAudioOptions* options, CdAudioCallback callback,
                                        void* user, uint64_t* token) {
    if (!options || options->struct_size < sizeof(CdAudioOptions) || options->max_fps < 1 || options->max_fps > 30 ||
        (options->output_device_id && std::strlen(options->output_device_id) > 512))
        return CD_ERROR_ARGUMENT;
    const auto result = audio(context, options->fft_bands, callback, user, token);
    if (result == CD_OK)
        static_cast<MockHost*>(context)->subscriptions.at(*token).interval = 1.0 / options->max_fps;
    return result;
}

CdResult CD_CALL MockHost::audioStatusService(void* context, const CdAudioOptions* options,
                                              CdAudioStatusCallback callback, void* user, uint64_t* token) {
    if (!callback)
        return CD_ERROR_ARGUMENT;
    const auto result = audioService(
        context, options,
        [](void*, const float*, uint32_t, uint32_t, uint32_t) {
        },
        user, token);
    if (result == CD_OK) {
        auto& value = static_cast<MockHost*>(context)->subscriptions.at(*token);
        value.audio = nullptr;
        value.audioStatus = callback;
        value.interval = 1;
    }
    return result;
}

CdResult CD_CALL MockHost::queryService(void* context, const char* id, uint32_t version, void* buffer, uint32_t size) {
    if (!context || !id || !buffer)
        return CD_ERROR_ARGUMENT;
    if (version != CD_SERVICE_VERSION)
        return CD_ERROR_VERSION;
    if (const auto result = static_cast<MockHost*>(context)->preview->query(id, version, buffer, size)) {
        return *result;
    }
    if (!std::strcmp(id, CD_SERVICE_INTERACTION)) {
        return writeService(
            CdInteractionApi{sizeof(CdInteractionApi), 1,
                             [](void* context, uint32_t enabled) -> CdResult {
                                 if (!context || enabled > 1) {
                                     return CD_ERROR_ARGUMENT;
                                 }
                                 auto& host = *static_cast<MockHost*>(context);
                                 host.streaming = enabled != 0;
                                 if (!enabled) {
                                     host.captured = false;
                                 }
                                 return CD_OK;
                             },
                             [](void* context, uint32_t captured) -> CdResult {
                                 if (!context || captured > 1) {
                                     return CD_ERROR_ARGUMENT;
                                 }
                                 auto& host = *static_cast<MockHost*>(context);
                                 if (captured && (!host.streaming || !host.deliveringPress || host.paused)) {
                                     return CD_ERROR_UNSUPPORTED;
                                 }
                                 host.captured = captured != 0;
                                 return CD_OK;
                             }},
            buffer, size);
    }
    if (!std::strcmp(id, CD_SERVICE_ENVIRONMENT)) {
        return writeService(CdEnvironmentApi{sizeof(CdEnvironmentApi), version, environment}, buffer, size);
    }
    if (!std::strcmp(id, CD_SERVICE_KEYBOARD)) {
        return writeService(CdKeyboardApi{sizeof(CdKeyboardApi), version, uiConnect, uiRequest, uiPublish}, buffer,
                            size);
    }
    if (!std::strcmp(id, CD_SERVICE_DIAGNOSTICS)) {
        return writeService(
            CdDiagnosticsApi{sizeof(CdDiagnosticsApi), version, registerDiagnostic, unregisterDiagnostic}, buffer,
            size);
    }
    const auto& caps = static_cast<MockHost*>(context)->capabilities;
    const auto allowed = [&](const char* name) {
        return caps.contains(name);
    };
    if (!std::strcmp(id, CD_SERVICE_AMBIENT)) {
        if (!allowed("MEDIA_CONTROL")) {
            return CD_ERROR_UNSUPPORTED;
        }
        return writeService(CdAmbientApi{sizeof(CdAmbientApi), version, ambientOpen, ambientClose, ambientConfigure,
                                         ambientCommand, ambientSnapshot},
                            buffer, size);
    }
    if ((!std::strcmp(id, CD_SERVICE_INPUT) && !allowed("INPUT_STATS")) ||
        (!std::strcmp(id, CD_SERVICE_SYSTEM) && !allowed("SYSTEM_INFO")) ||
        (!std::strcmp(id, CD_SERVICE_VOLUME) && !allowed("SYSTEM_INFO")) ||
        (!std::strcmp(id, CD_SERVICE_CONTENT) && !allowed("CONTENT")) ||
        (!std::strcmp(id, CD_SERVICE_LIBRARY) && !allowed("MEDIA_LIBRARY")) ||
        ((!std::strcmp(id, CD_SERVICE_MEDIA) || !std::strcmp(id, CD_SERVICE_SESSIONS)) && !allowed("MEDIA_CONTROL")) ||
        (!std::strcmp(id, CD_SERVICE_FFT) && !allowed("AUDIO_FFT")) ||
        (!std::strcmp(id, CD_SERVICE_PCM) && !allowed("AUDIO_PCM")) ||
        (!std::strcmp(id, CD_SERVICE_LOCATION) && !allowed("LOCATION")) ||
        (!std::strcmp(id, CD_SERVICE_WEATHER) && !allowed("WEATHER")) ||
        (!std::strcmp(id, CD_SERVICE_AIR) && !allowed("AIR_QUALITY")))
        return CD_ERROR_UNSUPPORTED;
    if (!std::strcmp(id, CD_SERVICE_INPUT))
        return writeService(CdInputServiceApi{sizeof(CdInputServiceApi), version, inputStats}, buffer, size);
    if (!std::strcmp(id, CD_SERVICE_LIBRARY)) {
        return writeService(
            CdLibraryServiceApi{sizeof(CdLibraryServiceApi), version, libraryService, librarySelect, libraryRefresh},
            buffer, size);
    }
    if (!std::strcmp(id, CD_SERVICE_CONTENT)) {
        return writeService(
            CdContentServiceApi{sizeof(CdContentServiceApi), version, contentService, contentRefresh, contentOpen},
            buffer, size);
    }
    if (!std::strcmp(id, CD_SERVICE_VOLUME)) {
        return writeService(CdVolumeServiceApi{sizeof(CdVolumeServiceApi), version, volumeService}, buffer, size);
    }
    if (!std::strcmp(id, CD_SERVICE_SYSTEM))
        return writeService(CdSystemServiceApi{sizeof(CdSystemServiceApi), version, system}, buffer, size);
    if (!std::strcmp(id, CD_SERVICE_MEDIA))
        return writeService(CdMediaServiceApi{sizeof(CdMediaServiceApi), version, media, command, commandRequest},
                            buffer, size);
    if (!std::strcmp(id, CD_SERVICE_FFT) || !std::strcmp(id, CD_SERVICE_PCM))
        return writeService(CdAudioServiceApi{sizeof(CdAudioServiceApi), version, audioService, audioStatusService},
                            buffer, size);
    if (!std::strcmp(id, CD_SERVICE_LOCATION))
        return writeService(
            CdLocationServiceApi{sizeof(CdLocationServiceApi), version, locationService, refreshService}, buffer, size);
    if (!std::strcmp(id, CD_SERVICE_WEATHER))
        return writeService(CdWeatherServiceApi{sizeof(CdWeatherServiceApi), version, weatherService, refreshService},
                            buffer, size);
    if (!std::strcmp(id, CD_SERVICE_AIR))
        return writeService(CdAirServiceApi{sizeof(CdAirServiceApi), version, airService, refreshService}, buffer,
                            size);
    if (!std::strcmp(id, CD_SERVICE_SESSIONS))
        return writeService(CdSessionsServiceApi{sizeof(CdSessionsServiceApi), version, sessionsService}, buffer, size);
    return CD_ERROR_UNSUPPORTED;
}

CdResult CD_CALL MockHost::ambientOpen(void* context, const char* key, uint64_t* token) {
    if (!context || !key || !token) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    *token = host.next++;
    CdAmbientSnapshot state{sizeof(state), CD_AMBIENT_STOPPED, 0, 0, {sizeof(CdAmbientConfig), 30, .25f, {}}, 0};
    state.config.levels[std::strcmp(key, "fire") == 0 ? 4 : std::strcmp(key, "stars") == 0 ? 5 : 0] = .6f;
    host.ambientGroups.emplace(*token, state);
    return CD_OK;
}

void CD_CALL MockHost::ambientClose(void* context, uint64_t token) {
    if (context) {
        static_cast<MockHost*>(context)->ambientGroups.erase(token);
    }
}

CdResult CD_CALL MockHost::ambientConfigure(void* context, uint64_t token, const CdAmbientConfig* config) {
    if (!context || !config || config->struct_size < sizeof(*config)) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    const auto found = host.ambientGroups.find(token);
    if (found == host.ambientGroups.end()) {
        return CD_ERROR_ARGUMENT;
    }
    int active = 0;
    if (!std::isfinite(config->master) || config->master < 0 || config->master > 1 || config->timer_minutes > 240) {
        return CD_ERROR_ARGUMENT;
    }
    for (float level : config->levels) {
        if (!std::isfinite(level) || level < 0 || level > 1) {
            return CD_ERROR_ARGUMENT;
        }
        active += level > 0;
    }
    if (active > 4) {
        return CD_ERROR_ARGUMENT;
    }
    if (found->second.config.timer_minutes != config->timer_minutes && found->second.status != CD_AMBIENT_STOPPED) {
        found->second.deadline = config->timer_minutes ? uint64_t(std::time(nullptr)) + config->timer_minutes * 60 : 0;
    }
    found->second.config = *config;
    return CD_OK;
}

CdResult CD_CALL MockHost::ambientCommand(void* context, uint64_t token, uint32_t command) {
    if (!context || command < CD_AMBIENT_PLAY || command > CD_AMBIENT_STOP) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    const auto found = host.ambientGroups.find(token);
    if (found == host.ambientGroups.end()) {
        return CD_ERROR_ARGUMENT;
    }
    auto& state = found->second;
    if (command == CD_AMBIENT_PLAY) {
        if (state.status == CD_AMBIENT_STOPPED) {
            state.deadline =
                state.config.timer_minutes ? uint64_t(std::time(nullptr)) + state.config.timer_minutes * 60 : 0;
        }
        state.status = CD_AMBIENT_PLAYING;
    } else {
        state.status = command == CD_AMBIENT_PAUSE ? CD_AMBIENT_PAUSED : CD_AMBIENT_STOPPED;
        if (command == CD_AMBIENT_STOP) {
            state.deadline = 0;
        }
    }
    host.dirty = true;
    return CD_OK;
}

CdResult CD_CALL MockHost::ambientSnapshot(void* context, uint64_t token, CdAmbientSnapshot* out) {
    if (!context || !out || out->struct_size < sizeof(*out)) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    const auto found = host.ambientGroups.find(token);
    if (found == host.ambientGroups.end()) {
        return CD_ERROR_ARGUMENT;
    }
    if (found->second.deadline && uint64_t(std::time(nullptr)) >= found->second.deadline) {
        found->second.status = CD_AMBIENT_STOPPED;
        found->second.deadline = 0;
    }
    *out = found->second;
    return CD_OK;
}

CdResult CD_CALL MockHost::libraryService(void* context, const CdLibraryQuery* query, CdLibraryCallback callback,
                                          void* user, uint64_t* token) try {
    if (!context || !query || query->struct_size < sizeof(*query) || !query->directory || !callback || !token ||
        !query->pixel_edge || query->pixel_edge > 2048 || query->recursive > 1 || query->reserved ||
        strnlen(query->directory, 4097) > 4096) {
        return CD_ERROR_ARGUMENT;
    }
    *token = 0;
    if (*query->directory && (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, query->directory, -1, nullptr, 0) ||
                              !utf8Path(query->directory).is_absolute())) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    if (!host.capabilities.contains("MEDIA_LIBRARY")) {
        return CD_ERROR_UNSUPPORTED;
    }
    Subscription value;
    value.library = callback;
    value.user = user;
    value.libraryEdge = query->pixel_edge;
    value.interval = 1e30;
    *token = host.next++;
    host.subscriptions.emplace(*token, std::move(value));
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::librarySelect(void* context, uint64_t token, uint64_t index, uint32_t edge) {
    if (!context || !edge || edge > 2048) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    const auto it = host.subscriptions.find(token);
    if (it == host.subscriptions.end() || !it->second.library) {
        return CD_ERROR_ARGUMENT;
    }
    it->second.libraryIndex = index % 3;
    it->second.libraryEdge = edge;
    it->second.nextTime = host.virtualTime;
    return CD_OK;
}

CdResult CD_CALL MockHost::libraryRefresh(void* context, uint64_t token) {
    if (!context) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    const auto it = host.subscriptions.find(token);
    if (it == host.subscriptions.end() || !it->second.library) {
        return CD_ERROR_ARGUMENT;
    }
    if (host.virtualTime - it->second.lastRefresh < 15) {
        return CD_ERROR_BUSY;
    }
    it->second.lastRefresh = host.virtualTime;
    it->second.nextTime = host.virtualTime;
    return CD_OK;
}

CdResult CD_CALL MockHost::contentService(void* context, const CdContentQuery* query, CdContentCallback callback,
                                          void* user, uint64_t* token) try {
    if (!context || !query || query->struct_size < sizeof(*query) || !callback || !token ||
        query->kind > CD_CONTENT_GITHUB || query->url_count > 6 ||
        (query->kind == CD_CONTENT_FEEDS && (!query->url_count || !query->urls))) {
        return CD_ERROR_ARGUMENT;
    }
    *token = 0;
    auto& host = *static_cast<MockHost*>(context);
    if (!host.capabilities.contains("CONTENT")) {
        return CD_ERROR_UNSUPPORTED;
    }
    for (uint32_t i = 0; i < query->url_count; ++i) {
        if (!query->urls || !query->urls[i] ||
            (!std::string_view(query->urls[i]).starts_with("https://") &&
             !std::string_view(query->urls[i]).starts_with("http://"))) {
            return CD_ERROR_ARGUMENT;
        }
    }
    Subscription value;
    value.user = user;
    value.content = callback;
    value.contentKind = query->kind;
    value.contentUrl = query->url_count ? query->urls[0] : "https://example.org/";
    value.interval = std::clamp(query->refresh_seconds ? query->refresh_seconds : 900u, 300u, 10800u);
    const auto id = host.next++;
    host.subscriptions.emplace(id, std::move(value));
    *token = id;
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::contentRefresh(void* context, uint64_t token) {
    if (!context) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    const auto it = host.subscriptions.find(token);
    if (it == host.subscriptions.end() || !it->second.content) {
        return CD_ERROR_ARGUMENT;
    }
    it->second.nextTime = 0;
    return CD_OK;
}

CdResult CD_CALL MockHost::contentOpen(void* context, const char* url) try {
    if (!context || !url ||
        (!std::string_view(url).starts_with("https://") && !std::string_view(url).starts_with("http://"))) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    if (!host.capabilities.contains("CONTENT")) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (host.openedUrls.size() >= 8) {
        return CD_ERROR_BUSY;
    }
    host.openedUrls.emplace_back(url);
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::volumeService(void* context, const CdVolumeQuery* query, CdVolumeCallback callback,
                                         void* user, uint64_t* token) try {
    if (!context || !query || query->struct_size < sizeof(*query) || !callback || !token || !query->root) {
        return CD_ERROR_ARGUMENT;
    }
    *token = 0;
    const auto* root = query->root;
    if (!root[0] || !root[1] || !root[2] || root[3] || root[1] != ':' || (root[2] != '/' && root[2] != '\\') ||
        !((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z'))) {
        return CD_ERROR_ARGUMENT;
    }
    auto& host = *static_cast<MockHost*>(context);
    if (!host.capabilities.contains("SYSTEM_INFO")) {
        return CD_ERROR_UNSUPPORTED;
    }
    Subscription value;
    value.user = user;
    value.volume = callback;
    value.volumeRoot = root;
    value.volumeRoot[0] = char(std::toupper(static_cast<unsigned char>(root[0])));
    value.volumeRoot[2] = '\\';
    value.interval = std::clamp(query->refresh_seconds ? query->refresh_seconds : 30u, 10u, 3600u);
    const auto id = host.next++;
    host.subscriptions.emplace(id, std::move(value));
    *token = id;
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::locationService(void* context, const CdLocationQuery* query, CdLocationCallback callback,
                                           void* user, uint64_t* token) {
    return network(context, query, user, token, callback, nullptr, nullptr);
}

CdResult CD_CALL MockHost::weatherService(void* context, const CdLocationQuery* query, CdWeatherCallback callback,
                                          void* user, uint64_t* token) {
    return network(context, query, user, token, nullptr, callback, nullptr);
}

CdResult CD_CALL MockHost::airService(void* context, const CdLocationQuery* query, CdAirCallback callback, void* user,
                                      uint64_t* token) {
    return network(context, query, user, token, nullptr, nullptr, callback);
}

CdResult CD_CALL MockHost::refreshService(void* context, uint64_t token) {
    if (!context)
        return CD_ERROR_ARGUMENT;
    auto& host = *static_cast<MockHost*>(context);
    const auto it = host.subscriptions.find(token);
    if (it == host.subscriptions.end() || (!it->second.location && !it->second.weather && !it->second.air))
        return CD_ERROR_ARGUMENT;
    if (host.virtualTime - it->second.lastRefresh < 30)
        return CD_ERROR_BUSY;
    it->second.lastRefresh = host.virtualTime;
    it->second.nextTime = 0;
    return CD_OK;
}

auto MockHost::tick(double seconds) -> void {
    const std::weak_ptr<const bool> alive = lifetime;
    virtualTime = seconds;
    preview->tick();
    if (alive.expired()) {
        return;
    }
    if (paused) {
        uiFocus(false);
        return;
    }
    // A callback may unsubscribe itself; copy entries, then check live tokens.
    const auto saved = subscriptions;
    for (const auto& [token, subscription] : saved) {
        if (!subscriptions.count(token))
            continue;
        if (seconds < subscription.nextTime)
            continue;
        if (scenario == MockScenario::Loading && (subscription.system || subscription.media || subscription.location ||
                                                  subscription.weather || subscription.air)) {
            continue;
        }
        subscriptions[token].nextTime = seconds + subscription.interval;
        if (subscription.library) {
            const bool ready = scenario == MockScenario::Normal || scenario == MockScenario::Empty;
            const uint32_t status = ready                               ? CD_DATA_READY
                                    : scenario == MockScenario::Loading ? CD_DATA_LOADING
                                                                        : CD_DATA_ERROR;
            const uint32_t width = scenario == MockScenario::Normal ? std::min(64u, subscription.libraryEdge) : 0;
            const uint32_t height = width ? std::max(1u, width * 3 / 4) : 0;
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            for (size_t i = 0; i < pixels.size(); i += 4) {
                pixels[i] = uint8_t(40 + subscription.libraryIndex * 50);
                pixels[i + 1] = 140;
                pixels[i + 2] = 220;
                pixels[i + 3] = 255;
            }
            const std::string path = "C:/preview/photo-" + std::to_string(subscription.libraryIndex) + ".png";
            const CdLibraryInfo info{{sizeof(CdLibraryInfo), status, ready ? CD_DATA_VALID : 0u,
                                      ready ? 0u : CD_DATA_NO_DATA, 0, 0, 1, "sdk-library-preview", ""},
                                     path.c_str(),
                                     status == CD_DATA_ERROR ? "preview_error" : "",
                                     subscription.libraryIndex,
                                     width ? 3u : 0u,
                                     1,
                                     width,
                                     height,
                                     0,
                                     0,
                                     pixels.data()};
            subscription.library(subscription.user, &info);
            continue;
        }
        if (subscription.content) {
            const bool ready = scenario == MockScenario::Normal || scenario == MockScenario::Empty;
            const uint32_t status = ready                               ? CD_DATA_READY
                                    : scenario == MockScenario::Loading ? CD_DATA_LOADING
                                                                        : CD_DATA_ERROR;
            const auto now = fixedUnixMs.value_or(baseUnixMs) + uint64_t(std::max(0., seconds) * 1000);
            const CdContentItem item{"Preview headline",
                                     "https://example.org/article",
                                     "Preview excerpt",
                                     "Preview feed",
                                     "C++",
                                     "120",
                                     "12",
                                     "https://example.org/discussion",
                                     "2026-01-01",
                                     1767225600,
                                     42,
                                     8};
            const CdContentInfo info{{sizeof(CdContentInfo), status, ready ? CD_DATA_VALID : 0u,
                                      status == CD_DATA_ERROR ? CD_DATA_NETWORK : 0u, ready ? now : 0, now, 1,
                                      "sdk-content-preview", subscription.contentUrl.c_str()},
                                     "Preview feed",
                                     status == CD_DATA_ERROR ? "network" : "",
                                     &item,
                                     scenario == MockScenario::Normal ? 1u : 0u,
                                     0};
            subscription.content(subscription.user, &info);
            continue;
        }
        if (subscription.volume) {
            const bool ready = scenario == MockScenario::Normal;
            const uint32_t status = ready                               ? CD_DATA_READY
                                    : scenario == MockScenario::Loading ? CD_DATA_LOADING
                                    : scenario == MockScenario::Empty   ? CD_DATA_UNAVAILABLE
                                                                        : CD_DATA_ERROR;
            const auto now = fixedUnixMs.value_or(baseUnixMs) + uint64_t(std::max(0., seconds) * 1000);
            constexpr uint64_t gib = 1024ull * 1024 * 1024;
            const CdVolumeInfo info{{sizeof(CdVolumeInfo), status, ready ? CD_DATA_VALID : 0u,
                                     status == CD_DATA_LOADING || ready ? 0u : CD_VOLUME_UNAVAILABLE, ready ? now : 0,
                                     now, uint64_t(std::max(0., seconds) * 1000) + 1, "sdk-volume-preview", ""},
                                    subscription.volumeRoot.c_str(),
                                    ready ? 200 * gib : 0,
                                    ready ? 512 * gib : 0,
                                    ready ? 220 * gib : 0};
            subscription.volume(subscription.user, &info);
            continue;
        }
        if (subscription.location || subscription.weather || subscription.air) {
            const bool available = hasNetworkData && scenario == MockScenario::Normal;
            const auto error = scenario == MockScenario::Error ? CD_DATA_NETWORK : dataError;
            const auto status = scenario == MockScenario::Empty ? CD_DATA_UNAVAILABLE
                                : error                         ? available ? CD_DATA_STALE : CD_DATA_ERROR
                                : hasNetworkData                ? CD_DATA_READY
                                                                : CD_DATA_LOADING;
            const CdDataInfo data{
                sizeof(CdDataInfo), status, available ? CD_DATA_VALID : 0u, error, 1791936000000ull, 1791936000000ull,
                uint64_t(seconds),  "mock", "https://example.invalid/"};
            if (subscription.location) {
                CdLocationInfo info{
                    data,          subscription.latitude, subscription.longitude,  subscription.city.c_str(),
                    "Demo Region", "Europe/London",       CD_LOCATION_COORDINATES, 0};
                info.info.struct_size = sizeof(info);
                subscription.location(subscription.user, &info);
            } else if (subscription.weather) {
                std::vector<std::string> dates;
                dates.reserve(subscription.days);
                std::vector<CdWeatherDay> days;
                for (uint32_t index = 0; index < subscription.days; ++index) {
                    dates.push_back("2026-10-" + std::to_string(14 + index));
                    days.push_back({dates.back().c_str(), int32_t(index % 4), 0, 22, 13, 10});
                }
                std::vector<CdWeatherHour> hours;
                for (uint32_t index = 0; index < subscription.hours; ++index)
                    hours.push_back({1791936000000ull + index * 3600000ull, 21, 10, 1, 0});
                CdWeatherInfo info{data,
                                   subscription.city.c_str(),
                                   "Demo Region",
                                   "2026-10-14T12:00",
                                   subscription.latitude,
                                   subscription.longitude,
                                   temperature.value_or(21),
                                   20,
                                   humidity.value_or(65),
                                   3,
                                   1,
                                   1,
                                   uint32_t(days.size()),
                                   uint32_t(hours.size()),
                                   days.data(),
                                   hours.data()};
                info.info.struct_size = sizeof(info);
                subscription.weather(subscription.user, &info);
            } else {
                const CdDataSource sources[]{{"Demo Observatory", "https://example.invalid/source"}};
                const CdAirForecast forecast[]{{"2026-10-14", 32, 22, 42}};
                CdAirInfo info{data,
                               subscription.city.c_str(),
                               "Demo Station",
                               "https://example.invalid/station",
                               "2026-10-14 12:00",
                               "pm25",
                               subscription.latitude,
                               subscription.longitude,
                               aqi.value_or(42),
                               {35, 22, 18, 12, 8, 5},
                               63,
                               1,
                               1,
                               0,
                               sources,
                               forecast};
                info.info.struct_size = sizeof(info);
                subscription.air(subscription.user, &info);
            }
        } else if (subscription.audioStatus) {
            const CdAudioStatus info{sizeof(info),
                                     hasAudioDevice ? CD_DATA_READY : CD_DATA_UNAVAILABLE,
                                     hasAudioDevice ? 0u : 1u,
                                     0,
                                     uint64_t(seconds * 1000),
                                     0,
                                     0};
            subscription.audioStatus(subscription.user, &info);
        } else if (subscription.sessions) {
            const CdMediaSession sessions[]{{"QQMusic.exe", "QQ Music", 7, 0}, {"Spotify.exe", "Spotify", 15, 0}};
            subscription.sessions(subscription.user, hasMediaSession ? sessions : nullptr, hasMediaSession ? 2 : 0);
        } else if (subscription.inputStats) {
            const auto ticks = uint64_t(std::max(0.0, seconds));
            const CdInputStats stats{
                sizeof(stats),      CD_INPUT_STATS_ACTIVE, 20261014,  0,     12000 + ticks * 3, 3000 + ticks,
                250000 + ticks * 3, 72000 + ticks,         ticks * 3, ticks, GetTickCount64()};
            subscription.inputStats(subscription.user, &stats);
        } else if (subscription.media) {
            const auto source = selectedMedia(subscription);
            auto& current = subscriptions.at(token);
            if (current.selectedSource != source || current.selectedGeneration != mediaGeneration) {
                current.selectedSource = source;
                current.selectedGeneration = mediaGeneration;
                current.requestId = 0;
                current.commandResult = CD_OK;
            }
            const bool active = !source.empty();
            const auto requestId = current.requestId;
            const auto commandResult = current.commandResult;
            const CdMediaInfoEx info{
                {sizeof(info), active ? CD_MEDIA_PLAYING : 0u, active ? (source == "Spotify.exe" ? 15u : 7u) : 0u, 0, 0,
                 0, source.c_str(), active ? "Demo track" : "", active ? "Demo artist" : "", active ? "Demo album" : "",
                 nullptr, 0, uint64_t(seconds * 1000), active ? std::fmod(seconds, 180) : 0, active ? 180.0 : 0.0, 1,
                 commandResult, 0},
                requestId,
                active ? mediaGeneration : 0};
            subscription.media(subscription.user, &info.base);
            if (const auto live = subscriptions.find(token); live != subscriptions.end())
                live->second.commandResult = CD_OK;
        } else if (subscription.timer)
            subscription.timer(subscription.user, uint64_t(seconds * 1000));
        else if (subscription.system) {
            CdSystemInfoEx info{{sizeof(CdSystemInfoEx), cpuPercent.value_or(40 + 20 * std::sin(seconds)),
                                 gpuPercent.value_or(25 + 15 * std::cos(seconds)), 8ull * 1024 * 1024 * 1024,
                                 16ull * 1024 * 1024 * 1024, 65536, 131072},
                                CD_SYSTEM_CPU_VALID | CD_SYSTEM_MEMORY_VALID | CD_SYSTEM_GPU_VALID,
                                0,
                                0,
                                GetTickCount64()};
            if (scenario == MockScenario::Empty) {
                info.valid_fields = 0;
                info.base.memory_total_bytes = info.base.memory_used_bytes = 0;
                info.base.cpu_percent = info.base.gpu_percent = 0;
            }
            subscription.system(subscription.user, &info.base);
        } else if (subscription.audio) {
            std::vector<float> data(subscription.bands ? subscription.bands : 1024);
            for (size_t i = 0; i < data.size(); ++i)
                data[i] = !hasAudioDevice      ? 0
                          : subscription.bands ? float(.45 + .4 * std::sin(seconds * 2 + i * .3))
                                               : float(std::sin(seconds * 4 + i * .1) * .5);
            subscription.audio(subscription.user, data.data(), subscription.bands ? uint32_t(data.size()) : 512,
                               subscription.bands ? 0 : 48000, subscription.bands ? 0 : 2);
        }
    }
}

NativeModule::~NativeModule() {
    if (module)
        FreeLibrary(module);
    if (directoryCookie)
        RemoveDllDirectory(directoryCookie);
}

auto NativeModule::open(const std::filesystem::path& root, const pb::PluginDescriptor& descriptor, Error* error)
    -> bool {
    for (const auto capability : descriptor.capabilities())
        capabilities.insert(pb::Capability_Name(capability));
    for (const auto& service : descriptor.required_services())
        requiredServices.emplace(service.id(), service.version());
    for (const auto& field : descriptor.settings_page().fields()) {
        settingsChoices.emplace(field.id(), field.type() == pb::CHOICE);
    }
    const auto absolute = std::filesystem::absolute(root);
    for (const auto& dependency : descriptor.dependencies())
        if (dependency.source() == pb::Dependency::EMBEDDED) {
            const auto name = utf8Path(dependency.name());
            const auto loaded = GetModuleHandleW(name.c_str());
            if (!loaded)
                continue;
            wchar_t path[32768]{};
            const auto count = GetModuleFileNameW(loaded, path, 32768);
            if (!count || _wcsicmp(std::filesystem::path(path).lexically_normal().c_str(),
                                   (absolute / name).lexically_normal().c_str()) != 0) {
                if (error)
                    *error = {"runtime.dependency.collision",
                              "Private DLL basename already loaded from another "
                              "plugin; prefix private names"};
                return false;
            }
        }
    directoryCookie = AddDllDirectory(absolute.c_str());
    module = LoadLibraryExW((absolute / utf8Path(descriptor.entry())).c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        if (error)
            *error = {"runtime.load", "DLL load failed: " + std::to_string(GetLastError())};
        return false;
    }
    auto query = reinterpret_cast<CdPluginQuery>(GetProcAddress(module, CD_PLUGIN_ENTRY_POINT));
    if (!query) {
        if (error)
            *error = {"runtime.entry",
                      "Missing new cd_plugin_query export; old Qt plugins are "
                      "unsupported"};
        return false;
    }
    CdPluginApi rejected;
    std::memset(&rejected, 0xcd, sizeof(rejected));
    const auto before = rejected;
    if (query(99, &rejected, sizeof(rejected)) != CD_ERROR_VERSION ||
        std::memcmp(&before, &rejected, sizeof(rejected)) != 0 ||
        query(CD_PLUGIN_ABI_VERSION, &rejected, sizeof(rejected) - 1) != CD_ERROR_BUFFER ||
        std::memcmp(&before, &rejected, sizeof(rejected)) != 0) {
        if (error)
            *error = {"runtime.contract", "Query must reject incompatible versions/buffers without writing"};
        return false;
    }
    if (query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api)) != CD_OK || api.struct_size != sizeof(api) ||
        api.abi_version != CD_PLUGIN_ABI_VERSION || !api.id || !api.version || !api.create ||
        descriptor.id() != api.id || descriptor.version() != api.version) {
        if (error)
            *error = {"runtime.contract", "Plugin query result disagrees with descriptor or ABI"};
        return false;
    }
    return true;
}

auto NativeInstance::input(const CdInput& event) -> void {
    if (host.paused) {
        return;
    }
    host.deliveringPress = event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT);
    api.input(api.instance, &event);
    host.deliveringPress = false;
    if (event.kind == CD_INPUT_RELEASE || event.kind == CD_INPUT_CANCEL) {
        host.captured = false;
    }
}

auto NativeInstance::pause(bool paused) -> void {
    if (paused) {
        input(CdInput{sizeof(CdInput), CD_INPUT_CANCEL});
        host.uiFocus(false);
    }
    host.paused = paused;
    host.preview->pause(paused);
    api.set_paused(api.instance, paused ? 1 : 0);
}

NativeInstance::~NativeInstance() {
    // A plugin may release keyboard focus from its destructor. The owner can
    // already have cleared its unique_ptr, so never call a UI owner closure
    // while destroying the instance. Revoke focus while the plugin is alive.
    host.requestKeyboardFocus = {};
    host.paused = true;
    host.preview->pause(true);
    host.uiFocus(false);
    if (api.instance && api.destroy)
        api.destroy(api.instance);
}

auto NativeInstance::create(const NativeModule& module, const std::filesystem::path& root,
                            const std::string& instanceId, Error* error) -> bool {
    host.capabilities = host.dataServicesEnabled ? module.capabilities : std::set<std::string>{};
    host.preview->configure(root, module.settingsChoices);
    for (const auto& [name, version] : module.requiredServices) {
        alignas(8) std::array<char, 256> table{};
        if (host.extended.query_service(&host, name.c_str(), version, table.data(), uint32_t(table.size())) != CD_OK) {
            if (error)
                *error = {"runtime.service", "Required service unavailable: " + name};
            return false;
        }
    }
    id = module.api.id;
    uuid = instanceId;
    const auto bytes = std::filesystem::absolute(root).generic_u8string();
    directory.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    CdCreateContext context{
        sizeof(context), CD_PLUGIN_ABI_VERSION, id.c_str(), uuid.c_str(), directory.c_str(), &host.api, &host};
    if (module.api.create(&context, &api, sizeof(api)) != CD_OK || api.struct_size != sizeof(api) ||
        api.abi_version != CD_PLUGIN_ABI_VERSION || !api.instance || !api.destroy || !api.render || !api.input ||
        !api.layout_changed || !api.set_paused || !api.save_state || !api.restore_state || !api.get_setting ||
        !api.apply_settings) {
        if (error)
            *error = {"runtime.create", "Invalid instance function table"};
        return false;
    }
    return true;
}

auto NativeInstance::state(Error* error) const -> std::string {
    uint64_t size = 0;
    if (api.save_state(api.instance, nullptr, 0, &size) != CD_OK || size > 2 * 1024 * 1024) {
        if (error)
            *error = {"runtime.state", "Invalid state size"};
        return {};
    }
    std::string value(size_t(size), '\0');
    uint64_t written = size;
    if (api.save_state(api.instance, reinterpret_cast<uint8_t*>(value.data()), size, &written) != CD_OK ||
        written != size) {
        if (error)
            *error = {"runtime.state", "State save failed"};
        return {};
    }
    return value;
}

auto NativeInstance::render(uint32_t width, uint32_t height, double dpr, std::vector<uint8_t>* pixels, Error* error)
    -> bool {
    pixels->assign(size_t(width) * height * 4, 0);
    CdFrame frame{sizeof(frame), width, height, width * 4, uint64_t(pixels->size()), pixels->data(), dpr};
    if (host.preview->renderPlugin(api, frame) != CD_OK) {
        if (error)
            *error = {"runtime.render", "Render failed"};
        return false;
    }
    return true;
}

bool loadDirectoryDescriptor(const std::filesystem::path& root, pb::PluginDescriptor* result, Error* error) {
    const auto binary = root / "descriptor.pb";
    const auto editable = root / "plugin.textproto";
    return readDescriptor(std::filesystem::exists(editable) ? editable : binary, result, error);
}

bool lifecycle(const std::filesystem::path& root, Error* error) {
    pb::PluginDescriptor descriptor;
    if (!loadDirectoryDescriptor(root, &descriptor, error))
        return false;
    NativeModule module;
    if (!module.open(root, descriptor, error))
        return false;
    NativeInstance first, second;
    if (!first.create(module, root, "sdk-test-1", error) || !second.create(module, root, "sdk-test-2", error))
        return false;
    if (!probeSettings(descriptor.settings_page(), first.api, second.api, error)) {
        return false;
    }
    CdCell cell{0, 0};
    CdLayout layout{sizeof(layout),     0, 0, 1, &cell, 96, 96, 1, {0, 0, 96, 96}, {235, 240, 245, 255},
                    {70, 200, 170, 255}};
    first.api.layout_changed(first.api.instance, &layout);
    second.api.layout_changed(second.api.instance, &layout);
    first.host.tick(.2);
    second.host.tick(.2);
    std::vector<uint8_t> pixels;
    if (!first.render(96, 96, 1, &pixels, error) || !second.render(96, 96, 1, &pixels, error))
        return false;
    const auto untouched = second.state(error);
    CdInput input{sizeof(input), CD_INPUT_PRESS, 1, 0, 48, 48, 0};
    first.api.input(first.api.instance, &input);
    input.kind = CD_INPUT_RELEASE;
    first.api.input(first.api.instance, &input);
    if (second.state(error) != untouched) {
        if (error)
            *error = {"runtime.independence", "Two instances share mutable state"};
        return false;
    }
    const auto saved = first.state(error);
    if (error && !error->code.empty())
        return false;
    first.host.paused = true;
    first.api.set_paused(first.api.instance, 1);
    first.api.set_paused(first.api.instance, 1);
    first.host.tick(.3);
    if (first.state(error) != saved) {
        if (error)
            *error = {"runtime.pause", "Paused plugin changed saved state"};
        return false;
    }
    first.api.set_paused(first.api.instance, 0);
    first.api.set_paused(first.api.instance, 0);
    first.host.paused = false;
    if (first.api.restore_state(first.api.instance, nullptr, 0) != CD_OK ||
        first.api.restore_state(first.api.instance, reinterpret_cast<const uint8_t*>(saved.data()), saved.size()) !=
            CD_OK ||
        first.state(error) != saved) {
        if (error)
            *error = {"runtime.restore", "State did not round-trip"};
        return false;
    }
    first.api.destroy(first.api.instance);
    first.api.instance = nullptr;
    second.api.destroy(second.api.instance);
    second.api.instance = nullptr;
    if (!first.host.subscriptions.empty() || !second.host.subscriptions.empty() || !first.host.diagnostics.empty() ||
        !second.host.diagnostics.empty() || first.host.uiCallback || second.host.uiCallback) {
        if (error)
            *error = {"runtime.subscriptions", "Plugin did not release its data subscriptions"};
        return false;
    }
    return true;
}

std::string utf8Argument(const wchar_t* text) {
    const int length = int(wcslen(text));
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string output(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, output.data(), size, nullptr, nullptr);
    return output;
}

CdResult CD_CALL MockHost::registerDiagnostic(void* context, CdDiagnosticCallback callback, void* user,
                                              uint64_t* token) try {
    if (!context || !callback || !token) {
        return CD_ERROR_ARGUMENT;
    }
    *token = 0;
    return static_cast<MockHost*>(context)->diagnostics.attach(callback, user, token);
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::uiPublish(void* context, const CdSemanticNode* nodes, uint32_t count) try {
    if (!context || count > 4096 || (count && !nodes)) {
        return CD_ERROR_ARGUMENT;
    }
    std::set<uint32_t> ids;
    std::vector<std::pair<std::string, std::string>> strings;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& node = nodes[i];
        if (node.struct_size < sizeof(node) || !node.id || node.role > 7 ||
            (node.parent && !ids.contains(node.parent)) || !ids.insert(node.id).second || !node.name || !node.value ||
            strnlen(node.name, 4097) > 4096 || strnlen(node.value, 4097) > 4096) {
            return CD_ERROR_ARGUMENT;
        }
        strings.emplace_back(node.name, node.value);
    }
    auto& host = *static_cast<MockHost*>(context);
    host.semanticStrings = std::move(strings);
    host.semantics.clear();
    if (count) {
        host.semantics.assign(nodes, nodes + count);
    }
    for (uint32_t i = 0; i < count; ++i) {
        host.semantics[i].name = host.semanticStrings[i].first.c_str();
        host.semantics[i].value = host.semanticStrings[i].second.c_str();
    }
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::system(void* context, uint32_t interval, CdSystemCallback callback, void* user,
                                  uint64_t* token) try {
    if (!context || !callback || !token)
        return CD_ERROR_ARGUMENT;
    auto* host = static_cast<MockHost*>(context);
    if (!host->capabilities.contains("SYSTEM_INFO"))
        return CD_ERROR_UNSUPPORTED;
    if (host->scenario == MockScenario::Error) {
        *token = 0;
        return CD_ERROR_INTERNAL;
    }
    *token = host->next++;
    host->subscriptions[*token] = {callback, nullptr, user, 0, nullptr, std::max(500u, interval) / 1000.0, 0};
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::audio(void* context, uint32_t bands, CdAudioCallback callback, void* user,
                                 uint64_t* token) try {
    if (!context || !callback || !token || bands > 128)
        return CD_ERROR_ARGUMENT;
    auto* host = static_cast<MockHost*>(context);
    if (!host->capabilities.contains(bands ? "AUDIO_FFT" : "AUDIO_PCM"))
        return CD_ERROR_UNSUPPORTED;
    *token = host->next++;
    host->subscriptions[*token] = {nullptr, callback, user, bands};
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::timer(void* context, uint32_t interval, CdTimerCallback callback, void* user,
                                 uint64_t* token) try {
    if (!context || !callback || !token || interval < 16 || interval > 3600000)
        return CD_ERROR_ARGUMENT;
    auto* host = static_cast<MockHost*>(context);
    *token = host->next++;
    host->subscriptions[*token] = {nullptr, nullptr, user, 0, callback, interval / 1000.0, 0};
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::media(void* context, const char* filter, CdMediaCallback callback, void* user,
                                 uint64_t* token) try {
    if (!context || !filter || !callback || !token)
        return CD_ERROR_ARGUMENT;
    auto* host = static_cast<MockHost*>(context);
    if (!host->capabilities.contains("MEDIA_CONTROL"))
        return CD_ERROR_UNSUPPORTED;
    if (host->scenario == MockScenario::Error) {
        *token = 0;
        return CD_ERROR_INTERNAL;
    }
    if (std::strlen(filter) > 128)
        return CD_ERROR_ARGUMENT;
    *token = host->next++;
    host->subscriptions[*token] = {nullptr, nullptr, user, 0, nullptr, 1, 0, callback};
    host->subscriptions[*token].sourceFilter = filter;
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::inputStats(void* context, CdInputStatsCallback callback, void* user, uint64_t* token) try {
    if (!context || !callback || !token)
        return CD_ERROR_ARGUMENT;
    auto* host = static_cast<MockHost*>(context);
    if (!host->capabilities.contains("INPUT_STATS"))
        return CD_ERROR_UNSUPPORTED;
    *token = host->next++;
    host->subscriptions[*token] = {nullptr, nullptr, user, 0, nullptr, 1, 0, nullptr, callback};
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

auto MockHost::network(void* context, const CdLocationQuery* query, void* user, uint64_t* token,
                       CdLocationCallback location, CdWeatherCallback weather, CdAirCallback air) -> CdResult try {
    if (!context || !query || query->struct_size < sizeof(CdLocationQuery) || !token ||
        (!location && !weather && !air) || query->mode > CD_LOCATION_STATION || !std::isfinite(query->latitude) ||
        !std::isfinite(query->longitude) || query->latitude < -90 || query->latitude > 90 || query->longitude < -180 ||
        query->longitude > 180 || query->forecast_days > 14 || query->forecast_hours > 168 ||
        (query->mode == CD_LOCATION_CITY && (!query->city || !*query->city)) ||
        (query->mode == CD_LOCATION_STATION && (!air || !query->station || !*query->station)) ||
        (query->city && std::strlen(query->city) > 128) || (query->station && std::strlen(query->station) > 32) ||
        (query->country_code && *query->country_code && std::strlen(query->country_code) != 2) ||
        (query->credential_id && std::strlen(query->credential_id) > 128))
        return CD_ERROR_ARGUMENT;
    auto& host = *static_cast<MockHost*>(context);
    if (!host.capabilities.contains(location ? "LOCATION" : weather ? "WEATHER" : "AIR_QUALITY"))
        return CD_ERROR_UNSUPPORTED;
    const auto id = host.next++;
    Subscription value;
    value.user = user;
    value.location = location;
    value.weather = weather;
    value.air = air;
    value.days = query->forecast_days ? query->forecast_days : 5;
    value.hours = query->forecast_hours;
    value.lastRefresh = host.virtualTime;
    value.interval = std::clamp(query->refresh_seconds ? query->refresh_seconds : 900u, 300u, 10800u);
    if (query->city && *query->city)
        value.city = query->city;
    if (query->mode == CD_LOCATION_COORDINATES) {
        value.latitude = query->latitude;
        value.longitude = query->longitude;
    }
    host.subscriptions.emplace(id, std::move(value));
    *token = id;
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}

CdResult CD_CALL MockHost::sessionsService(void* context, CdSessionsCallback callback, void* user,
                                           uint64_t* token) try {
    if (!context || !callback || !token)
        return CD_ERROR_ARGUMENT;
    auto& host = *static_cast<MockHost*>(context);
    if (!host.capabilities.contains("MEDIA_CONTROL"))
        return CD_ERROR_UNSUPPORTED;
    Subscription value;
    value.user = user;
    value.sessions = callback;
    value.interval = 1;
    *token = host.next++;
    host.subscriptions.emplace(*token, std::move(value));
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
}  // namespace cdsdk
