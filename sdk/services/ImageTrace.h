#ifndef CYBERDESKTOP_IMAGE_TRACE_SERVICE_H
#define CYBERDESKTOP_IMAGE_TRACE_SERVICE_H
#include "sdk/PluginApi.h"
#define CD_SERVICE_IMAGE_TRACE "diagnostics.images"
#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
/* Optional, bounded diagnostic capture. Pixels/resources are borrowed only for
   the duration of observe(). The returned AddRef'd IUnknown lease owns no pixels;
   release it when the observed allocation dies. No instance context is retained. */
typedef struct CdImageTraceResource {
    uint32_t struct_size, domain; /* 0: CPU pixels; 1: GPU payload estimate. */
    const void* identity;
    uint64_t bytes;
    uint32_t width, height, stride, format;
    const char* source_utf8;
    const char* file_utf8;
    uint32_t line;
} CdImageTraceResource;

typedef void*(CD_CALL* CdImageTraceObserve)(const CdImageTraceResource* resource);

typedef struct CdImageTraceApi {
    uint32_t struct_size, version;
    CdImageTraceObserve observe;
} CdImageTraceApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
