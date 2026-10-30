#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>

namespace cybercanvas {
using Microsoft::WRL::ComPtr;

void require(HRESULT result);

D2D1_COLOR_F color(uint32_t rgba);
}  // namespace cybercanvas
