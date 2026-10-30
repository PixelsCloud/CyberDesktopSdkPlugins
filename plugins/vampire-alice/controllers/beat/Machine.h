#pragma once

#include "plugins/beat-machine/Machine.h"
#include "plugins/vampire-alice/Skin.h"

namespace alice_beat {
using namespace beatmachine;

double laneHeight(double x, bool compact = false);

class Model {
public:
    explicit Model(unsigned count = 36, bool compact = false);
    void reset(unsigned count);
    bool advance(double seconds, const Signal& signal = {});
    void strike(unsigned index, double strength = .8);
    void nudge(unsigned index);
    void quiet();
    bool beginDrag(uint32_t token, Point point);
    void drag(Point point);
    void cancelDrag();
    bool active() const;
    bool dragging() const;
    physics::BodyState state(physics::Body body) const;
    physics::PhysicsStats stats() const;
    const std::vector<Ball>& balls() const;
    const std::vector<Segment>& rails() const;
    const std::array<Actuator, 5>& pistons() const;
    const std::array<Actuator, 2>& paddles() const;
    const std::array<Point, 3>& bumpers() const;
    const std::array<double, 3>& flashes() const;
    double time() const;
    uint64_t launches() const;
    uint64_t ejections() const;
    uint64_t returns() const;
    uint64_t hits() const;
    uint64_t recoveries() const;
    bool compact() const;
    double width() const;

private:
    void step(const Signal& signal);
    void controls();
    std::unique_ptr<physics::World> world_;
    physics::Body ground_, dragged_;
    physics::Joint mouse_;
    std::vector<Ball> balls_;
    std::vector<Segment> rails_;
    std::array<Actuator, 5> pistons_{};
    std::array<Actuator, 2> paddles_{};
    std::array<Point, 3> bumpers_{{{766, 142}, {865, 232}, {932, 126}}};
    std::array<physics::Shape, 3> bumperShapes_{};
    std::array<double, 3> flashes_{};
    double time_ = 0, accumulator_ = 0, lastLaunch_ = -2, bass_ = 0, middle_ = 0;
    uint64_t beat_ = 0, accent_ = 0, launches_ = 0, returns_ = 0, hits_ = 0, recoveries_ = 0;
    uint64_t ejections_ = 0;
    unsigned nextHammer_ = 0;
    std::array<uint64_t, 5> pistonBeats_{};
    bool loaderOccupied_ = false;
    bool compact_ = false;
};

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& assets = {}, bool compact = false);
    void bind(const CdCreateContext& context);
    bool pollAssets();
    bool pendingAssets() const;
    cdsdk::ImageSlotStats assetStats() const;
    void pauseAssets(bool paused);
    void draw(cybercanvas::Canvas& canvas, const Model& model, const Signal& signal, Appearance appearance,
              double scale);

    alice::View view(CdRect safe, cybercanvas::Size world);

private:
    std::unique_ptr<alice::Skin> skin_;
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
    void subscribe();
    void cancel();
    void schedule();
    void arrange();
    bool frozen() const;
    CdCreateContext context_;
    Model model_;
    AudioMotion audio_;
    Renderer renderer_;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    cdsdk::AudioWatch watch_;
    cdsdk::AudioStatusWatch statusWatch_;
    CdRect safe_{0, 0, 600, 200};
    Point origin_{};
    double scale_ = 1;
    uint64_t lastAudio_ = 0;
    uint32_t status_ = CD_DATA_LOADING;
    bool hidden_ = false, arranged_ = false;
};

}  // namespace alice_beat
