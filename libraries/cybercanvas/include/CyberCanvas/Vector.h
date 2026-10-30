#pragma once

#include "CyberCanvas/AnimatedImages.h"

namespace cybercanvas {
enum class VectorKind { Svg, Lottie };
// A CPU ThorVG source, consumed only by AnimationStore's worker. Output uses
// aspect-fit and centered transparent margins at the requested physical size.
// SVG supports shapes/groups/gradients/clips/masks/use; script, CSS stylesheets,
// foreign objects, text, filters and image tags are rejected. Lottie supports
// 2D transforms/shapes/masks/gradients and WIC-decoded package/embedded images;
// expressions, text/font assets, audio, 3D and layer effects are rejected.
AnimationSourceFactory vectorAnimation(std::filesystem::path relative, VectorKind kind);
}  // namespace cybercanvas
