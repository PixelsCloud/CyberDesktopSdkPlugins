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

namespace alice {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
using cybercanvas::Size;

inline constexpr uint32_t kInk = 0x100b0fff, kIvory = 0xf2dfbaff, kRuby = 0xe51942ff, kSilver = 0xc8b9a3ff;

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
    void pause(bool paused);
    bool poll();
    bool pending() const;
    bool ready() const;
    uint64_t cacheBuilds() const;
    cdsdk::ImageSlotStats assetStats() const;
    void input(const CdInput& event);
    void cancelInput();
    void controls(CdRect safe, const std::vector<cdsdk::Controls::Item>& items);
    Rect region(std::string_view name) const;
    Rect displayRegion(std::string_view name, CdRect safe) const;
    View view(CdRect safe, Size world);
    bool render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& paint);
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    void backdrop(Canvas& canvas);
    void worldBackdrop(Canvas& canvas, bool clear = true);
    void artLayer(Canvas& canvas, const std::function<void(Canvas&)>& paint);
    void label(Canvas& canvas, const std::wstring& text, Rect bounds, double size = 0, uint32_t ink = kIvory,
               bool fill = true);
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
               uint32_t accent = kRuby);
    void clear(Canvas& canvas, Rect bounds, uint32_t color = kInk, bool force = false);
    Size size() const;
    unsigned columns() const;
    unsigned rows() const;
    const std::string& folder() const;
    bool textured() const;

private:
    void load(const std::filesystem::path& assets);
    void arrange(CdRect safe);
    void maskedMaterial(Canvas& canvas, Rect imageBounds, Rect clip, const std::function<void()>& paint);
    std::filesystem::path assets_;
    std::string folder_;
    unsigned columns_ = 1, rows_ = 1;
    bool textured_ = false;
    bool nativeTicks_ = false;
    Rect crop_{};
    std::map<std::string, Rect, std::less<>> regions_;
    std::map<std::string, Rect, std::less<>> hardware_;
    cdsdk::Canvas canvas_;
    cdsdk::Images images_;
    cdsdk::Icons icons_;
    cdsdk::Controls controls_;
    std::vector<cdsdk::Controls::Item> items_;
    std::shared_ptr<const cybercanvas::ImageData> art_;
    cybercanvas::SurfaceImage matteBitmap_;
    cybercanvas::ComPtr<ID2D1BitmapBrush> matteBrush_;
    cybercanvas::ComPtr<ID2D1RenderTarget> matteTarget_;
    uint64_t matteId_ = 0;
    std::function<void()> changed_;
    View artView_{}, worldView_{};
    uint64_t builds_ = 0;
};
}  // namespace alice
