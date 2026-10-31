#pragma once
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string_view>

#include "sdk/Environment.h"
#include "sdk/PluginApi.h"
#include "sdk/cpp/DiagnosticHost.h"
#include "sdk/services/AmbientAudio.h"
#include "sdk/services/Content.h"
#include "sdk/services/DataServices.h"
#include "sdk/services/Diagnostics.h"
#include "sdk/services/Interaction.h"
#include "sdk/services/Keyboard.h"
#include "sdk/services/Library.h"
#include "sdk/services/Storage.h"
#include "support/Package.h"
#include "tools/SettingsProbe.h"

namespace cdsdk {
class PreviewServices;
class WindowsApartment;
enum class MockScenario { Normal, Loading, Empty, Error };

struct MockHost {
    MockHost();
    ~MockHost();
    std::shared_ptr<const bool> lifetime;
    std::unique_ptr<WindowsApartment> apartment;
    std::unique_ptr<PreviewServices> preview;

    struct Subscription {
        CdSystemCallback system = nullptr;
        CdAudioCallback audio = nullptr;
        void* user = nullptr;
        uint32_t bands = 0;
        CdTimerCallback timer = nullptr;
        double interval = .05, nextTime = 0;
        CdMediaCallback media = nullptr;
        CdInputStatsCallback inputStats = nullptr;
        CdLocationCallback location = nullptr;
        CdWeatherCallback weather = nullptr;
        CdAirCallback air = nullptr;
        CdLibraryCallback library = nullptr;
        uint64_t libraryIndex = 0;
        uint32_t libraryEdge = 256;
        CdContentCallback content = nullptr;
        uint32_t contentKind = 0;
        std::string contentUrl;
        CdVolumeCallback volume = nullptr;
        std::string volumeRoot;
        CdSessionsCallback sessions = nullptr;
        std::string city = "Demo City";
        double latitude = 51.5, longitude = -0.12;
        std::string sourceFilter = "auto";
        uint64_t requestId = 0;
        CdResult commandResult = CD_OK;
        double lastRefresh = -1e9;
        uint32_t days = 5, hours = 0;
        CdAudioStatusCallback audioStatus = nullptr;
        std::string selectedSource;
        uint64_t selectedGeneration = 0;
    };

    std::map<uint64_t, Subscription> subscriptions;
    std::map<uint64_t, CdAmbientSnapshot> ambientGroups;
    uint64_t next = 1;
    uint64_t nextRequest = 1;
    double virtualTime = 0;
    std::atomic_bool dirty{true};
    std::optional<uint64_t> fixedUnixMs;
    uint64_t baseUnixMs = Environment::systemSnapshot().unix_ms;
    int32_t utcOffsetMinutes = Environment::systemSnapshot().utc_offset_minutes;
    uint32_t theme = CD_ENV_THEME_DARK;
    std::string locale = Environment::systemSnapshot().locale;

    static CdResult CD_CALL environment(void* context, CdEnvironmentSnapshot* output);

    bool paused = false, dataServicesEnabled = true;
    MockScenario scenario = MockScenario::Normal;
    bool streaming = false, captured = false, deliveringPress = false;
    std::optional<double> cpuPercent, gpuPercent;
    std::optional<double> temperature, humidity, aqi;
    uint32_t dataError = 0;
    bool hasNetworkData = true;
    bool hasAudioDevice = true;
    bool hasMediaSession = true;
    std::string currentMediaSource = "QQMusic.exe";
    uint64_t mediaGeneration = 1;
    CdUiCallback uiCallback = nullptr;
    void* uiUser = nullptr;
    bool uiGranted = false;
    std::function<void(bool)> requestKeyboardFocus;
    std::vector<CdSemanticNode> semantics;
    std::vector<std::pair<std::string, std::string>> semanticStrings;
    DiagnosticHost diagnostics;
    std::vector<std::string> openedUrls;
    std::function<void(uint32_t, std::string_view)> onLog;
    std::function<void(double, std::span<float>)> onAudio;

    static CdResult CD_CALL registerDiagnostic(void* context, CdDiagnosticCallback callback, void* user,
                                               uint64_t* token);

    static void CD_CALL unregisterDiagnostic(void* context, uint64_t token);

    void uiFocus(bool focused);

    bool uiKey(uint32_t key, uint32_t modifiers = 0);

    static CdResult CD_CALL uiConnect(void* context, CdUiCallback callback, void* user);

    static CdResult CD_CALL uiRequest(void* context, uint32_t focused);

    static CdResult CD_CALL uiPublish(void* context, const CdSemanticNode* nodes, uint32_t count);

    std::string selectedMedia(const Subscription& subscription) const;

    std::set<std::string> capabilities{"SYSTEM_INFO", "AUDIO_PCM", "AUDIO_FFT", "MEDIA_CONTROL",
                                       "INPUT_STATS", "LOCATION",  "WEATHER",   "AIR_QUALITY"};

    static void CD_CALL redraw(void* context);

    static void CD_CALL log(void*, uint32_t, const char*, uint32_t);

    static CdResult CD_CALL system(void* context, uint32_t interval, CdSystemCallback callback, void* user,
                                   uint64_t* token);

