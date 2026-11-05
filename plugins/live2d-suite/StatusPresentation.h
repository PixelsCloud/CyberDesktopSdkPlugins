#pragma once

#include <CyberCanvas/Types.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace cybercanvas {
class Canvas;
}

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
                               uint32_t foreground, std::wstring_view fontFamily = L"Microsoft YaHei UI");
}  // namespace live2dplugin
