/* Verify that the published ABI compiles in C and is stable under caller
 * packing. */
#pragma pack(push, 1)
#include <sdk/PackageApi.h>
#include <sdk/PluginApi.h>
#include <sdk/services/AmbientAudio.h>
#include <sdk/services/DataServices.h>
#include <sdk/services/Diagnostics.h>
#include <sdk/services/Environment.h>
#include <sdk/services/Icons.h>
#include <sdk/services/Interaction.h>
#include <sdk/services/Keyboard.h>
#include <sdk/services/RenderImages.h>
#include <sdk/services/RenderMemory.h>
#include <sdk/services/RenderOutput.h>
#pragma pack(pop)
_Static_assert(sizeof(CdKeyboardApi) == 32 && sizeof(CdUiEvent) == 32 && sizeof(CdSemanticNode) == 104, "keyboard ABI");
_Static_assert(sizeof(CdDiagnosticStats) == 520 && sizeof(CdDebugNode) == 1008 && sizeof(CdDiagnosticsApi) == 24,
               "diagnostics ABI");
_Static_assert(sizeof(CdEnvironmentSnapshot) == 64 && sizeof(CdEnvironmentApi) == 16, "environment ABI");
_Static_assert(sizeof(CdIconsApi) == 16, "icons service ABI");
_Static_assert(sizeof(CdRenderSession) == 48 && sizeof(CdRenderReport) == 48 && sizeof(CdRenderOutputApi) == 32,
               "render output service ABI");
_Static_assert(sizeof(CdRenderMemoryApi) == 24 && sizeof(CdRenderImagesApi) == 16, "render resource services ABI");
_Static_assert(sizeof(CdInteractionApi) == 24, "interaction service ABI");
_Static_assert(sizeof(CdLayout) == 88, "layout ABI");
_Static_assert(sizeof(CdFrame) == 40, "frame ABI");
_Static_assert(sizeof(CdHostApi) == 56, "host ABI");
_Static_assert(sizeof(CdServiceHostApi) == 88 && offsetof(CdServiceHostApi, input) == 0, "service discovery ABI");
_Static_assert(sizeof(CdDataInfo) == 56 && sizeof(CdLocationQuery) == 80, "data header and query ABI");
_Static_assert(sizeof(CdMediaHostApi) == 72 && offsetof(CdMediaHostApi, base) == 0, "media host extension ABI");
_Static_assert(sizeof(CdMediaInfo) == 112, "media snapshot ABI");
_Static_assert(sizeof(CdMediaInfoEx) == 128 && offsetof(CdMediaInfoEx, base) == 0, "media correlation extension");
_Static_assert(sizeof(CdAudioStatus) == 40 && sizeof(CdAudioServiceApi) == 24, "audio status ABI");
_Static_assert(sizeof(CdAmbientConfig) == 36 && sizeof(CdAmbientSnapshot) == 64 && sizeof(CdAmbientApi) == 48,
               "ambient playback ABI");
_Static_assert(sizeof(CdLocationInfo) == 104 && sizeof(CdWeatherInfo) == 160 && sizeof(CdAirInfo) == 200,
               "network payload ABI");
_Static_assert(sizeof(CdInputStats) == 72 && sizeof(CdInputStatsHostApi) == 80 &&
                   offsetof(CdInputStatsHostApi, media) == 0,
               "input stats extension ABI");
_Static_assert(sizeof(CdInstanceApi) == 88, "instance ABI");
_Static_assert(sizeof(CdCreateContext) == 48, "create ABI");
_Static_assert(sizeof(CdPluginApi) == 32, "plugin ABI");
_Static_assert(sizeof(CdSystemInfo) == 56 && sizeof(CdSystemInfoEx) == 80, "system extension prefix ABI");
_Static_assert(offsetof(CdSystemInfoEx, base) == 0, "system prefix remains at zero");

int cd_sdk_c_contract(void) {
    return CD_PLUGIN_ABI_VERSION == 1u;
}
