#ifndef CYBERDESKTOP_LIVE2D_ACTIONS_SERVICE_H
#define CYBERDESKTOP_LIVE2D_ACTIONS_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_LIVE2D_ACTIONS "render.live2d_actions"
#define CD_LIVE2D_ACTION_POINTER 1u
#define CD_LIVE2D_ACTION_STORY 2u
#define CD_LIVE2D_ACTION_PREVIEW 3u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* Optional UI-thread v1. Play one exact group/index ID from the loaded model's
   real motions list. Does not change the configured idle motion. The worker
   returns to idle after the clip finishes. Priorities 1..3: pointer, story,
   explicit preview; a lower-priority request cannot interrupt an active higher
   one. OK accepts the command, BUSY means model is not ready. Old pointer and
   render.live2d services retain their existing behavior. No guessed bindings. */
typedef struct CdLive2DActionsApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* perform)(void* context, const char* motion, uint32_t priority);
} CdLive2DActionsApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
