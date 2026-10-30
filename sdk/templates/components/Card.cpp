#include "templates/components/Card.h"

namespace componentdemo {
Card::Card(const CdCreateContext& context, int kind)
    : kind_(kind), live_(context, kind, [this] {
          arrange();
          invalidate();
      }) {
    environment_.bind(context);
    redraw_.bind(context);
    timer_.bind(context);
    controls_.bind(context, [this](uint32_t id) {
        if (!paused_) {
            activate(id);
        }
    });
    schedule();
}

auto Card::invalidate() -> void {
    redraw_.invalidate();
}

auto Card::layoutChanged(const CdLayout& layout) -> void {
    safe_ = layout.safe_content;
    if (safe_.width <= 0 || safe_.height <= 0) {
        safe_ = {0, 0, layout.logical_width, layout.logical_height};
    }
    controls_.cancel();
    arrange();
}

auto Card::input(const CdInput& event) -> void {
    if (!paused_) {
        arrange();
        controls_.input(event);
    }
}

auto Card::setPaused(bool paused) -> void {
    paused_ = paused;
    controls_.cancel();
    redraw_.pause(paused);
    schedule();
}

auto Card::applySettings(const CdSettingValue* values, uint32_t count) -> CdResult {
    const auto result = settings.apply(values, count);
    if (result == CD_OK) {
        controls_.cancel();
        schedule();
        invalidate();
    }
    return result;
}

auto Card::render(const CdFrame& frame) -> CdResult {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    if (safe_.width <= 0 || safe_.height <= 0) {
        safe_ = {0, 0, frame.width / frame.device_pixel_ratio, frame.height / frame.device_pixel_ratio};
    }
    arrange();
    const auto theme = cybercanvas::WidgetTheme::desktop(environment_.snapshot().theme == CD_ENV_THEME_LIGHT);
    canvas_.render(frame, safe_, [&](auto& draw, auto area) {
        if (settings.at("show_background").integer) {
            draw.box(0, 0, area.width, area.height, theme.radius, theme.surface);
        }
        draw.clipped({0, 0, area.width, area.height}, [&] {
            if (area.width < 120 || area.height < (kind_ == 2 ? 180 : 140)) {
                if (area.width > 16 && area.height > 16) {
                    label(cn() ? L"请放大卡片" : L"Enlarge card", {8, 8, area.width - 16, area.height - 16}, theme)
                        .draw(draw);
                }
                return;
            }
            const std::wstring titles[]{cn() ? L"信息卡" : L"Information", cn() ? L"今日待办" : L"Tasks",
                                        cn() ? L"媒体控制" : L"Media controls", monthTitle()};
            label(titles[kind_], {12, 8, std::max(0., area.width - 104), 28}, theme, 18, true).draw(draw);
            if (kind_ == 0) {
                drawInfo(theme);
            } else if (kind_ == 1) {
                drawTasks(theme);
            } else if (kind_ == 2) {
                drawMedia(theme);
            } else {
                drawCalendar(theme);
            }
            controls_.drawFocus(draw, theme);
        });
    });
    return CD_OK;
}

auto Card::saveState() const -> std::vector<uint8_t> {
    const auto month = uint16_t(monthOffset_ + 1200);
    return {'C',
            'D',
            1,
            uint8_t(kind_),
            tasks_,
            track_,
            uint8_t(playing_),
            uint8_t(position_),
            uint8_t(month),
            uint8_t(month >> 8),
            selected_,
            sample_};
}

auto Card::validState(std::span<const uint8_t> state, int kind) -> bool {
    return state.empty() ||
           !(state.size() != 12 || state[0] != 'C' || state[1] != 'D' || state[2] != 1 || state[3] != kind ||
             state[4] > 15 || state[5] > 2 || state[6] > 1 || state[7] > 180 ||
             (uint16_t(state[8]) | uint16_t(state[9]) << 8) > 2400 || state[10] > 31 || state[11] > 100);
}

auto Card::restoreState(std::span<const uint8_t> state) -> CdResult {
    if (!validState(state, kind_)) {
        return CD_ERROR_ARGUMENT;
    }
    tasks_ = state.empty() ? 0 : state[4];
    track_ = state.empty() ? 0 : state[5];
    playing_ = !state.empty() && state[6];
    position_ = state.empty() ? 0 : state[7];
    monthOffset_ = state.empty() ? 0 : int(uint16_t(state[8]) | uint16_t(state[9]) << 8) - 1200;
    selected_ = state.empty() ? 0 : state[10];
    sample_ = state.empty() ? 64 : state[11];
    controls_.cancel();
    schedule();
    return CD_OK;
}

auto Card::live() const -> bool {
    return (kind_ == 0 || kind_ == 2) && settings.at("data_source").text == "host";
}

auto Card::playing() const -> bool {
    return live() ? live_.view().playing : playing_;
}

auto Card::serviceStatus() const -> std::wstring {
    const auto& value = live_.view();
    using State = ServiceModel::State;
    if (value.state == State::Loading) {
        return cn() ? L"正在连接宿主服务…" : L"Connecting to host…";
    }
    if (value.state == State::Unavailable) {
        return cn() ? L"服务不可用 · 点击重试" : L"Service unavailable · retry";
    }
    if (value.state == State::Error) {
        return cn() ? L"读取失败 · 点击重试" : L"Data error · retry";
    }
    if (value.state == State::Timeout) {
        return cn() ? L"连接超时 · 点击重试" : L"Connection timed out · retry";
    }
    if (value.state == State::Empty) {
        return kind_ == 2 ? (cn() ? L"请在播放器中开始播放" : L"Start playback in a media app")
                          : (cn() ? L"暂无监控数据" : L"No telemetry available");
    }
    if (value.pending) {
        return cn() ? L"正在等待播放器响应…" : L"Waiting for player…";
    }
    if (value.commandError) {
        return cn() ? L"操作失败 · 可重试" : L"Command failed · retry available";
    }
    return kind_ == 0 ? (cn() ? L"实时系统监控" : L"Live system telemetry") : cdsdk::fontWide(value.source);
}

auto Card::cn() const -> bool {
    const auto& language = settings.at("language").text;
    return (language == "auto" ? std::string(environment_.snapshot().locale) : language).starts_with("zh");
}

auto Card::label(std::wstring text, Rect bounds, const Theme& theme, double size, bool bold) const
    -> cybercanvas::Label {
    auto font = theme.font;
    font.family = cn() ? L"Microsoft YaHei" : L"Segoe UI";
    font.size = size;
    font.weight = bold ? cybercanvas::FontWeight::Bold : cybercanvas::FontWeight::Normal;
    return {bounds, std::move(text), font, theme.foreground, true};
}

auto Card::tasks() const -> std::array<std::wstring, 4> {
    return cn() ? std::array<std::wstring, 4>{L"整理桌面", L"阅读十分钟", L"完成一个小目标", L"休息一下"}
                : std::array<std::wstring, 4>{L"Tidy the desktop", L"Read for ten minutes", L"Finish one small goal",
                                              L"Take a break"};
}

auto Card::firstDay() const -> std::chrono::year_month_day {
    const auto now = environment_.localTime();
    const auto month = std::chrono::year{now.tm_year + 1900} / std::chrono::month{unsigned(now.tm_mon + 1)} +
                       std::chrono::months{monthOffset_};
    return month / 1;
}

auto Card::daysInMonth() const -> unsigned {
    const auto first = firstDay();
    return unsigned(std::chrono::year_month_day_last{first.year(), std::chrono::month_day_last{first.month()}}.day());
}

auto Card::dayOffset() const -> size_t {
    return std::chrono::weekday{std::chrono::sys_days{firstDay()}}.iso_encoding() - 1;
}

auto Card::monthTitle() const -> std::wstring {
    const auto date = firstDay();
    return std::to_wstring(int(date.year())) + L" / " + std::to_wstring(unsigned(date.month()));
}

auto Card::grid() const -> cybercanvas::CalendarGrid {
    return {{12, 66, std::max(0., safe_.width - 24), std::max(0., safe_.height - 76)}, 6};
}

auto Card::arrange() -> void {
    items_.clear();
    if (safe_.width < 120 || safe_.height < (kind_ == 2 ? 180 : 140)) {
        controls_.items({});
        return;
    }
    if (kind_ == 0) {
        items_.push_back({1, {safe_.width - 88, 8, 76, 28}, cn() ? "刷新" : "Refresh"});
    } else if (kind_ == 1) {
        const auto text = tasks();
        const double height = std::max(0., (safe_.height - 64) / 4);
        for (uint32_t i = 0; i < 4; ++i) {
            // Stable row IDs are independent of translated labels.
            const char* english[]{"Tidy the desktop", "Read for ten minutes", "Finish one small goal", "Take a break"};
            const char* chinese[]{"整理桌面", "阅读十分钟", "完成一个小目标", "休息一下"};
            const auto name = cn() ? chinese[i] : english[i];
            items_.push_back(
                {i + 1, {12, 44 + i * height, safe_.width - 24, height}, name, true, true, bool(tasks_ & (1 << i))});
        }
    } else if (kind_ == 2) {
        for (uint32_t i = 0; i < 3; ++i) {
            items_.push_back({i + 1,
                              {safe_.width * .5 - 64 + i * 44, safe_.height - 64, 40, 32},
                              i == 0      ? (cn() ? "上一首" : "Previous")
                              : i == 2    ? (cn() ? "下一首" : "Next")
                              : playing() ? (cn() ? "暂停" : "Pause")
                                          : (cn() ? "播放" : "Play"),
                              !live() || live_.can(i == 0   ? CD_MEDIA_PREVIOUS
                                                   : i == 2 ? CD_MEDIA_NEXT
                                                            : CD_MEDIA_TOGGLE)});
        }
        if (live()) {
            items_.push_back({4, {safe_.width - 88, 8, 76, 28}, cn() ? "重新连接" : "Reconnect"});
        }
    } else {
        items_.push_back({1, {safe_.width - 84, 8, 32, 28}, cn() ? "上个月" : "Previous month", monthOffset_ > -1200});
        items_.push_back({2, {safe_.width - 44, 8, 32, 28}, cn() ? "下个月" : "Next month", monthOffset_ < 1200});
        for (uint32_t day = 1; day <= daysInMonth(); ++day) {
            items_.push_back({100 + day, grid().cell(dayOffset() + day - 1),
                              std::to_string(int(firstDay().year())) + "-" +
                                  std::to_string(unsigned(firstDay().month())) + "-" + std::to_string(day),
                              true, true, selected_ == day});
        }
    }
    controls_.items(items_, {safe_.x, safe_.y});
}

auto Card::button(const cdsdk::Controls::Item& item, const Theme& theme, std::string icon, std::wstring fallback)
    -> void {
    cybercanvas::IconButton control(item.id, item.bounds, std::move(icon), std::move(fallback));
    control.enabled = item.enabled;
    control.iconSize = 18;
    control.draw(canvas_, theme, controls_.icons.painter(), controls_.visual(item.id));
}

auto Card::drawInfo(const Theme& theme) -> void {
    if (items_.empty()) {
        return;
    }
    button(items_[0], theme, "refresh-cw", cn() ? L"刷新" : L"Refresh");
    const bool ready = !live() || live_.view().state == ServiceModel::State::Ready;
    const auto amount = live() ? live_.view().cpu : sample_;
    const double diameter = std::max(24., std::min(safe_.height - 82, safe_.width * .35));
    ring_.draw(canvas_, {18, 48, diameter, diameter}, ready ? amount / 100. : 0, theme, 5);
    auto percent = label(ready ? std::to_wstring(amount) + L"%" : L"--", {18, 48, diameter, diameter}, theme, 24, true);
    percent.font.align = cybercanvas::TextAlign::Center;
    percent.draw(canvas_);
    cybercanvas::MetricItem metric;
    metric.caption = label(live() ? L"CPU / RAM" : (cn() ? L"本周目标" : L"Weekly target"),
                           {diameter + 34, 50, safe_.width - diameter - 46, 24}, theme);
    metric.number = label(live() ? (ready ? std::to_wstring(live_.view().memory) + L"% RAM" : L"--")
                                 : std::to_wstring(sample_) + L" / 100",
                          {diameter + 34, 78, safe_.width - diameter - 46, 34}, theme, 26, true);
    metric.draw(canvas_);
    auto source = label(live() ? serviceStatus() : (cn() ? L"示例数据 · 点击刷新" : L"Sample data · click refresh"),
                        {12, safe_.height - 28, safe_.width - 24, 20}, theme, 11);
    source.color = theme.muted;
    cybercanvas::StatusLine{source, {}}.draw(canvas_);
}

auto Card::drawTasks(const Theme& theme) -> void {
    const auto text = tasks();
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        const auto r = item.bounds;
        auto title = label(text[i], {r.x + 30, r.y, r.width - 30, r.height}, theme);
        if (item.checked) {
            title.color = theme.muted;
        }
        cybercanvas::TextRow row{r, title, {}, theme.track};
        cybercanvas::TaskRow task{row, cybercanvas::Checkbox{item.id, {r.x, r.y + (r.height - 18) / 2, 18, 18}},
                                  item.checked};
        task.draw(canvas_, theme, controls_.icons.painter(), controls_.visual(item.id));
    }
}

