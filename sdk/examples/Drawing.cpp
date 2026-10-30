#include "examples/Drawing.h"

namespace sample {
FontResource::FontResource() {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.GetAddressOf()))))
        throw std::runtime_error("DirectWrite font factory unavailable");
    fonts = std::make_unique<cdsdk::WindowsFonts>(factory.Get());
    if (!path.empty())
        registered = AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0;
    if (!zqk.empty())
        zqkRegistered = AddFontResourceExW(zqk.c_str(), FR_PRIVATE, nullptr) > 0;
}

FontResource::~FontResource() {
    if (registered)
        RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    if (zqkRegistered)
        RemoveFontResourceExW(zqk.c_str(), FR_PRIVATE, nullptr);
}

void rectangle(const CdFrame& frame, int x, int y, int width, int height, CdColor color) {
    const int left = std::clamp(x, 0, int(frame.width)), top = std::clamp(y, 0, int(frame.height));
    const int right = std::clamp(x + width, 0, int(frame.width)), bottom = std::clamp(y + height, 0, int(frame.height));
    for (int py = top; py < bottom; ++py)
        for (int px = left; px < right; ++px) {
            auto* pixel = frame.rgba + uint64_t(py) * frame.stride + uint64_t(px) * 4;
            pixel[0] = uint8_t(uint32_t(color.r) * color.a / 255);
            pixel[1] = uint8_t(uint32_t(color.g) * color.a / 255);
            pixel[2] = uint8_t(uint32_t(color.b) * color.a / 255);
            pixel[3] = color.a;
        }
}

void roundedRectangle(const CdFrame& frame, int x, int y, int width, int height, int radius, CdColor color) {
    radius = std::clamp(radius, 0, std::max(0, std::min(width, height) / 2));
    if (!radius) {
        rectangle(frame, x, y, width, height, color);
        return;
    }
    const double halfWidth = width * .5, halfHeight = height * .5;
    for (int row = 0; row < height; ++row) {
        const int py = y + row;
        if (py < 0 || py >= int(frame.height)) {
            continue;
        }
        for (int column = 0; column < width; ++column) {
            const int px = x + column;
            if (px < 0 || px >= int(frame.width)) {
                continue;
            }
            const double dx = std::abs(column + .5 - halfWidth) - halfWidth + radius;
            const double dy = std::abs(row + .5 - halfHeight) - halfHeight + radius;
            const double distance =
                std::hypot(std::max(dx, 0.), std::max(dy, 0.)) + std::min(std::max(dx, dy), 0.) - radius;
            const auto coverage = std::clamp(.5 - distance, 0., 1.);
            if (coverage <= 0.) {
                continue;
            }
            const auto opacity = uint32_t(std::lround(color.a * coverage));
            auto* pixel = frame.rgba + uint64_t(py) * frame.stride + uint64_t(px) * 4;
            const uint8_t channels[] = {color.r, color.g, color.b};
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = uint8_t((channels[channel] * opacity + pixel[channel] * (255 - opacity) + 127) / 255);
            }
            pixel[3] = uint8_t(opacity + (pixel[3] * (255 - opacity) + 127) / 255);
        }
    }
}

void text(const CdFrame& frame, const std::wstring& value, int y, int height, int fontPixels, CdColor color,
          FontResource& resource, const std::string& family) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = LONG(frame.width);
    info.bmiHeader.biHeight = -LONG(frame.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        return;
    }
    std::memset(bits, 0, size_t(frame.width) * frame.height * 4);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    resource.fonts->select(resource.factory.Get(), family);
    HFONT font = CreateFontW(-std::max(8, fontPixels), 0, 0, 0, resource.fonts->weight(), FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, resource.fonts->family());
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    RECT area{0, y, LONG(frame.width), y + height};
    DrawTextW(dc, value.c_str(), int(value.size()), &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const auto* mask = static_cast<const uint8_t*>(bits);
    for (uint32_t py = 0; py < frame.height; ++py)
        for (uint32_t px = 0; px < frame.width; ++px) {
            const auto index = (size_t(py) * frame.width + px) * 4;
            const uint32_t alpha = uint32_t(mask[index]) * color.a / 255;
            if (!alpha)
                continue;
            auto* pixel = frame.rgba + uint64_t(py) * frame.stride + uint64_t(px) * 4;
            const uint32_t remaining = 255 - alpha;
            pixel[0] = uint8_t(std::min(255u, uint32_t(color.r) * alpha / 255 + uint32_t(pixel[0]) * remaining / 255));
            pixel[1] = uint8_t(std::min(255u, uint32_t(color.g) * alpha / 255 + uint32_t(pixel[1]) * remaining / 255));
            pixel[2] = uint8_t(std::min(255u, uint32_t(color.b) * alpha / 255 + uint32_t(pixel[2]) * remaining / 255));
            pixel[3] = uint8_t(std::min(255u, alpha + uint32_t(pixel[3]) * remaining / 255));
        }
    SelectObject(dc, previousFont);
    SelectObject(dc, previous);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
}  // namespace sample
