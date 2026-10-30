#include <CyberCanvas/Flex.h>
#include <CyberCanvas/Layout.h>
#include <yoga/Yoga.h>

#include <exception>
#include <limits>
#include <map>
#include <vector>

namespace cybercanvas {
namespace {
void extent(double value) {
    if (!std::isfinite(value) || value < 0 || value > 1e7) {
        throw std::invalid_argument("Invalid flex extent");
    }
}

void validate(const FlexStyle& style) {
    if (static_cast<unsigned>(style.justify) > static_cast<unsigned>(FlexJustify::SpaceBetween) ||
        static_cast<unsigned>(style.align) > static_cast<unsigned>(FlexAlign::Stretch) ||
        static_cast<unsigned>(style.lineAlign) > static_cast<unsigned>(FlexAlign::Stretch) ||
        static_cast<unsigned>(style.axis) > static_cast<unsigned>(Axis::Vertical)) {
        throw std::invalid_argument("Invalid flex enum");
    }
    for (const auto value : {style.width, style.height, style.basis}) {
        if (static_cast<unsigned>(value.unit) > static_cast<unsigned>(Length::Unit::Percent)) {
            throw std::invalid_argument("Invalid flex length unit");
        }
        extent(value.value);
    }
    for (double value : {style.minimum.width, style.minimum.height, style.padding.left, style.padding.top,
                         style.padding.right, style.padding.bottom, style.margin.left, style.margin.top,
                         style.margin.right, style.margin.bottom, style.gap, style.grow, style.shrink}) {
        extent(value);
    }
    if (style.maximum) {
        extent(style.maximum->width);
        extent(style.maximum->height);
        if (style.maximum->width < style.minimum.width || style.maximum->height < style.minimum.height) {
            throw std::invalid_argument("Flex minimum exceeds maximum");
        }
    }
}

YGAlign align(FlexAlign value) {
    switch (value) {
        case FlexAlign::Start:
            return YGAlignFlexStart;
        case FlexAlign::Center:
            return YGAlignCenter;
        case FlexAlign::End:
            return YGAlignFlexEnd;
        default:
            return YGAlignStretch;
    }
}

void apply(YGNodeRef node, const FlexStyle& style) {
    YGNodeStyleSetFlexDirection(node, style.axis == Axis::Horizontal ? YGFlexDirectionRow : YGFlexDirectionColumn);
    YGNodeStyleSetFlexWrap(node, style.wrap ? YGWrapWrap : YGWrapNoWrap);
    YGNodeStyleSetDisplay(node, style.visible ? YGDisplayFlex : YGDisplayNone);
    YGNodeStyleSetAlignItems(node, align(style.align));
    YGNodeStyleSetAlignContent(node, align(style.lineAlign));
    const YGJustify justify[]{YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd, YGJustifySpaceBetween};
    YGNodeStyleSetJustifyContent(node, justify[static_cast<int>(style.justify)]);
    const auto dimension = [&](Length value, auto points, auto percent, auto automatic) {
        if (value.unit == Length::Unit::Auto) {
            automatic(node);
        } else if (value.unit == Length::Unit::Percent) {
            percent(node, float(value.value));
        } else {
            points(node, float(value.value));
        }
    };
    dimension(style.width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto);
    dimension(style.height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto);
    dimension(style.basis, YGNodeStyleSetFlexBasis, YGNodeStyleSetFlexBasisPercent, YGNodeStyleSetFlexBasisAuto);
    YGNodeStyleSetMinWidth(node, float(style.minimum.width));
    YGNodeStyleSetMinHeight(node, float(style.minimum.height));
    YGNodeStyleSetMaxWidth(node, style.maximum ? float(style.maximum->width) : YGUndefined);
    YGNodeStyleSetMaxHeight(node, style.maximum ? float(style.maximum->height) : YGUndefined);
    const YGEdge edges[]{YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
    const double padding[]{style.padding.left, style.padding.top, style.padding.right, style.padding.bottom};
    const double margin[]{style.margin.left, style.margin.top, style.margin.right, style.margin.bottom};
    for (int i = 0; i < 4; ++i) {
        YGNodeStyleSetPadding(node, edges[i], float(padding[i]));
        YGNodeStyleSetMargin(node, edges[i], float(margin[i]));
    }
    YGNodeStyleSetGap(node, YGGutterAll, float(style.gap));
    YGNodeStyleSetFlexGrow(node, float(style.grow));
    YGNodeStyleSetFlexShrink(node, float(style.shrink));
}

Rect intersection(Rect a, Rect b) {
    const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0., std::min(a.x + a.width, b.x + b.width) - x),
            std::max(0., std::min(a.y + a.height, b.y + b.height) - y)};
}
}  // namespace

struct FlexLayout::Impl {
    struct Entry {
        YGNodeRef node = nullptr;
        Node parent = 0;
        Node id = 0;
        Measure measure;
        Impl* owner = nullptr;
        Rect bounds{}, clipped{};

        ~Entry() {
            if (node) {
                YGNodeFree(node);
            }
        }
    };

