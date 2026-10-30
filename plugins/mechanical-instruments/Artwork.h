#pragma once

#include <CyberCanvas/PanelLayout.h>
#include <sdk/Images.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "plugins/mechanical-instruments/Icons.h"

namespace mechanicalart {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
inline constexpr uint32_t kIvory = 0xf1e8d0ff;
inline constexpr uint32_t kBrass = 0xd9b46cff;

enum class Kind { Timer, Disk, Player, Weather };

struct Stage {
    double scale = 1;
    Point origin;

    Rect map(Rect r) const;

    template <class Paint>
    void draw(Canvas& canvas, Paint&& paint) const {
        canvas.saved([&] {
            canvas.translate(origin.x, origin.y);
            canvas.scale(scale, scale);
            paint();
        });
    }
};

Stage arrange(Kind kind, double width, double height);

Rect timerButton(unsigned index);

Rect playerButton(unsigned index);

void text(Canvas& c, const std::wstring& value, Rect bounds, double size, uint32_t color = kIvory, bool bold = true);

Point polar(Point center, double radius, double degrees);

void scale(Canvas& c, Point center, double radius, double maximum, bool ivory = false, bool full = false);

void needle(Canvas& c, Point center, double length, double degrees, bool valid = true);

class Artwork {
public:
    void bind(const CdCreateContext& context);

    void pause(bool paused);

    void onChange(std::function<void()> callback);

    bool pending() const;

    bool poll();

    std::shared_ptr<const cybercanvas::ImageData> material(Canvas& c, const std::string& name, double logicalEdge);

    bool body(Canvas& c, Kind kind, const Stage& stage);

    void icon(Canvas& c, const Stage& stage, Rect box, Icon symbol, uint32_t color = kIvory);

    void paintIcon(Canvas& c, Rect bounds, Icon symbol, uint32_t color = kIvory);

private:
    cdsdk::Images images_;
    std::filesystem::path root_;
    std::shared_ptr<const cybercanvas::ImageData> image_;
    std::map<std::string, std::shared_ptr<const cybercanvas::ImageData>> materials_;
};
}  // namespace mechanicalart
