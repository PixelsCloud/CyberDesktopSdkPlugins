#include <CyberCanvas/DataWidgets.h>
#include <sdk/Canvas.h>
#include <sdk/Icons.h>
#include <sdk/Language.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

#include "plugins/news-suite/News.h"

namespace news {
namespace {
cybercanvas::FlowParts tabsFor(Spec spec, double width, double dpr) {
    using Key = std::tuple<Kind, double, double>;
    thread_local std::map<Key, cybercanvas::FlowParts> cache;
    const Key key{spec.kind, width, dpr};
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    const bool github = spec.kind == Kind::Github;
    const double reserve = github && width >= 240 ? 70 : 0;
    auto result = cybercanvas::wrappingRow({12, 43, std::max(0., width - 24 - reserve), 125},
                                           std::vector<cybercanvas::Size>(github ? 3 : 5, {48, 22}), 3, true, dpr);
    if (cache.size() >= 16) {
        cache.clear();
    }
    cache.emplace(key, result);
    return result;
}
}  // namespace

Geometry geometry(Spec spec, CdRect safe, double dpr) {
    const auto width = std::max(1., safe.width), height = std::max(1., safe.height);
    const double top = std::min(height, 43 + tabsFor(spec, width, dpr).height + 4);
    const double bottom = std::max(top, height - 25);
    const int count = std::clamp(int((bottom - top) / (spec.rows == 2 ? 51 : 73)), 1, 8);
    return {width, height, top, bottom, (bottom - top) / count, count};
}

namespace {
CdRect pluginRect(cybercanvas::Rect area) {
    return {area.x, area.y, area.width, area.height};
}

struct ControlLayout {
    cybercanvas::Rect refresh, saved, previous, next, heading, language;
    std::vector<cybercanvas::Rect> tabs, rows;
};

ControlLayout controls(Spec spec, Geometry g, double dpr) {
    using namespace cybercanvas;
    const auto header = titleActions({8, 9, std::max(0., g.width - 16), 26}, 2, 24, 3, dpr);
    const auto footer = titleActions({7, g.height - 24, std::max(0., g.width - 14), 23}, 2, 24, 3, dpr);
    ControlLayout result;
    result.refresh = header.actions[1];
    result.saved = header.actions[0];
    result.next = footer.actions[1];
    result.previous = footer.actions[0];
    result.heading = inset(header.title, {37, 0, 0, 0});
    result.tabs = tabsFor(spec, g.width, dpr).items;
    if (spec.kind == Kind::Github && g.width >= 240) {
        result.language = {g.width - 82, 46, 70, 16};
    }
    result.rows = divide({0, g.top, g.width, g.bottom - g.top}, Axis::Vertical, g.capacity);
    return result;
}

cybercanvas::ChoiceGroup sourceChoices(Spec spec, const ControlLayout& layout, bool zh) {
    const std::array<const wchar_t*, 5> reader = zh ? std::array{L"全球", L"科技", L"中文", L"HN", L"订阅"}
                                                    : std::array{L"World", L"Tech", L"中文", L"HN", L"Feeds"};
    const std::array<const wchar_t*, 3> github =
        zh ? std::array{L"今日", L"本周", L"本月"} : std::array{L"Today", L"Week", L"Month"};
    std::vector<cybercanvas::ChoiceOption> options;
    for (size_t i = 0; i < layout.tabs.size(); ++i) {
        const auto area = layout.tabs[i];
        options.push_back({uint32_t(10 + i),
                           {area.x, area.y, std::max(0., area.width - 3), 20},
                           spec.kind == Kind::Github ? github[i] : reader[i]});
    }
    return cybercanvas::ChoiceGroup(std::move(options));
}
}  // namespace

std::vector<Hit> hitRegions(Spec spec, const Model& m, CdRect safe, double dpr, bool scrolling) {
    const auto g = geometry(spec, safe, dpr);
    const auto layout = controls(spec, g, dpr);
    std::vector<Hit> hits;
    const auto add = [&](double x, double y, double w, double h, int action) {
        const double left = std::clamp(x, 0., g.width), top = std::clamp(y, 0., g.height);
        const double right = std::clamp(x + w, left, g.width), bottom = std::clamp(y + h, top, g.height);
        if (right > left && bottom > top) {
            hits.push_back({{safe.x + left, safe.y + top, right - left, bottom - top}, action});
        }
    };
    const auto control = [&](cybercanvas::Rect area, int action) {
        add(area.x, area.y, area.width, area.height, action);
    };
    control(layout.refresh, 1);
    control(layout.saved, 2);
    const auto choices = sourceChoices(spec, layout, false);
    for (const auto& option : choices.options()) {
        control(option.bounds, int(option.id));
    }
    if (scrolling) {
        control(layout.next, 5);
        return hits;
    }
    control({layout.previous.x - 27, layout.previous.y, 24, 23}, 5);
    control(layout.previous, 3);
    control(layout.next, 4);
    const auto& list = visible(m);
    const int page = std::clamp(m.page, 0, std::max(0, (int(list.size()) - 1) / g.capacity));
    for (int row = 0; row < g.capacity; ++row) {
        const int index = page * g.capacity + row;
        if (index >= int(list.size())) {
            break;
        }
        const auto y = layout.rows[row].y;
        // Specific controls precede the encompassing article region.
        add(g.width - 36, y + 3, 24, 24, 200 + index);
        if (!list[index].discussion.empty()) {
            add(g.width - 91, y + g.rowHeight - 18, 55, 16, 300 + index);
        }
        add(10, y, g.width - 20, g.rowHeight - 1, 100 + index);
    }
    return hits;
}

struct Renderer::Impl {
    cdsdk::Canvas drawing;
    cdsdk::Icons icons;
    cdsdk::ListView list;
    std::vector<Item> snapshot;
    std::map<std::string, uint64_t> revisions;
    uint64_t nextRevision = 0;
    double rowHeight = 0;
    cdsdk::DiagnosticSource diagnostics;

