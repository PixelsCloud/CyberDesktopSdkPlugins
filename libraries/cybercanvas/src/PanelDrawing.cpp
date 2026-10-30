#include "CyberCanvas/PanelDrawing.h"

#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/PanelLayout.h>

#include <array>

namespace cybercanvas {
void panelImage(Canvas& canvas, const ImageData& image, Rect area, Size design, Insets edges, double scale) {
    detail::layoutRect(area);
    for (double edge : {edges.left, edges.top, edges.right, edges.bottom}) {
        detail::layoutExtent(edge);
    }
    if (area.width <= 0 || area.height <= 0 || !std::isfinite(scale) || scale <= 0 || !std::isfinite(design.width) ||
        !std::isfinite(design.height) || design.width <= edges.left + edges.right ||
        design.height <= edges.top + edges.bottom) {
        throw std::invalid_argument("Invalid panel image");
    }
    if (edges.left + edges.right > 0) {
        scale = std::min(scale, area.width / (edges.left + edges.right));
    }
    if (edges.top + edges.bottom > 0) {
        scale = std::min(scale, area.height / (edges.top + edges.bottom));
    }
    const std::array<double, 4> sourceX{0, edges.left, design.width - edges.right, design.width};
    const std::array<double, 4> sourceY{0, edges.top, design.height - edges.bottom, design.height};
    std::array<double, 4> targetX{area.x, area.x + edges.left * scale, area.x + area.width - edges.right * scale,
                                  area.x + area.width};
    std::array<double, 4> targetY{area.y, area.y + edges.top * scale, area.y + area.height - edges.bottom * scale,
                                  area.y + area.height};
    D2D1_MATRIX_3X2_F transform;
    canvas.target->GetTransform(&transform);
    const double dpi = canvas.devicePixelRatio();
    if (transform._12 == 0 && transform._21 == 0 && transform._11 > 0 && transform._22 > 0) {
        for (size_t edge : {size_t(1), size_t(2)}) {
            targetX[edge] =
                std::clamp((std::round((targetX[edge] * transform._11 + transform._31) * dpi) / dpi - transform._31) /
                               transform._11,
                           targetX.front(), targetX.back());
            targetY[edge] =
                std::clamp((std::round((targetY[edge] * transform._22 + transform._32) * dpi) / dpi - transform._32) /
                               transform._22,
                           targetY.front(), targetY.back());
        }
    }
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            const Rect piece{targetX[column], targetY[row], targetX[column + 1] - targetX[column],
                             targetY[row + 1] - targetY[row]};
            const double sourceWidth = sourceX[column + 1] - sourceX[column];
            const double sourceHeight = sourceY[row + 1] - sourceY[row];
            if (piece.width <= 0 || piece.height <= 0 || sourceWidth <= 0 || sourceHeight <= 0) {
                continue;
            }
            const double sx = piece.width / sourceWidth, sy = piece.height / sourceHeight;
            canvas.clipped(piece, [&] {
                canvas.image(image,
                             {piece.x - sourceX[column] * sx, piece.y - sourceY[row] * sy, design.width * sx,
                              design.height * sy},
                             {.fit = ImageFit::Stretch, .clip = false});
            });
        }
    }
}
}  // namespace cybercanvas
