#ifndef CYBERDESKTOP_IMAGE_VIEWER_SERVICE_H
#define CYBERDESKTOP_IMAGE_VIEWER_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_IMAGE_VIEWER "app.image_viewer"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* Optional UI-thread v1 service. Opens one absolute local photo path in the
   bundled CyberImage viewer. UTF-8 bytes are borrowed only during this call,
   without a trailing NUL, length 1..65536. Embedded NULs and invalid UTF-8 are
   rejected. The host selects the executable and passes the path as one process
   argument; callers cannot supply commands, options or an executable. OK means
   process launch accepted; CyberImage handles its existing single-instance IPC. */
typedef struct CdImageViewerApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* open)(void* context, const char* path_utf8, uint32_t length);
} CdImageViewerApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
