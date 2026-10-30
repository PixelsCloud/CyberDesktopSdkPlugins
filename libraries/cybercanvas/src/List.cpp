#include <CyberCanvas/List.h>

#include <limits>
#include <map>
#include <set>
#include <unordered_map>

namespace cybercanvas {
namespace {
bool same(Rect a, Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

Rect intersection(Rect a, Rect b) {
    const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0., std::min(a.x + a.width, b.x + b.width) - x),
            std::max(0., std::min(a.y + a.height, b.y + b.height) - y)};
}

void extent(double value) {
    if (!std::isfinite(value) || value < 0 || value > 1e9) {
        throw std::invalid_argument("Invalid list extent");
    }
}

void height(double value) {
    extent(value);
    if (value < 1) {
        throw std::invalid_argument("List row height must be at least one logical pixel");
    }
}

bool inside(Rect rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}
}  // namespace

ScrollView::ScrollView(ScrollStyle style) : style_(style) {
    for (double value : {style.width, style.margin, style.minimumThumb, style.duration, style.wheelStep}) {
        extent(value);
    }
    if (!style.width || !style.minimumThumb || style.duration < 1e-6) {
        throw std::invalid_argument("Invalid scroll style");
    }
}

bool ScrollView::arrange(Rect viewport, double contentHeight, double dpr, bool rtl) {
    detail::layoutRect(viewport);
    extent(viewport.width);
    extent(viewport.height);
    extent(contentHeight);
    if (!std::isfinite(dpr) || dpr < .5 || dpr > 4) {
        throw std::invalid_argument("Invalid scroll DPI");
    }
    if (same(viewport_, viewport) && content_ == contentHeight && dpr_ == dpr && rtl_ == rtl) {
        return false;
    }
    viewport_ = viewport;
    content_ = contentHeight;
    dpr_ = dpr;
    rtl_ = rtl;
    offset_ = std::clamp(offset_, 0., maximum());
    target_ = start_ = offset_;
    age_ = 0;
    return true;
}

bool ScrollView::scrollTo(double offset, bool smooth) {
    if (!std::isfinite(offset)) {
        throw std::invalid_argument("Invalid scroll offset");
    }
    const double next = std::clamp(offset, 0., maximum());
    if (next == target_ && (smooth || offset_ == next)) {
        return false;
    }
    target_ = next;
    start_ = offset_;
    age_ = 0;
    if (!smooth) {
        offset_ = target_;
    }
    return true;
}

bool ScrollView::scrollBy(double delta, bool smooth) {
    return scrollTo(target_ + delta, smooth);
}

bool ScrollView::advance(double seconds) {
    extent(seconds);
    if (!active() || !seconds) {
        return false;
    }
    const double before = offset_;
    age_ = std::min(style_.duration, age_ + seconds);
    offset_ =
        Tween<double>(start_, target_, {.duration = style_.duration, .easing = Ease::OutCubic}).sample(age_).value;
    if (age_ >= style_.duration) {
        offset_ = target_;
    }
    return before != offset_;
}

void ScrollView::cancel() {
    target_ = start_ = offset_;
    age_ = 0;
}

double ScrollView::offset() const {
    return offset_;
}

double ScrollView::target() const {
    return target_;
}

double ScrollView::maximum() const {
    return std::max(0., content_ - viewport_.height);
}

bool ScrollView::active() const {
    return offset_ != target_;
}

Rect ScrollView::viewport() const {
    return viewport_;
}

Rect ScrollView::contentArea() const {
    const double reserve = maximum() > 0 ? std::min(viewport_.width, style_.width + 2 * style_.margin) : 0;
    return {viewport_.x + (rtl_ ? reserve : 0), viewport_.y, viewport_.width - reserve, viewport_.height};
}

Rect ScrollView::track() const {
    if (!maximum() || viewport_.height <= 0) {
        return {};
    }
    const double width = std::min(style_.width, viewport_.width),
                 margin = std::min(style_.margin, viewport_.height * .5);
    return {rtl_ ? viewport_.x + std::min(style_.margin, std::max(0., viewport_.width - width))
                 : viewport_.x + std::max(0., viewport_.width - width - style_.margin),
            viewport_.y + margin, width, std::max(0., viewport_.height - 2 * margin)};
}

