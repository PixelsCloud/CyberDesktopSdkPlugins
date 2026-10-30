#pragma once

#include <CyberCanvas/Physics2D.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace brickclock {
namespace physics = cybercanvas::physics;
using cybercanvas::Point;
using Contours = std::vector<std::vector<Point>>;
inline constexpr double kWidth = 1000, kHeight = 250, kFloor = 232, kGlyphScale = 1.4;
inline constexpr size_t kMaximumChips = 120;
inline constexpr double kChipLifetime = 3.2;

const Contours& glyph(int digit);
bool inGlyph(int digit, Point point);
Point digitOrigin(size_t index);

struct Carving {
    int previous = 0;
    double elapsed = 1;
    size_t emitted = 0;
    std::vector<Point> samples;
};

struct Chip {
    physics::Body body;
    std::vector<Point> vertices;
    Point source;
    size_t digit = 0;
    double born = 0;
};

struct EnclosureGeometry {
    double halfWidth = 5.1, left = -.05, right = 10.05;
};

inline constexpr uint32_t kDefaultSeed = 0x42524943;

class Model {
public:
    explicit Model(uint32_t seed = kDefaultSeed);
    explicit Model(EnclosureGeometry enclosure, uint32_t seed = kDefaultSeed);
    bool setTime(int hour, int minute, int second, bool animate = true);
    bool advance(double seconds);
    void settle();
    void replay();
    void setChipCount(unsigned count);
    bool active() const;
    const std::array<int, 6>& digits() const;
    const std::array<Carving, 6>& carvings() const;
    const std::vector<Chip>& chips() const;
    physics::BodyState state(const Chip& chip) const;
    physics::PhysicsStats stats() const;
    double chipAge(const Chip& chip) const;
    uint64_t contacts() const;

private:
    void beginCarving(size_t index, int previous);
    void emitChip(size_t index, Point sample);
    void removeChip(size_t index);
    double random(double low, double high);
    physics::World world_;
    std::array<int, 6> digits_{};
    std::array<Carving, 6> carvings_;
    std::vector<Chip> chips_;
    std::mt19937 random_;
    double time_ = 0;
    int previousSecond_ = -1;
    unsigned chipCount_ = 12;
    uint64_t contacts_ = 0;
};

struct Palette {
    uint32_t brick, alternate, shadow, edge, mortar, cut, cutShadow, cutEdge, chip;
};

Palette palette(const std::string& name);

class Renderer {
public:
    void draw(cybercanvas::Canvas& canvas, const Model& model, const std::string& colors, double scale = 1,
              double panelHeight = kHeight);

private:
    void wall(cybercanvas::Canvas& canvas, const Palette& colors, double height);
    void number(cybercanvas::Canvas& canvas, size_t index, int digit, const Palette& colors, double reveal,
                double opacity = 1);
    void prepare(cybercanvas::Canvas& canvas);
    cybercanvas::OffscreenSurface wall_;
    std::array<std::unique_ptr<cybercanvas::Path>, 10> glyphs_;
    std::unique_ptr<cybercanvas::Path> chip_, facet_;
    cybercanvas::ComPtr<ID2D1Factory> factory_;
    double scale_ = 0;
    std::string palette_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    void input(const CdInput& input);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();

private:
    bool synchronize(bool animate);
    bool frozen() const;
    void schedule();
    Model model_;
    cdsdk::Canvas canvas_;
    Renderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    std::optional<uint64_t> lastSecond_;
    bool hidden_ = false;
};
}  // namespace brickclock