    static CdResult CD_CALL audio(void* context, uint32_t bands, CdAudioCallback callback, void* user, uint64_t* token);

    static void CD_CALL unsubscribe(void* context, uint64_t token);

    static CdResult CD_CALL timer(void* context, uint32_t interval, CdTimerCallback callback, void* user,
                                  uint64_t* token);

    static CdResult CD_CALL media(void* context, const char* filter, CdMediaCallback callback, void* user,
                                  uint64_t* token);

    static CdResult CD_CALL command(void* context, uint64_t token, uint32_t command, double value);

    static CdResult CD_CALL commandRequest(void* context, uint64_t token, uint32_t command, double value,
                                           uint64_t* requestId);

    static CdResult CD_CALL inputStats(void* context, CdInputStatsCallback callback, void* user, uint64_t* token);

    static CdResult CD_CALL audioService(void* context, const CdAudioOptions* options, CdAudioCallback callback,
                                         void* user, uint64_t* token);

    static CdResult CD_CALL audioStatusService(void* context, const CdAudioOptions* options,
                                               CdAudioStatusCallback callback, void* user, uint64_t* token);

    template <class Table>
    static CdResult writeService(Table value, void* buffer, uint32_t size) {
        if (!buffer)
            return CD_ERROR_ARGUMENT;
        if (size < sizeof(Table))
            return CD_ERROR_BUFFER;
        std::memcpy(buffer, &value, sizeof(Table));
        return CD_OK;
    }

    static CdResult CD_CALL libraryService(void*, const CdLibraryQuery*, CdLibraryCallback, void*, uint64_t*);
    static CdResult CD_CALL librarySelect(void*, uint64_t, uint64_t, uint32_t);
    static CdResult CD_CALL libraryRefresh(void*, uint64_t);

    static CdResult CD_CALL queryService(void* context, const char* id, uint32_t version, void* buffer, uint32_t size);

    static CdResult CD_CALL ambientOpen(void* context, const char* key, uint64_t* token);

    static void CD_CALL ambientClose(void* context, uint64_t token);

    static CdResult CD_CALL ambientConfigure(void* context, uint64_t token, const CdAmbientConfig* config);

    static CdResult CD_CALL ambientCommand(void* context, uint64_t token, uint32_t command);

    static CdResult CD_CALL ambientSnapshot(void* context, uint64_t token, CdAmbientSnapshot* out);

    static CdResult network(void* context, const CdLocationQuery* query, void* user, uint64_t* token,
                            CdLocationCallback location, CdWeatherCallback weather, CdAirCallback air);

    static CdResult CD_CALL contentService(void*, const CdContentQuery*, CdContentCallback, void*, uint64_t*);
    static CdResult CD_CALL contentRefresh(void*, uint64_t);
    static CdResult CD_CALL contentOpen(void*, const char*);

    static CdResult CD_CALL volumeService(void* context, const CdVolumeQuery* query, CdVolumeCallback callback,
                                          void* user, uint64_t* token);

    static CdResult CD_CALL locationService(void* context, const CdLocationQuery* query, CdLocationCallback callback,
                                            void* user, uint64_t* token);

    static CdResult CD_CALL weatherService(void* context, const CdLocationQuery* query, CdWeatherCallback callback,
                                           void* user, uint64_t* token);

    static CdResult CD_CALL airService(void* context, const CdLocationQuery* query, CdAirCallback callback, void* user,
                                       uint64_t* token);

    static CdResult CD_CALL refreshService(void* context, uint64_t token);

    static CdResult CD_CALL sessionsService(void* context, CdSessionsCallback callback, void* user, uint64_t* token);

    CdServiceHostApi extended{
        {{{sizeof(CdServiceHostApi), CD_PLUGIN_ABI_VERSION, redraw, log, system, audio, unsubscribe, timer},
          media,
          command},
         inputStats},
        queryService};
    CdHostApi& api = extended.input.media.base;

    void tick(double seconds);
};

class NativeModule {
    HMODULE module = nullptr;
    DLL_DIRECTORY_COOKIE directoryCookie = nullptr;

public:
    CdPluginApi api{};
    std::set<std::string> capabilities;
    std::map<std::string, uint32_t> requiredServices;
    std::map<std::string, bool> settingsChoices;
    NativeModule() = default;
    NativeModule(const NativeModule&) = delete;

    ~NativeModule();

    bool open(const std::filesystem::path& root, const pb::PluginDescriptor& descriptor, Error* error);
};

class NativeInstance {
public:
    MockHost host;
    CdInstanceApi api{};
    std::string id, uuid, directory;

    void input(const CdInput& event);

    void pause(bool paused);

    ~NativeInstance();

    bool create(const NativeModule& module, const std::filesystem::path& root, const std::string& instanceId,
                Error* error);

    std::string state(Error* error) const;

    bool render(uint32_t width, uint32_t height, double dpr, std::vector<uint8_t>* pixels, Error* error);
};

bool loadDirectoryDescriptor(const std::filesystem::path& root, pb::PluginDescriptor* result, Error* error);

bool lifecycle(const std::filesystem::path& root, Error* error);

std::string utf8Argument(const wchar_t* text);
}  // namespace cdsdk
