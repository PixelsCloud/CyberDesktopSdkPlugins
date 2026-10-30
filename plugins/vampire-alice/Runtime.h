#pragma once

#include <sdk/PluginApi.h>

#if defined(CD_ALICE_RUNTIME_BUILD)
#define CD_ALICE_API __declspec(dllexport)
#else
#define CD_ALICE_API __declspec(dllimport)
#endif

extern "C" CD_ALICE_API CdResult CD_CALL cd_alice_create(const char* family, const char* source, int kind, int rows,
                                                         int columns, const CdCreateContext* context,
                                                         CdInstanceApi* output, uint32_t size);
