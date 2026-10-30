#pragma once

#include "plugins/mecha-desk/Skin.h"
#include "plugins/physics-toys/Toys.h"

namespace aeris_toys {
using namespace toys;

// Reuses the existing Box2D model; RedLine pendants have their own geometry.
class Model {
public:
    explicit Model(Kind kind, bool compact = false, bool mechanical = false);
    void configure(const std::filesystem::path& assets);
    void reset(bool compact);
    bool fit(Size size, cybercanvas::Rect limits, std::function<Point(Point)> map);
    bool advance(double seconds, double wind, double speed);
    bool setTime(int hour, int minute, int second, bool twelve, bool animate = true);
    void pluck();
    void beginDrag(size_t index, Point target);
    void drag(Point target);
    void cancelDrag();
    bool active() const;
    Size size() const;
    Point anchor(const Piece& piece) const;
    Point attachment(const Piece& piece) const;
    const std::vector<Piece>& pieces() const;
    physics::BodyState state(size_t index) const;
    physics::PhysicsStats stats() const;
    const std::wstring& digits() const;
    bool afternoon() const;
    Kind kind() const;
    bool mechanical() const;
    void setWindSeed(uint32_t seed);

private:
    Kind kind_;
    bool compact_ = false, afternoon_ = false, mechanical_ = false;
    double time_ = 0, accumulator_ = 0;
    std::wstring digits_;
    std::unique_ptr<physics::World> world_;
    physics::Body ground_;
    physics::Joint drag_;
    std::vector<Piece> pieces_;
    cybercanvas::NoiseField breeze_{{.seed = 721, .frequency = .6}};
    Size registeredSize_{};
    Size fittedSize_{};
    cybercanvas::Rect limits_{};
    std::function<Point(Point)> layoutMap_;
    std::vector<Point> registeredAnchors_;
    std::vector<cybercanvas::Rect> registeredBodies_, registeredTails_;
};

class MechanicalRenderer {
public:
    MechanicalRenderer();
    ~MechanicalRenderer();
    void bind(const CdCreateContext& context, std::function<void()> invalidate,
              std::function<void(uint32_t)> action = {});
    void pause(bool paused);
    bool pending() const;
    bool fit(Model& model, CdRect safe);
    void draw(cybercanvas::Canvas& canvas, const Model& model, const Appearance& appearance, double scale,
              const cdsdk::Settings& settings);
    bool input(const CdInput& event, CdRect safe);

    aerisskin::View view(CdRect safe, cybercanvas::Size world);

private:
    std::unique_ptr<aerisskin::Skin> skin_;
    cdsdk::Environment environment_;
    std::array<cybercanvas::OffscreenSurface, 6> cards_;
    std::array<wchar_t, 6> glyphs_{};
    double density_ = 0;
    uint64_t artworkRevision_ = 0;
};

class Instance {
public:
    Instance(const CdCreateContext& context, Kind kind, bool mechanical = false);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void invalidate();

private:
    void synchronizeTime();
    void schedule();
    void cancel();
    bool frozen() const;
    Model model_;
    std::unique_ptr<MechanicalRenderer> mechanical_;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    Point origin_{};
    double scale_ = 1;
    bool hidden_ = false, compact_ = false, dark_ = true, laidOut_ = false;
};

template <Kind kind, bool mechanical = true>
class Plugin : public Instance {
public:
    explicit Plugin(const CdCreateContext& context) : Instance(context, kind, mechanical) {}
};

}  // namespace aeris_toys