Rect ScrollView::thumb() const {
    const auto area = track();
    if (area.height <= 0) {
        return area;
    }
    const double size = std::min(
        area.height, std::max(std::min(style_.minimumThumb, area.height), area.height * viewport_.height / content_));
    const double y = area.y + (area.height - size) * offset_ / maximum();
    const double top = std::clamp(std::round(y * dpr_) / dpr_, area.y, area.y + area.height);
    const double bottom = std::clamp(std::round((y + size) * dpr_) / dpr_, top, area.y + area.height);
    return {area.x, top, area.width, bottom - top};
}

HitRegion ScrollView::region(uint32_t id, Point origin) const {
    auto area = track();
    const auto hit =
        intersection({area.x - style_.margin, area.y, area.width + 2 * style_.margin, area.height}, viewport_);
    Control control{id, hit, maximum() > 0 && hit.width > 0 && hit.height > 0, 1000};
    return control.region(origin, PointerBehavior::Drag);
}

bool ScrollView::drag(const PointerResult& input, uint32_t id) {
    if (input.target != id ||
        (input.drag != DragPhase::Begin && input.drag != DragPhase::Update && input.drag != DragPhase::End)) {
        return false;
    }
    const auto bar = thumb(), area = track();
    if (input.drag == DragPhase::Begin) {
        grab_ = input.local.y >= bar.y && input.local.y <= bar.y + bar.height ? input.local.y - bar.y : bar.height * .5;
    }
    const double travel = area.height - bar.height;
    return scrollTo(travel > 0 ? (input.local.y - area.y - grab_) / travel * maximum() : 0, false);
}

void ScrollView::draw(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual) const {
    const auto area = track(), bar = thumb();
    if (bar.height <= 0 || bar.width <= 0) {
        return;
    }
    canvas.withOpacity(.1 + visual.feedback * .12, [&] {
        canvas.box(area.x, area.y, area.width, area.height, area.width * .5, theme.track);
    });
    canvas.withOpacity(.45 + visual.feedback * .4, [&] {
        canvas.box(bar.x, bar.y, bar.width, bar.height, bar.width * .5, visual.pressed ? theme.accent : theme.muted);
    });
}

struct ListView::Impl {
    static constexpr uint32_t bar = 1, retry = 2;
    ScrollView scroll;
    std::vector<ListItem> items;
    std::vector<double> prefix{0};
    std::unordered_map<std::string, size_t> indices;
    std::map<std::pair<std::string, uint32_t>, uint32_t> ids;

    struct Target {
        std::string id;
        uint32_t action;
    };

    std::map<uint32_t, Target> targets;
    uint32_t nextId = 16;
    Actions actions;
    InputRouter router;
    WidgetState feedback;
    ListStatus status;
    ListStats stats;
    std::string selected;
    uint32_t keyboardAction = 0;
    Point origin{};
    double dpr = 1;
    bool rtl = false, dirty = true, targetsDirty = true, busy = false;
    std::function<void()> invalidated, inputReset;

    explicit Impl(ScrollStyle style) : scroll(style) {}

    void mutableList() const {
        if (busy) {
            throw std::logic_error("Cannot mutate a list during drawing or target generation");
        }
    }

    void invalidate() {
        const bool notify = !dirty;
        dirty = true;
        if (notify && invalidated) {
            invalidated();
        }
    }

    void reset() {
        router.dispatch({PointerKind::Cancel});
        feedback = {};
        targetsDirty = true;
        if (inputReset) {
            inputReset();
        }
    }

    uint32_t controlId(const std::string& item, uint32_t action) {
        const auto key = std::make_pair(item, action);
        if (const auto found = ids.find(key); found != ids.end()) {
            return found->second;
        }
        if (nextId == std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("List target ID limit");
        }
        const auto id = nextId++;
        ids.emplace(key, id);
        return id;
    }

    size_t first() const {
        return size_t(std::upper_bound(prefix.begin() + 1, prefix.end(), scroll.offset()) - prefix.begin() - 1);
    }

