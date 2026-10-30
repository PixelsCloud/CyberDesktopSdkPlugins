#pragma once
#include <sdk/PluginApi.h>
#if defined(CD_MECHA_RUNTIME_BUILD)
#define CD_MECHA_RUNTIME_API __declspec(dllexport)
#else
#define CD_MECHA_RUNTIME_API __declspec(dllimport)
#endif
extern "C" CD_MECHA_RUNTIME_API CdResult CD_CALL cd_mecha_create(const char* family, const char* source, int kind,
                                                                 int rows, int columns, const CdCreateContext* context,
                                                                 CdInstanceApi* output, uint32_t size);
// Shared artwork/controller factory. The original export remains ABI-compatible
// with the already installed AERIS built-ins.
extern "C" CD_MECHA_RUNTIME_API CdResult CD_CALL cd_artwork_create(const char* family, const char* source, int kind,
                                                                   int rows, int columns,
                                                                   const CdCreateContext* context,
                                                                   CdInstanceApi* output, uint32_t size);
