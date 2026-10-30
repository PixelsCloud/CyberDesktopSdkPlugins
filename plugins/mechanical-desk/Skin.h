#pragma once

#include <sdk/Canvas.h>
#include <sdk/Controls.h>
#include <sdk/Diagnostics.h>
#include <sdk/Icons.h>
#include <sdk/Images.h>
#include <sdk/WindowsFonts.h>

#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace mechanicaldesk {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
using cybercanvas::Size;
inline constexpr uint32_t kIvory = 0xf4eddaff, kBrass = 0xdfbd73ff, kSteel = 0xcbd4d2ff, kInk = 0x182326ff;

struct View {
    Point origin;
    double scale = 1;
};

// Regions are authored in native image coordinates; drawing and input share
// this one transform. The image service decodes only requested source regions.
class Skin {
public:
    explicit Skin(const std::filesystem::path& assets = {});
    void bind(const CdCreateContext& context, std::function<void(uint32_t)> action = {});
    void onChange(std::function<void()> changed);
    void pause(bool paused);
    bool pending() const;
    bool ready() const;
    bool poll();
    uint64_t revision() const;
    cdsdk::ImageSlotStats stats() const;
    void input(const CdInput& event);
    void cancel();
    void controls(CdRect safe, std::vector<cdsdk::Controls::Item> items);
    Rect region(std::string_view name) const;
    Rect absolute(Rect native) const;
    Rect display(Rect local, CdRect safe) const;
    View view(CdRect safe) const;
    bool render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& draw,
                const std::function<void(Canvas&)>& overlay = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    void backdrop(Canvas& canvas);
    void backdrop(Canvas& canvas, double scale);
    void part(Canvas& canvas, std::string_view name, Rect destination = {});
    void sprite(Canvas& canvas, std::string_view name, Rect area, double fraction = 1);
    void label(Canvas& canvas, const std::wstring& value, Rect area, double size, uint32_t color = kIvory,
               bool bold = false, const std::wstring& family = L"Bahnschrift");
    void icon(Canvas& canvas, Rect area, std::string_view name, uint32_t color = kIvory);
    std::wstring font(const std::string& family);
    Canvas& layoutCanvas();
    Size size() const;
    const std::string& folder() const;
    unsigned columns() const;
    unsigned rows() const;

private:
    struct Part {
        std::string file;
        Rect source, destination;
        Point root{.5, 1};
    };

    void load(const std::filesystem::path& assets);
    void request(Canvas& canvas, const std::string& name, const Part& part, Rect destination);
    cdsdk::Canvas canvas_;
    cdsdk::WindowsFonts fonts_{canvas_.nativeTextFactory()};
    cdsdk::Images images_;
    cdsdk::Icons icons_;
    cdsdk::Controls controls_;
    cdsdk::DiagnosticSource diagnostics_;
    std::filesystem::path assets_;
    std::string folder_;
    unsigned columns_ = 1, rows_ = 1;
    Rect crop_;
    double inset_ = 6.25, scale_ = 1;
    std::map<std::string, Rect, std::less<>> regions_;
    std::map<std::string, Part, std::less<>> parts_;
    std::map<std::string, uint64_t> retries_;
    std::set<std::string> frameParts_, retainedParts_;
    CdHostApi host_{};
    void* context_ = nullptr;
    uint64_t revision_ = 0;
};
}  // namespace mechanicaldesk
