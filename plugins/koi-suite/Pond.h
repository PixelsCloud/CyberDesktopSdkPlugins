#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <functional>
#include <memory>
#include <random>
#include <string_view>
#include <vector>

namespace koiplugin {
inline constexpr double kPi = 3.14159265358979323846;

struct Vec {
    double x = 0, y = 0;

    Vec operator+(Vec value) const {
        return {x + value.x, y + value.y};
    }

    Vec operator-(Vec value) const {
        return {x - value.x, y - value.y};
    }

    Vec operator*(double value) const {
        return {x * value, y * value};
    }

    double length() const;
};

Vec direction(double angle);
double angleDifference(double angle, double anchor);

// C++ adaptation of argonautcode/animal-proc-anim Chain.pde and Fish.pde.
// MIT, Copyright (c) 2024 argonaut. See licenses/animal-proc-anim-MIT.txt.
struct Fish {
    static constexpr std::array<double, 10> widths{68, 81, 84, 83, 77, 64, 51, 38, 32, 19};
    std::array<Vec, 12> joints{};
    std::array<double, 12> angles{};
    Vec target;
    double link = 5, heading = 0, phase = 0, age = 0, depth = 0;
    double turnRate = 0, finPhase = 0, alarm = 0;
    double boundaryRecovery = 0;
    int boundaryTurn = 0;
    double travelHeading = 0;
    bool entering = false, leaving = false;
    int entryEdge = 0;
    int pattern = 0;
    void resolve(Vec head);
    Vec edge(int joint, double offset, double extra = 0) const;
};

struct FinPose {
    Vec center;
    double angle = 0, radiusX = 0, radiusY = 0;
};

FinPose finPose(const Fish& fish, int joint, int side);
std::vector<Vec> bodyOutline(const Fish& fish);
std::array<Vec, 8> tailOutline(const Fish& fish);
CdRect fishBounds(const Fish& fish);
double fishLengthForGrid(double width, double height, int rows, int columns);

struct LilyPad {
    Vec center;
    double radius = 0, angle = 0;
    bool notched = false;
};

std::vector<LilyPad> lilyPads(double width, double height);

enum class MouseBehavior { Flee, Follow };

struct Ripple {
    Vec center;
    double age = 0;
};

inline constexpr std::array<std::wstring_view, 50> kBlessingWords{
    L"福",   L"禄",   L"寿",   L"喜",   L"财",   L"吉祥",     L"如意",     L"安康",     L"平安",     L"顺遂",
    L"暴富", L"发财", L"招财", L"进宝", L"盈利", L"丰收",     L"财旺",     L"富足",     L"金满仓",   L"财源广进",
    L"加薪", L"升职", L"高升", L"晋级", L"上岸", L"转正",     L"开单",     L"爆单",     L"事业有成", L"前程似锦",
    L"高分", L"满分", L"上榜", L"夺魁", L"必过", L"金榜题名", L"学业有成", L"逢考必过", L"名列前茅", L"前途无量",
    L"好运", L"好事", L"成功", L"圆满", L"美满", L"幸福",     L"心想事成", L"万事胜意", L"乘风破浪", L"大展宏图"};
inline constexpr double kBlessingDuration = 4.6;
inline constexpr double kDefaultMotionSpeed = .75;

struct Blessing {
    Vec center;
    int word = -1;
    double age = 0;
    double opacity() const;
};

float blessingFontSize(double width, double height, size_t characters);

struct Spec {
    const char* id;
    int rows, columns, fishCount;
};

class Scene {
public:
    // Optional closed habitat. The predicate includes clearance around a point;
    // an unset boundary preserves the original open-water entry/exit behavior.
    void setBoundary(std::function<bool(Vec, double)> boundary);
    void configure(double width, double height, int count, uint32_t seed, double fishLength = 0);
    void advance(double seconds, double speed);
    void ripple(Vec center);
    void setMouseBehavior(MouseBehavior behavior);
    void pointer(Vec center);
    void clearPointer();
    void click(Vec center);

    const std::vector<Fish>& fish() const {
        return fish_;
    }

    const std::vector<Ripple>& ripples() const {
        return ripples_;
    }

    double time() const {
        return time_;
    }

    double width() const {
        return width_;
    }

    double height() const {
        return height_;
    }

    uint64_t reentries() const {
        return reentries_;
    }

    CdRect lastExitBounds() const {
        return lastExitBounds_;
    }

    const Blessing& blessing() const {
        return blessing_;
    }

private:
    double random(double low, double high);
    Vec destination(double margin);
    bool fits(const Fish& fish) const;
    void moveWithinBoundary(Fish& fish, Vec next, double turn);
    void step(double seconds, double speed);
    void reenter(Fish& fish, int exitEdge);
    void nextBlessing();
    std::mt19937 random_{7};
    std::mt19937 blessingRandom_{7};
    std::array<int, kBlessingWords.size()> blessingOrder_{};
    size_t blessingCursor_ = kBlessingWords.size();
    int lastBlessing_ = -1;
    Blessing blessing_;
    double nextBlessingAt_ = 0;
    std::vector<Fish> fish_;
    std::vector<Ripple> ripples_;
    double width_ = 0, height_ = 0, time_ = 0, accumulator_ = 0;
    double fishLength_ = 0;
    uint32_t seed_ = 0;
    MouseBehavior mouseBehavior_ = MouseBehavior::Flee;
    Vec pointer_{}, threat_{};
    bool hasPointer_ = false;
    double scareUntil_ = 0;
    uint64_t reentries_ = 0;
    CdRect lastExitBounds_{};
    std::function<bool(Vec, double)> boundary_;
};

struct Appearance {
    double night = 0;
    double shimmer = .65;
    float radius = 5;
    bool leaves = true, flowers = true;
    uint32_t koiColor = 0xDE593CFF;
    bool background = true;
    bool blessings = true;
    double motionTime = -1;
    bool redrawPond = true;
};

class Renderer {
public:
    void bindOutput(const CdCreateContext& context);
    Renderer();
    ~Renderer();
    bool render(const Scene& scene, const Appearance& appearance, const CdFrame& frame, CdRect safe);
    bool savePng(const std::wstring& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

cdsdk::Settings defaults(Spec spec);
CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size);
}  // namespace koiplugin
