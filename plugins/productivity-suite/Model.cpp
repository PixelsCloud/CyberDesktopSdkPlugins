#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "libraries/content-model/Text.h"
#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/productivity-suite/Productivity.h"

namespace productivity {
std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    if (text.size() > 65536)
        throw std::runtime_error("Text too large");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!size)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring result(size, L' ');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size);
    return result;
}

std::vector<std::string> lines(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream in(value);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            result.push_back(line);
    }
    return result;
}

bool driveRoot(const std::string& value) {
    return value.size() == 3 && ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')) &&
           value[1] == ':' && (value[2] == '\\' || value[2] == '/');
}

bool webUrl(const std::string& value) {
    return cdcontent::webUrl(value);
}

int64_t wallSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t daysUntil(const std::string& value, bool annual, int y, int m, int d) {
    using namespace std::chrono;
    if (value.size() != 10 || value[4] != '-' || value[7] != '-')
        throw std::runtime_error("Use YYYY-MM-DD");
    for (size_t i = 0; i < value.size(); ++i)
        if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9'))
            throw std::runtime_error("Invalid date");
    const int ty = std::stoi(value.substr(0, 4)), tm = std::stoi(value.substr(5, 2)),
              td = std::stoi(value.substr(8, 2));
    year_month_day target{year(ty), month(unsigned(tm)), day(unsigned(td))};
    if (ty < 1601 || ty > 9999 || !target.ok())
        throw std::runtime_error("Invalid date");
    const sys_days today{year(y) / month(unsigned(m)) / day(unsigned(d))};
    if (annual) {
        for (int candidate = y; candidate <= y + 8; ++candidate) {
            year_month_day next{year(candidate), month(unsigned(tm)), day(unsigned(td))};
            if (next.ok() && sys_days(next) >= today) {
                target = next;
                break;
            }
        }
    }
    return (sys_days(target) - today).count();
}

void reconcileTasks(Data& data, const std::string& value) {
    const auto contents = lines(value);
    if (contents.size() > 20)
        throw std::runtime_error("At most 20 tasks");
    auto previous = data.tasks;
    std::vector<bool> used(previous.size());
    std::vector<Task> updated;
    for (const auto& text : contents) {
        bool done = false;
        for (size_t i = 0; i < previous.size(); ++i)
            if (!used[i] && previous[i].text == text) {
                done = previous[i].done;
                used[i] = true;
                break;
            }
        updated.push_back({text, done});
    }
    data.tasks = std::move(updated);
    data.scroll = std::clamp(data.scroll, 0, std::max(0, int(data.tasks.size()) - 1));
}

void advanceTimer(Data& data, const cdsdk::Settings& settings, int64_t now) {
    if (!data.running)
        return;
    data.remaining = std::clamp<int64_t>(data.deadline - now, 0, 10800);
    if (data.remaining)
        return;
    data.running = false;
    data.finished = true;
    data.deadline = 0;
    if (settings.at("mode").text == "focus" && !data.resting) {
        ++data.completed;
        if (settings.at("auto_break").integer) {
            data.resting = true;
            data.finished = false;
            data.remaining = settings.at("break_minutes").integer * 60;
            data.deadline = now + data.remaining;
            data.running = true;
        }
    }
}

std::string stateBytes(const Data& d, Kind kind) {
    using nlohmann::json;
    json value{{"version", 1}, {"kind", int(kind)}};
    if (kind == Kind::Timer)
        value.update({{"remaining", d.remaining},
                      {"deadline", d.deadline},
                      {"running", d.running},
                      {"resting", d.resting},
                      {"finished", d.finished},
                      {"completed", d.completed}});
    if (kind == Kind::Todo) {
        value["tasks"] = json::array();
        for (const auto& t : d.tasks)
            value["tasks"].push_back({{"text", t.text}, {"done", t.done}});
        value["scroll"] = d.scroll;
    }
    return value.dump();
}

bool restoreBytes(Data& data, Kind kind, const std::string& bytes) {
    try {
        if (bytes.empty())
            return true;
        if (bytes.size() > 16384)
            return false;
        const auto j = nlohmann::json::parse(bytes);
        if (j.at("version") != 1 || j.at("kind") != int(kind))
            return false;
        auto changed = data;
        if (kind == Kind::Timer) {
            changed.remaining = j.at("remaining").get<int64_t>();
            changed.deadline = j.at("deadline").get<int64_t>();
            changed.running = j.at("running").get<bool>();
            changed.resting = j.at("resting").get<bool>();
            changed.finished = j.at("finished").get<bool>();
            changed.completed = j.at("completed").get<int>();
            if (changed.remaining < 0 || changed.remaining > 10800 || changed.deadline < 0 ||
                changed.deadline > 253402300799LL || changed.completed < 0 || changed.completed > 1000000)
                return false;
            if (changed.running && changed.deadline == 0)
                return false;
        }
        if (kind == Kind::Todo) {
            changed.tasks.clear();
            if (!j.at("tasks").is_array() || j.at("tasks").size() > 20)
                return false;
            for (const auto& item : j.at("tasks")) {
                Task t{item.at("text").get<std::string>(), item.at("done").get<bool>()};
                if (t.text.empty() || t.text.size() > 2048)
                    return false;
                wide(t.text);
                changed.tasks.push_back(std::move(t));
            }
            changed.scroll = j.at("scroll").get<int>();
            if (changed.scroll < 0 || changed.scroll > 20)
                return false;
        }
        data = std::move(changed);
        return true;
    } catch (...) {
        return false;
    }
}

