#include <algorithm>
#include <cmath>

#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
MatrixGeometry arrangeMatrix(float width, float height, int rows) {
    MatrixGeometry result;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 16 || height <= 16)
        return result;

    const float plotWidth = width - 12, plotHeight = height - 13;
    result.rows = std::clamp(rows, 6, 24);
    // Rows determine the cell size. Keep a square lattice even on wide tiles;
    // a fixed column limit would stretch the horizontal gaps at higher densities.
    const float pitch = plotHeight / (result.rows - 1 + .78f);
    result.diameter = std::min(plotWidth, .78f * pitch);
    result.pitchY = (plotHeight - result.diameter) / (result.rows - 1);
    result.pitchX = result.pitchY;
    result.columns = int(std::floor((plotWidth - result.diameter) / result.pitchX)) + 1;

    const float gridWidth = (result.columns - 1) * result.pitchX + result.diameter;
    result.left += (plotWidth - gridWidth) * .5f;
    return result;
}
}  // namespace widgetplugin
