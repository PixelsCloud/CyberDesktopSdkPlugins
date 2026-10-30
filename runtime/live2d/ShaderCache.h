#pragma once
#include <d3dcompiler.h>

HRESULT WINAPI CdLive2DCompile(const void* source, SIZE_T length, const char* name, const D3D_SHADER_MACRO* defines,
                               ID3DInclude* includes, const char* entry, const char* target, UINT flags,
                               UINT effectFlags, ID3DBlob** code, ID3DBlob** errors);
bool live2dShaderCacheHealthy();
// Applied only when compiling the pinned Framework. Its sources remain untouched.
#define D3DCompile CdLive2DCompile
