#pragma once

#include <array>
#include <cstdint>

#include "plugins/beat-machine/Machine.h"
#include "plugins/vampire-alice/Skin.h"

namespace alice_beat {
using namespace beatmachine;

double laneHeight(double x, bool compact = false);

class Model final : public beatmachine::Model {
public:
    explicit Model(unsigned count = 36, bool compact = false);
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

    std::array<uint64_t, 4> physicsCounters() const;

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
