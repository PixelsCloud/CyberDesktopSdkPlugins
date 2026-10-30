#ifndef CYBERDESKTOP_RENDER_IMAGES_SERVICE_H
#define CYBERDESKTOP_RENDER_IMAGES_SERVICE_H
#include "sdk/PluginApi.h"
#define CD_SERVICE_RENDER_IMAGES "render.images"
#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
/* UI-thread image-source diagnostics. Sources use a stable instance-local ID;
   reporting zero counts unregisters a destroyed source. Counts do not duplicate
   the memory allocator's lease accounting. */
typedef struct CdRenderImagesApi {
    uint32_t struct_size, version;
    void(CD_CALL* update)(void* context, uint64_t source, uint32_t ready, uint32_t pending, uint32_t failed);
} CdRenderImagesApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