    YGConfigRef config = YGConfigNew();
    std::map<Node, std::unique_ptr<Entry>> entries;
    uint64_t nextNode = 0;
    std::exception_ptr failure;
    Rect available{};
    double dpr = 0;
    bool dirty = true, rtl = false, calculated = false, calculating = false;

    ~Impl() {
        entries.clear();
        YGConfigFree(config);
    }

    Entry& entry(Node node) const {
        const auto found = entries.find(node);
        if (found == entries.end()) {
            throw std::out_of_range("Unknown flex node");
        }
        return *found->second;
    }

    std::vector<Node> order(Node node = root) const {
        std::vector<Node> result, pending{node};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            result.push_back(id);
            const auto current = entry(id).node;
            for (size_t i = YGNodeGetChildCount(current); i > 0; --i) {
                pending.push_back(static_cast<Entry*>(YGNodeGetContext(YGNodeGetChild(current, i - 1)))->id);
            }
        }
        return result;
    }

    void mutableTree() const {
        if (calculating) {
            throw std::logic_error("Cannot mutate a flex tree during measurement");
        }
    }

    static YGSize measureNode(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                              YGMeasureMode heightMode) noexcept {
        auto& entry = *static_cast<Entry*>(YGNodeGetContext(node));
        const auto mode = [](YGMeasureMode mode) {
            return mode == YGMeasureModeExactly  ? MeasureMode::Exactly
                   : mode == YGMeasureModeAtMost ? MeasureMode::AtMost
                                                 : MeasureMode::Undefined;
        };
        try {
            auto size = entry.measure({std::isfinite(width) ? double(width) : 0,
                                       std::isfinite(height) ? double(height) : 0, mode(widthMode), mode(heightMode)});
            extent(size.width);
            extent(size.height);
            if (widthMode == YGMeasureModeExactly) {
                size.width = width;
            } else if (widthMode == YGMeasureModeAtMost) {
                size.width = std::min(size.width, double(width));
            }
            if (heightMode == YGMeasureModeExactly) {
                size.height = height;
            } else if (heightMode == YGMeasureModeAtMost) {
                size.height = std::min(size.height, double(height));
            }
            return {float(size.width), float(size.height)};
        } catch (...) {
            if (!entry.owner->failure) {
                entry.owner->failure = std::current_exception();
            }
            return {0, 0};
        }
    }
};

FlexLayout::FlexLayout(const FlexStyle& style) : impl_(std::make_unique<Impl>()) {
    YGConfigSetUseWebDefaults(impl_->config, true);
    add(root, style);
}

FlexLayout::~FlexLayout() = default;
FlexLayout::FlexLayout(FlexLayout&&) noexcept = default;
FlexLayout& FlexLayout::operator=(FlexLayout&&) noexcept = default;

FlexLayout::Node FlexLayout::add(Node parent, const FlexStyle& style, Measure measure) {
    impl_->mutableTree();
    validate(style);
    if (impl_->entries.size() >= 4096 || impl_->nextNode > std::numeric_limits<Node>::max()) {
        throw std::length_error("Flex node limit reached");
    }
    if (!impl_->entries.empty() && impl_->entry(parent).measure) {
        throw std::logic_error("Measured leaves cannot have children");
    }
    auto entry = std::make_unique<Impl::Entry>();
    entry->node = YGNodeNewWithConfig(impl_->config);
    entry->parent = parent;
    entry->id = Node(impl_->nextNode);
    entry->measure = std::move(measure);
    entry->owner = impl_.get();
    apply(entry->node, style);
    YGNodeSetContext(entry->node, entry.get());
    if (entry->measure) {
        YGNodeSetMeasureFunc(entry->node, Impl::measureNode);
    }
    const auto id = entry->id;
    impl_->entries.emplace(id, std::move(entry));
    ++impl_->nextNode;
    if (id) {
        YGNodeInsertChild(impl_->entry(parent).node, impl_->entry(id).node,
                          YGNodeGetChildCount(impl_->entry(parent).node));
    }
    impl_->dirty = true;
    return id;
}

void FlexLayout::remove(Node node) {
    impl_->mutableTree();
    impl_->entry(node);
    if (node == root) {
        throw std::invalid_argument("Cannot remove the flex root");
    }
    const auto removed = impl_->order(node);
    // Free children first; YGNodeFree disconnects from the owner as well.
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
        impl_->entries.erase(*it);
    }
    impl_->dirty = true;
}

bool FlexLayout::move(Node node, Node parent, Node before) {
    impl_->mutableTree();
    auto& entry = impl_->entry(node);
    auto& destination = impl_->entry(parent);
    if (node == root || destination.measure) {
        throw std::invalid_argument("Cannot move the root or move into a measured leaf");
    }
    for (Node ancestor = parent;; ancestor = impl_->entry(ancestor).parent) {
        if (ancestor == node) {
            throw std::invalid_argument("Cannot create a flex cycle");
        }
        if (ancestor == root) {
            break;
        }
    }
    if (before && impl_->entry(before).parent != parent) {
        throw std::invalid_argument("Flex insertion sibling belongs to another parent");
    }
    auto siblings = children(parent);
    const auto old = std::find(siblings.begin(), siblings.end(), node);
    const auto target = before ? std::find(siblings.begin(), siblings.end(), before) : siblings.end();
    if (entry.parent == parent && (before == node || std::next(old) == target)) {
        return false;
    }
    auto index = size_t(target - siblings.begin());
    if (entry.parent == parent && old < target) {
        --index;
    }
    YGNodeRemoveChild(impl_->entry(entry.parent).node, entry.node);
    YGNodeInsertChild(destination.node, entry.node, index);
    entry.parent = parent;
    impl_->dirty = true;
    return true;
}

