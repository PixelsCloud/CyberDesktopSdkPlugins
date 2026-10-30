#ifndef CYBERDESKTOP_PACKAGE_API_H
#define CYBERDESKTOP_PACKAGE_API_H
#include "sdk/PluginApi.h"
#if defined(_WIN32)
#if defined(CD_SDK_RUNTIME_BUILD)
#define CD_PACKAGE_API __declspec(dllexport)
#else
#define CD_PACKAGE_API __declspec(dllimport)
#endif
#else
#define CD_PACKAGE_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Runtime owns every handle/buffer. These functions bridge a /MT SDK module
   and an application using another CRT without sharing C++ allocations. */
typedef void* CdPackageHandle;
CD_PACKAGE_API CdResult CD_CALL cd_package_open(const char* utf8_path, CdPackageHandle* output, char* error,
                                                uint32_t error_capacity);
CD_PACKAGE_API CdResult CD_CALL cd_descriptor_open(const char* utf8_path, CdPackageHandle* output, char* error,
                                                   uint32_t error_capacity);
CD_PACKAGE_API void CD_CALL cd_package_close(CdPackageHandle handle);
CD_PACKAGE_API CdResult CD_CALL cd_package_descriptor_json(CdPackageHandle handle, const char** utf8, uint64_t* size);
CD_PACKAGE_API CdResult CD_CALL cd_package_extract(CdPackageHandle handle, const char* utf8_empty_directory,
                                                   char* error, uint32_t error_capacity);
#ifdef __cplusplus
}
#endif
#endif
