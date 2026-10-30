#ifndef CYBERDESKTOP_STORAGE_SERVICE_H
#define CYBERDESKTOP_STORAGE_SERVICE_H

#include "sdk/services/DataServices.h"

#define CD_SERVICE_VOLUME "storage.volume"
#define CD_VOLUME_UNAVAILABLE 1u
#define CD_VOLUME_REMOTE 2u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* SYSTEM_INFO capability. Root is an ASCII Windows drive root (C:\ or c:/).
   The host copies it before returning. Refresh is clamped to 10..3600 seconds.
   Local volumes only; network drives are reported unavailable without probing
   their capacity. No filesystem enumeration or per-plugin worker is required. */
typedef struct CdVolumeQuery {
    uint32_t struct_size, refresh_seconds;
    const char* root;
} CdVolumeQuery;

/* Strings are borrowed until the UI callback returns. info.struct_size is the
   full CdVolumeInfo size. STALE retains the last successful capacity; its
   observed timestamp stays unchanged. fetched timestamp is the latest attempt. */
typedef struct CdVolumeInfo {
    CdDataInfo info;
    const char* root;
    uint64_t available_bytes, total_bytes, free_bytes;
} CdVolumeInfo;

typedef void(CD_CALL* CdVolumeCallback)(void*, const CdVolumeInfo*);

typedef struct CdVolumeServiceApi {
    uint32_t struct_size, version;
    /* Initial delivery is queued, never inline. Unsubscribe through CdHostApi.
       After unsubscribe returns, no further callback may use this user pointer. */
    CdResult(CD_CALL* subscribe)(void*, const CdVolumeQuery*, CdVolumeCallback, void*, uint64_t*);
} CdVolumeServiceApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
