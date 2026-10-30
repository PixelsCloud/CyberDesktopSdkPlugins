#pragma once

#include <sdk/Canvas.h>
#include <sdk/Controls.h>
#include <sdk/Icons.h>
#include <sdk/Images.h>
#include <sdk/Settings.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace whaleskin {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
using cybercanvas::Size;

inline constexpr uint32_t kInk = 0x173b66ff, kIvory = 0x3f5b89ff, kRuby = 0xbfa57dff, kSilver = 0x7f9ab9ff;

struct View {
    Point origin{};
    double scale = 1;
};

class Skin {
public:
    explicit Skin(const std::filesystem::path& assets = {});
    ~Skin();
    void bind(const CdCreateContext& context, std::function<void(uint32_t)> action = {});
    void onChange(std::function<void()> callback);
    void appearance(const cdsdk::Settings& settings);
    void font(std::string name);
    void pause(bool paused);
    bool poll();
    bool pending() const;
    bool ready() const;
    cdsdk::ImageSlotStats imageStats() const;
    uint64_t cacheBuilds() const;
    void input(const CdInput& event);
    void cancelInput();
    void controls(CdRect safe, const std::vector<cdsdk::Controls::Item>& items);
    Rect region(std::string_view name) const;
    Rect displayRegion(std::string_view name, CdRect safe) const;
    View view(CdRect safe, Size world);
    bool render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& paint,
                cybercanvas::Pose pose = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    void backdrop(Canvas& canvas);
    void worldBackdrop(Canvas& canvas, bool clear = true);
    void artLayer(Canvas& canvas, const std::function<void(Canvas&)>& paint);
    void label(Canvas& canvas, const std::wstring& text, Rect bounds, double size = 0, uint32_t ink = kIvory,
               bool fill = true, std::span<const std::wstring> references = {});
    void caption(Canvas& canvas, const std::wstring& text, Rect bounds, double size = 0, uint32_t ink = kIvory);
    void reading(Canvas& canvas, const std::wstring& value, const std::wstring& unit, Rect bounds, double size,
                 uint32_t ink = kIvory);
    void button(Canvas& canvas, Rect bounds, std::string_view icon, bool selected = false, bool enabled = true);
    void icon(Canvas& canvas, Rect bounds, std::string_view icon, uint32_t ink = kIvory);
    void weather(Canvas& canvas, Rect bounds, std::string_view name, std::string_view slot);
    void progress(Canvas& canvas, Rect bounds, double fraction);
    void photo(Canvas& canvas, Rect bounds, const cybercanvas::ImageData& image, double opacity = 1,
               std::string_view fit = "cover");
    void part(Canvas& canvas, std::string_view name, Rect destination, bool spring = false);
    void movingPart(Canvas& canvas, std::string_view name, Point delta = {}, double angle = 0,
                    std::string_view pivot = {});
    Rect worldRegion(std::string_view name, Size world) const;
    void gauge(Canvas& canvas, Rect bounds, double fraction, const std::wstring& value, const std::wstring& title = {},
               uint32_t accent = kRuby, const std::wstring& unit = {});
    void clear(Canvas& canvas, Rect bounds, uint32_t color = kInk, bool force = false);
    Size size() const;
    unsigned columns() const;
    unsigned rows() const;
    const std::string& folder() const;
    bool textured() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace whaleskin
