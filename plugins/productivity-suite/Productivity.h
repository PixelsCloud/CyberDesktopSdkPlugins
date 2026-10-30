#pragma once
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace productivity {
enum class Kind { Timer, Todo, Countdown, Network, Disk, Notes, Rss };

struct Spec {
    const char* id;
    Kind kind;
    int rows, columns;
    bool mechanical = false;
};

struct Task {
    std::string text;
    bool done = false;
    bool operator==(const Task&) const = default;
};

struct Article {
    std::string title, url, date;
    bool operator==(const Article&) const = default;
};

struct Data {
    bool running = false, resting = false, finished = false, valid = false, loading = false, stale = false;
    int64_t remaining = 25 * 60, deadline = 0, days = 0;
    int completed = 0, scroll = 0;
    uint64_t down = 0, up = 0, available = 0, total = 0, free = 0;
    std::deque<std::pair<uint64_t, uint64_t>> history;
    std::vector<Task> tasks;
    std::vector<Article> articles;
    std::string source, error;
    uint64_t copiedUntil = 0;
    std::array<double, 2> needles{-1, -1};
};

std::wstring wide(const std::string&);
std::vector<std::string> lines(const std::string&);
bool driveRoot(const std::string&);
bool webUrl(const std::string&);
int64_t wallSeconds();
int64_t daysUntil(const std::string&, bool annual, int year, int month, int day);
void reconcileTasks(Data&, const std::string&);
void advanceTimer(Data&, const cdsdk::Settings&, int64_t now);
std::string stateBytes(const Data&, Kind);
bool restoreBytes(Data&, Kind, const std::string&);

cdsdk::Settings defaults(Kind, bool mechanical = false);

// Hit regions are shared by rendering and input; zero means no action.
struct Hit {
    CdRect rect;
    int action = 0;
};

std::vector<Hit> hitRegions(Spec, const Data&, const cdsdk::Settings&, CdRect);

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bind(const CdCreateContext&, std::function<void(uint32_t)>);
    void input(const CdInput&);
    void cancelInput();
    void pause(bool paused);
    void syncInput(Spec spec, const cdsdk::Settings& settings, const Data& data, CdRect safe);
    bool render(Spec, const cdsdk::Settings&, const Data&, const CdFrame&, CdRect);
    bool savePng(const std::filesystem::path&, const CdFrame&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace productivity
