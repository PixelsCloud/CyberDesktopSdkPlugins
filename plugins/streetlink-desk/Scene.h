#pragma once

#include <CyberCanvas/Types.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct CdCreateContext;
struct CdFrame;
struct CdRect;

namespace cybercanvas {
class Canvas;
}

namespace streetlink {
// Coordinates are authored against the visible source bounds, not the padded
// PNG canvas. This is also the transform used for hit testing and motion bounds.
struct Placement {
    cybercanvas::Rect body;
    double scale = 1;
    double extension = 0;
};

Placement place(cybercanvas::Size viewport, cybercanvas::Size visible);

class Scene {
public:
    explicit Scene(const CdCreateContext& context);
    ~Scene();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    uint64_t cacheBuilds() const;
    uint64_t referencedBytes() const;
    bool render(const CdFrame& frame, CdRect safe, const std::function<void(cybercanvas::Canvas&)>& paint);
    cybercanvas::Rect region(std::string_view name) const;
    void text(cybercanvas::Canvas& canvas, const std::wstring& value, cybercanvas::Rect bounds, uint32_t color,
              double maximumSize = 0);
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace streetlink