    Rect retryArea() const {
        auto area = inset(scroll.viewport(), {12, 8, 12, 8});
        if (area.height < 70) {
            const double width = std::min(72., area.width * .45), height = std::min(26., area.height);
            return {area.x + area.width - width, area.y + (area.height - height) * .5, width, height};
        }
        return {area.x + std::max(0., (area.width - 96) * .5), area.y + std::max(0., area.height - 28),
                std::min(96., area.width), std::min(26., area.height)};
    }
};

ListView::ListView(ScrollStyle style) : impl_(std::make_unique<Impl>(style)) {}

ListView::~ListView() = default;

bool ListView::items(std::vector<ListItem> items) {
    impl_->mutableList();
    if (items == impl_->items) {
        return false;
    }
    if (items.size() == impl_->items.size() &&
        std::equal(items.begin(), items.end(), impl_->items.begin(), [](const ListItem& a, const ListItem& b) {
            return a.id == b.id && a.height == b.height;
        })) {
        for (const auto& item : items) {
            update(item.id, item.revision);
        }
        return true;
    }
    if (items.size() > 1000000) {
        throw std::length_error("List item limit reached");
    }
    ScopedDuration duration(impl_->stats.prefixNanoseconds);
    std::unordered_map<std::string, size_t> indices;
    std::vector<double> prefix{0};
    prefix.reserve(items.size() + 1);
    indices.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].id.empty() || items[i].id.size() > 4096 || !indices.emplace(items[i].id, i).second) {
            throw std::invalid_argument("List IDs must be unique, nonempty and bounded");
        }
        height(items[i].height);
        const double total = prefix.back() + items[i].height;
        extent(total);
        prefix.push_back(total);
    }
    double anchor = impl_->scroll.offset();
    if (!impl_->items.empty()) {
        const auto first = std::min(impl_->first(), impl_->items.size() - 1);
        const auto oldOffset = anchor - impl_->prefix[first];
        for (size_t i = first; i < impl_->items.size(); ++i) {
            if (const auto found = indices.find(impl_->items[i].id); found != indices.end()) {
                anchor =
                    prefix[found->second] + (i == first ? std::min(oldOffset, items[found->second].height - 1) : 0);
                break;
            }
        }
    }
    impl_->reset();
    impl_->items = std::move(items);
    impl_->indices = std::move(indices);
    impl_->prefix = std::move(prefix);
    std::erase_if(impl_->ids, [&](const auto& pair) {
        return !impl_->indices.contains(pair.first.first);
    });
    if (!impl_->indices.contains(impl_->selected)) {
        impl_->selected.clear();
    }
    impl_->scroll.arrange(impl_->scroll.viewport(), impl_->prefix.back(), impl_->dpr, impl_->rtl);
    impl_->scroll.scrollTo(anchor, false);
    ++impl_->stats.prefixBuilds;
    impl_->invalidate();
    return true;
}

bool ListView::update(const std::string& id, uint64_t revision, std::optional<double> newHeight) {
    impl_->mutableList();
    const auto found = impl_->indices.find(id);
    if (found == impl_->indices.end()) {
        return false;
    }
    const size_t index = found->second;
    auto& item = impl_->items[index];
    if (newHeight && *newHeight != item.height) {
        auto copy = impl_->items;
        copy[index].height = *newHeight;
        copy[index].revision = revision;
        return items(std::move(copy));
    }
    if (item.revision == revision) {
        return false;
    }
    item.revision = revision;
    ++impl_->stats.updates;
    if (impl_->prefix[index + 1] > offset() && impl_->prefix[index] < offset() + viewport().height) {
        impl_->reset();
        impl_->invalidate();
    }
    return true;
}

void ListView::status(ListStatus status) {
    impl_->mutableList();
    if (impl_->status == status) {
        return;
    }
    impl_->status = std::move(status);
    impl_->reset();
    impl_->invalidate();
}

void ListView::actions(Actions provider) {
    impl_->mutableList();
    impl_->actions = std::move(provider);
    impl_->reset();
    impl_->invalidate();
}

void ListView::select(std::string id) {
    impl_->mutableList();
    if (!id.empty() && !impl_->indices.contains(id)) {
        throw std::invalid_argument("Unknown list selection");
    }
    if (impl_->selected == id) {
        return;
    }
    impl_->selected = std::move(id);
    impl_->invalidate();
}

const std::string& ListView::selected() const {
    return impl_->selected;
}