    Impl() {
        diagnostics.onSample([this] {
            cdsdk::InstanceStats result;
            result.canvas = drawing.stats();
            result.list = list.stats();
            return result;
        });
        list.actions([this](const cybercanvas::ListRow& row) {
            const auto& r = row.bounds;
            std::vector<cybercanvas::ListAction> actions{{1, {r.x + r.width - 36, r.y + 3, 24, 24}}};
            if (row.index < snapshot.size() && !snapshot[row.index].discussion.empty()) {
                actions.push_back({2, {r.x + r.width - 91, r.y + r.height - 18, 55, 16}});
            }
            return actions;
        });
    }

    static cybercanvas::TextStyle textStyle(double size, bool bold = false, bool wrap = false) {
        cybercanvas::TextStyle style;
        style.size = size;
        style.weight = bold ? cybercanvas::FontWeight::SemiBold : cybercanvas::FontWeight::Normal;
        style.vertical = cybercanvas::VerticalAlign::Top;
        style.lineHeight = size * 1.35;
        style.baseline = size * 1.02;
        style.wrap = wrap;
        style.ellipsis = true;
        return style;
    }

    void text(const std::string& value, double size, CdRect area, uint32_t color, bool bold = false,
              bool wrap = false) {
        if (!value.empty() && area.width > 0 && area.height > 0) {
            drawing.text(wide(value), textStyle(size, bold, wrap), area, color);
        }
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bindHost(const CdCreateContext& context) {
    impl_->icons.bind(context);
    impl_->drawing.bindGpu(context);
    impl_->list.bind(context);
    impl_->diagnostics.bind(context);
}

void Renderer::onInvalidate(std::function<void()> callback) {
    impl_->list.onInvalidate(std::move(callback));
}

void Renderer::onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) {
    impl_->diagnostics.extras(std::move(callback));
}

cdsdk::ListView& Renderer::listView(Spec spec, const cdsdk::Settings& settings, const Model& model, CdRect safe,
                                    double dpr) {
    auto& p = *impl_;
    const auto& current = visible(model);
    const double height = spec.rows == 2 ? 68 : 90;
    if (current != p.snapshot || height != p.rowHeight) {
        std::map<std::string, size_t> old;
        for (size_t i = 0; i < p.snapshot.size(); ++i) {
            old.emplace(p.snapshot[i].url, i);
        }
        std::map<std::string, uint64_t> revisions;
        std::vector<cybercanvas::ListItem> items;
        for (const auto& item : current) {
            const auto found = old.find(item.url);
            const uint64_t revision =
                found != old.end() && p.snapshot[found->second] == item ? p.revisions.at(item.url) : ++p.nextRevision;
            revisions.emplace(item.url, revision);
            items.push_back({item.url, height, revision});
        }
        p.list.items(std::move(items));
        p.snapshot = current;
        p.revisions = std::move(revisions);
        p.rowHeight = height;
    }
    const auto g = geometry(spec, safe, dpr);
    p.list.arrange({0, g.top, g.width, g.bottom - g.top}, dpr);
    const bool zh = chinese(settings);
    cybercanvas::ListStatus status;
    status.state = !current.empty()       ? cybercanvas::ListState::Ready
                   : model.loading        ? cybercanvas::ListState::Loading
                   : !model.error.empty() ? cybercanvas::ListState::Error
                                          : cybercanvas::ListState::Empty;
    status.title = model.savedOnly        ? (zh ? L"还没有收藏" : L"No saved stories")
                   : model.loading        ? (zh ? L"正在获取最新内容" : L"Fetching the latest stories")
                   : !model.error.empty() ? (zh ? L"暂时无法更新" : L"Unable to update")
                                          : (zh ? L"这个来源暂时没有内容" : L"No stories in this source");
    status.detail = model.savedOnly ? (zh ? L"点亮星标，留住值得读的内容" : L"Star a story to keep it here")
                    : model.error == "no_feeds"
                        ? (zh ? L"在设置中添加 RSS / Atom 地址" : L"Add RSS / Atom URLs in settings")
                    : !model.error.empty() ? (zh ? L"请检查来源，或稍后重试" : L"Check the source or try again later")
                                           : L"";
    status.canRetry = !model.savedOnly && !model.loading && !model.error.empty() && model.error != "no_feeds";
    status.retry = zh ? L"重试" : L"Retry";
    p.list.status(std::move(status));
    return p.list;
}

void Renderer::cancelList() {
    impl_->list.cancel();
}

void Renderer::resetList() {
    impl_->list.cancel();
    impl_->list.scrollTo(0, false);
}

bool Renderer::advanceList(double seconds) {
    return impl_->list.advance(seconds);
}

bool Renderer::listActive() const {
    return impl_->list.active();
}

namespace {
std::string timeLabel(int64_t stamp, bool zh) {
    if (stamp <= 0) {
        return "";
    }
    const auto seconds = std::max(int64_t(0), wallSeconds() - stamp);
    if (seconds < 60) {
        return zh ? "刚刚" : "now";
    }
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + (zh ? " 分钟前" : "m ago");
    }
    if (seconds < 86400) {
        return std::to_string(seconds / 3600) + (zh ? " 小时前" : "h ago");
    }
    return std::to_string(seconds / 86400) + (zh ? " 天前" : "d ago");
}

uint32_t languageColor(const std::string& value) {
    if (value == "TypeScript") {
        return 0x438FDFFF;
    }
    if (value == "Python") {
        return 0xE6B85AFF;
    }
    if (value == "Rust") {
        return 0xD89075FF;
    }
    if (value == "Go") {
        return 0x51B7CEFF;
    }
    if (value == "JavaScript") {
        return 0xCFB94EFF;
    }
    return 0x9A8AC8FF;
}
}  // namespace

bool Renderer::render(Spec spec, const cdsdk::Settings& s, const Model& m, const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope languageScope(s.at("language").text);
    auto& p = *impl_;
    auto& d = p.drawing;
    if (!d.valid(frame)) {
        return false;
    }
    auto painting = d.frame(frame, safe);
    safe = painting.area();
    const auto g = geometry(spec, safe, frame.device_pixel_ratio);
    const auto layout = controls(spec, g, frame.device_pixel_ratio);
    const bool github = spec.kind == Kind::Github, zh = chinese(s);
    const bool scrolling = s.at("list_mode").text == "scroll";
    if (scrolling) {
        listView(spec, s, m, safe, frame.device_pixel_ratio);
    }
    const auto lightSetting = s.at("lighting").text;
    const bool light = lightSetting == "day" || (lightSetting == "theme" && m.light);
    const uint32_t bg = light ? 0xF7F9FCFF : 0x151B26FF;
    const uint32_t fg = light ? 0x202B3CFF : 0xECF1FAFF;
    const uint32_t muted = light ? 0x708095FF : 0x91A0B7FF;
    const uint32_t line = light ? 0xDDE5F0FF : 0x2B3545FF;
    const uint32_t accent = github ? (light ? 0x7256B8FF : 0xB29CE7FF) : (light ? 0x237C85FF : 0x6DD4C7FF);
    const uint32_t tint = github ? (light ? 0xECE7F6FF : 0x292339FF) : (light ? 0xE4F2F1FF : 0x193735FF);
    const uint32_t hover = light ? 0xEAF0F8C0 : 0x283447C0;
    auto theme = light ? cybercanvas::WidgetTheme::light() : cybercanvas::WidgetTheme::dark();
    theme.foreground = fg;
    theme.muted = muted;
    theme.accent = accent;
    theme.surface = hover;
    theme.font.size = 10;
    theme.padding = 4;
    auto selectedTheme = theme;
    selectedTheme.accent = tint;
    selectedTheme.onAccent = accent;
    const auto iconButton = [&](uint32_t id, cybercanvas::Rect area, const char* name, const wchar_t* label,
                                bool selected = false) {
        cybercanvas::IconButton button(id, area, name, label);
        button.selected = selected;
        button.draw(d, selected ? selectedTheme : theme, p.icons.painter(), m.widgets.visual(id));
    };
    if (s.at("show_background").integer) {
        d.box(0, 0, g.width, g.height, double(s.at("background_corner_radius").integer), bg);
    }
    const auto& list = visible(m);
    const int pages = std::max(1, (int(list.size()) + g.capacity - 1) / g.capacity);
    const int page = std::clamp(m.page, 0, pages - 1);
    const double headerY = g.height < 160 ? 8 : 12;
    d.box(12, headerY, 25, 25, 7, tint);
    if (github) {
        p.text("<>", 12, {16, headerY + 4, 21, 18}, accent, true);
    } else {
        d.box(19, headerY + 6, 11, 13, 1, accent);
        d.line(22, headerY + 10, 27, headerY + 10, bg, 1.2);
        d.line(22, headerY + 13, 27, headerY + 13, bg, 1.2);
        d.line(22, headerY + 16, 25, headerY + 16, bg, 1.2);
    }
    p.text(github ? (zh ? "开源热榜" : "Open source") : (zh ? "资讯阅读" : "Daily reader"), 14,
           {layout.heading.x, headerY - 1, layout.heading.width, 22}, fg, true);
    std::string eyebrow;
    if (m.savedOnly) {
        eyebrow = zh ? "已收藏" : "SAVED STORIES";
    } else if (github) {
        eyebrow = "GITHUB TRENDING";
    } else {
        const auto q = query(spec.kind, s);
        for (const auto& source : sources()) {
            if (q.source == source.id) {
                eyebrow = source.name;
            }
        }
        if (q.source == "hn") {
            eyebrow = "HACKER NEWS · " + q.hnOrder;
        } else if (q.source == "custom") {
            eyebrow = zh ? "自定义订阅" : "YOUR SUBSCRIPTIONS";
        }
    }
    p.text(eyebrow, 8.2, {layout.heading.x, headerY + 19, layout.heading.width, 13}, muted);
    iconButton(1, layout.refresh, "refresh-cw", zh ? L"刷新" : L"Refresh");
    iconButton(2, layout.saved, "star", zh ? L"收藏" : L"Saved", m.savedOnly);
    const auto source = s.at("source").text;
    int selected = github               ? (s.at("period").text == "daily"    ? 0
                                           : s.at("period").text == "weekly" ? 1
                                                                             : 2)
                   : source == "hn"     ? 3
                   : source == "custom" ? 4
                   : source == "ithome" || source == "solidot" || source == "sspai" || source == "ruanyifeng" ||
                           source == "chinanews"
                       ? 2
                   : source == "verge" || source == "ars" ? 1
                                                          : 0;
    sourceChoices(spec, layout, zh).draw(d, selectedTheme, m.savedOnly ? 0 : uint32_t(10 + selected), [&](uint32_t id) {
        return m.widgets.visual(id);
    });
    if (github) {
        const auto lang = s.at("code_language").text;
        const std::string label = lang == "all" ? (zh ? "全部语言" : "All languages") : lang;
        p.text(label, 8.5, pluginRect(layout.language), muted);
    }
    d.line(12, g.top - 1, g.width - 12, g.top - 1, line, .7);
    if (!scrolling && list.empty()) {
        const double cy = g.top + (g.bottom - g.top) * .36;
        const bool emptySaved = m.savedOnly;
        p.icons.paint(d, {g.width / 2 - 12, cy - 18, 24, 24}, emptySaved ? "star" : "circle", accent);
        const auto message =
            emptySaved              ? (zh ? "点亮星标，留住值得读的内容" : "Star a story to keep it here")
            : m.loading             ? (zh ? "正在获取最新内容…" : "Fetching the latest stories…")
            : m.error == "no_feeds" ? (zh ? "在设置中添加 RSS / Atom 地址" : "Add RSS / Atom URLs in settings")
            : !m.error.empty() ? (zh ? "暂时无法更新，稍后自动重试" : "Unable to update. Retrying later.")
                               : (zh ? "这个来源暂时没有内容" : "No stories in this source yet");
        p.text(message, 10.3, {22, cy + 18, g.width - 44, 32}, muted, false, true);
    }
    const auto drawRow = [&](int index, cybercanvas::Rect rowArea) {
        const auto& item = list[index];
        const double rowWidth = rowArea.width, rowHeight = rowArea.height;
        const double y = rowArea.y;
        const bool read = std::find(m.read.begin(), m.read.end(), item.url) != m.read.end();
        const bool saved = std::any_of(m.saved.begin(), m.saved.end(), [&](const Item& v) {
            return v.url == item.url;
        });
        auto rowVisual = scrolling ? p.list.visual(item.url) : m.widgets.visual(100 + index);
        rowVisual.feedback = std::max(
            {rowVisual.feedback, (scrolling ? p.list.visual(item.url, 1) : m.widgets.visual(200 + index)).feedback,
             (scrolling ? p.list.visual(item.url, 2) : m.widgets.visual(300 + index)).feedback});
        cybercanvas::Button article(100 + index, {8, y + 2, rowWidth - 16, rowHeight - 4});
        article.background(d, theme, rowVisual);
        const double left = github ? 34 : 17;
        auto contentArea = cybercanvas::inset(rowArea, {left, 5, 38, 3});
        const auto titleStyle = Impl::textStyle(github ? 12 : 12.2, true, !github);
        const auto content = cybercanvas::textSections(d, contentArea, wide(item.title), titleStyle, github ? 18. : 34.,
                                                       13, 3, d.devicePixelRatio());
        const auto titleArea = content.title;
        const auto metadata = content.footer;
        if (github) {
            p.text(index < 9 ? "0" + std::to_string(index + 1) : std::to_string(index + 1), 9.5, {13, y + 7, 20, 15},
                   index < 3 ? accent : muted, true);
        } else if (!read) {
            d.oval(11.5, y + 12, 1.7, 1.7, accent);
        }
        p.text(item.title, titleStyle.size, pluginRect(titleArea), read ? muted : fg, true, !github);
        if (scrolling) {
            cybercanvas::IconButton star(1, {rowWidth - 36, y + 3, 24, 24}, "star", zh ? L"收藏" : L"Save");
            star.selected = saved;
            star.draw(d, saved ? selectedTheme : theme, p.icons.painter(), p.list.visual(item.url, 1));
        } else {
            iconButton(200 + index, {rowWidth - 36, y + 3, 24, 24}, "star", zh ? L"收藏" : L"Save", saved);
        }
        auto description = content.body;
        if (description.height >= 12 && !item.summary.empty()) {
            description.height = std::min(29., description.height);
            p.text(item.summary, github && rowHeight < 64 ? 9.1 : 10.2, pluginRect(description), muted, false,
                   description.height >= 27);
        }
        const double metaY = metadata.y;
        if (github) {
            const std::string lang = item.language.empty() ? "—" : item.language;
            d.oval(left + 2, metaY + 6, 2.6, 2.6, languageColor(item.language));
            p.text(lang, 8.5, {left + 9, metaY, 70, 13}, muted);
            p.text(item.stars.empty() ? "" : "☆ " + item.stars, 8.5, {left + 81, metaY, 72, 13}, muted);
            p.text(item.growth.empty() ? "" : "↗ " + item.growth, 8.5, {rowWidth - 77, metaY, 63, 13}, accent, true);
        } else {
            const auto age = timeLabel(item.published, zh);
            std::string meta = item.source + (age.empty() ? "" : " · " + age);
            if (!item.discussion.empty()) {
                meta = "▲ " + compactNumber(item.score) + " · " + age;
            }
            p.text(meta, 8.5, {left, metaY, rowWidth - left - (item.discussion.empty() ? 18 : 92), 13}, muted);
            if (!item.discussion.empty()) {
                cybercanvas::Button discussion(300 + index, {rowWidth - 91, y + rowHeight - 18, 55, 16},
                                               wide(std::to_string(item.comments) + (zh ? " 讨论" : " replies")));
                auto discussionTheme = theme;
                discussionTheme.foreground = accent;
                discussionTheme.font.size = 8.3;
                discussion.draw(d, discussionTheme,
                                scrolling ? p.list.visual(item.url, 2) : m.widgets.visual(discussion.id));
            }
        }
        if (index + 1 < int(list.size())) {
            d.line(16, y + rowHeight, rowWidth - 16, y + rowHeight, line, .6);
        }
    };
    if (scrolling) {
        p.list.draw(
            d, theme,
            [&](auto&, const cybercanvas::ListRow& row) {
                drawRow(int(row.index), row.bounds);
            },
            p.icons.painter());
    } else {
        for (int row = 0; row < g.capacity; ++row) {
            const int index = page * g.capacity + row;
            if (index >= int(list.size())) {
                break;
            }
            d.clipped(pluginRect(layout.rows[row]), [&] {
                drawRow(index, layout.rows[row]);
            });
        }
    }
    d.line(12, g.bottom + 1, g.width - 12, g.bottom + 1, line, .7);
    const auto footerY = g.height - 18;
    const bool warning = !m.error.empty() || m.partial;
    d.oval(16, footerY + 6, 2, 2, warning ? 0xD9A85EFF : m.loading ? accent : muted);
    const std::string status = m.loading                       ? (zh ? "更新中" : "Updating")
                               : !m.error.empty() && m.fetched ? (zh ? "离线缓存" : "Cached · offline")
                               : m.partial        ? (zh ? "部分来源未更新" : "Some sources unavailable")
                               : !m.error.empty() ? (zh ? "更新失败" : "Update unavailable")
                               : m.savedOnly      ? std::to_string(list.size()) + (zh ? " 条收藏" : " saved")
                               : m.fetched        ? (zh ? "已更新 " : "Updated ") + timeLabel(m.fetched, zh)
                                                  : (zh ? "等待更新" : "Waiting for update");
    p.text(status, 8.2, {23, footerY, g.width - (scrolling ? 125 : 180), 14}, muted);
    if (scrolling) {
        p.text(std::to_string(list.size()) + (zh ? " 条" : " items"), 8.5, {g.width - 86, footerY, 48, 14}, muted);
        iconButton(5, layout.next, "layout-grid", zh ? L"分页" : L"Pages");
    } else {
        p.text(std::to_string(page + 1) + " / " + std::to_string(pages), 8.5, {g.width - 124, footerY, 36, 14}, muted);
        iconButton(5, {layout.previous.x - 27, layout.previous.y, 24, 23}, "list", zh ? L"滚动" : L"Scroll");
        iconButton(3, layout.previous, "chevron-left", zh ? L"上页" : L"Prev");
        iconButton(4, layout.next, "chevron-right", zh ? L"下页" : L"Next");
    }
    painting.finish();
    return true;
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->drawing.save(path.wstring(), frame);
}
}  // namespace news
