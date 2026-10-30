#pragma once
// Optional Windows DirectWrite helper. Font files remain external assets.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dwrite.h>

#include <filesystem>
#include <memory>
#include <string>

namespace cdsdk {
std::wstring fontWide(const std::string& value);

std::filesystem::path bundledFontPath(
    const std::filesystem::path& relative = L"assets/fonts/SiYuanZhenHei-Heavy/GenShinGothic-Heavy-2.ttf");

class WindowsFonts {
public:
    ~WindowsFonts();
    WindowsFonts(const WindowsFonts&);
    WindowsFonts& operator=(const WindowsFonts&);
    WindowsFonts(WindowsFonts&&) noexcept;
    WindowsFonts& operator=(WindowsFonts&&) noexcept;

    explicit WindowsFonts(IDWriteFactory* factory);

    WindowsFonts(IDWriteFactory* factory, const std::filesystem::path& path);

    // Explicit package-owned font, retained by a private DirectWrite collection.
    bool selectFile(IDWriteFactory* factory, const std::filesystem::path& path);

    bool select(IDWriteFactory* factory, const std::string& value);

    const wchar_t* family() const;

    IDWriteFontCollection* collection() const;

    DWRITE_FONT_WEIGHT weight() const;

    bool bundledAvailable(const std::string& id = "@siyuan-heavy") const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