#define COMMON                                                                                         \
    {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},                                 \
        {"font_family", CD_SETTING_FONT, 0, 0, mechanical ? "Microsoft YaHei UI" : "@siyuan-heavy"}, { \
        "language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {                                          \
            "zh-CN", "en"                                                                              \
        }                                                                                              \
    }

#define MECHANICAL \
    { "reduced_motion", CD_SETTING_TOGGLE, 0 }

cdsdk::Settings defaults(Kind kind, bool mechanical) {
    switch (kind) {
        case Kind::Timer:
            return {{{"mode", CD_SETTING_CHOICE, 0, 0, "focus", 0, 0, {"focus", "countdown"}},
                     {"focus_minutes", CD_SETTING_INTEGER, 25, 0, "", 1, 180},
                     {"break_minutes", CD_SETTING_INTEGER, 5, 0, "", 1, 60},
                     {"auto_break", CD_SETTING_TOGGLE, 0},
                     MECHANICAL,
                     COMMON}};
        case Kind::Todo:
            return {{{"tasks", CD_SETTING_TEXT, 0, 0, "整理桌面\n完成今天的计划\n阅读十分钟", 0, 2048}, COMMON}};
        case Kind::Countdown:
            return {{{"title", CD_SETTING_TEXT, 0, 0, "下个假期", 0, 128},
                     {"date", CD_SETTING_TEXT, 0, 0, "2027-01-01", 0, 10},
                     {"annual", CD_SETTING_TOGGLE, 0},
                     COMMON}};
        case Kind::Network:
            return {{{"history", CD_SETTING_TOGGLE, 1}, MECHANICAL, COMMON}};
        case Kind::Disk:
            return {{{"drive", CD_SETTING_TEXT, 0, 0, "C:\\", 0, 3},
                     {"refresh_seconds", CD_SETTING_INTEGER, 30, 0, "", 10, 300},
                     MECHANICAL,
                     COMMON}};
        case Kind::Notes:
            return {{{"text", CD_SETTING_TEXT, 0, 0, "记录灵感，保持专注\n今天也要把事情做好", 0, 2048},
                     {"copy_all", CD_SETTING_TOGGLE, 1},
                     COMMON}};
        case Kind::Rss:
            return {{{"url", CD_SETTING_TEXT, 0, 0, "https://www.qt.io/blog/rss.xml", 0, 2048},
                     {"refresh_minutes", CD_SETTING_INTEGER, 15, 0, "", 5, 180},
                     {"rotation_seconds", CD_SETTING_INTEGER, 10, 0, "", 5, 60},
                     COMMON}};
    }
    throw std::runtime_error("Unknown kind");
}

#undef COMMON
#undef MECHANICAL

std::vector<Hit> hitRegions(Spec spec, const Data& data, const cdsdk::Settings& settings, CdRect safe) {
    std::vector<Hit> result;
    const double w = safe.width, h = safe.height;
    const auto add = [&](double x, double y, double width, double height, int action) {
        result.push_back({{safe.x + x, safe.y + y, width, height}, action});
    };
    if (spec.kind == Kind::Timer) {
        if (spec.mechanical && spec.rows == 2 && spec.columns == 2) {
            const auto stage = mechanicalart::arrange(mechanicalart::Kind::Timer, w, h);
            for (unsigned i = 0; i < 2; ++i) {
                const auto rect = stage.map(mechanicalart::timerButton(i));
                add(rect.x, rect.y, rect.width, rect.height, int(i + 1));
            }
        } else if (spec.mechanical) {
            const double buttonHeight = std::clamp(h * .16, 16., 28.);
            const double buttonWidth = (w - 28) * .5;
            add(10, h - buttonHeight - 8, buttonWidth, buttonHeight, 1);
            add(18 + buttonWidth, h - buttonHeight - 8, buttonWidth, buttonHeight, 2);
        } else {
            add(10, h - 34, (w - 26) * .65, 24, 1);
            add(10 + (w - 26) * .65, h - 34, (w - 26) * .35, 24, 2);
        }
    }
    if (spec.kind == Kind::Todo || spec.kind == Kind::Notes || spec.kind == Kind::Rss) {
        const int count = spec.kind == Kind::Todo    ? int(data.tasks.size())
                          : spec.kind == Kind::Notes ? int(lines(settings.at("text").text).size())
                                                     : int(data.articles.size());
        const int visible = spec.rows == 1 ? (spec.kind == Kind::Notes ? 2 : 1)
                                           : (spec.kind == Kind::Rss ? 2 : std::clamp(int((h - 60) / 32), 1, 4));
        const double top = spec.kind == Kind::Notes ? 32 : 36, bottom = h - 24,
                     row = (bottom - top) / std::max(1, visible);
        for (int i = 0; i < visible && data.scroll + i < count; ++i)
            add(10, top + i * row, w - 26, row, 100 + data.scroll + i);
        if (count > visible) {
            add(w - 57, h - 23, 20, 18, 3);
            add(w - 32, h - 23, 20, 18, 4);
        }
    }
    return result;
}
}  // namespace productivity