bool ListView::arrange(Rect viewport, double dpr, bool rtl) {
    impl_->mutableList();
    if (!impl_->scroll.arrange(viewport, impl_->prefix.back(), dpr, rtl)) {
        return false;
    }
    impl_->dpr = dpr;
    impl_->rtl = rtl;
    impl_->reset();
    impl_->invalidate();
    return true;
}

std::vector<ListRow> ListView::rows() const {
    std::vector<ListRow> result;
    const auto area = impl_->scroll.contentArea();
    if (area.width <= 0 || area.height <= 0 || impl_->items.empty()) {
        return result;
    }
    const double end = offset() + area.height;
    for (size_t i = impl_->first(); i < impl_->items.size() && impl_->prefix[i] < end; ++i) {
        const auto& item = impl_->items[i];
        Rect bounds{area.x, area.y + impl_->prefix[i] - offset(), area.width, item.height};
        result.push_back({item.id, i, bounds, intersection(bounds, area), item.id == impl_->selected});
    }
    return result;
}

bool ListView::scrollTo(double offset, bool smooth) {
    impl_->mutableList();
    if (!impl_->scroll.scrollTo(offset, smooth)) {
        return false;
    }
    impl_->reset();
    impl_->invalidate();
    return true;
}

bool ListView::scrollBy(double delta, bool smooth) {
    return scrollTo(impl_->scroll.target() + delta, smooth);
}

double ListView::offset() const {
    return impl_->scroll.offset();
}

double ListView::maximum() const {
    return impl_->scroll.maximum();
}

Rect ListView::viewport() const {
    return impl_->scroll.viewport();
}

Rect ListView::thumb() const {
    return impl_->scroll.thumb();
}

bool ListView::contains(Point point) const {
    return inside(viewport(), point);
}

bool ListView::engaged() const {
    return impl_->router.pressed() != 0;
}

bool ListView::wheel(double delta) {
    return scrollBy(delta);
}

ListEvent ListView::key(KeyEvent event, bool focusGranted) {
    impl_->mutableList();
    if (!focusGranted || event.key == Key::Tab || event.key == Key::Escape) {
        return {};
    }
    ListEvent result;
    if (impl_->items.empty()) {
        if ((event.key == Key::Enter || event.key == Key::Space) && impl_->status.canRetry &&
            impl_->status.state == ListState::Error) {
            result.clicked = result.handled = result.retry = true;
        }
        return result;
    }
    const auto found = impl_->indices.find(impl_->selected);
    size_t index = found == impl_->indices.end() ? 0 : found->second;
    const bool vertical =
        event.key == Key::Up || event.key == Key::Down || event.key == Key::Home || event.key == Key::End;
    if (vertical) {
        if (event.key == Key::Up) {
            index = index ? index - 1 : 0;
        } else if (event.key == Key::Down) {
            index = found == impl_->indices.end() ? 0 : std::min(index + 1, impl_->items.size() - 1);
        } else {
            index = event.key == Key::Home ? 0 : impl_->items.size() - 1;
        }
        impl_->keyboardAction = 0;
    } else if (event.key != Key::Left && event.key != Key::Right && event.key != Key::Enter &&
               event.key != Key::Space) {
        return {};
    }
    select(impl_->items[index].id);
    if (impl_->prefix[index] < offset()) {
        scrollTo(impl_->prefix[index], false);
    } else if (impl_->prefix[index + 1] > offset() + viewport().height) {
        scrollTo(std::max(impl_->prefix[index], impl_->prefix[index + 1] - viewport().height), false);
    }
    std::vector<uint32_t> actions{0};
    if (impl_->actions) {
        const auto visible = rows();
        const auto row = std::find_if(visible.begin(), visible.end(), [&](const auto& value) {
            return value.index == index;
        });
        if (row != visible.end()) {
            const auto provided = impl_->actions(*row);
            if (provided.size() > 16) {
                throw std::length_error("Too many list row actions");
            }
            for (const auto& action : provided) {
                if (action.enabled && action.action &&
                    std::find(actions.begin(), actions.end(), action.action) == actions.end()) {
                    actions.push_back(action.action);
                }
            }
        }
    }
    const auto current = std::find(actions.begin(), actions.end(), impl_->keyboardAction);
    const size_t actionIndex = current == actions.end() ? 0 : size_t(current - actions.begin());
    if (event.key == Key::Left || event.key == Key::Right) {
        const auto next = (actionIndex + actions.size() + (event.key == Key::Left ? -1 : 1)) % actions.size();
        impl_->keyboardAction = actions[next];
        impl_->invalidate();
    } else {
        impl_->keyboardAction = actions[actionIndex];
    }
    result.id = impl_->selected;
    result.index = index;
    result.action = impl_->keyboardAction;
    result.handled = true;
    result.clicked = event.key == Key::Enter || event.key == Key::Space;
    return result;
}

