#ifndef CYBERDESKTOP_DIAGNOSTICS_SERVICE_H
#define CYBERDESKTOP_DIAGNOSTICS_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_DIAGNOSTICS "dev.diagnostics"
#define CD_DIAGNOSTIC_METRIC_COUNT 64u
#define CD_DEBUG_VISIBLE 1u
#define CD_DEBUG_ENABLED 2u
#define CD_DEBUG_INTERACTIVE 4u
#define CD_DEBUG_FOCUSED 8u
#define CD_DEBUG_LAYOUT_DIRTY 16u
#define CD_DEBUG_LOADING 32u
#define CD_DEBUG_ERROR 64u

typedef enum CdDiagnosticMetric {
    CD_DIAG_FRAMES,
    CD_DIAG_FRAME_NS,
    CD_DIAG_COPIES,
    CD_DIAG_COPY_NS,
    CD_DIAG_SURFACE_REBUILDS,
    CD_DIAG_SURFACE_BYTES,
    CD_DIAG_TEXT_MEASURES,
    CD_DIAG_TEXT_MEASURE_NS,
    CD_DIAG_TEXT_LAYOUTS,
    CD_DIAG_TEXT_LAYOUT_NS,
    CD_DIAG_TEXT_CACHE_HITS,
    CD_DIAG_TEXT_ENTRIES,
    CD_DIAG_UPLOADS,
    CD_DIAG_UPLOAD_NS,
    CD_DIAG_UPLOADED_BYTES,
    CD_DIAG_RESOURCE_BYTES,
    CD_DIAG_PIXEL_BYTES,
    CD_DIAG_LEGACY_BITMAP_BYTES,
    CD_DIAG_LEGACY_SOURCE_BYTES,
    CD_DIAG_TRACKED_BYTES,
    CD_DIAG_PEAK_BYTES,
    CD_DIAG_SCENE_LAYOUTS,
    CD_DIAG_SCENE_LAYOUT_NS,
    CD_DIAG_SCENE_PAINTS,
    CD_DIAG_SCENE_PAINT_NS,
    CD_DIAG_SCENE_MEASURES,
    CD_DIAG_SCENE_MEASURE_NS,
    CD_DIAG_LIST_PREFIX_BUILDS,
    CD_DIAG_LIST_PREFIX_NS,
    CD_DIAG_LIST_DRAWS,
    CD_DIAG_LIST_PAINT_NS,
    CD_DIAG_LIST_ROWS,
    CD_DIAG_IMAGE_REQUESTS,
    CD_DIAG_IMAGE_READY,
    CD_DIAG_IMAGE_PENDING,
    CD_DIAG_IMAGE_FAILED,
    CD_DIAG_IMAGE_REUSED,
    CD_DIAG_SLOT_BYTES,
    CD_DIAG_SLOT_PEAK_BYTES,
    CD_DIAG_REFERENCED_DECODE_NS,
    CD_DIAG_SHARED_DECODES,
    CD_DIAG_SHARED_DECODE_NS,
    CD_DIAG_SHARED_DECODE_BYTES,
    CD_DIAG_SHARED_DECODE_PEAK,
    CD_DIAG_INVALIDATIONS,
    CD_DIAG_FRAME_REQUESTS,
    CD_DIAG_COALESCED,
    CD_DIAG_LEGACY_DECODE_NS,
    CD_DIAG_CANCELLED_FRAMES,
    CD_DIAG_SHARED_PENDING,
    CD_DIAG_SHARED_ENTRIES,
    CD_DIAG_BITMAP_CACHE_HITS,
    CD_DIAG_LEGACY_DECODES,
    CD_DIAG_TEXT_DRAWS,
    CD_DIAG_SCENE_REFERENCED_BYTES,
    CD_DIAG_IMAGE_NOTIFICATIONS,
    CD_DIAG_IMAGE_POLLS,
    CD_DIAG_LIST_UPDATES,
    CD_DIAG_BITMAP_ENTRIES,
    CD_DIAG_TRANSIENT_BYTES,
    /* Physics counters are per World. NS/MAX_NS are optional CPU-side wall
       timings, never GPU time. Maximums must not be summed across Worlds. */
    CD_DIAG_PHYSICS_STEPS,
    CD_DIAG_PHYSICS_NS,
    CD_DIAG_PHYSICS_AWAKE,
    CD_DIAG_PHYSICS_MAX_NS
} CdDiagnosticMetric;

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CdDiagnosticStats {
    uint32_t struct_size, version;
    uint64_t values[CD_DIAGNOSTIC_METRIC_COUNT];
} CdDiagnosticStats;

/* Logical layout bounds, effective clip, and interactive bounding box. The
   latter does not replace path/ellipse hit testing. IDs include scene root 0. */
typedef struct CdDebugNode {
    uint32_t struct_size, id, kind, state;
    CdRect bounds, clip, hit;
    char name[128], resource[512], error[256];
} CdDebugNode;

typedef CdResult(CD_CALL* CdDiagnosticCallback)(void* user, CdDiagnosticStats* stats, CdDebugNode* nodes,
                                                uint32_t capacity, uint32_t* required);

/* Optional developer service; all calls, including sampling, run on the owner
   UI thread. Registration retains callback/user until unregister; unregister
   must stop delivery before DLL unload. Hosts allow <=16 sources per instance.
   Sample with nodes=NULL/capacity=0 to get required capacity (<=4096). BUFFER
   leaves the node buffer untouched, still returning stats and required count.
   Durations are inclusive nanoseconds; shared decoder values are NOT additive
   across sources/instances. Tracked bytes are known pixel payload, not RSS or
   driver/DirectWrite allocations. Unused metric slots remain zero. */
typedef struct CdDiagnosticsApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* register_source)(void* context, CdDiagnosticCallback callback, void* user, uint64_t* token);
    void(CD_CALL* unregister_source)(void* context, uint64_t token);
} CdDiagnosticsApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
