#pragma once

#include <sdk/PluginApi.h>

#ifdef CD_MECHANICAL_DESK_RUNTIME_BUILD
#define CD_MECHANICAL_DESK_API __declspec(dllexport)
#else
#define CD_MECHANICAL_DESK_API __declspec(dllimport)
#endif

extern "C" CD_MECHANICAL_DESK_API CdResult CD_CALL cd_mechanical_desk_create(const char* family, const char* kind,
                                                                             int rows, int columns,
                                                                             const CdCreateContext* context,
                                                                             CdInstanceApi* output, uint32_t size);