InputRouter& ListView::router() {
    return impl_->router;
}

void ListView::targets(Point origin) {
    impl_->mutableList();
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y)) {
        throw std::invalid_argument("Invalid list input origin");
    }
    if (origin.x != impl_->origin.x || origin.y != impl_->origin.y) {
        impl_->reset();
        impl_->origin = origin;
    }
    if (!impl_->targetsDirty) {
        return;
    }
    std::vector<HitRegion> regions;
    std::map<uint32_t, Impl::Target> targets;
    impl_->busy = true;
    try {
        const auto add = [&](Rect bounds, uint32_t id, Rect clip, int priority = 0) {
            Button button(id, bounds);
            button.priority = priority;
            auto region = button.region(origin);
            region.clips.push_back(
                HitShape::rectangle({clip.x + origin.x, clip.y + origin.y, clip.width, clip.height}));
            regions.push_back(std::move(region));
        };
        for (const auto& row : rows()) {
            const auto id = impl_->controlId(row.id, 0);
            targets.emplace(id, Impl::Target{row.id, 0});
            add(row.bounds, id, row.clip);
            if (impl_->actions) {
                const auto actions = impl_->actions(row);
                if (actions.size() > 16) {
                    throw std::length_error("Too many list row actions");
                }
                std::set<uint32_t> seen;
                for (const auto& action : actions) {
                    if (!action.action || !seen.insert(action.action).second) {
                        throw std::invalid_argument("List row action IDs must be unique and nonzero");
                    }
                    detail::layoutRect(action.bounds);
                    if (!action.enabled) {
                        continue;
                    }
                    const auto child = impl_->controlId(row.id, action.action);
                    targets.emplace(child, Impl::Target{row.id, action.action});
                    add(action.bounds, child, row.clip, 1);
                }
            }
        }
        if (impl_->items.empty() && impl_->status.canRetry && impl_->status.state == ListState::Error) {
            add(impl_->retryArea(), Impl::retry, viewport());
        }
        regions.push_back(impl_->scroll.region(Impl::bar, origin));
        impl_->router.regions(std::move(regions));
        impl_->targets = std::move(targets);
        if (impl_->ids.size() > 4096) {
            std::erase_if(impl_->ids, [&](const auto& pair) {
                return !impl_->targets.contains(pair.second);
            });
        }
        impl_->targetsDirty = false;
        impl_->busy = false;
    } catch (...) {
        impl_->busy = false;
        throw;
    }
}

ListEvent ListView::accept(PointerResult result) {
    impl_->mutableList();
    ListEvent event;
    event.pointer = result;
    event.handled = true;
    if (impl_->scroll.drag(result, Impl::bar)) {
        impl_->targetsDirty = true;
        impl_->invalidate();
    }
    if (result.clicked == Impl::retry) {
        event.clicked = event.retry = true;
    } else if (result.clicked) {
        const auto target = impl_->targets.find(result.clicked);
        if (target != impl_->targets.end()) {
            const auto index = impl_->indices.find(target->second.id);
            if (index != impl_->indices.end()) {
                event.clicked = true;
                event.id = index->first;
                event.index = index->second;
                event.action = target->second.action;
            }
        }
    }
    impl_->feedback.sync(impl_->router);
    if (result.changed || result.clicked) {
        impl_->invalidate();
    }
    return event;
}

ListEvent ListView::input(PointerEvent event, Point origin) {
    if (!engaged() && !contains({event.position.x - origin.x, event.position.y - origin.y}) &&
        event.kind != PointerKind::Leave && event.kind != PointerKind::Cancel) {
        return {};
    }
    if (event.kind == PointerKind::Press && event.left && !engaged()) {
        scrollTo(offset(), false);
    }
    targets(origin);
    return accept(impl_->router.dispatch(event));
}

