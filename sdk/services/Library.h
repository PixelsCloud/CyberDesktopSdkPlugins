#ifndef CYBERDESKTOP_LIBRARY_SERVICE_H
#define CYBERDESKTOP_LIBRARY_SERVICE_H

#include "sdk/services/DataServices.h"

#define CD_SERVICE_LIBRARY "media.library"

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* MEDIA_LIBRARY capability. UI-thread calls, queued callbacks. The host copies
   the absolute UTF-8 directory (up to 4096 bytes); empty selects Pictures.
   The caller supplies a physical-pixel edge, 1..2048, including its actual DPI.
   Directory discovery and oriented image decoding run in background workers. */
typedef struct CdLibraryQuery {
    uint32_t struct_size, recursive, pixel_edge, reserved;
    const char* directory;
} CdLibraryQuery;

/* One bounded page: the selected photo. Pixels are tightly packed premultiplied
   BGRA8, borrowed until callback return. Empty READY is valid. On error old
   pixels may remain STALE. Partial means the directory scan reached its budget.
   index/count refer to the current directory snapshot, not a permanent file ID.
   revision changes with file metadata; directory paths never confer DB access. */
typedef struct CdLibraryInfo {
    CdDataInfo info;
    const char *path, *error_code;
    uint64_t index, count, revision;
    uint32_t width, height, partial, reserved;
    const uint8_t* pixels;
} CdLibraryInfo;

typedef void(CD_CALL* CdLibraryCallback)(void*, const CdLibraryInfo*);

typedef struct CdLibraryServiceApi {
    uint32_t struct_size, version;
    CdResult(CD_CALL* subscribe)(void*, const CdLibraryQuery*, CdLibraryCallback, void*, uint64_t*);
    /* Select an absolute position in this subscription; no scan is triggered.
       Different subscribers keep independent selection and target dimensions. */
    CdResult(CD_CALL* select)(void*, uint64_t token, uint64_t index, uint32_t pixel_edge);
    /* Explicit refresh; identical directory requests coalesce, 15-second floor.
       Background directory refresh otherwise occurs at most once per minute. */
    CdResult(CD_CALL* refresh)(void*, uint64_t token);
} CdLibraryServiceApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
