#ifndef CYBERDESKTOP_AMBIENT_AUDIO_H
#define CYBERDESKTOP_AMBIENT_AUDIO_H
#include "sdk/PluginApi.h"

#define CD_SERVICE_AMBIENT "audio.ambient"
#define CD_AMBIENT_STOPPED 0u
#define CD_AMBIENT_PLAYING 1u
#define CD_AMBIENT_PAUSED 2u
#define CD_AMBIENT_PLAY 1u
#define CD_AMBIENT_PAUSE 2u
#define CD_AMBIENT_STOP 3u
#define CD_AMBIENT_TRACKS 6u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* Tracks: rain, ocean, stream, birds/forest, fireplace, wind.
   Levels and master are 0..1; at most four levels may be nonzero.
   Deadlines are UTC epoch seconds and continue while a tile is hidden.
   MEDIA_CONTROL capability is required. Display pause does not close groups. */
typedef struct CdAmbientConfig {
    uint32_t struct_size, timer_minutes;
    float master, levels[CD_AMBIENT_TRACKS];
} CdAmbientConfig;

typedef struct CdAmbientSnapshot {
    uint32_t struct_size, status, error, reserved;
    CdAmbientConfig config;
    uint64_t deadline;
} CdAmbientSnapshot;

typedef struct CdAmbientApi {
    uint32_t struct_size, version;
    /* Empty group: private instance; "rain", "fire", "stars": shared sound scene. */
    CdResult(CD_CALL* open)(void*, const char* group, uint64_t*);
    void(CD_CALL* close)(void*, uint64_t);
    CdResult(CD_CALL* configure)(void*, uint64_t, const CdAmbientConfig*);
    CdResult(CD_CALL* command)(void*, uint64_t, uint32_t);
    CdResult(CD_CALL* snapshot)(void*, uint64_t, CdAmbientSnapshot*);
} CdAmbientApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
