#pragma once

#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <optional>
#include <string_view>

namespace precisionclock {
inline constexpr double kExtent = 1000;
enum class Kind { Skeleton, Green, Dress };
enum class Motion { Sweep, Beat, Tick };

struct Spec {
    Kind kind;
    const char* id;
    const char* folder;
    cybercanvas::Point center;
    double radius;
    cybercanvas::Rect artworkBounds;
};

const Spec& specification(Kind kind);
Kind identify(std::string_view id);

struct Options {
    Motion motion = Motion::Beat;
    bool animation = true, reduced = false, seconds = true, background = true, detail = true;
    bool moving() const;
};

Options options(const cdsdk::Settings& settings);
cdsdk::Settings defaults();
uint32_t cadence(Kind kind, const Options& options, uint64_t unixMs, bool pendingAssets);
double moonPhase(uint64_t unixMs);
double gearAngle(double seconds, unsigned teeth, bool reverse);

struct State {
    double hour = 0, minute = 0, second = 0;
    double mechanismSeconds = 0, balance = 0, lunarPhase = 0;
    int year = 1970, month = 1, day = 1;
    bool operator==(const State&) const = default;
};

class Model {
public:
    explicit Model(Kind kind);
    bool synchronize(const CdEnvironmentSnapshot& environment, const Options& options);
    const State& state() const;

private:
    Kind kind_;
    State state_;
    std::optional<uint64_t> moonMinute_;
};

class Renderer {
public:
    Renderer(Kind kind, const std::filesystem::path& assets = {});
    bool poll();
    bool pending() const;
    void pause(bool paused);
    cdsdk::ImageSlotStats assets() const;
    void draw(cybercanvas::Canvas& canvas, const State& state, const Options& options, double scale);

private:
    void base(cybercanvas::Canvas& canvas, const Options& options);
    void mechanism(cybercanvas::Canvas& canvas, const State& state);
    void lunar(cybercanvas::Canvas& canvas, const State& state);
    void hands(cybercanvas::Canvas& canvas, const State& state, const Options& options);
    void prepare(cybercanvas::Canvas& canvas, const Options& options, double scale);
    const Spec& spec_;
    std::filesystem::path assets_;
    cdsdk::Images images_;
    std::shared_ptr<const cybercanvas::ImageData> face_, brass_;
    cybercanvas::OffscreenSurface backdrop_;
    std::array<cybercanvas::OffscreenSurface, 6> wheels_;
    ID2D1RenderTarget* owner_ = nullptr;
    uint64_t revision_ = 0, cachedRevision_ = 0;
    uint32_t edge_ = 0, pixels_ = 0;
    double dpi_ = 0;
    bool background_ = true;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);

private:
    CdResult schedule(const cdsdk::Settings& next);
    void arrange();
    static void CD_CALL wake(void* value, uint64_t now) noexcept;
    Kind kind_;
    Model model_;
    // Renderer owns compatible targets and must release them before Canvas/COM.
    cdsdk::Canvas canvas_;
    Renderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    CdRect safe_{};
    cybercanvas::Point origin_{};
    double scale_ = .2;
    bool hidden_ = false;
};
}  // namespace precisionclock
