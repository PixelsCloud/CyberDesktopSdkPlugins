#ifndef CYBERDESKTOP_PLUGIN_API_H
#define CYBERDESKTOP_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define CD_CALL __cdecl
#if defined(CD_PLUGIN_BUILD)
#define CD_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CD_PLUGIN_EXPORT
#endif
#else
#define CD_CALL
#define CD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#pragma pack(push, 8)

#ifdef __cplusplus
extern "C" {
#endif

#define CD_PLUGIN_ABI_VERSION 1u
#define CD_PLUGIN_ENTRY_POINT "cd_plugin_query"

typedef int32_t CdResult;
#define CD_OK 0
#define CD_ERROR_ARGUMENT (-1)
#define CD_ERROR_VERSION (-2)
#define CD_ERROR_BUFFER (-3)
#define CD_ERROR_INTERNAL (-4)
#define CD_ERROR_UNSUPPORTED (-5)
#define CD_ERROR_BUSY (-6)

typedef struct CdColor {
    uint8_t r, g, b, a;
} CdColor;

typedef struct CdCell {
    int32_t a, b;
} CdCell;

typedef struct CdRect {
    double x, y, width, height;
} CdRect;

typedef struct CdLayout {
    uint32_t struct_size;
    uint32_t topology; /* 0 rectangular, 1 axial hex */
    uint32_t rotation;
    uint32_t cell_count;
    const CdCell* cells; /* Borrowed for this callback only. */
    double logical_width, logical_height, device_pixel_ratio;
    CdRect safe_content;
    CdColor text_color, accent_color;
} CdLayout;

/* Host owns this RGBA8 premultiplied buffer. The plugin writes synchronously,
   never retains its pointer, and never allocates/free memory across the ABI. */
typedef struct CdFrame {
    uint32_t struct_size;
    uint32_t width, height, stride;
    uint64_t capacity;
    uint8_t* rgba;
    double device_pixel_ratio;
} CdFrame;

#define CD_INPUT_MOVE 0u
#define CD_INPUT_PRESS 1u
#define CD_INPUT_RELEASE 2u
#define CD_INPUT_DOUBLE_CLICK 3u
#define CD_INPUT_WHEEL 4u
#define CD_INPUT_LEAVE 5u
#define CD_BUTTON_LEFT 1u
#define CD_BUTTON_RIGHT 2u
#define CD_BUTTON_MIDDLE 4u
#define CD_MOD_SHIFT 1u
#define CD_MOD_CONTROL 2u
#define CD_MOD_ALT 4u
#define CD_MOD_META 8u

typedef struct CdInput {
    uint32_t struct_size, kind, buttons, modifiers;
    double x, y, wheel_delta;
} CdInput;

typedef struct CdSystemInfo {
    uint32_t struct_size;
    double cpu_percent, gpu_percent;
    uint64_t memory_used_bytes, memory_total_bytes;
    uint64_t network_up_bytes_per_second, network_down_bytes_per_second;
} CdSystemInfo;

#define CD_SYSTEM_CPU_VALID 1u
#define CD_SYSTEM_MEMORY_VALID 2u
#define CD_SYSTEM_GPU_VALID 4u
#define CD_SYSTEM_CPU_TEMPERATURE_VALID 8u

/* Optional extended payload for subscribe_system. The original 56-byte prefix
   is unchanged. Inspect base.struct_size before accessing this extension.
   Missing temperatures are zero with their validity bit clear. */
typedef struct CdSystemInfoEx {
    CdSystemInfo base;
    uint32_t valid_fields, reserved;
    double cpu_temperature_celsius;
    uint64_t sampled_at_ms; /* Windows monotonic milliseconds (GetTickCount64). */
} CdSystemInfoEx;

typedef void(CD_CALL* CdSystemCallback)(void* user, const CdSystemInfo* info);
typedef void(CD_CALL* CdAudioCallback)(void* user, const float* data, uint32_t count, uint32_t sample_rate,
                                       uint32_t channels);
typedef void(CD_CALL* CdTimerCallback)(void* user, uint64_t elapsed_ms);

/* Services and host_context remain valid until destroy returns. Callbacks are
   on the UI thread. A plugin must stop its work before destroying an instance.
 */
typedef struct CdHostApi {
    uint32_t struct_size, abi_version;
    void(CD_CALL* request_redraw)(void* host_context);
    void(CD_CALL* log)(void* host_context, uint32_t level, const char* utf8, uint32_t length);
    CdResult(CD_CALL* subscribe_system)(void* host_context, uint32_t interval_ms, CdSystemCallback callback, void* user,
                                        uint64_t* subscription);
    CdResult(CD_CALL* subscribe_audio)(void* host_context, uint32_t fft_bands, CdAudioCallback callback, void* user,
                                       uint64_t* subscription); /* bands=0: PCM */
    void(CD_CALL* unsubscribe)(void* host_context, uint64_t subscription);
    CdResult(CD_CALL* subscribe_timer)(void* host_context, uint32_t interval_ms, CdTimerCallback callback, void* user,
                                       uint64_t* subscription);
} CdHostApi;

#define CD_MEDIA_PLAYING 1u
#define CD_MEDIA_PAUSED 2u
#define CD_MEDIA_CAN_TOGGLE 1u
#define CD_MEDIA_CAN_PREVIOUS 2u
#define CD_MEDIA_CAN_NEXT 4u
#define CD_MEDIA_CAN_SEEK 8u
#define CD_MEDIA_TOGGLE 1u
#define CD_MEDIA_PREVIOUS 2u
#define CD_MEDIA_NEXT 3u
#define CD_MEDIA_SEEK 4u

/* Borrowed UTF-8 strings and premultiplied RGBA cover pixels, valid only during
   the UI-thread callback. Copy retained values. An empty source_id means the
   selected player has no session. Position is relative to the timeline start.
   command_result: CD_OK, CD_ERROR_BUSY (queued), or a failed command result. */
typedef struct CdMediaInfo {
    uint32_t struct_size, status, controls, cover_width, cover_height, cover_stride;
    const char *source_id, *title, *artist, *album;
    const uint8_t* cover_rgba;
    uint64_t cover_generation, sampled_at_ms;
    double position_seconds, duration_seconds, playback_rate;
    CdResult command_result;
    uint32_t reserved;
} CdMediaInfo;

typedef void(CD_CALL* CdMediaCallback)(void* user, const CdMediaInfo* info);

/* Optional C ABI 1 extension. Check host->struct_size >= sizeof(CdMediaHostApi)
   before casting. The unchanged CdHostApi prefix handles unsubscribe/cleanup.
   source_filter: "auto" or a case-insensitive SourceAppUserModelId substring.
   Commands target this subscription's last delivered session, never another
   player. CD_OK accepts an asynchronous command; observe command_result and
   subsequent playback state for completion. Calls must be on the UI thread. */
typedef struct CdMediaHostApi {
    CdHostApi base;
    CdResult(CD_CALL* subscribe_media)(void* host_context, const char* source_filter, CdMediaCallback callback,
                                       void* user, uint64_t* subscription);
    CdResult(CD_CALL* media_command)(void* host_context, uint64_t subscription, uint32_t command, double value);
} CdMediaHostApi;

#define CD_INPUT_STATS_ACTIVE 1u
#define CD_INPUT_STATS_STORAGE_ERROR 2u

/* Aggregate physical key presses and mouse-button DOWN transitions only.
   Keyboard autorepeat, mouse movement and wheel events are excluded.
   day is local YYYYMMDD; today resets at local midnight. Totals are persisted
   by the host; session counters begin at this process's first subscription.
   No key identities, text, pointer positions or per-event records are exposed. */
typedef struct CdInputStats {
    uint32_t struct_size, status, day, reserved;
    uint64_t keyboard_today, mouse_today, keyboard_total, mouse_total;
    uint64_t keyboard_session, mouse_session, sampled_at_ms;
} CdInputStats;

typedef void(CD_CALL* CdInputStatsCallback)(void* user, const CdInputStats* stats);

/* Optional extension, preserving both previous host prefixes at offset zero.
   Check host->struct_size >= sizeof(CdInputStatsHostApi) before casting.
   UI-thread callbacks are change-only, at most once per second. Raw input and
   persistence run on the host worker. unsubscribe stops display delivery;
   once enabled, recording continues until the host exits, including fullscreen. */
typedef struct CdInputStatsHostApi {
    CdMediaHostApi media;
    CdResult(CD_CALL* subscribe_input_stats)(void* host_context, CdInputStatsCallback callback, void* user,
                                             uint64_t* subscription);
} CdInputStatsHostApi;

/* Optional service discovery extension. The existing 80-byte prefix is
   unchanged. Query on the UI thread; the caller owns the function-table buffer.
   Unknown IDs return UNSUPPORTED; unsupported versions return VERSION. */
typedef struct CdServiceHostApi {
    CdInputStatsHostApi input;
    CdResult(CD_CALL* query_service)(void* host_context, const char* service_id, uint32_t version, void* table,
                                     uint32_t table_size);
} CdServiceHostApi;

typedef struct CdCreateContext {
    uint32_t struct_size, abi_version;
    const char* plugin_id;
    const char* instance_id;
    const char* plugin_directory; /* UTF-8, resources extracted from this package */
    const CdHostApi* host;
    void* host_context;
} CdCreateContext;

#define CD_SETTING_TOGGLE 1u
#define CD_SETTING_INTEGER 2u
#define CD_SETTING_NUMBER 3u
#define CD_SETTING_TEXT 4u
#define CD_SETTING_CHOICE 5u
#define CD_SETTING_COLOR 6u
#define CD_SETTING_FONT                                                          \
    7u /* UTF-8 family, or @siyuan-heavy / @zqk-free for shared external fonts \ \
        */

typedef struct CdSettingValue {
    uint32_t struct_size, type;
    const char* id;
    int64_t integer; /* INTEGER, TOGGLE (0/1), COLOR (0xRRGGBBAA) */
    double number;
    const char* text; /* UTF-8 TEXT/CHOICE/FONT, borrowed until next plugin call */
} CdSettingValue;

typedef struct CdInstanceApi {
    uint32_t struct_size, abi_version;
    void* instance;
    void(CD_CALL* destroy)(void* instance);
    CdResult(CD_CALL* render)(void* instance, const CdFrame* frame);
    void(CD_CALL* input)(void* instance, const CdInput* event);
    void(CD_CALL* layout_changed)(void* instance, const CdLayout* layout);
    void(CD_CALL* set_paused)(void* instance, uint32_t paused);
    /* First call with NULL/0 asks for required bytes. Host allocates the buffer.
     */
    CdResult(CD_CALL* save_state)(void* instance, uint8_t* bytes, uint64_t capacity, uint64_t* size);
    CdResult(CD_CALL* restore_state)(void* instance, const uint8_t* bytes, uint64_t size);
    /* Mandatory. Settings-page schema is embedded in the protobuf descriptor.
       get_setting must reject short outputs before writing. apply_settings is
       atomic: invalid values leave every setting unchanged. No retained inputs.
     */
    CdResult(CD_CALL* get_setting)(void* instance, const char* id, CdSettingValue* output, uint32_t output_size);
    CdResult(CD_CALL* apply_settings)(void* instance, const CdSettingValue* values, uint32_t count);
} CdInstanceApi;

typedef struct CdPluginApi {
    uint32_t struct_size, abi_version;
    const char* id;
    const char* version;
    CdResult(CD_CALL* create)(const CdCreateContext* context, CdInstanceApi* output, uint32_t output_size);
} CdPluginApi;

/* Query does not create instances. It must reject an unsupported version or
   short output buffer before writing. Exceptions cannot cross any callback. */
typedef CdResult(CD_CALL* CdPluginQuery)(uint32_t requested_abi, CdPluginApi* output, uint32_t output_size);
CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t requested_abi, CdPluginApi* output, uint32_t output_size);

#ifdef __cplusplus
}
static_assert(sizeof(CdColor) == 4);
static_assert(sizeof(CdCell) == 8);
static_assert(sizeof(void*) == 8, "C ABI 1 is Windows x64 only");
static_assert(sizeof(CdLayout) == 88 && sizeof(CdFrame) == 40);
static_assert(sizeof(CdInput) == 40 && sizeof(CdSystemInfo) == 56);
static_assert(sizeof(CdSystemInfoEx) == 80 && offsetof(CdSystemInfoEx, base) == 0);
static_assert(sizeof(CdHostApi) == 56 && sizeof(CdCreateContext) == 48);
static_assert(sizeof(CdMediaHostApi) == 72 && offsetof(CdMediaHostApi, base) == 0);
static_assert(sizeof(CdMediaInfo) == 112);
static_assert(sizeof(CdInputStats) == 72);
static_assert(sizeof(CdInputStatsHostApi) == 80 && offsetof(CdInputStatsHostApi, media) == 0);
static_assert(sizeof(CdSettingValue) == 40);
static_assert(sizeof(CdInstanceApi) == 88 && sizeof(CdPluginApi) == 32);
#endif
#pragma pack(pop)
#endif
