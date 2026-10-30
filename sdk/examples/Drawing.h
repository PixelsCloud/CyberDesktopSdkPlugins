#pragma once

#include <sdk/PluginApi.h>
#include <wrl/client.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace sample {
struct FontResource {
    std::filesystem::path path = cdsdk::bundledFontPath();
    std::filesystem::path zqk = cdsdk::bundledFontPath(L"assets/fonts/ZQKFreeFont/ZQKfreefont-2.ttf");
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    std::unique_ptr<cdsdk::WindowsFonts> fonts;
    bool registered = false, zqkRegistered = false;

    FontResource();

    ~FontResource();
};

void rectangle(const CdFrame& frame, int x, int y, int width, int height, CdColor color);

void roundedRectangle(const CdFrame& frame, int x, int y, int width, int height, int radius, CdColor color);

void text(const CdFrame& frame, const std::wstring& value, int y, int height, int fontPixels, CdColor color,
          FontResource& resource, const std::string& family);
}  // namespace sample
