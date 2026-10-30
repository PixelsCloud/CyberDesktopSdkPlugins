#ifndef CYBERDESKTOP_IMAGE_SKINS_SERVICE_H
#define CYBERDESKTOP_IMAGE_SKINS_SERVICE_H

#include "sdk/PluginApi.h"

#define CD_SERVICE_IMAGE_SKINS "render.image_skins"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* IMAGE_SKINS capability. UI-thread, optional image-only overrides.
   Sources must be existing files within this plugin package assets directory;
   canonical paths escaping that directory are rejected. Plugin IDs are not
   privileges. Overrides are restricted by the host-installed image contract. The caller owns the UTF-8 output
   buffer. A zero revision means the original image. Notifications never alter
   plugin settings, layout, subscriptions or native state. */
typedef void(CD_CALL* CdImageSkinChanged)(void* user, uint64_t revision);

typedef struct CdImageSkinsApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* resolve)(void* context, const char* source, char* output, uint32_t capacity, uint64_t* revision);
    CdResult(CD_CALL* subscribe)(void* context, CdImageSkinChanged callback, void* user, uint64_t* token);
    void(CD_CALL* unsubscribe)(void* context, uint64_t token);
} CdImageSkinsApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
