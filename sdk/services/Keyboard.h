#ifndef CYBERDESKTOP_KEYBOARD_SERVICE_H
#define CYBERDESKTOP_KEYBOARD_SERVICE_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_KEYBOARD "ui.keyboard"
#define CD_UI_FOCUS 1u
#define CD_UI_KEY 2u
#define CD_UI_ACTION 3u
#define CD_UI_PRESS 1u
#define CD_UI_SET_FOCUS 2u
#define CD_UI_SET_VALUE 3u
#define CD_UI_INCREASE 4u
#define CD_UI_DECREASE 5u
/* Key codes 1..10: Tab, Left, Right, Up, Down, Enter, Space, Escape, Home, End. */
#define CD_KEY_TAB 1u
#define CD_KEY_LEFT 2u
#define CD_KEY_RIGHT 3u
#define CD_KEY_UP 4u
#define CD_KEY_DOWN 5u
#define CD_KEY_ENTER 6u
#define CD_KEY_SPACE 7u
#define CD_KEY_ESCAPE 8u
#define CD_KEY_HOME 9u
#define CD_KEY_END 10u
#define CD_UI_ENABLED 1u
#define CD_UI_FOCUSABLE 2u
#define CD_UI_FOCUSED 4u
#define CD_UI_SELECTED 8u
#define CD_UI_CHECKED 16u
#define CD_UI_BUSY 32u
#define CD_UI_ERROR 64u
/* Roles 0..7: group, text, image, button, toggle, slider, list, list item. */
#define CD_ROLE_GROUP 0u
#define CD_ROLE_TEXT 1u
#define CD_ROLE_IMAGE 2u
#define CD_ROLE_BUTTON 3u
#define CD_ROLE_TOGGLE 4u
#define CD_ROLE_SLIDER 5u
#define CD_ROLE_LIST 6u
#define CD_ROLE_LIST_ITEM 7u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CdUiEvent {
    uint32_t struct_size, kind, key, modifiers, node, reserved;
    double value;
} CdUiEvent;

typedef CdResult(CD_CALL* CdUiCallback)(void* user, const CdUiEvent* event);

typedef struct CdSemanticNode {
    uint32_t struct_size, id, parent, role, state, reserved;
    CdRect bounds;
    const char* name;
    const char* value;
    double number, minimum, maximum, step;
} CdSemanticNode;

/* UI thread only. connect(NULL,NULL) unregisters and clears the snapshot before
   DLL destruction. A replacement callback revokes the old focus. request_focus
   is a request: only CD_UI_FOCUS(value=1) grants keyboard delivery. Loss, hide,
   pause and destroy revoke it. Key/action callbacks return OK only if consumed.
   publish copies UTF-8 strings and nodes synchronously (<=4096 nodes, <=4096
   bytes/string); pointers need only survive the call. IDs are stable nonzero
   handles, parents must precede children. Full snapshots remove stale objects.
   The host exposes these semantics through its system accessibility bridge.
   Optional service: unsupported hosts keep the existing pointer-only ABI. */
typedef struct CdKeyboardApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* connect)(void* context, CdUiCallback callback, void* user);
    CdResult(CD_CALL* request_focus)(void* context, uint32_t focused);
    CdResult(CD_CALL* publish)(void* context, const CdSemanticNode* nodes, uint32_t count);
} CdKeyboardApi;
#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
