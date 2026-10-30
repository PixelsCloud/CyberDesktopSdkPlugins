#include <sdk/Canvas.h>
#include <sdk/Scene.h>

#include <cstring>
#include <iostream>

namespace {
bool captured = false, allow = true;
int redraws = 0;

void CD_CALL redraw(void*) {
    ++redraws;
}

CdResult CD_CALL enable(void*, uint32_t) {
    return CD_OK;
}

CdResult CD_CALL capture(void*, uint32_t value) {
    captured = value && allow;
    return value && !allow ? CD_ERROR_UNSUPPORTED : CD_OK;
}

CdResult CD_CALL query(void*, const char* name, uint32_t version, void* output, uint32_t capacity) {
    if (std::strcmp(name, CD_SERVICE_INTERACTION) || version != 1 || capacity < sizeof(CdInteractionApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdInteractionApi*>(output) = {sizeof(CdInteractionApi), 1, enable, capture};
    return CD_OK;
}
}  // namespace

int main() {
    try {
        using namespace cybercanvas;
        CdServiceHostApi host{};
        host.input.media.base.struct_size = sizeof(host);
        host.input.media.base.request_redraw = redraw;
        host.query_service = query;
        CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, "test", "test", ".", &host.input.media.base};
        cdsdk::Canvas canvas;
        cdsdk::Scene scene;
        scene.bind(context);
        View slider;
        slider.kind = ViewKind::Slider;
        slider.key = 42;
        slider.layout.height = Length::points(4);
        const auto node = scene.add(0, slider);
        scene.arrange(canvas, {0, 0, 100, 100}, 2);
        std::vector<uint8_t> pixels(200 * 200 * 4);
        CdFrame frame{sizeof(frame), 200, 200, 800, pixels.size(), pixels.data(), 2};
        canvas.render(frame, {}, [&](auto&, auto) {
            scene.draw(canvas);
        });
        scene.value(node, .25);
        scene.value(node, .35);
        if (redraws != 1 || scene.needsLayout()) {
            return 1;
        }
        CdInput press{sizeof(press), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 60, 22};
        auto result = scene.input(press, {10, 20});
        if (!captured || !result.value || *result.value != .5 || result.key != 42) {
            return 1;
        }
        scene.visible(node, false);
        if (captured || scene.router().captured()) {
            return 1;
        }
        scene.visible(node, true);
        scene.arrange(canvas, {0, 0, 100, 100}, 2);
        allow = false;
        result = scene.input(press, {10, 20});
        if (captured || scene.router().captured() || result.value) {
            return 1;
        }
        allow = true;
        scene.input(press, {10, 20});
        scene.cancel();
        if (captured) {
            return 1;
        }
        const auto container = scene.add(0, {});
        scene.arrange(canvas, {0, 0, 100, 100}, 2);
        scene.input(press, {10, 20});
        scene.move(node, container);
        if (captured || scene.router().captured() || scene.view(node).value != .5 || scene.find(42) != node) {
            return 1;
        }
        scene.arrange(canvas, {0, 0, 100, 100}, 2);
        scene.input(press, {10, 20});
        if (!captured) {
            return 1;
        }
        scene.remove(container);
        if (captured || scene.router().captured() || scene.contains(node) || scene.find(42)) {
            return 1;
        }
        scene.arrange(canvas, {0, 0, 100, 100}, 2);
        canvas.render(frame, {}, [&](auto&, auto) {
            scene.draw(canvas);
        });
        const auto beforeAdd = redraws;
        const auto replacement = scene.add(0, slider);
        scene.clear();
        if (redraws != beforeAdd + 1 || replacement <= node || scene.size() != 1) {
            return 1;
        }
        CdHostApi old{sizeof(old), CD_PLUGIN_ABI_VERSION};
        context.host = &old;
        cdsdk::Scene fallback;
        fallback.bind(context);
        fallback.add(0, slider);
        fallback.arrange(canvas, {0, 0, 100, 100});
        press.x = 30;
        press.y = 2;
        result = fallback.input(press);
        if (!result.value || *result.value != .3) {
            return 1;
        }
        std::cout
            << "PASS scene shared router, translated input, host capture, visibility cancellation, rejected capture "
               "move/remove capture cancellation, structural redraw coalescing and old host\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
