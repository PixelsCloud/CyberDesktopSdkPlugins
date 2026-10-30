#pragma once

#include <sdk/PluginApi.h>

#if defined(CD_REDLINE_RUNTIME_BUILD)
#define CD_REDLINE_API __declspec(dllexport)
#else
#define CD_REDLINE_API __declspec(dllimport)
#endif

extern "C" CD_REDLINE_API CdResult CD_CALL cd_redline_create(const char* family, const char* source, int kind, int rows,
                                                             int columns, const CdCreateContext* context,
                                                             CdInstanceApi* output, uint32_t size);
