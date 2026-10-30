#pragma once

#include <sdk/PluginApi.h>

#ifdef CD_STREETLINK_RUNTIME_BUILD
#define CD_STREETLINK_API __declspec(dllexport)
#else
#define CD_STREETLINK_API __declspec(dllimport)
#endif

extern "C" CD_STREETLINK_API CdResult CD_CALL cd_streetlink_create(const CdCreateContext* context,
                                                                   CdInstanceApi* output, uint32_t size);
