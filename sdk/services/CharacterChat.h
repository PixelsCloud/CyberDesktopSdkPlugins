#ifndef CYBERDESKTOP_CHARACTER_CHAT_SERVICE_H
#define CYBERDESKTOP_CHARACTER_CHAT_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_CHARACTER_CHAT "ai.character_chat"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

typedef void(CD_CALL* CdCharacterChatCallback)(void* user, const char* json_utf8, uint32_t length);

/* Optional UI-thread v1 service. The host owns storage, credentials, scheduling
   and network workers. No key is accepted by this JSON command API. The host
   opens its own connection editor for the connection_settings command.
   Commands are bounded UTF-8 JSON objects (<=32KiB), snapshots <=256KiB;
   schema_version=1. Borrowed callback bytes must be copied synchronously.
   connect(NULL,NULL) cancels the instance and revokes callbacks before return.
   Commands are asynchronous; OK means accepted, snapshots report the outcome.
   Instance identity and resource root are supplied by the host, never by JSON.
   The service does not issue hidden retries or completion POSTs. */
typedef struct CdCharacterChatApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* connect)(void* context, CdCharacterChatCallback callback, void* user);
    CdResult(CD_CALL* command)(void* context, const char* json_utf8, uint32_t length);
} CdCharacterChatApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
