#pragma once

#include <CyberCanvas/Canvas.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace live2dplugin {
struct FailureExplanation {
    std::wstring reason;
    std::wstring advice;
};

FailureExplanation explainFailure(std::string_view detail);

struct StatusPresentation {
    cybercanvas::Rect bounds;
    cybercanvas::TextMetrics metrics;
    double fontSize = 13;
};

StatusPresentation drawFailure(cybercanvas::Canvas& canvas, cybercanvas::Rect area, std::string_view detail,
                               uint32_t foreground);
}  // namespace live2dplugin
