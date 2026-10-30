#pragma once

#include <CyberCanvas/Canvas.h>

#include "tools/Diagnostics.h"

namespace cdsdk {
void previewOverlay(cybercanvas::Canvas& canvas, const std::vector<DiagnosticSample>& samples, uint32_t width,
                    uint32_t height, double dpr, std::vector<uint8_t>& pixels);
}  // namespace cdsdk
