#ifndef CYBERDESKTOP_RENDER_OUTPUT_SERVICE_H
#define CYBERDESKTOP_RENDER_OUTPUT_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_RENDER_OUTPUT "render.output"
#define CD_OUTPUT_CPU 0u
#define CD_OUTPUT_GPU 1u
#define CD_OUTPUT_PAINTED 1u
#define CD_OUTPUT_UNCHANGED 2u
#define CD_OUTPUT_PENDING 3u
#define CD_OUTPUT_NEEDS_CPU 4u
#define CD_OUTPUT_FAILED 5u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
/* Optional service, independent of C ABI 1. Only the actual main frame may
   obtain the borrowed GPU target. Local/nested CPU frames receive CPU sessions.
   Epochs identify output allocations, not semantic layout changes. complete is
   legal only during the matching render callback, after a balanced Canvas end.
   No C++ ownership or allocator state crosses the DLL boundary. */
typedef struct CdRenderSession {
    uint32_t struct_size, version, backend, host_draw_scope;
    uint64_t output_epoch, content_version, layout_version;
    void* target;
} CdRenderSession;

typedef struct CdRenderReport {
    uint32_t struct_size, version, result, backend;
    uint64_t output_epoch, pixel_copy_bytes, sprite_batches, sprite_instances;
} CdRenderReport;

typedef struct CdRenderOutputApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* configure)(void* context);
    CdResult(CD_CALL* session)(void* context, const CdFrame* frame, CdRenderSession* output);
    CdResult(CD_CALL* complete)(void* context, const CdRenderReport* report);
} CdRenderOutputApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
