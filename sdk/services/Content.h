#ifndef CYBERDESKTOP_CONTENT_SERVICE_H
#define CYBERDESKTOP_CONTENT_SERVICE_H

#include "sdk/services/DataServices.h"

#define CD_SERVICE_CONTENT "content.feeds"
#define CD_CONTENT_FEEDS 0u
#define CD_CONTENT_HACKER_NEWS 1u
#define CD_CONTENT_GITHUB 2u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* CONTENT capability. All strings are copied during subscribe. Feed URLs are
   HTTP(S), without credentials, at most 6 x 2048 bytes. Order is meaningful.
   Refresh defaults to 900 seconds and is clamped to 300..10800. */
typedef struct CdContentQuery {
    uint32_t struct_size, kind, refresh_seconds, url_count;
    const char* const* urls;
    const char* period;
    const char* language;
    const char* order;
} CdContentQuery;

typedef struct CdContentItem {
    const char *title, *url, *summary, *source, *language, *stars, *growth, *discussion, *date;
    int64_t published;
    int32_t score, comments;
} CdContentItem;

/* At most 30 items. Strings and item storage are borrowed until callback return.
   Empty successful feeds are VALID. STALE retains the last successful items.
   Local pagination/bookmarks do not cause fetches. Initial delivery is queued. */
typedef struct CdContentInfo {
    CdDataInfo info;
    const char* title;
    const char* error_code;
    const CdContentItem* items;
    uint32_t item_count, partial;
} CdContentInfo;

typedef void(CD_CALL* CdContentCallback)(void*, const CdContentInfo*);

typedef struct CdContentServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const CdContentQuery*, CdContentCallback, void*, uint64_t*);
    /* Uses this instance's subscription token. Shared refresh is throttled. */
    CdResult(CD_CALL* refresh)(void*, uint64_t);
    /* Explicit user action only; HTTP(S), no credentials, dispatched off UI.
       Success means accepted, not that the external browser finished opening. */
    CdResult(CD_CALL* open_url)(void*, const char*);
} CdContentServiceApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
