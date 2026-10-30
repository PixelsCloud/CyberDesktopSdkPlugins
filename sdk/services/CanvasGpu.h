#ifndef CYBERDESKTOP_CANVAS_GPU_SERVICE_H
#define CYBERDESKTOP_CANVAS_GPU_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_CANVAS_GPU "render.canvas.gpu"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
/* Optional Windows UI-thread service. Existing render ABI and RGBA fallback
   remain valid. configure opts in; current_target borrows an ID2D1RenderTarget
   only during the host's GPU render callback. The plugin must not retain the
   target for drawing outside that callback or read pixels back. Target-bound
   caches may retain COM references, and must be released on target replacement.
   host_draw_scope=1 means the host owns a frame-wide BeginDraw/EndDraw batch;
   otherwise the Canvas owns a balanced pair. The host owns output textures,
   state restoration, and the compatibility CPU scratch (not rendered pixels).
   Outside GPU rendering current_target returns null (CPU previews/export).
   Device loss returns to the CPU path and permits a later GPU retry. */
typedef struct CdCanvasGpuApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* configure)(void* context, uint32_t enabled);
    void*(CD_CALL* current_target)(void* context);
    uint32_t(CD_CALL* host_draw_scope)(void* context);
} CdCanvasGpuApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
