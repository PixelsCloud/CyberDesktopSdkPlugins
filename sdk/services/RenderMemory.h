#ifndef CYBERDESKTOP_RENDER_MEMORY_SERVICE_H
#define CYBERDESKTOP_RENDER_MEMORY_SERVICE_H
#include "sdk/PluginApi.h"
#define CD_SERVICE_RENDER_MEMORY "render.memory"
#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
/* reserve returns an AddRef'd IUnknown lease; Release it with COM conventions.
   The host owns allocation/accounting. The lease survives instance teardown,
   and remains charged while an attached GPU texture is externally retained. */
typedef struct CdRenderMemoryApi {
    uint32_t struct_size, version;
    void*(CD_CALL* reserve)(void* context, uint64_t bytes);
    void* resource_context; /* Process-lifetime allocator context; independent of the widget. */
} CdRenderMemoryApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
