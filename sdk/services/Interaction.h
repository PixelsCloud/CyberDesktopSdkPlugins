#ifndef CYBERDESKTOP_INTERACTION_SERVICE_H
#define CYBERDESKTOP_INTERACTION_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_INTERACTION "ui.interaction"
/* Additive input event; existing event values and ABI layouts are unchanged. */
#define CD_INPUT_CANCEL 6u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* UI-thread only. Enable during creation to receive real press/move/release.
   Capture may be claimed synchronously during a left press, and released at
   any time. The host then reserves the gesture for this instance instead of
   moving its tile. Cancel is delivered on lost capture, hide, or layout change.
   Without this optional service old hosts deliver stationary clicks only. */
typedef struct CdInteractionApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* enable)(void* context, uint32_t enabled);
    CdResult(CD_CALL* capture)(void* context, uint32_t captured);
} CdInteractionApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
