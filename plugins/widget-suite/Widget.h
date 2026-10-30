#pragma once
#include <CyberCanvas/Types.h>
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cybercanvas {
class ImageData;
}

namespace widgetplugin {
enum class Kind { Bars, Radial, Wide, Album, Calendar, RoundMatrix, SquareMatrix, Player, InputStats };

constexpr bool isMatrix(Kind kind) {
    return kind == Kind::RoundMatrix || kind == Kind::SquareMatrix;
}

constexpr bool isMusic(Kind kind) {
    return kind != Kind::Album && kind != Kind::Calendar && kind != Kind::Player && kind != Kind::InputStats;
}

struct Spec {
    const char* id;
    Kind kind;
    int rows, columns;
    bool mechanical = false;
};

struct Photo {
    std::shared_ptr<const cybercanvas::ImageData> bitmap;
    size_t index = 0, count = 0;
    uint64_t generation = 0;
    std::wstring filename;
    std::wstring filepath;
};

struct AlbumPlayback {
    bool paused = false;
    bool pending = false;
    double motionSeconds = 0, previousMotionSeconds = 0;
    bool motionEnabled = true;
};

struct Media {
    std::wstring source, title, artist, album;
    Photo cover;
    uint32_t status = 0, controls = 0;
    uint64_t coverGeneration = 0, sampledAt = 0;
    double position = 0, duration = 0, rate = 1;
    CdResult commandResult = CD_OK;
    double recordDegrees = 0;
    double tonearmProgress = 0, tonearmSeconds = 0;
};

struct TonearmPose {
    cybercanvas::Point pivot, contact;
    double length = 0, rotation = 0;
};

TonearmPose tonearmPose(double progress);
void advanceTonearm(Media& media, double seconds, double position);

struct PlayerLayout {
    CdRect cover{}, title{}, artist{}, album{}, source{}, timeline{}, elapsed{}, duration{};
    std::array<CdRect, 3> buttons{};
    cybercanvas::Point tonearmPivot{};
    float titleSize = 14, detailSize = 10;
};

PlayerLayout arrangePlayer(Spec spec, float width, float height);
int playerHit(const PlayerLayout&, double x, double y, uint32_t controls);
double mediaPosition(const Media&, uint64_t now);
std::wstring playerName(const std::wstring&, bool english);

class Album {
public:
    Album();
    ~Album();
    void bind(const CdCreateContext&, std::function<void()> changed);
    void pause(bool paused);
    void pixelEdge(uint32_t edge);
    void request(const std::string& directory, bool recursive, int step = 0, bool rescan = false);
    bool take(Photo&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

uint32_t albumTimerInterval(const Photo& photo, float fade, bool motion, bool slideshowPaused, uint64_t intervalMs,
                            uint64_t lastPhoto, uint64_t now);

struct Spectrum {
    enum class PeakMotion { Default, FastBall };
    static constexpr size_t count = 63;
    std::array<float, count> input{}, level{}, peak{}, hold{};
    std::array<float, count> publishedLevel{}, publishedPeak{};
    void feed(const float*, uint32_t);
    bool advance(float seconds, bool stale, double gain, double releaseMs, PeakMotion motion = PeakMotion::Default);
    float sample(double position, bool peaks = false) const;
    float range(double first, double last, bool peaks = false) const;
};

struct MatrixGeometry {
    int columns = 0, rows = 0;
    float left = 5, top = 5, diameter = 0, pitchX = 0, pitchY = 0;
};

MatrixGeometry arrangeMatrix(float width, float height, int rows);

struct Date {
    int year = 2026, month = 10, day = 13;
    bool operator==(const Date&) const = default;
};

Date localDate();
Date shiftMonth(Date, int offset);
int monthDays(Date);
int monthStart(Date, bool monday);

class Canvas {
public:
    Canvas();
    ~Canvas();
    void bind(const CdCreateContext&, std::function<void(uint32_t)>);
    void input(const CdInput&);
    void cancelInput();
    void pause(bool paused);
    void syncInput(Spec spec, const Media& media, CdRect safe, const Photo& photo = {}, AlbumPlayback album = {});
    bool render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float fade, Date,
                int monthOffset, const CdFrame&, CdRect, const Media& media = {}, int pressed = 0,
                const CdInputStats& inputStats = {}, const class CounterDrums* counterDrums = nullptr,
                AlbumPlayback album = {});
    bool savePng(const std::wstring&, const CdFrame&);
    bool pendingImages() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

cdsdk::Settings defaults(Kind, bool mechanical = false);
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace widgetplugin
