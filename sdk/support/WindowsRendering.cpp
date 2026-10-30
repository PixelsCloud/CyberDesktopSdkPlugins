#include "sdk/WindowsRendering.h"

namespace cdsdk {
void configureAntialiasing(ID2D1RenderTarget* target) {
    if (!target) {
        return;
    }
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}
}  // namespace cdsdk
