#ifndef CYBERDESKTOP_ENVIRONMENT_SERVICE_H
#define CYBERDESKTOP_ENVIRONMENT_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_ENVIRONMENT "host.environment"
#define CD_ENV_THEME_UNKNOWN 0u
#define CD_ENV_THEME_LIGHT 1u
#define CD_ENV_THEME_DARK 2u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CdEnvironmentSnapshot {
    uint32_t struct_size, version;
    uint64_t unix_ms, monotonic_ms;
    int32_t utc_offset_minutes;
    uint32_t theme;
    char locale[32];
} CdEnvironmentSnapshot;

/* Optional owner-thread environment. Production returns system time; preview
   can freeze wall time while its supplied monotonic time continues advancing.
   UTC offsets range from -14h to +14h. Locale is a terminated UTF-8 BCP-47 tag.
   Callers own the snapshot; no pointer is retained. Old hosts use SDK fallback. */
typedef struct CdEnvironmentApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* snapshot)(void* context, CdEnvironmentSnapshot* output);
} CdEnvironmentApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
