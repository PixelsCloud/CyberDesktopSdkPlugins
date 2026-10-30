#pragma once
#include <CyberCanvas/Types.h>
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cybercanvas {
class Canvas;
}

namespace clockplugin {
enum class Display { Classic, Lcd, Pixel, PixelRound };

constexpr bool isPixel(Display display) {
    return display == Display::Pixel || display == Display::PixelRound;
}

struct Spec {
    const char* id;
    bool analog;
    int rows, columns;
    bool flip = false;
    Display display = Display::Classic;
};

struct Time {
    int year = 2026, month = 10, day = 12, weekday = 1, hour = 10, minute = 8, second = 30, millisecond = 0;
};

struct Options {
    bool twelveHour = false, seconds = false, date = true, weekday = true, smooth = false, themeColors = true,
         adaptiveSeconds = true;
    std::string layout = "auto", dateFormat = "auto", timezone = "local";
    std::string fontFamily = "@siyuan-heavy", language = "zh-CN";
    int utcOffset = 480;
    double sizePercent = 100;
    double reliefPercent = 65;
    double flipDurationMs = 500;
    double glowPercent = 70;
    double cornerRadius = 5;
    CdColor cardColor{248, 248, 248, 255};
    CdColor primary{43, 41, 36, 255}, secondary{118, 107, 96, 255}, accent{201, 110, 55, 255};
};

struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};

struct Geometry {
    Rect dial, time, seconds, date, weekday;
    bool stacked = false, compactDate = false;
};

Geometry arrange(Spec spec, double width, double height, const Options& options);
cdsdk::Settings defaults(Spec spec);
Options options(const cdsdk::Settings& values, CdColor themeText);
std::wstring dateCaption(Time time, const Options& options, bool compact);
Time currentTime(const Options& options);

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bindOutput(const CdCreateContext& context);
    void drawElectronic(cybercanvas::Canvas& canvas, Spec spec, const Options& options, Time time,
                        cybercanvas::Rect area);
    Renderer(const Renderer&) = delete;
    bool render(Spec spec, const Options& options, Time time, const CdFrame& frame, Rect safe = {},
                std::string* error = nullptr);
    void privateFonts(const std::filesystem::path& latin, const std::filesystem::path& cjk, std::string family);
    bool savePng(const std::wstring& path, const CdFrame& frame, CdColor background);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

CdResult create(Spec spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace clockplugin
