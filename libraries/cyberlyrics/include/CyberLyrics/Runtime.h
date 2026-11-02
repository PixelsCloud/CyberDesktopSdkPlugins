#pragma once

#ifdef _WIN32
#ifdef CYBERLYRICS_RUNTIME_BUILD
#define CYBERLYRICS_RUNTIME_API __declspec(dllexport)
#else
#define CYBERLYRICS_RUNTIME_API __declspec(dllimport)
#endif
#else
#define CYBERLYRICS_RUNTIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*CyberLyricsLineCallback)(void* context, double seconds, const char* utf8, int length);
CYBERLYRICS_RUNTIME_API int cyberlyrics_read_lrc(const char* path, CyberLyricsLineCallback callback, void* context,
                                                 char* error, int error_capacity);
CYBERLYRICS_RUNTIME_API const char* cyberlyrics_runtime_license(void);
#ifdef __cplusplus
}
#endif