ControlVisual ListView::visual(const std::string& id, uint32_t action) const {
    const auto found = impl_->ids.find({id, action});
    return found == impl_->ids.end() ? ControlVisual{} : impl_->feedback.visual(found->second);
}

void ListView::cancel() {
    impl_->mutableList();
    const bool changed = impl_->scroll.active() || impl_->feedback.size() || impl_->router.pressed();
    impl_->scroll.cancel();
    impl_->reset();
    if (changed) {
        impl_->invalidate();
    }
}

bool ListView::advance(double seconds) {
    impl_->mutableList();
    bool changed = impl_->feedback.advance(seconds);
    if (impl_->scroll.advance(seconds)) {
        impl_->targetsDirty = true;
        changed = true;
    }
    if (changed) {
        impl_->invalidate();
    }
    return changed;
}

bool ListView::active() const {
    return impl_->scroll.active() || impl_->feedback.active();
}

bool ListView::needsPaint() const {
    return impl_->dirty;
}

ListStats ListView::stats() const {
    return impl_->stats;
}

void ListView::onInvalidate(std::function<void()> callback) {
    impl_->invalidated = std::move(callback);
}

void ListView::onInputReset(std::function<void()> callback) {
    impl_->inputReset = std::move(callback);
}

void ListView::draw(Canvas& canvas, const WidgetTheme& theme, const PaintRow& paint, const IconPainter& icons) {
    impl_->mutableList();
    ScopedDuration duration(impl_->stats.paintNanoseconds);
    impl_->busy = true;
    try {
        canvas.clipped(viewport(), [&] {
            if (impl_->items.empty()) {
                const auto area = inset(viewport(), {12, 8, 12, 8});
                const bool retry = impl_->status.canRetry && impl_->status.state == ListState::Error;
                auto font = theme.font;
                font.align = TextAlign::Center;
                font.wrap = true;
                font.ellipsis = true;
                if (area.height < 70) {
                    auto title = area;
                    if (retry) {
                        title.width = std::max(0., impl_->retryArea().x - title.x - 6);
                        font.align = TextAlign::Start;
                        font.wrap = false;
                    }
                    canvas.text(impl_->status.title, font, title, theme.foreground);
                    if (retry) {
                        Button(Impl::retry, impl_->retryArea(), impl_->status.retry, ButtonStyle::Outline)
                            .draw(canvas, theme, impl_->feedback.visual(Impl::retry));
                    }
                    return;
                }
                Stack column(area, Axis::Vertical, 4);
                const auto glyph = column.take(std::min(28., area.height * .25));
                const char* name = impl_->status.state == ListState::Error     ? "triangle-alert"
                                   : impl_->status.state == ListState::Loading ? "loader-circle"
                                                                               : "inbox";
                if (icons) {
                    icons(canvas, aligned(glyph, {24, 24}), name, theme.muted);
                }
                canvas.text(impl_->status.title, font, column.take(std::min(38., area.height * .3)), theme.foreground);
                auto detail = column.remaining();
                if (retry) {
                    detail.height = std::max(0., impl_->retryArea().y - detail.y - 4);
                }
                if (!impl_->status.detail.empty() && detail.width > 0 && detail.height > 0) {
                    canvas.text(impl_->status.detail, font, detail, theme.muted);
                }
                if (retry) {
                    Button(Impl::retry, impl_->retryArea(), impl_->status.retry, ButtonStyle::Outline)
                        .draw(canvas, theme, impl_->feedback.visual(Impl::retry));
                }
            } else {
                for (const auto& row : rows()) {
                    canvas.saved([&] {
                        canvas.clipped(row.clip, [&] {
                            if (paint) {
                                paint(canvas, row);
                            }
                        });
                    });
                    ++impl_->stats.drawnRows;
                }
                impl_->scroll.draw(canvas, theme, impl_->feedback.visual(Impl::bar));
            }
        });
        ++impl_->stats.draws;
        impl_->dirty = false;
        impl_->busy = false;
    } catch (...) {
        impl_->busy = false;
        impl_->dirty = true;
        throw;
    }
}
}  // namespace cybercanvas
