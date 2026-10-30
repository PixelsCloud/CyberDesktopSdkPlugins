#pragma once

#include <CyberCanvas/Canvas.h>
#include <sdk/PluginApi.h>

namespace cdsdk {
using cybercanvas::color;
using cybercanvas::require;

// The reusable engine has no SDK dependency. This adapter handles the host ABI
// and widget language while retaining existing native widget drawing calls.
class Canvas : public cybercanvas::Canvas {
public:
    using Engine = cybercanvas::Canvas;
    Canvas();
    ~Canvas();

    void bindGpu(const CdCreateContext& context);

    class Frame {
    public:
        Frame(Engine::Frame frame, Canvas& canvas);

        CdRect area() const;

        void finish();

    private:
        Engine::Frame frame_;
        Canvas& canvas_;
    };

    bool valid(const CdFrame& frame) const;

    Frame frame(const CdFrame& output, CdRect safe = {});

    CdRect begin(const CdFrame& output, CdRect safe);

    void end(const CdFrame& output);

    template <typename Paint>
    void render(const CdFrame& output, CdRect safe, Paint&& paint) {
        auto scope = frame(output, safe);
        std::forward<Paint>(paint)(*this, scope.area());
        scope.finish();
    }

    template <typename Paint>
    void clipped(CdRect area, Paint&& paint) {
        Engine::clipped(rect(area), std::forward<Paint>(paint));
    }

    void image(const std::wstring& filename, CdRect area, uint32_t cell = 0, uint32_t cells = 1);

    void image(const cybercanvas::ImageData& image, CdRect area, cybercanvas::ImagePlacement placement = {});

    void text(const std::wstring& source, double size, CdRect area, uint32_t rgba, bool center = true,
              bool bold = false);

    // Styled content is rendered verbatim, matching measureText(). Use the legacy
    // overload above for SDK UI phrases that need displayText() translation.
    void text(const std::wstring& value, const cybercanvas::TextStyle& style, CdRect area, uint32_t rgba);

    bool save(const std::wstring& filename, const CdFrame& output, CdColor background = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void select(const CdFrame& output);

    void report();

    static cybercanvas::Rect rect(CdRect value);

    static cybercanvas::FrameBuffer buffer(const CdFrame& value);

    void check(const CdFrame& value) const;
};
}  // namespace cdsdk
