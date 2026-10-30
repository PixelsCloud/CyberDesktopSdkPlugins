#pragma once
#include "sdk/PluginApi.h"

#define CD_SERVICE_SETTINGS_CHOICES "ui.settings_choices"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* Optional UI-thread v1. Publish per-instance overrides for declared settings.
   UTF-8 JSON (<=256 KiB): {"fields":{"id":{"context":"scope",
   "choices":[{"value":"auto","label":"Automatic"}]}}}.
   Only CHOICE fields accept choices (1..257 unique, <=256 UTF-8 bytes each).
   context (<=256 UTF-8 bytes) is optional for all field types; changing it
   makes an open settings editor reread that field's current plugin value.
   Updates are atomic, copied during this call, and do not change the ABI or
   package descriptor. Publish again after a character/model inventory change.
   Older hosts return UNSUPPORTED; static descriptor defaults remain usable. */
typedef struct CdSettingsChoicesApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* publish)(void* context, const char* json, uint32_t length);
} CdSettingsChoicesApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
