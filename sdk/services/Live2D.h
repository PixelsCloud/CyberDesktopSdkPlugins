#pragma once
#include "sdk/PluginApi.h"

#define CD_SERVICE_LIVE2D "render.live2d"

#pragma pack(push, 8)

// Optional ABI 1 host service. Owner UI-thread calls. Strings are borrowed for
// configure only; the host copies them. All model parsing occurs in a child.
typedef struct CdLive2DOptions {
    uint32_t struct_size, fps;
    const char *directory, *motion, *expression;
    double scale, x, y;
    uint32_t paused, physics, follow, reserved;
} CdLive2DOptions;

typedef struct CdLive2DStatus {
    uint32_t struct_size, state; /* 0 empty, 1 loading, 2 ready, 3 failed */
    uint64_t frames;
    char message[512];
    char motions[1024];
    char expressions[1024];
} CdLive2DStatus;

typedef struct CdLive2DApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* configure)(void* context, const CdLive2DOptions* options);
    CdResult(CD_CALL* snapshot)(void* context, CdLive2DStatus* status);
    void(CD_CALL* pointer)(void* context, double x, double y, uint32_t tap);
} CdLive2DApi;

#pragma pack(pop)

#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(CdLive2DOptions) == 72);
static_assert(sizeof(CdLive2DStatus) == 2576);
static_assert(sizeof(CdLive2DApi) == 32);
#endif
