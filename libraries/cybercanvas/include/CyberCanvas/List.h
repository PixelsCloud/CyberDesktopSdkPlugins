#pragma once

#include <memory>

#include "CyberCanvas/Semantics.h"
#include "CyberCanvas/Widgets.h"

namespace cybercanvas {
struct ScrollStyle {
    double width = 5, margin = 3, minimumThumb = 24, duration = .18, wheelStep = 48;
};

// Vertical scrolling in logical pixels. Uses the existing time-based easing;
// the host advances time and schedules frames only while active().
class ScrollView {
public:
    explicit ScrollView(ScrollStyle style = {});
    bool arrange(Rect viewport, double contentHeight, double dpr = 1, bool rtl = false);
    bool scrollTo(double offset, bool smooth = true);
    bool scrollBy(double delta, bool smooth = true);
    bool advance(double seconds);
    void cancel();
    double offset() const;
    double target() const;
    double maximum() const;
    bool active() const;
    Rect viewport() const;
    Rect contentArea() const;
    Rect track() const;
    Rect thumb() const;
    HitRegion region(uint32_t id, Point origin = {}) const;
    bool drag(const PointerResult& input, uint32_t id);
    void draw(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual = {}) const;

private:
    ScrollStyle style_;
    Rect viewport_{};
    double content_ = 0, dpr_ = 1, offset_ = 0, target_ = 0, start_ = 0, age_ = 0, grab_ = 0;
    bool rtl_ = false;
};

struct ListItem {
    std::string id;
    double height = 64;
    uint64_t revision = 0;
    bool operator==(const ListItem&) const = default;
};

struct ListRow {
    std::string id;
    size_t index = 0;
    Rect bounds{}, clip{};
    bool selected = false;
};

struct ListAction {
    uint32_t action = 0;  // Zero is the encompassing row click.
    Rect bounds{};
    bool enabled = true;
};

enum class ListState { Ready, Loading, Empty, Error };

struct ListStatus {
    ListState state = ListState::Empty;
    std::wstring title = L"No items", detail, retry = L"Retry";
    bool canRetry = false;
    bool operator==(const ListStatus&) const = default;
};

struct ListEvent {
    std::string id;
    size_t index = 0;
    uint32_t action = 0;
    bool handled = false, clicked = false, retry = false;
    PointerResult pointer;
};

struct ListStats {
    uint64_t prefixBuilds = 0, draws = 0, drawnRows = 0, updates = 0;
    uint64_t prefixNanoseconds = 0, paintNanoseconds = 0;
};

class ListView {
public:
    using Actions = std::function<std::vector<ListAction>(const ListRow&)>;
    using PaintRow = std::function<void(Canvas&, const ListRow&)>;
    explicit ListView(ScrollStyle style = {});
    ~ListView();
    ListView(const ListView&) = delete;
    ListView& operator=(const ListView&) = delete;

    // Snapshot changes retain the top visible ID and its offset when possible.
    // Heights are supplied/estimated by the caller; offscreen text is not laid out.
    bool items(std::vector<ListItem> items);
    bool update(const std::string& id, uint64_t revision, std::optional<double> height = {});
    void status(ListStatus status);
    void actions(Actions provider);
    void select(std::string id);
    const std::string& selected() const;
    bool arrange(Rect viewport, double dpr = 1, bool rtl = false);
    std::vector<ListRow> rows() const;
    bool scrollTo(double offset, bool smooth = true);
    bool scrollBy(double delta, bool smooth = true);
    double offset() const;
    double maximum() const;
    Rect viewport() const;
    Rect thumb() const;
    bool contains(Point point) const;
    bool engaged() const;

    ListEvent input(PointerEvent event, Point origin = {});
    bool wheel(double delta);
    // Call only while the owning host-granted focus is on this list. Navigation
    // keeps the selected stable ID visible; left/right choose row actions.
    ListEvent key(KeyEvent event, bool focusGranted);
    void targets(Point origin = {});
    ListEvent accept(PointerResult result);
    InputRouter& router();
    ControlVisual visual(const std::string& id, uint32_t action = 0) const;
    void cancel();
    bool advance(double seconds);
    bool active() const;
    bool needsPaint() const;
    ListStats stats() const;
    void onInvalidate(std::function<void()> callback);
    void onInputReset(std::function<void()> callback);
    void draw(Canvas& canvas, const WidgetTheme& theme, const PaintRow& paint, const IconPainter& icons = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
