#ifndef CYBERDESKTOP_RENDER_IMAGES_SERVICE_H
#define CYBERDESKTOP_RENDER_IMAGES_SERVICE_H
#include <stddef.h>

#include "sdk/PluginApi.h"
#define CD_SERVICE_RENDER_IMAGES "render.images"
#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
/* UI-thread image-source diagnostics. Sources use a stable instance-local ID;
   reporting zero counts unregisters a destroyed source. Counts do not duplicate
   the memory allocator's lease accounting. */
typedef struct CdImageStoreMemory {
    uint32_t struct_size, version;
    uint64_t identity, decoded_bytes, active_bytes, cold_bytes, entries, pending, decodes, cache_hits, evictions;
} CdImageStoreMemory;

typedef void(CD_CALL* CdImageMemoryRead)(void* source_context, CdImageStoreMemory* output);

typedef struct CdRenderImagesApi {
    uint32_t struct_size, version;
    void(CD_CALL* update)(void* context, uint64_t source, uint32_t ready, uint32_t pending, uint32_t failed);
    /* Optional v1 tail. Read only during explicit diagnostics on the UI thread.
       The SDK unregisters before source destruction/rebind. Zero read removes
       the borrowed registration; a store identity deduplicates its consumers. */
    void(CD_CALL* memory_source)(void* context, uint64_t source, CdImageMemoryRead read, void* source_context);
} CdRenderImagesApi;

#define CD_RENDER_IMAGES_BASE_SIZE ((uint32_t)offsetof(CdRenderImagesApi, memory_source))
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
