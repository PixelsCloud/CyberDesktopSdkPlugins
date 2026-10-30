#pragma once
#include <CyberCanvas/Canvas.h>

#include <algorithm>
#include <array>

namespace pixeldesk {
inline constexpr cybercanvas::Size kBeatPartsSize{1414, 691};
inline constexpr std::array<cybercanvas::Rect, 6> kBeatParts{{{2, 2, 556, 143},
                                                              {672, 13, 272, 135},
                                                              {1127, 19, 285, 159},
                                                              {51, 331, 362, 356},
                                                              {575, 334, 368, 355},
                                                              {1142, 405, 239, 241}}};

void beatPart(cybercanvas::Canvas& layer, const cybercanvas::ImageData& image, unsigned index, cybercanvas::Rect area);
}  // namespace pixeldesk
