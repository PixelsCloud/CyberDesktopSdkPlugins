#ifndef CYBERDESKTOP_TEXT_INPUT_SERVICE_H
#define CYBERDESKTOP_TEXT_INPUT_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_TEXT_INPUT "ui.text_input"
#define CD_TEXT_CHANGED 1u
#define CD_TEXT_SUBMIT 2u
#define CD_TEXT_FOCUS_LOST 3u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

typedef void(CD_CALL* CdTextInputCallback)(void* user, uint32_t event, const char* utf8, uint32_t length);

/* Optional UI-thread service, v1. The host owns a real text editor and IME.
   Bounds use the plugin's local logical content coordinates, not screen pixels.
   At most one focused editor per host. Enter submits, Shift+Enter adds a line;
   an Enter consumed by IME never submits. UTF-8 <=16000 bytes/2000 code points.
   connect(NULL,NULL) revokes callbacks and focus synchronously; pause, movement,
   hide and destruction revoke focus. Text is retained until explicitly replaced.
   All strings are copied during calls/callbacks, with no allocation across ABI.
   configure does not focus or overwrite text; set_text does not emit CHANGED.
   focus may return UNSUPPORTED until the host has a valid presentation window. */
typedef struct CdTextInputApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* connect)(void* context, CdTextInputCallback callback, void* user);
    CdResult(CD_CALL* configure)(void* context, const CdRect* bounds, const char* placeholder, uint32_t length);
    CdResult(CD_CALL* set_text)(void* context, const char* utf8, uint32_t length);
    CdResult(CD_CALL* focus)(void* context, uint32_t focused);
} CdTextInputApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
