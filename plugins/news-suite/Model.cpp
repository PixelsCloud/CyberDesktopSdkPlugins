#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

#include "plugins/news-suite/News.h"

namespace news {
using Json = nlohmann::json;

int64_t wallSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string compactNumber(int64_t value) {
    if (value < 1000) {
        return std::to_string(value);
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(value < 10000 ? 1 : 0) << double(value) / 1000 << 'k';
    return out.str();
}

namespace {
void to_json(Json& j, const Item& item) {
    j = Json::array({item.title, item.url, item.summary, item.source, item.language, item.stars, item.growth,
                     item.discussion, item.published, item.score, item.comments});
}

Item fromItem(const Json& j) {
    if (!j.is_array() || j.size() != 11) {
        throw std::runtime_error("invalid_state");
    }
    Item v;
    std::string* texts[]{&v.title, &v.url, &v.summary, &v.source, &v.language, &v.stars, &v.growth, &v.discussion};
    const size_t limits[]{1000, 2048, 700, 120, 60, 32, 64, 2048};
    for (size_t i = 0; i < 8; ++i) {
        *texts[i] = j[i].get<std::string>();
        if (texts[i]->size() > limits[i]) {
            throw std::runtime_error("invalid_state");
        }
        wide(*texts[i]);
    }
    if (!webUrl(v.url) || (!v.discussion.empty() && !webUrl(v.discussion)) || v.title.empty()) {
        throw std::runtime_error("invalid_state");
    }
    v.published = j[8].get<int64_t>();
    v.score = j[9].get<int>();
    v.comments = j[10].get<int>();
    return v;
}
}  // namespace

const std::vector<Source>& sources() {
    static const std::vector<Source> values{
        {"bbc", "BBC World", "https://feeds.bbci.co.uk/news/world/rss.xml"},
        {"guardian", "The Guardian", "https://www.theguardian.com/world/rss"},
        {"verge", "The Verge", "https://www.theverge.com/rss/index.xml"},
        {"ars", "Ars Technica", "https://feeds.arstechnica.com/arstechnica/index"},
        {"dw", "Deutsche Welle", "https://rss.dw.com/rdf/rss-de-all"},
        {"nhk", "NHK", "https://www3.nhk.or.jp/rss/news/cat0.xml"},
        {"ithome", "IT之家", "https://www.ithome.com/rss/"},
        {"solidot", "Solidot", "https://www.solidot.org/index.rss"},
        {"sspai", "少数派", "https://sspai.com/feed"},
        {"chinanews", "中国新闻网", "https://www.chinanews.com.cn/rss/importnews.xml"},
        {"ruanyifeng", "阮一峰的网络日志", "https://www.ruanyifeng.com/blog/atom.xml"}};
    return values;
}

bool chinese(const cdsdk::Settings& s) {
    const auto value = s.at("language").text;
    return value == "zh-CN" || (value == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE);
}

cdsdk::Settings defaults(Kind kind) {
    return {{{"source",
              CD_SETTING_CHOICE,
              0,
              0,
              "bbc",
              0,
              0,
              {"bbc", "guardian", "verge", "ars", "dw", "nhk", "ithome", "solidot", "sspai", "chinanews", "ruanyifeng",
               "hn", "custom"}},
             {"feeds", CD_SETTING_TEXT, 0, 0, "", 0, 4096},
             {"hn_order", CD_SETTING_CHOICE, 0, 0, "top", 0, 0, {"top", "new", "best"}},
             {"period", CD_SETTING_CHOICE, 0, 0, "daily", 0, 0, {"daily", "weekly", "monthly"}},
             {"code_language",
              CD_SETTING_CHOICE,
              0,
              0,
              "all",
              0,
              0,
              {"all", "python", "typescript", "javascript", "c++", "c", "rust", "go", "java", "c#", "swift", "kotlin",
               "ruby", "php", "shell"}},
             {"refresh_minutes", CD_SETTING_INTEGER, kind == Kind::Github ? 30 : 20, 0, "", 5, 180},
             {"list_mode", CD_SETTING_CHOICE, 0, 0, "scroll", 0, 0, {"scroll", "pages"}},
             {"lighting", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "day", "night"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"background_corner_radius", CD_SETTING_INTEGER, 8, 0, "", 0, 32},
             {"language", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto", "zh-CN", "en"}}}};
}

Query query(Kind kind, const cdsdk::Settings& s) {
    Query out{kind, s.at("source").text, s.at("period").text, s.at("code_language").text, s.at("hn_order").text};
    if (kind == Kind::Github || out.source == "hn") {
        return out;
    }
    if (out.source == "custom") {
        std::istringstream input(s.at("feeds").text);
        std::string value;
        while (std::getline(input, value)) {
            value = clean(value, 4096);
            if (value.empty()) {
                continue;
            }
            if (!webUrl(value)) {
                throw std::runtime_error("invalid_url");
            }
            if (std::find(out.urls.begin(), out.urls.end(), value) == out.urls.end()) {
                if (out.urls.size() >= 6) {
                    throw std::runtime_error("invalid_url");
                }
                out.urls.push_back(value);
            }
        }
    } else {
        for (const auto& source : sources()) {
            if (out.source == source.id) {
                out.urls.push_back(source.url);
                break;
            }
        }
    }
    return out;
}

std::string Query::key() const {
    if (kind == Kind::Github) {
        return "github:" + period + ":" + language;
    }
    return "reader:" + source + ":" + (source == "hn" ? hnOrder : Json(urls).dump());
}

std::string Query::address() const {
    if (kind == Kind::Github) {
        std::string slug = language == "all"   ? ""
                           : language == "c++" ? "c%2B%2B"
                           : language == "c#"  ? "c%23"
                                               : language;
        return "https://github.com/trending" + (slug.empty() ? "" : "/" + slug) + "?since=" + period;
    }
    if (source == "hn") {
        return "https://hacker-news.firebaseio.com/v0/" + hnOrder + "stories.json";
    }
    return urls.empty() ? "" : urls.front();
}

const std::vector<Item>& visible(const Model& m) {
    return m.savedOnly ? m.saved : m.items;
}

void accept(Model& m, Result r) {
    m.loading = false;
    m.error = std::move(r.error);
    m.partial = r.partial;
    if (r.valid) {
        m.key = std::move(r.key);
        m.items = std::move(r.items);
        m.fetched = r.fetched;
        m.page = 0;
    }
}

void toggleSaved(Model& m, const Item& item) {
    const auto found = std::find_if(m.saved.begin(), m.saved.end(), [&](const Item& v) {
        return v.url == item.url;
    });
    if (found != m.saved.end()) {
        m.saved.erase(found);
    } else if (m.saved.size() < 60) {
        m.saved.insert(m.saved.begin(), item);
    }
}

void markRead(Model& m, const std::string& url) {
    std::erase(m.read, url);
    m.read.insert(m.read.begin(), url);
    if (m.read.size() > 200) {
        m.read.resize(200);
    }
}

std::string stateBytes(const Model& m) {
    Json items = Json::array(), saved = Json::array();
    for (const auto& v : m.items) {
        Json j;
        to_json(j, v);
        items.push_back(std::move(j));
    }
    for (const auto& v : m.saved) {
        Json j;
        to_json(j, v);
        saved.push_back(std::move(j));
    }
    return Json{{"version", 1},   {"key", m.key},   {"fetched", m.fetched},
                {"items", items}, {"saved", saved}, {"read", m.read}}
        .dump();
}

bool restoreBytes(Model& m, const std::string& bytes) {
    try {
        if (bytes.size() > 2 * 1024 * 1024) {
            return false;
        }
        const auto j = Json::parse(bytes);
        if (j.at("version") != 1 || j.at("items").size() > 30 || j.at("saved").size() > 60 ||
            j.at("read").size() > 200) {
            return false;
        }
        Model out;
        out.light = m.light;
        out.key = j.at("key").get<std::string>();
        if (out.key.size() > 15000) {
            return false;
        }
        out.fetched = j.at("fetched").get<int64_t>();
        for (const auto& v : j.at("items")) {
            out.items.push_back(fromItem(v));
        }
        for (const auto& v : j.at("saved")) {
            out.saved.push_back(fromItem(v));
        }
        out.read = j.at("read").get<std::vector<std::string>>();
        for (const auto& url : out.read) {
            if (!webUrl(url)) {
                return false;
            }
        }
        m = std::move(out);
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace news