bool FlexLayout::contains(Node node) const {
    return impl_->entries.contains(node);
}

FlexLayout::Node FlexLayout::parent(Node node) const {
    return impl_->entry(node).parent;
}

std::vector<FlexLayout::Node> FlexLayout::children(Node node) const {
    const auto current = impl_->entry(node).node;
    std::vector<Node> result;
    result.reserve(YGNodeGetChildCount(current));
    for (size_t i = 0; i < YGNodeGetChildCount(current); ++i) {
        result.push_back(static_cast<Impl::Entry*>(YGNodeGetContext(YGNodeGetChild(current, i)))->id);
    }
    return result;
}

void FlexLayout::style(Node node, const FlexStyle& value) {
    impl_->mutableTree();
    validate(value);
    apply(impl_->entry(node).node, value);
    impl_->dirty = true;
}

void FlexLayout::measure(Node node, Measure value) {
    impl_->mutableTree();
    auto& entry = impl_->entry(node);
    if (YGNodeGetChildCount(entry.node)) {
        throw std::logic_error("Only leaves can be measured");
    }
    const bool wasMeasured = bool(entry.measure);
    if (wasMeasured) {
        YGNodeMarkDirty(entry.node);
    }
    entry.measure = std::move(value);
    YGNodeSetMeasureFunc(entry.node, entry.measure ? Impl::measureNode : nullptr);
    if (entry.measure) {
        YGNodeMarkDirty(entry.node);
    }
    impl_->dirty = true;
}

void FlexLayout::invalidate(Node node) {
    impl_->mutableTree();
    auto& entry = impl_->entry(node);
    if (!entry.measure) {
        throw std::logic_error("Only measured leaves can be invalidated");
    }
    YGNodeMarkDirty(entry.node);
    impl_->dirty = true;
}

bool FlexLayout::calculate(Rect available, double dpr, bool rtl) {
    impl_->mutableTree();
    detail::layoutRect(available);
    extent(available.width);
    extent(available.height);
    if (!std::isfinite(dpr) || dpr < .5 || dpr > 4 || std::abs(available.x) > 1e7 || std::abs(available.y) > 1e7) {
        throw std::invalid_argument("Invalid flex DPI or origin");
    }
    const auto old = impl_->available;
    if (!impl_->dirty && impl_->dpr == dpr && impl_->rtl == rtl && old.x == available.x && old.y == available.y &&
        old.width == available.width && old.height == available.height) {
        return false;
    }
    impl_->calculating = true;
    impl_->failure = nullptr;
    YGConfigSetPointScaleFactor(impl_->config, float(dpr));
    auto rootNode = impl_->entry(0).node;
    YGNodeStyleSetWidth(rootNode, float(available.width));
    YGNodeStyleSetHeight(rootNode, float(available.height));
    YGNodeCalculateLayout(rootNode, float(available.width), float(available.height),
                          rtl ? YGDirectionRTL : YGDirectionLTR);
    impl_->calculating = false;
    if (impl_->failure) {
        impl_->dirty = true;
        for (const auto& [id, entry] : impl_->entries) {
            if (entry->measure) {
                YGNodeMarkDirty(entry->node);
            }
        }
        std::rethrow_exception(impl_->failure);
    }
    for (Node i : impl_->order()) {
        auto& entry = impl_->entry(i);
        const auto parent = i ? impl_->entry(entry.parent).bounds : available;
        entry.bounds = {parent.x + YGNodeLayoutGetLeft(entry.node), parent.y + YGNodeLayoutGetTop(entry.node),
                        YGNodeLayoutGetWidth(entry.node), YGNodeLayoutGetHeight(entry.node)};
        entry.clipped = intersection(entry.bounds, i ? impl_->entry(entry.parent).clipped : available);
    }
    impl_->available = available;
    impl_->dpr = dpr;
    impl_->rtl = rtl;
    impl_->dirty = false;
    impl_->calculated = true;
    return true;
}

Rect FlexLayout::bounds(Node node) const {
    if (!impl_->calculated || impl_->dirty) {
        throw std::logic_error("Flex layout must be calculated first");
    }
    return impl_->entry(node).bounds;
}

Rect FlexLayout::clippedBounds(Node node) const {
    bounds(node);
    return impl_->entry(node).clipped;
}

size_t FlexLayout::size() const {
    return impl_->entries.size();
}
}  // namespace cybercanvas

namespace cybercanvas {
auto Length::points(double value) -> Length {
    return {Unit::Points, value};
}

auto Length::percent(double value) -> Length {
    return {Unit::Percent, value};
}
}  // namespace cybercanvas
