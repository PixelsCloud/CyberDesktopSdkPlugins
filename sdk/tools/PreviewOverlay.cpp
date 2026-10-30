#include "tools/PreviewOverlay.h"

namespace cdsdk {
void previewOverlay(cybercanvas::Canvas& canvas, const std::vector<DiagnosticSample>& samples, uint32_t width,
                    uint32_t height, double dpr, std::vector<uint8_t>& pixels) {
    const auto original = pixels;
    cybercanvas::FrameBuffer frame{width, height, width * 4, pixels.size(), pixels.data(), dpr};
    auto painting = canvas.frame(frame);
    canvas.blit({width, height, width * 4, original.size(), const_cast<uint8_t*>(original.data()), dpr},
                {0, 0, width / dpr, height / dpr});
    uint64_t frames = 0, nanoseconds = 0, bytes = 0;
    cybercanvas::TextStyle text;
    text.size = 10;
    for (const auto& sample : samples) {
        frames += sample.stats.values[CD_DIAG_FRAMES];
        nanoseconds += sample.stats.values[CD_DIAG_FRAME_NS];
        bytes += sample.stats.values[CD_DIAG_TRACKED_BYTES];
        for (const auto& node : sample.nodes) {
            const auto outline = [&](CdRect rect, uint32_t color, double stroke) {
                if (rect.width > 0 && rect.height > 0) {
                    cybercanvas::StrokeStyle style;
                    style.width = stroke;
                    canvas.strokeBox({rect.x, rect.y, rect.width, rect.height}, 0, color, style);
                }
            };
            outline(node.bounds, 0x40BFFFFF, 1);
            outline(node.clip, 0xFFC857CC, .7);
            outline(node.hit, 0xF06CFFFF, .7);
            if (node.state & CD_DEBUG_FOCUSED) {
                outline(node.hit, 0xFFFFFFFF, 2);
            }
            std::wstring label = std::to_wstring(node.id);
            if (node.error[0]) {
                label += L" ! resource error";
            }
            canvas.text(label, text, {node.bounds.x + 2, node.bounds.y + 1, 120, 14}, 0xFFFFFFFF);
        }
    }
    const auto label = L"DEV " + std::to_wstring(samples.size()) + L" sources | " + std::to_wstring(frames) +
                       L" frames | avg " + std::to_wstring(frames ? nanoseconds / frames / 1000 : 0) + L" us | " +
                       std::to_wstring(bytes / 1024) + L" KiB";
    const double y = std::max(0., height / dpr - 22);
    canvas.box(0, y, width / dpr, 22, 0, 0x10151EEA);
    canvas.text(label, text, {5, y + 3, width / dpr - 10, 16}, 0xFFFFFFFF);
    painting.finish();
}
}  // namespace cdsdk
