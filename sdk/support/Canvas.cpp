#include <sdk/Canvas.h>
#include <sdk/Language.h>
#include <sdk/RenderOutput.h>

#include <algorithm>

namespace cdsdk {
struct Canvas::Impl {
    OutputBinding output_;
    CdRenderSession session_{};
    cybercanvas::CanvasStats before_{};
    uint64_t frameBytes_ = 0;
};

Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}

Canvas::~Canvas() = default;

void Canvas::bindGpu(const CdCreateContext& context) {
    impl_->output_.bind(context);
    Engine::resourceAccount(impl_->output_.account());
}

bool Canvas::valid(const CdFrame& frame) const {
    return frame.struct_size >= sizeof(CdFrame) && Engine::valid(buffer(frame));
}

Canvas::Frame Canvas::frame(const CdFrame& output, CdRect safe) {
    check(output);
    select(output);
    return Frame(Engine::frame(buffer(output), rect(safe)), *this);
}

CdRect Canvas::begin(const CdFrame& output, CdRect safe) {
    check(output);
    select(output);
    const auto area = Engine::begin(buffer(output), rect(safe));
    return {area.x, area.y, area.width, area.height};
}

void Canvas::end(const CdFrame& output) {
    if (!valid(output)) {
        cancel();
        throw std::invalid_argument("Invalid plugin frame");
    }
    Engine::end(buffer(output));
    report();
}

void Canvas::image(const std::wstring& filename, CdRect area, uint32_t cell, uint32_t cells) {
    Engine::image(filename, rect(area), cell, cells);
}

void Canvas::image(const cybercanvas::ImageData& image, CdRect area, cybercanvas::ImagePlacement placement) {
    Engine::image(image, rect(area), placement);
}

void Canvas::text(const std::wstring& source, double size, CdRect area, uint32_t rgba, bool center, bool bold) {
    cybercanvas::TextStyle style;
    style.size = size;
    style.family = L"Microsoft YaHei";
    style.locale = drawingLocale();
    style.align = center ? cybercanvas::TextAlign::Center : cybercanvas::TextAlign::Start;
    style.bold = bold;
    Engine::text(displayText(source), style, rect(area), rgba);
}

void Canvas::text(const std::wstring& value, const cybercanvas::TextStyle& style, CdRect area, uint32_t rgba) {
    Engine::text(value, style, rect(area), rgba);
}

bool Canvas::save(const std::wstring& filename, const CdFrame& output, CdColor background) try {
    if (!valid(output)) {
        return false;
    }
    if (!background.a) {
        return Engine::save(filename, buffer(output));
    }
    std::vector<uint8_t> composite(size_t(output.width) * output.height * 4);
    const uint32_t channels[]{uint32_t(background.r) * background.a / 255, uint32_t(background.g) * background.a / 255,
                              uint32_t(background.b) * background.a / 255};
    for (uint32_t y = 0; y < output.height; ++y) {
        for (uint32_t x = 0; x < output.width; ++x) {
            const auto* source = output.rgba + size_t(y) * output.stride + x * 4;
            auto* pixel = composite.data() + (size_t(y) * output.width + x) * 4;
            const uint32_t remainder = 255 - source[3];
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = uint8_t(std::min(255u, source[channel] + channels[channel] * remainder / 255));
            }
            pixel[3] = uint8_t(source[3] + uint32_t(background.a) * remainder / 255);
        }
    }
    return Engine::save(filename, {output.width, output.height, output.width * 4, composite.size(), composite.data(),
                                   output.device_pixel_ratio});
} catch (...) {
    return false;
}

void Canvas::select(const CdFrame& output) {
    impl_->session_ = impl_->output_.session(output);
    impl_->frameBytes_ = uint64_t(output.width) * output.height * 4;
    Engine::targetForFrame(static_cast<ID2D1RenderTarget*>(impl_->session_.target), buffer(output),
                           impl_->session_.host_draw_scope != 0);
    impl_->before_ = stats();
}

void Canvas::report() {
    const auto after = stats();
    if (impl_->session_.output_epoch) {
        impl_->output_.complete(
            impl_->session_, CD_OUTPUT_PAINTED, impl_->session_.backend == CD_OUTPUT_GPU ? 0 : impl_->frameBytes_,
            after.spriteBatches - impl_->before_.spriteBatches, after.spriteInstances - impl_->before_.spriteInstances);
    }
}

cybercanvas::Rect Canvas::rect(CdRect value) {
    return {value.x, value.y, value.width, value.height};
}

cybercanvas::FrameBuffer Canvas::buffer(const CdFrame& value) {
    return {value.width, value.height, value.stride, value.capacity, value.rgba, value.device_pixel_ratio};
}

void Canvas::check(const CdFrame& value) const {
    if (!valid(value)) {
        throw std::invalid_argument("Invalid plugin frame");
    }
}

Canvas::Frame::Frame(Engine::Frame frame, Canvas& canvas) : frame_(std::move(frame)), canvas_(canvas) {}

CdRect Canvas::Frame::area() const {
    const auto value = frame_.area();
    return {value.x, value.y, value.width, value.height};
}

void Canvas::Frame::finish() {
    frame_.finish();
    canvas_.report();
}
}  // namespace cdsdk
