#include <sdk/Icons.h>

#include <cstring>
#include <iostream>

namespace {
int rasterCalls = 0;

CdResult CD_CALL raster(void*, const char* name, uint32_t color, const CdFrame* out) {
    ++rasterCalls;
    if (std::strcmp(name, "check")) {
        return CD_ERROR_UNSUPPORTED;
    }
    for (uint32_t y = 0; y < out->height; ++y) {
        for (uint32_t x = 0; x < out->width; ++x) {
            auto* pixel = out->rgba + y * out->stride + x * 4;
            pixel[0] = uint8_t(color >> 24);
            pixel[1] = uint8_t(color >> 16);
            pixel[2] = uint8_t(color >> 8);
            pixel[3] = 255;
        }
    }
    return CD_OK;
}

CdResult CD_CALL query(void*, const char* name, uint32_t version, void* output, uint32_t capacity) {
    if (std::strcmp(name, CD_SERVICE_ICONS) || version != 1 || capacity < sizeof(CdIconsApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdIconsApi*>(output) = {sizeof(CdIconsApi), 1, raster};
    return CD_OK;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    };
    try {
        CdServiceHostApi host{};
        host.input.media.base.struct_size = sizeof(host);
        host.query_service = query;
        CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, "test", "test", ".", &host.input.media.base};
        cdsdk::Icons icons;
        icons.bind(context);
        Canvas canvas;
        std::vector<uint8_t> pixels;
        const auto draw = [&](double dpr, uint32_t color) {
            pixels.assign(size_t(80 * dpr) * size_t(40 * dpr) * 4, 0);
            FrameBuffer frame{uint32_t(80 * dpr), uint32_t(40 * dpr), uint32_t(80 * dpr) * 4,
                              pixels.size(),      pixels.data(),      dpr};
            bool rendered = false;
            canvas.render(frame, {}, [&](auto& canvas, auto) {
                rendered = icons.paint(canvas, {2, 2, 16, 16}, "check", color);
            });
            return rendered;
        };
        check("SDK icons raster through the optional host service", draw(1, 0xFF0000FF) && rasterCalls == 1);
        const auto red = pixels;
        check("same icon and DPI reuse CPU and native caches",
              draw(1, 0xFF0000FF) && rasterCalls == 1 && pixels == red);
        check("theme color is part of the icon cache key", draw(1, 0x00FF00FF) && rasterCalls == 2 && pixels != red);
        check("actual output DPI is part of the cache key", draw(2, 0x00FF00FF) && rasterCalls == 3);
        check("returning to a previous DPI reuses its source",
              draw(1, 0xFF0000FF) && rasterCalls == 3 && pixels == red);
        FrameBuffer frame{80, 40, 320, pixels.size(), pixels.data(), 1};
        bool unsupported = false;
        canvas.render(frame, {}, [&](auto& draw, auto) {
            unsupported = !icons.paint(draw, {0, 0, 10, 10}, "missing", 0xFFFFFFFF);
        });
        check("unsupported icons allow a text fallback", unsupported);
        CdHostApi old{sizeof(old), CD_PLUGIN_ABI_VERSION};
        context.host = &old;
        icons.bind(context);
        canvas.render(frame, {}, [&](auto& draw, auto) {
            check("old hosts retain usable icon-button labels",
                  !icons.paint(draw, {0, 0, 10, 10}, "check", 0xFFFFFFFF));
            IconButton(1, {0, 0, 60, 30}, "check", L"Apply").draw(draw, WidgetTheme::dark(), icons.painter());
        });
        check("fallback labels produce actual pixels", std::any_of(pixels.begin(), pixels.end(), [](uint8_t p) {
                  return p != 0;
              }));
        check("C ABI layouts remain unchanged",
              sizeof(CdIconsApi) == 16 && sizeof(CdHostApi) == 56 && sizeof(CdInstanceApi) == 88);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
