#pragma once

#include <sdk/Canvas.h>

// Existing plugins share the public SDK canvas while retaining their current
// drawing calls. New plugins can include <sdk/Canvas.h> directly.
namespace living {
using cdsdk::color;
using cdsdk::require;
using Microsoft::WRL::ComPtr;
using Drawing = cdsdk::Canvas;
}  // namespace living
