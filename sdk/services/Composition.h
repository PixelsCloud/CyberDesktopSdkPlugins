#ifndef CYBERDESKTOP_COMPOSITION_SERVICE_H
#define CYBERDESKTOP_COMPOSITION_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_COMPOSITION "render.composition"
#define CD_COMPOSITION_ALL 0u
#define CD_COMPOSITION_BACKGROUND 1u
#define CD_COMPOSITION_FOREGROUND 2u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* Optional UI-thread v1. configure enables a local Live2D layer between two
   plugin render passes. Bounds are local logical content coordinates. Host
   calls the existing render ABI separately for BACKGROUND and FOREGROUND;
   current_pass reports the active pass, ALL outside layered GPU composition.
   Foreground pixels must be transparent where the model should remain visible.
   invalidate flags are BACKGROUND|FOREGROUND; it also invalidates ALL fallback.
   The host retains CPU layers and blends the shared Live2D GPU texture directly,
   with no model readback in the Desktop GPU host. Public CPU previews/export
   may implement the same passes with bounded model readback and CPU composition;
   those paths do not advertise render.canvas.gpu. Missing service leaves ordinary plugin rendering.
   Disabling restores the existing full-instance Live2D behavior for old plugins.
   The plugin must still configure render.live2d for the actual model/session. */
typedef struct CdCompositionApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* configure)(void* context, const CdRect* live2d_bounds, uint32_t enabled);
    uint32_t(CD_CALL* current_pass)(void* context);
    void(CD_CALL* invalidate)(void* context, uint32_t layers);
} CdCompositionApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
