#ifndef CYBERDESKTOP_ICONS_SERVICE_H
#define CYBERDESKTOP_ICONS_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_ICONS "ui.icons"
#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* UI-thread service, no external capability. Renders a bundled Lucide icon
   through CyberGlass/Icons.h into caller-owned premultiplied RGBA pixels.
   Names use lowercase ASCII letters/digits/hyphens, at most 64 characters.
   Output dimensions are physical pixels, limited to 256 in each direction.
   Unknown names return CD_ERROR_UNSUPPORTED. Color is 0xRRGGBBAA. */
typedef struct CdIconsApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* raster)(void* context, const char* name, uint32_t color, const CdFrame* output);
} CdIconsApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