auto Card::drawMedia(const Theme& theme) -> void {
    if (live()) {
        const auto& value = live_.view();
        const auto title =
            value.state == ServiceModel::State::Ready
                ? (value.title.empty() ? (cn() ? L"未命名曲目" : L"Untitled track") : cdsdk::fontWide(value.title))
                : (cn() ? L"媒体播放" : L"Media playback");
        label(title, {12, 42, safe_.width - 24, 30}, theme, 20, true).draw(canvas_);
        label(value.state == ServiceModel::State::Ready ? cdsdk::fontWide(value.artist) : L"",
              {12, 74, safe_.width - 24, 20}, theme, 12)
            .draw(canvas_);
        auto status = label(serviceStatus(),
                            {12, 96, safe_.width - 24, std::max(0., std::min(22., safe_.height - 164))}, theme, 11);
        status.color = theme.muted;
        status.draw(canvas_);
    } else {
        label((cn() ? L"示例曲目 " : L"Demo track ") + std::to_wstring(track_ + 1), {12, 52, safe_.width - 24, 32},
              theme, 22, true)
            .draw(canvas_);
        label(cn() ? L"交互示例 · 不播放音频" : L"Interaction demo · no audio", {12, 88, safe_.width - 24, 22}, theme,
              12)
            .draw(canvas_);
    }
    for (const auto& item : items_) {
        if (item.id == 4) {
            button(item, theme, "refresh-cw", cn() ? L"重试" : L"Retry");
            continue;
        }
        button(item, theme,
               item.id == 1   ? "skip-back"
               : item.id == 3 ? "skip-forward"
               : playing()    ? "pause"
                              : "play",
               item.id == 1   ? L"Prev"
               : item.id == 3 ? L"Next"
               : playing()    ? L"Pause"
                              : L"Play");
    }
    cybercanvas::ProgressBar{{12, safe_.height - 20, std::max(0., safe_.width - 24), 4}}.draw(
        canvas_, theme,
        live() ? (live_.view().duration > 0 ? double(live_.view().position) / live_.view().duration : 0.)
               : position_ / 180.);
}

