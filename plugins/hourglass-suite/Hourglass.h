#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <memory>
#include <string>
#include <vector>

namespace hourglassplugin {
inline constexpr const char* kPluginId = "com.cyberdesktop.hourglass.pixel.1x2";
inline constexpr const char* kSquarePluginId = "com.cyberdesktop.hourglass.pixel.1x1";
inline constexpr int kGridWidth = 19, kGridHeight = 29;
inline constexpr double kFlipDuration = 1.0;

struct Spec {
    const char* id = kPluginId;
    bool showTimer = true;
};

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};

int chamberHalfWidth(int row);

enum class Phase { Flowing, Turning, Finished };

struct Sand {
    std::vector<Cell> upper, lower, falling;
};

class Model {
public:
    Model();
    void reset(double duration);
    void advance(double seconds, bool repeat);
    Sand sample() const;
    double progress() const;
    double angle(bool continuous = false) const;
    int remaining() const;
    size_t capacity() const;
    Phase phase() const;
    uint64_t cycles() const;

private:
    std::vector<Cell> upper_, lower_;
    double duration_ = 30, elapsed_ = 0, turning_ = 0;
    Phase phase_ = Phase::Flowing;
    uint64_t cycles_ = 0;
};

class PhysicalSand;

struct Options {
    bool dark = true, running = true, background = true, showTimer = true;
    float radius = 5;
    double glow = .35;
    std::string color = "amber";
    const PhysicalSand* physicalSand = nullptr;
};

CdRect glassArea(double width, double height, bool showTimer);

class Renderer {
public:
    void bindOutput(const CdCreateContext& context);
    Renderer();
    ~Renderer();
    bool render(const Model& model, const Options& options, const CdFrame& frame, CdRect safe);
    bool savePng(const std::wstring& filename, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

cdsdk::Settings defaults(Spec spec = {});
CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size);
}  // namespace hourglassplugin