auto Card::drawCalendar(const Theme& theme) -> void {
    if (items_.empty()) {
        return;
    }
    button(items_[0], theme, "chevron-left", L"Prev");
    button(items_[1], theme, "chevron-right", L"Next");
    const std::wstring names[]{L"M", L"T", L"W", L"T", L"F", L"S", L"S"};
    const std::wstring chinese[]{L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
    const auto now = environment_.localTime();
    for (int i = 0; i < 7; ++i) {
        auto weekday = label(cn() ? chinese[i] : names[i],
                             {12 + i * grid().bounds.width / 7, 40, grid().bounds.width / 7, 22}, theme, 11);
        weekday.font.align = cybercanvas::TextAlign::Center;
        weekday.color = theme.muted;
        weekday.draw(canvas_);
    }
    for (size_t i = 2; i < items_.size(); ++i) {
        const auto& item = items_[i];
        const auto day = item.id - 100;
        cybercanvas::CalendarCell cell;
        cell.bounds = cell.highlight = item.bounds;
        cell.day = label(std::to_wstring(day), item.bounds, theme, 13);
        cell.day.font.align = cybercanvas::TextAlign::Center;
        cell.selected = selected_ == day;
        cell.today = monthOffset_ == 0 && day == now.tm_mday;
        cell.selection = theme.accent;
        cell.todayColor = theme.track;
        if (cell.selected) {
            cell.day.color = theme.onAccent;
        }
        cell.draw(canvas_);
    }
}

auto Card::activate(uint32_t id) -> void {
    if (live()) {
        if ((kind_ == 0 && id == 1) || (kind_ == 2 && id == 4)) {
            live_.retry();
        } else if (kind_ == 2 && id >= 1 && id <= 3) {
            live_.command(id == 1 ? CD_MEDIA_PREVIOUS : id == 3 ? CD_MEDIA_NEXT : CD_MEDIA_TOGGLE);
        }
        return;
    }
    if (kind_ == 0 && id == 1) {
        sample_ = uint8_t((sample_ + 13) % 101);
    } else if (kind_ == 1 && id >= 1 && id <= 4) {
        tasks_ ^= uint8_t(1 << (id - 1));
    } else if (kind_ == 2) {
        if (id == 2) {
            playing_ = !playing_;
        } else if (id == 1 || id == 3) {
            track_ = uint8_t((track_ + (id == 1 ? 2 : 1)) % 3);
            position_ = 0;
        }
    } else if (kind_ == 3) {
        if (id == 1 || id == 2) {
            monthOffset_ = std::clamp(monthOffset_ + (id == 1 ? -1 : 1), -1200, 1200);
            selected_ = 0;
        } else if (id >= 101 && id <= 100 + daysInMonth()) {
            selected_ = uint8_t(id - 100);
        }
    }
    arrange();
    schedule();
    invalidate();
}

auto Card::schedule() -> void {
    timer_.stop();
    live_.active(live() && !paused_);
    lastSecond_ = environment_.snapshot().unix_ms / 1000;
    const auto today = environment_.localTime();
    lastDay_ = today.tm_year * 366 + today.tm_yday;
    if (!paused_ && ((kind_ == 2 && !live() && playing_) || kind_ == 3)) {
        timer_.replace(kind_ == 3 ? 60000 : 1000, tick, this);
    }
}

void CD_CALL Card::tick(void* user, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Card*>(user);
        const auto now = self.environment_.snapshot().unix_ms / 1000;
        if (self.paused_ || now == self.lastSecond_) {
            return;
        }
        if (self.kind_ == 2 && self.playing_) {
            self.position_ = unsigned(
                std::min<uint64_t>(180, self.position_ + (now >= self.lastSecond_ ? now - self.lastSecond_ : 0)));
            if (self.position_ == 180) {
                self.playing_ = false;
                self.timer_.stop();
            }
            self.invalidate();
        } else if (self.kind_ == 3) {
            const auto today = self.environment_.localTime();
            const int day = today.tm_year * 366 + today.tm_yday;
            if (day != self.lastDay_) {
                self.lastDay_ = day;
                self.invalidate();
            }
        }
        self.lastSecond_ = now;
    } catch (...) {
    }
}
}  // namespace componentdemo
