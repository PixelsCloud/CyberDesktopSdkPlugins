#include <CyberCanvas/Scene.h>

#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace cybercanvas {
namespace {
bool same(const TextStyle& a, const TextStyle& b) {
    return std::tie(a.size, a.family, a.locale, a.align, a.vertical, a.bold, a.wrap, a.ellipsis, a.weight, a.direction,
                    a.lineHeight, a.baseline, a.tabularFigures, a.characterSpacing) ==
           std::tie(b.size, b.family, b.locale, b.align, b.vertical, b.bold, b.wrap, b.ellipsis, b.weight, b.direction,
                    b.lineHeight, b.baseline, b.tabularFigures, b.characterSpacing);
}

bool metrics(const WidgetTheme& a, const WidgetTheme& b) {
    return same(a.font, b.font) && a.padding == b.padding && a.gap == b.gap && a.controlHeight == b.controlHeight;
}

bool same(const WidgetTheme& a, const WidgetTheme& b) {
    return metrics(a, b) && a.error == b.error && a.disabledOpacity == b.disabledOpacity &&
           std::tie(a.foreground, a.muted, a.accent, a.surface, a.border, a.track, a.onAccent, a.radius,
                    a.thumbRadius) == std::tie(b.foreground, b.muted, b.accent, b.surface, b.border, b.track,
                                               b.onAccent, b.radius, b.thumbRadius);
}

WidgetTheme patched(WidgetTheme result, const ThemePatch& patch) {
    result.foreground = patch.foreground.value_or(result.foreground);
    result.muted = patch.muted.value_or(result.muted);
    result.accent = patch.accent.value_or(result.accent);
    result.surface = patch.surface.value_or(result.surface);
    result.border = patch.border.value_or(result.border);
    result.track = patch.track.value_or(result.track);
    result.onAccent = patch.onAccent.value_or(result.onAccent);
    result.error = patch.error.value_or(result.error);
    result.radius = patch.radius.value_or(result.radius);
    result.padding = patch.padding.value_or(result.padding);
    result.gap = patch.gap.value_or(result.gap);
    result.thumbRadius = patch.thumbRadius.value_or(result.thumbRadius);
    result.controlHeight = patch.controlHeight.value_or(result.controlHeight);
    result.disabledOpacity = patch.disabledOpacity.value_or(result.disabledOpacity);
    result.font = patch.font.value_or(result.font);
    if (patch.fontSize) {
        result.font.size = *patch.fontSize;
    }
    for (double number : {result.radius, result.padding, result.gap, result.thumbRadius, result.font.size,
                          result.controlHeight, result.disabledOpacity}) {
        detail::layoutExtent(number);
    }
    if (result.disabledOpacity > 1) {
        throw std::invalid_argument("Disabled opacity must be between zero and one");
    }
    return result;
}

bool same(const FlexStyle& a, const FlexStyle& b) {
    const auto fields = [](const FlexStyle& v) {
        return std::tuple(v.axis, v.wrap, v.visible, v.align, v.lineAlign, v.justify, v.width.unit, v.width.value,
                          v.height.unit, v.height.value, v.basis.unit, v.basis.value, v.minimum.width, v.minimum.height,
                          bool(v.maximum), v.maximum ? v.maximum->width : 0, v.maximum ? v.maximum->height : 0,
                          v.padding.left, v.padding.top, v.padding.right, v.padding.bottom, v.margin.left, v.margin.top,
                          v.margin.right, v.margin.bottom, v.gap, v.grow, v.shrink);
    };
    return fields(a) == fields(b);
}

bool same(const Pose& a, const Pose& b) {
    return std::tie(a.translation.x, a.translation.y, a.pivot.x, a.pivot.y, a.scale.width, a.scale.height, a.rotation,
                    a.opacity) == std::tie(b.translation.x, b.translation.y, b.pivot.x, b.pivot.y, b.scale.width,
                                           b.scale.height, b.rotation, b.opacity);
}

bool container(ViewKind kind) {
    return kind == ViewKind::Column || kind == ViewKind::Row;
}

bool control(ViewKind kind) {
    return kind == ViewKind::Button || kind == ViewKind::Slider || kind == ViewKind::Toggle;
}

void validateTransform(const Pose& value) {
    for (double number : {value.translation.x, value.translation.y, value.pivot.x, value.pivot.y, value.scale.width,
                          value.scale.height, value.rotation, value.opacity}) {
        if (!std::isfinite(number) || std::abs(number) > 1e7) {
            throw std::invalid_argument("Invalid scene transform");
        }
    }
    if (value.opacity < 0 || value.opacity > 1) {
        throw std::invalid_argument("Scene opacity must be within 0..1");
    }
}

Rect transformedBounds(Rect area, const D2D1::Matrix3x2F& matrix) {
    auto first = matrix.TransformPoint(D2D1::Point2F(float(area.x), float(area.y)));
    double left = first.x, right = first.x, top = first.y, bottom = first.y;
    for (auto point : {Point{area.x + area.width, area.y}, Point{area.x, area.y + area.height},
                       Point{area.x + area.width, area.y + area.height}}) {
        auto p = matrix.TransformPoint(D2D1::Point2F(float(point.x), float(point.y)));
        left = std::min(left, double(p.x));
        top = std::min(top, double(p.y));
        right = std::max(right, double(p.x));
        bottom = std::max(bottom, double(p.y));
    }
    return {left, top, right - left, bottom - top};
}

void validate(View& view) {
    validateTransform(view.transform);
    if (view.semanticRole &&
        static_cast<unsigned>(*view.semanticRole) > static_cast<unsigned>(SemanticRole::ListItem)) {
        throw std::invalid_argument("Invalid semantic role");
    }
    if (static_cast<unsigned>(view.kind) > static_cast<unsigned>(ViewKind::Decoration)) {
        throw std::invalid_argument("Invalid scene view kind");
    }
    if (static_cast<unsigned>(view.imageFit) > static_cast<unsigned>(ImageFit::Stretch) ||
        static_cast<unsigned>(view.resource.state) > static_cast<unsigned>(ImageState::Cancelled) ||
        (view.resourceImage && view.resource.state == ImageState::Ready && !view.resource.image)) {
        throw std::invalid_argument("Invalid scene image resource");
    }
    for (double number : {view.intrinsic.width, view.intrinsic.height, view.iconSize, view.hitSlop, view.step}) {
        detail::layoutExtent(number);
    }
    if (!view.imageCells || view.imageCell >= view.imageCells) {
        throw std::invalid_argument("Invalid scene image cells");
    }
    if (view.kind == ViewKind::Slider || view.kind == ViewKind::Toggle) {
        const auto fraction = ProgressBar{{}, view.minimum, view.maximum}.fraction(view.value);
        view.value = view.minimum + fraction * (view.maximum - view.minimum);
    }
    patched({}, view.theme);
}
}  // namespace

struct Scene::Impl {
    struct Entry {
        View view;
        Node parent = 0;
        FlexLayout::Node layoutNode = 0;
        std::vector<Node> children;
        WidgetTheme theme;
        bool visible = true, enabled = true, measureDirty = true;
        ComPtr<ID2D1RectangleGeometry> clip;
        ComPtr<ID2D1Factory> clipFactory;
        Rect clipBounds{};
        unsigned layerPadding = 0;
        bool layerPaddingEligible = false;
        std::unique_ptr<OffscreenSurface> layer;
        bool layerValid = false;
        uint64_t layerBytes = 0;
        double layerDpi = 0;
    };

    std::map<Node, Entry> entries;
    uint64_t nextNode = 1;
    std::unique_ptr<FlexLayout> layout;
    WidgetTheme theme;
    WidgetState feedback;
    InputRouter router;
    Canvas* canvas = nullptr;
    Rect area{};
    Point origin{};
    double dpr = 0;
    bool rtl = false, layoutDirty = true, paintDirty = true, targetsDirty = true, busy = false;
    bool transformed = false;
    SceneStats stats;
    Node focused = 0;
    bool focusGranted = false;
    std::function<void()> inputReset, invalidated, semanticsChanged;

    Entry& entry(Node node) {
        const auto found = entries.find(node);
        if (found == entries.end()) {
            throw std::out_of_range("Unknown scene node");
        }
        return found->second;
    }

    std::vector<Node> order(Node node = root) const {
        std::vector<Node> result, pending{node};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            result.push_back(id);
            const auto& children = entries.at(id).children;
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
        return result;
    }

    Rect bounds(Node node) const {
        const auto id = entries.at(node).layoutNode;
        return transformed ? layout->bounds(id) : layout->clippedBounds(id);
    }

    void updateTransforms() {
        transformed = std::any_of(entries.begin(), entries.end(), [](const auto& item) {
            return !same(item.second.view.transform, Pose{});
        });
    }

    D2D1::Matrix3x2F localTransform(Node node) const {
        const auto& e = entries.at(node);
        const auto area = bounds(node);
        const auto& pose = e.view.transform;
        const auto pivot = D2D1::Point2F(float(area.x + pose.pivot.x), float(area.y + pose.pivot.y));
        return D2D1::Matrix3x2F::Scale(float(pose.scale.width), float(pose.scale.height), pivot) *
               D2D1::Matrix3x2F::Rotation(float(pose.rotation), pivot) *
               D2D1::Matrix3x2F::Translation(float(pose.translation.x), float(pose.translation.y));
    }

    D2D1::Matrix3x2F worldTransform(Node node) const {
        auto result = D2D1::Matrix3x2F::Identity();
        for (Node current = node;; current = entries.at(current).parent) {
            result = result * localTransform(current);
            if (!current) {
                return result;
            }
        }
    }

    bool painted(Node node) const {
        for (Node current = node;; current = entries.at(current).parent) {
            const auto& pose = entries.at(current).view.transform;
            if (pose.opacity == 0 || pose.scale.width == 0 || pose.scale.height == 0) {
                return false;
            }
            if (!current) {
                return true;
            }
        }
    }

    Rect visualBounds(Node node) const {
        if (!painted(node)) {
            return {};
        }
        auto result = transformedBounds(bounds(node), worldTransform(node));
        for (Node current = node; current;) {
            current = entries.at(current).parent;
            auto clip = transformedBounds(bounds(current), worldTransform(current));
            const double right = std::min(result.x + result.width, clip.x + clip.width);
            const double bottom = std::min(result.y + result.height, clip.y + clip.height);
            result.x = std::max(result.x, clip.x);
            result.y = std::max(result.y, clip.y);
            result.width = std::max(0., right - result.x);
            result.height = std::max(0., bottom - result.y);
        }
        return result;
    }

    void mutableScene() const {
        if (busy) {
            throw std::logic_error("Cannot mutate a scene during layout or drawing");
        }
    }

    void resetInput() {
        if (inputReset) {
            inputReset();
        }
        router.dispatch({PointerKind::Cancel});
        feedback = {};
        targetsDirty = true;
    }

    bool focusEligible(Node node) const {
        if (!node || !entries.contains(node)) {
            return false;
        }
        const auto& view = entries.at(node).view;
        if (!control(view.kind) && !view.clickable) {
            return false;
        }
        for (Node current = node;; current = entries.at(current).parent) {
            const auto& ancestor = entries.at(current).view;
            if (!ancestor.enabled || !ancestor.layout.visible) {
                return false;
            }
            if (!current) {
                return true;
            }
        }
    }

    bool focusable(Node node) const {
        if (!focusEligible(node) || layoutDirty || !layout) {
            return false;
        }
        const auto area = visualBounds(node);
        return area.width > 0 && area.height > 0;
    }

    void dirty(bool reflow = false, std::optional<Node> transformedNode = {}) {
        mutableScene();
        if (transformedNode) {
            for (Node node = *transformedNode; node != root;) {
                node = entry(node).parent;
                entry(node).layerValid = false;
            }
        } else {
            for (auto& [id, entry] : entries) {
                entry.layerValid = false;
            }
        }
        if (!focusEligible(focused)) {
            focused = 0;
        }
        const bool notify = !paintDirty;
        if (reflow && !layoutDirty) {
            resetInput();
        }
        layoutDirty |= reflow;
        paintDirty = true;
        if (semanticsChanged) {
            semanticsChanged();
        }
        if (notify && invalidated) {
            invalidated();
        }
    }

    void resolve() {
        updateTransforms();
        for (Node i : order()) {
            auto& e = entries.at(i);
            const auto inherited = patched(i ? entries.at(e.parent).theme : theme, e.view.theme);
            e.measureDirty |= !metrics(e.theme, inherited);
            e.theme = inherited;
            e.visible = e.view.layout.visible && (!i || entries.at(e.parent).visible);
            e.enabled = e.view.enabled && (!i || entries.at(e.parent).enabled);
        }
    }

    FlexStyle style(const Entry& e) const {
        auto result = e.view.layout;
        result.visible = e.visible;
        if (container(e.view.kind)) {
            result.axis = e.view.kind == ViewKind::Row ? Axis::Horizontal : Axis::Vertical;
            if (e.view.inheritGap && result.gap == 0) {
                result.gap = e.theme.gap;
            }
        }
        return result;
    }

    FlexLayout::Measure measure(Node id) {
        if (container(entries.at(id).view.kind)) {
            return {};
        }
        return [this, id](MeasureConstraint limit) {
            ++stats.measurements;
            ScopedDuration duration(stats.measureNanoseconds);
            const auto& e = entries.at(id);
            const auto& v = e.view;
            if (v.kind == ViewKind::Image || v.kind == ViewKind::Decoration || v.kind == ViewKind::Toggle) {
                return v.intrinsic;
            }
            if (v.kind == ViewKind::Slider) {
                return Size{96, 4};
            }
            const bool button = v.kind == ViewKind::Button;
            const double padding = button ? e.theme.padding : 0;
            auto font = e.theme.font;
            if (button) {
                font.wrap = false;
            }
            const double width =
                limit.widthMode == MeasureMode::Undefined ? 1e6 : std::max(.01, limit.width - 2 * padding);
            const auto text = canvas->measureText(v.text, font, {width, 1e6});
            return Size{std::max(text.widthIncludingTrailingWhitespace, v.icon.empty() ? 0 : v.iconSize) + 2 * padding,
                        std::max(std::max(text.height, v.icon.empty() ? 0 : v.iconSize) + 2 * padding,
                                 button ? e.theme.controlHeight : 0.)};
        };
    }

    Slider slider(Node id) const {
        const auto& e = entries.at(id);
        Slider result(id, bounds(id), e.view.minimum, e.view.maximum, e.view.step);
        result.enabled = e.enabled;
        result.hitSlop = e.view.hitSlop;
        return result;
    }

    Button button(Node id) const {
        const auto& e = entries.at(id);
        Button result(id, bounds(id), e.view.text, e.view.appearance);
        result.enabled = e.enabled;
        result.selected = e.view.selected;
        result.round = e.view.round;
        return result;
    }
};

Scene::Scene(View rootView) : impl_(std::make_unique<Impl>()) {
    validate(rootView);
    if (!container(rootView.kind)) {
        throw std::invalid_argument("Scene root must be a row or column");
    }
    impl_->entries.emplace(root, Impl::Entry{std::move(rootView)});
    impl_->resolve();
    impl_->layout = std::make_unique<FlexLayout>(impl_->style(impl_->entry(root)));
}

Scene::~Scene() = default;

Scene::Node Scene::add(Node parent, View view) {
    impl_->mutableScene();
    if (!container(impl_->entry(parent).view.kind)) {
        throw std::logic_error("Scene leaves cannot have children");
    }
    if (impl_->entries.size() >= 4096 || impl_->nextNode > std::numeric_limits<Node>::max()) {
        throw std::length_error("Scene node limit reached");
    }
    if (view.key && find(view.key)) {
        throw std::invalid_argument("Duplicate scene key");
    }
    validate(view);
    const auto id = Node(impl_->nextNode);
    auto& siblings = impl_->entry(parent).children;
    siblings.reserve(siblings.size() + 1);
    impl_->entries.emplace(id, Impl::Entry{std::move(view), parent});
    try {
        auto& e = impl_->entry(id);
        e.layoutNode = impl_->layout->add(impl_->entry(parent).layoutNode, impl_->style(e), impl_->measure(id));
    } catch (...) {
        impl_->entries.erase(id);
        throw;
    }
    siblings.push_back(id);
    ++impl_->nextNode;
    impl_->dirty(true);
    return id;
}

void Scene::remove(Node node) {
    impl_->mutableScene();
    const auto parent = impl_->entry(node).parent;
    if (node == root) {
        throw std::invalid_argument("Cannot remove the scene root");
    }
    const auto removed = impl_->order(node);
    impl_->layout->remove(impl_->entry(node).layoutNode);
    std::erase(impl_->entry(parent).children, node);
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
        impl_->entries.erase(*it);
    }
    impl_->dirty(true);
}

void Scene::clear(Node parent) {
    impl_->mutableScene();
    const auto children = impl_->entry(parent).children;
    for (Node child : children) {
        remove(child);
    }
}

bool Scene::move(Node node, Node parent, Node before) {
    impl_->mutableScene();
    auto& e = impl_->entry(node);
    auto& destination = impl_->entry(parent);
    if (!container(destination.view.kind)) {
        throw std::invalid_argument("Scene leaves cannot have children");
    }
    if (before && impl_->entry(before).parent != parent) {
        throw std::invalid_argument("Scene insertion sibling belongs to another parent");
    }
    destination.children.reserve(destination.children.size() + 1);
    if (!impl_->layout->move(e.layoutNode, destination.layoutNode, before ? impl_->entry(before).layoutNode : 0)) {
        return false;
    }
    std::erase(impl_->entry(e.parent).children, node);
    const auto target = before ? std::find(destination.children.begin(), destination.children.end(), before)
                               : destination.children.end();
    destination.children.insert(target, node);
    e.parent = parent;
    impl_->dirty(true);
    return true;
}

bool Scene::contains(Node node) const {
    return impl_->entries.contains(node);
}

Scene::Node Scene::parent(Node node) const {
    return impl_->entry(node).parent;
}

std::vector<Scene::Node> Scene::children(Node node) const {
    return impl_->entry(node).children;
}

size_t Scene::size() const {
    return impl_->entries.size();
}

const View& Scene::view(Node node) const {
    return impl_->entry(node).view;
}

void Scene::style(Node node, const FlexStyle& value) {
    impl_->mutableScene();
    auto& current = impl_->entry(node).view.layout;
    if (same(current, value)) {
        return;
    }
    current = value;
    impl_->dirty(true);
}

void Scene::theme(Node node, const ThemePatch& value) {
    impl_->mutableScene();
    auto& entry = impl_->entry(node);
    const auto parent = node ? resolvedTheme(entry.parent) : impl_->theme;
    const auto before = patched(parent, entry.view.theme), after = patched(parent, value);
    entry.view.theme = value;
    if (!same(before, after)) {
        impl_->dirty(!metrics(before, after));
    }
}

void Scene::theme(const WidgetTheme& value) {
    impl_->mutableScene();
    patched(value, {});
    const auto old = impl_->theme;
    if (same(old, value)) {
        return;
    }
    impl_->theme = value;
    impl_->dirty(!metrics(old, value));
}

void Scene::text(Node node, std::wstring value) {
    impl_->mutableScene();
    auto& view = impl_->entry(node).view;
    auto& current = view.text;
    if (current == value) {
        return;
    }
    current = std::move(value);
    impl_->entry(node).measureDirty = true;
    impl_->dirty(view.layout.width.unit == Length::Unit::Auto || view.layout.height.unit == Length::Unit::Auto);
}

void Scene::value(Node node, double value) {
    impl_->mutableScene();
    auto& v = impl_->entry(node).view;
    const double fraction = ProgressBar{{}, v.minimum, v.maximum}.fraction(value);
    const double next = v.minimum + (v.maximum - v.minimum) * fraction;
    if (v.value == next) {
        return;
    }
    v.value = next;
    impl_->dirty();
}

void Scene::accessibleName(Node node, std::wstring value) {
    impl_->mutableScene();
    auto& name = impl_->entry(node).view.accessibleName;
    if (name != value) {
        name = std::move(value);
        if (impl_->semanticsChanged) {
            impl_->semanticsChanged();
        }
    }
}

void Scene::enabled(Node node, bool value) {
    impl_->mutableScene();
    auto& current = impl_->entry(node).view.enabled;
    if (current == value) {
        return;
    }
    current = value;
    impl_->resetInput();
    impl_->dirty();
}

void Scene::visible(Node node, bool value) {
    auto next = view(node).layout;
    next.visible = value;
    style(node, next);
}

void Scene::selected(Node node, bool value) {
    impl_->mutableScene();
    auto& current = impl_->entry(node).view.selected;
    if (current != value) {
        current = value;
        impl_->dirty();
    }
}

void Scene::icon(Node node, std::string value) {
    impl_->mutableScene();
    auto& view = impl_->entry(node).view;
    auto& current = view.icon;
    if (current != value) {
        current = std::move(value);
        impl_->entry(node).measureDirty = true;
        impl_->dirty(view.layout.width.unit == Length::Unit::Auto || view.layout.height.unit == Length::Unit::Auto);
    }
}

void Scene::imageFrame(Node node, uint32_t cell) {
    impl_->mutableScene();
    auto& v = impl_->entry(node).view;
    if (v.kind != ViewKind::Image || cell >= v.imageCells) {
        throw std::invalid_argument("Invalid scene image cell");
    }
    if (v.imageCell != cell) {
        v.imageCell = cell;
        impl_->dirty();
    }
}

void Scene::image(Node node, std::wstring path) {
    impl_->mutableScene();
    auto& v = impl_->entry(node).view;
    if (v.kind != ViewKind::Image) {
        throw std::invalid_argument("Expected a scene image");
    }
    if (v.image != path || v.resourceImage) {
        v.image = std::move(path);
        v.resourceImage = false;
        v.resource = {};
        impl_->dirty();
    }
}

void Scene::imageResource(Node node, ImageSnapshot resource) {
    impl_->mutableScene();
    auto& v = impl_->entry(node).view;
    if (v.kind != ViewKind::Image ||
        static_cast<unsigned>(resource.state) > static_cast<unsigned>(ImageState::Cancelled) ||
        (resource.state == ImageState::Ready && !resource.image)) {
        throw std::invalid_argument("Invalid scene image resource");
    }
    if (!v.resourceImage || v.resource != resource) {
        v.resourceImage = true;
        v.resource = std::move(resource);
        impl_->dirty();
    }
}

void Scene::pose(Node node, const Pose& value) {
    impl_->mutableScene();
    auto& v = impl_->entry(node).view;
    if (v.kind != ViewKind::Image) {
        throw std::invalid_argument("Scene poses are visual image transforms");
    }
    for (double number : {value.translation.x, value.translation.y, value.pivot.x, value.pivot.y, value.scale.width,
                          value.scale.height, value.rotation, value.opacity}) {
        if (!std::isfinite(number)) {
            throw std::invalid_argument("Invalid scene image pose");
        }
    }
    if (!same(v.pose, value)) {
        v.pose = value;
        impl_->dirty();
    }
}

void Scene::invalidate(bool layout) {
    impl_->mutableScene();
    if (layout) {
        for (auto& [id, entry] : impl_->entries) {
            entry.measureDirty = true;
        }
    }
    impl_->dirty(layout);
}

void Scene::transform(Node node, const Pose& value) {
    impl_->mutableScene();
    validateTransform(value);
    auto& current = impl_->entry(node).view.transform;
    if (!same(current, value)) {
        current = value;
        impl_->updateTransforms();
        impl_->targetsDirty = true;
        impl_->dirty(false, node);
    }
}

bool Scene::arrange(Canvas& canvas, Rect area, double dpr, bool rtl) {
    impl_->mutableScene();
    detail::layoutRect(area);
    if (!std::isfinite(dpr) || dpr < .5 || dpr > 4) {
        throw std::invalid_argument("Invalid scene DPI");
    }
    const auto old = impl_->area;
    const bool changed = old.x != area.x || old.y != area.y || old.width != area.width || old.height != area.height ||
                         impl_->dpr != dpr || impl_->rtl != rtl || impl_->canvas != &canvas;
    if (changed) {
        impl_->dirty(true);
    }
    if (impl_->canvas != &canvas) {
        for (auto& [id, entry] : impl_->entries) {
            entry.measureDirty = true;
            entry.clip.Reset();
            entry.clipFactory.Reset();
        }
    }
    impl_->resolve();
    if (!impl_->layoutDirty) {
        return false;
    }
    impl_->canvas = &canvas;
    ScopedDuration duration(impl_->stats.layoutNanoseconds);
    impl_->busy = true;
    try {
        for (Node i : impl_->order()) {
            const auto& e = impl_->entry(i);
            impl_->layout->style(e.layoutNode, impl_->style(e));
            if (!container(e.view.kind) && e.measureDirty) {
                impl_->layout->invalidate(e.layoutNode);
            }
        }
        impl_->layout->calculate(area, dpr, rtl);
        for (auto& [id, entry] : impl_->entries) {
            entry.measureDirty = false;
        }
        impl_->area = area;
        impl_->dpr = dpr;
        impl_->rtl = rtl;
        impl_->layoutDirty = false;
        if (!impl_->focusable(impl_->focused)) {
            impl_->focused = 0;
        }
        impl_->targetsDirty = true;
        ++impl_->stats.layouts;
        impl_->busy = false;
    } catch (...) {
        impl_->busy = false;
        throw;
    }
    if (impl_->semanticsChanged) {
        impl_->semanticsChanged();
    }
    return true;
}

Rect Scene::bounds(Node node) const {
    if (impl_->layoutDirty || !impl_->layout) {
        throw std::logic_error("Arrange the scene before reading bounds");
    }
    impl_->entry(node);
    return impl_->bounds(node);
}

Rect Scene::visualBounds(Node node) const {
    bounds(node);
    return impl_->visualBounds(node);
}

Rect Scene::layoutBounds(Node node) const {
    bounds(node);
    return impl_->layout->bounds(impl_->entry(node).layoutNode);
}

D2D1::Matrix3x2F Scene::worldTransform(Node node) const {
    bounds(node);
    return impl_->worldTransform(node);
}

Scene::Node Scene::find(uint32_t key) const {
    if (key) {
        for (const auto& [i, entry] : impl_->entries) {
            if (i && entry.view.key == key) {
                return i;
            }
        }
    }
    return root;
}

WidgetTheme Scene::resolvedTheme(Node node) const {
    std::vector<Node> ancestors;
    for (Node id = node;; id = impl_->entry(id).parent) {
        ancestors.push_back(id);
        if (id == root) {
            break;
        }
    }
    auto result = impl_->theme;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        result = patched(result, impl_->entry(*it).view.theme);
    }
    return result;
}

bool Scene::needsLayout() const {
    return impl_->layoutDirty;
}

bool Scene::needsPaint() const {
    return impl_->paintDirty;
}

SceneStats Scene::stats() const {
    auto result = impl_->stats;
    result.layerBytes = 0;
    std::set<uint64_t> images;
    for (const auto& [id, entry] : impl_->entries) {
        result.layerBytes += entry.layerBytes;
        const auto& image = entry.view.resource.image;
        if (image && images.insert(image->id()).second) {
            result.referencedImageBytes += image->pixels().size();
        }
    }
    return result;
}

std::vector<DebugNode> Scene::debugNodes() const {
    std::vector<DebugNode> result;
    for (Node id : impl_->order()) {
        const auto& entry = impl_->entries.at(id);
        DebugNode node;
        node.id = id;
        node.kind = uint32_t(entry.view.kind);
        node.name = entry.view.accessibleName.empty() ? entry.view.text : entry.view.accessibleName;
        node.resource = entry.view.resource.source ? *entry.view.resource.source : entry.view.image;
        node.error = entry.view.resource.error;
        node.loading = entry.view.resourceImage && entry.view.resource.state == ImageState::Loading;
        node.failed = entry.view.resourceImage && entry.view.resource.state == ImageState::Failed;
        node.layoutDirty = impl_->layoutDirty;
        node.focused = id && id == focused();
        node.enabled = node.visible = true;
        for (Node ancestor = id;; ancestor = impl_->entries.at(ancestor).parent) {
            node.visible &= impl_->entries.at(ancestor).view.layout.visible;
            node.enabled &= impl_->entries.at(ancestor).view.enabled;
            if (!ancestor) {
                break;
            }
        }
        node.visible &= impl_->layoutDirty || impl_->painted(id);
        node.interactive = id && node.visible && node.enabled && (control(entry.view.kind) || entry.view.clickable);
        if (!node.layoutDirty && node.visible) {
            node.bounds = transformedBounds(impl_->layout->bounds(entry.layoutNode), impl_->worldTransform(id));
            node.clip = control(entry.view.kind) && id ? impl_->visualBounds(entry.parent) : impl_->visualBounds(id);
            if (node.interactive) {
                const double slop = entry.view.kind == ViewKind::Slider ? entry.view.hitSlop : 0;
                const auto bounds = impl_->bounds(id);
                const auto area = transformedBounds(
                    {bounds.x - slop, bounds.y - slop, bounds.width + slop * 2, bounds.height + slop * 2},
                    impl_->worldTransform(id));
                const double left = std::max(node.clip.x, area.x);
                const double top = std::max(node.clip.y, area.y);
                const double right = std::min(node.clip.x + node.clip.width, area.x + area.width);
                const double bottom = std::min(node.clip.y + node.clip.height, area.y + area.height);
                node.hit = {left, top, std::max(0., right - left), std::max(0., bottom - top)};
            }
        }
        result.push_back(node);
    }
    return result;
}

InputRouter& Scene::router() {
    return impl_->router;
}

bool Scene::active() const {
    return impl_->feedback.active();
}

void Scene::targets(Point origin) {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y)) {
        throw std::invalid_argument("Invalid scene input origin");
    }
    if (impl_->layoutDirty) {
        throw std::logic_error("Arrange the scene before input");
    }
    if (origin.x != impl_->origin.x || origin.y != impl_->origin.y) {
        cancel();
        impl_->origin = origin;
    }
    if (!impl_->targetsDirty) {
        return;
    }
    impl_->resolve();
    std::vector<HitRegion> regions;
    for (Node i : impl_->order()) {
        const auto& e = impl_->entry(i);
        if (i == root) {
            continue;
        }
        if (!e.visible || !e.enabled || !impl_->painted(i) ||
            (!control(e.view.kind) && !e.view.clickable && !e.view.draggable)) {
            continue;
        }
        const auto bounds = impl_->bounds(i);
        if (bounds.width <= 0 || bounds.height <= 0) {
            continue;
        }
        auto region = e.view.kind == ViewKind::Slider ? impl_->slider(i).region() : impl_->button(i).region();
        if (e.view.draggable) {
            region.behavior = PointerBehavior::Drag;
        }
        const auto offset = D2D1::Matrix3x2F::Translation(float(origin.x), float(origin.y));
        region.shape = region.shape.transformed(impl_->worldTransform(i) * offset);
        for (Node ancestor = e.parent;; ancestor = impl_->entry(ancestor).parent) {
            region.clips.push_back(
                HitShape::rectangle(impl_->bounds(ancestor)).transformed(impl_->worldTransform(ancestor) * offset));
            if (!ancestor) {
                break;
            }
        }
        regions.push_back(std::move(region));
    }
    impl_->router.regions(std::move(regions));
    impl_->targetsDirty = false;
}

SceneEvent Scene::accept(PointerResult result) {
    impl_->mutableScene();
    SceneEvent event;
    event.pointer = result;
    const auto id = result.clicked ? result.clicked : result.target;
    if (id && contains(id) && !impl_->layoutDirty) {
        auto& e = impl_->entry(id);
        event.key = e.view.key;
        event.handled = true;
        if (impl_->focusGranted && impl_->router.pressed() == id) {
            focus(id);
        }
        event.clicked = result.clicked != 0;
        if (e.view.kind == ViewKind::Slider) {
            event.value = impl_->slider(id).change(result);
        } else if (e.view.kind == ViewKind::Toggle && event.clicked) {
            event.value = e.view.value ? 0 : 1;
        }
        if (event.value) {
            value(id, *event.value);
        }
    }
    impl_->feedback.sync(impl_->router);
    if (result.changed || result.clicked) {
        impl_->dirty();
    }
    return event;
}

SceneEvent Scene::input(PointerEvent event, Point origin) {
    targets(origin);
    return accept(impl_->router.dispatch(event));
}

void Scene::focusGranted(bool granted) {
    impl_->mutableScene();
    if (impl_->focusGranted != granted) {
        impl_->focusGranted = granted;
        impl_->focused = 0;
        impl_->dirty();
    }
    if (!granted) {
        cancel();
    }
}

bool Scene::focus(Node node) {
    impl_->mutableScene();
    if (!impl_->focusGranted || (node && !impl_->focusable(node))) {
        return false;
    }
    if (impl_->focused != node) {
        impl_->focused = node;
        impl_->dirty();
    }
    return true;
}

Scene::Node Scene::focused() const {
    return impl_->focusGranted && impl_->focusable(impl_->focused) ? impl_->focused : 0;
}

SceneEvent Scene::key(KeyEvent event) {
    impl_->mutableScene();
    if (!impl_->focusGranted || impl_->layoutDirty) {
        return {};
    }
    if (event.key == Key::Escape) {
        cancel();
        focus(0);
        SceneEvent result;
        result.handled = true;
        return result;
    }
    const auto node = focused();
    if (node && impl_->entry(node).view.onKey) {
        const auto callback = impl_->entry(node).view.onKey;
        auto result = callback(event);
        if (result.handled || focused() != node) {
            return result;
        }
    }
    const bool arrow =
        event.key == Key::Left || event.key == Key::Right || event.key == Key::Up || event.key == Key::Down;
    if (event.key == Key::Tab || (arrow && (!node || impl_->entry(node).view.kind != ViewKind::Slider))) {
        std::vector<Node> order;
        for (Node id : impl_->order()) {
            if (impl_->focusable(id) && impl_->bounds(id).width > 0 && impl_->bounds(id).height > 0) {
                order.push_back(id);
            }
        }
        if (order.empty()) {
            return {};
        }
        const bool back = event.key == Key::Tab ? event.shift : event.key == Key::Left || event.key == Key::Up;
        const auto found = std::find(order.begin(), order.end(), node);
        const auto index = found == order.end()
                               ? (back ? order.size() - 1 : 0)
                               : (size_t(found - order.begin()) + order.size() + (back ? -1 : 1)) % order.size();
        focus(order[index]);
        SceneEvent result;
        result.key = impl_->entry(order[index]).view.key;
        result.handled = true;
        return result;
    }
    if (!node) {
        return {};
    }
    const auto& view = impl_->entry(node).view;
    SceneEvent result;
    result.key = view.key;
    if (view.kind == ViewKind::Slider && (arrow || event.key == Key::Home || event.key == Key::End)) {
        const double step = view.step > 0 ? view.step : (view.maximum - view.minimum) / 100;
        const bool decrease = event.key == Key::Left || event.key == Key::Down;
        const double next = event.key == Key::Home  ? view.minimum
                            : event.key == Key::End ? view.maximum
                                                    : view.value + (decrease ? -step : step);
        value(node, std::clamp(next, view.minimum, view.maximum));
        result.value = impl_->entry(node).view.value;
        result.handled = true;
    } else if ((event.key == Key::Enter || event.key == Key::Space) && view.kind != ViewKind::Slider) {
        result.clicked = result.handled = true;
        if (view.kind == ViewKind::Toggle) {
            value(node, view.value ? 0 : 1);
            result.value = impl_->entry(node).view.value;
        }
    }
    return result;
}

std::vector<SemanticNode> Scene::semantics() const {
    std::vector<SemanticNode> result;
    for (Node id : impl_->order()) {
        if (!id) {
            continue;
        }
        const auto& entry = impl_->entries.at(id);
        const auto& view = entry.view;
        bool visible = true, enabled = true;
        for (Node ancestor = id;; ancestor = impl_->entries.at(ancestor).parent) {
            visible &= impl_->entries.at(ancestor).view.layout.visible;
            enabled &= impl_->entries.at(ancestor).view.enabled;
            if (!ancestor) {
                break;
            }
        }
        if (!visible) {
            continue;
        }
        SemanticNode node;
        node.id = id;
        node.parent = entry.parent;
        node.bounds = impl_->layoutDirty ? Rect{} : impl_->visualBounds(id);
        node.name = view.accessibleName.empty() ? view.text : view.accessibleName;
        node.enabled = enabled;
        node.focusable = impl_->focusable(id);
        node.focused = focused() == id;
        node.selected = view.selected;
        node.checked = view.kind == ViewKind::Toggle && view.value != 0;
        node.number = view.value;
        node.minimum = view.minimum;
        node.maximum = view.maximum;
        node.step = view.step;
        switch (view.kind) {
            case ViewKind::Button:
                node.role = SemanticRole::Button;
                break;
            case ViewKind::Toggle:
                node.role = SemanticRole::Toggle;
                break;
            case ViewKind::Slider:
                node.role = SemanticRole::Slider;
                break;
            case ViewKind::Text:
                node.role = SemanticRole::Text;
                break;
            case ViewKind::Image:
                node.role = SemanticRole::Image;
                node.busy = view.resourceImage && view.resource.state == ImageState::Loading;
                node.error = view.resourceImage && view.resource.state == ImageState::Failed;
                break;
            default:
                node.role = view.clickable ? SemanticRole::Button : SemanticRole::Group;
                break;
        }
        if (view.kind == ViewKind::Slider || view.kind == ViewKind::Toggle) {
            node.value = std::to_wstring(view.value);
        }
        node.role = view.semanticRole.value_or(node.role);
        if (view.semanticValue) {
            node.value = view.semanticValue();
        }
        result.push_back(std::move(node));
    }
    return result;
}

void Scene::cancel() {
    impl_->mutableScene();
    const bool changed = impl_->router.hovered() || impl_->router.pressed() || impl_->feedback.size();
    impl_->resetInput();
    if (changed) {
        impl_->dirty();
    }
}

bool Scene::advance(double seconds) {
    impl_->mutableScene();
    const bool changed = impl_->feedback.advance(seconds);
    if (changed) {
        impl_->dirty();
    }
    return changed;
}

void Scene::onInputReset(std::function<void()> callback) {
    impl_->inputReset = std::move(callback);
}

void Scene::onInvalidate(std::function<void()> callback) {
    impl_->invalidated = std::move(callback);
}

void Scene::onSemanticsChanged(std::function<void()> callback) {
    impl_->semanticsChanged = std::move(callback);
}

void Scene::draw(Canvas& primary, const IconPainter& icons) {
    impl_->mutableScene();
    if (impl_->layoutDirty || impl_->canvas != &primary) {
        throw std::logic_error("Arrange with this Canvas before drawing");
    }
    impl_->resolve();
    ScopedDuration duration(impl_->stats.paintNanoseconds);
    impl_->busy = true;
    try {
        const auto order = impl_->order();
        const bool transformed = std::any_of(order.begin(), order.end(), [&](Node i) {
            return !same(impl_->entry(i).view.transform, Pose{}) || impl_->entry(i).view.cacheSubtree;
        });
        const auto drawOne = [&](Canvas& canvas, Node i) {
            const auto& e = impl_->entry(i);
            const auto& v = e.view;
            const auto area = impl_->bounds(i);
            if (!e.visible || area.width <= 0 || area.height <= 0) {
                return;
            }
            const auto clip = control(v.kind) && i ? impl_->bounds(e.parent) : area;
            const auto paint = [&] {
                if (v.paint) {
                    canvas.saved([&] {
                        v.paint(canvas, area, e.theme);
                    });
                }
                if (v.paintLocal) {
                    canvas.saved([&] {
                        canvas.translate(area.x, area.y);
                        v.paintLocal(canvas, {0, 0, area.width, area.height}, e.theme);
                    });
                }
                if (v.kind == ViewKind::Text) {
                    canvas.text(v.text, e.theme.font, area, e.theme.foreground);
                } else if (v.kind == ViewKind::Image) {
                    canvas.saved([&] {
                        canvas.translate(area.x, area.y);
                        canvas.posed(v.pose, [&] {
                            if (!v.resourceImage) {
                                canvas.image(v.image, {0, 0, area.width, area.height}, v.imageCell, v.imageCells);
                            } else if (v.resource.state == ImageState::Ready) {
                                canvas.image(*v.resource.image, {0, 0, area.width, area.height},
                                             {v.imageFit, v.imageCell, v.imageCells});
                            } else {
                                const Rect placeholder{1, 1, std::max(0., area.width - 2),
                                                       std::max(0., area.height - 2)};
                                canvas.box(placeholder.x, placeholder.y, placeholder.width, placeholder.height,
                                           std::min(6., placeholder.height * .15), e.theme.surface);
                                if (v.resource.state == ImageState::Failed && icons) {
                                    const double size = std::min({20., placeholder.width, placeholder.height});
                                    icons(canvas,
                                          {area.width * .5 - size * .5, area.height * .5 - size * .5, size, size},
                                          "image-off", e.theme.muted);
                                }
                            }
                        });
                    });
                } else if (v.kind == ViewKind::Button) {
                    const auto button = impl_->button(i);
                    if (v.icon.empty()) {
                        button.draw(canvas, e.theme, impl_->feedback.visual(i));
                    } else {
                        IconButton icon(i, area, v.icon, v.text, v.appearance);
                        static_cast<Button&>(icon) = button;
                        icon.iconSize = v.iconSize;
                        icon.draw(canvas, e.theme, icons, impl_->feedback.visual(i));
                    }
                } else if (v.kind == ViewKind::Slider) {
                    impl_->slider(i).draw(canvas, e.theme, v.value, impl_->feedback.visual(i));
                } else if (v.kind == ViewKind::Toggle) {
                    Toggle toggle(i, area);
                    toggle.enabled = e.enabled;
                    toggle.draw(canvas, e.theme, v.value != 0, impl_->feedback.visual(i));
                }
                if (i && i == focused()) {
                    canvas.ink(e.theme.accent);
                    const auto ring = inset(area, {1, 1, 1, 1});
                    canvas.target->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(float(ring.x), float(ring.y), float(ring.x + ring.width),
                                                      float(ring.y + ring.height)),
                                          float(e.theme.radius), float(e.theme.radius)),
                        canvas.brush.Get(), 1);
                }
            };
            if (transformed) {
                paint();
            } else {
                canvas.clipped(clip, paint);
            }
        };
        if (!transformed) {
            // Preserve the established axis-aligned rendering path and its pixels.
            for (Node i : order) {
                drawOne(primary, i);
            }
        } else {
            std::function<void(Canvas&, Node, unsigned)> drawTree;
            drawTree = [&](Canvas& canvas, Node i, unsigned depth) {
                if (depth > 64) {
                    throw std::length_error("Transformed scene depth exceeds 64");
                }
                auto& e = impl_->entry(i);
                if (!e.visible || !impl_->painted(i)) {
                    return;
                }
                const auto area = impl_->bounds(i);
                if (area.width <= 0 || area.height <= 0) {
                    return;
                }
                canvas.saved([&] {
                    D2D1::Matrix3x2F previous;
                    canvas.target->GetTransform(&previous);
                    canvas.target->SetTransform(impl_->localTransform(i) * previous);
                    const auto paintContent = [&](Canvas& destination) {
                        drawOne(destination, i);
                        for (Node child : e.children) {
                            drawTree(destination, child, depth + 1);
                        }
                    };
                    double dpi = 0;
                    if (e.view.cacheSubtree) {
                        const auto matrix = impl_->worldTransform(i);
                        dpi = impl_->dpr *
                              std::max(std::hypot(matrix._11, matrix._12), std::hypot(matrix._21, matrix._22));
                    }
                    // A cached subtree is already one composited image. Its opacity can
                    // be applied by DrawBitmap without allocating another group layer.
                    // Extreme scales retain the vector/group path and its opacity scope.
                    const bool cachedLayer = e.view.cacheSubtree && dpi >= .5 && dpi <= 4;
                    D2D1::Matrix3x2F nativeTransform;
                    canvas.target->GetTransform(&nativeTransform);
                    float targetDpiX = 0, targetDpiY = 0;
                    canvas.target->GetDpi(&targetDpiX, &targetDpiY);
                    const bool exactCacheScale = nativeTransform._12 == 0 && nativeTransform._21 == 0 &&
                                                 std::abs(nativeTransform._11) * targetDpiX / 96 == dpi &&
                                                 std::abs(nativeTransform._22) * targetDpiY / 96 == dpi;
                    const auto prepareCache = [&] {
                        if (!cachedLayer) {
                            return;
                        }
                        auto width = uint32_t(std::ceil(area.width * dpi)),
                             height = uint32_t(std::ceil(area.height * dpi));
                        uint64_t bytes = uint64_t(width) * height * 4;
                        if (!width || !height || width > 4096 || height > 4096 || bytes > 16 * 1024 * 1024) {
                            throw std::length_error("Scene layer cache exceeds budget");
                        }
                        if (!e.layer) {
                            e.layer = std::make_unique<OffscreenSurface>();
                        }
                        const bool paddingEligible = exactCacheScale && area.width * dpi == width &&
                                                     area.height * dpi == height && width <= 4094 && height <= 4094;
                        if (!e.layerValid || e.layerDpi != dpi || e.layerPaddingEligible != paddingEligible ||
                            !e.layer->bitmap()) {
                            uint64_t total = bytes;
                            for (const auto& [other, entry] : impl_->entries) {
                                if (other != i) {
                                    total += entry.layerBytes;
                                }
                            }
                            if (total > 16 * 1024 * 1024) {
                                throw std::length_error("Scene layer cache total budget exceeded");
                            }
                            const uint64_t paddedBytes = uint64_t(width + 2) * (height + 2) * 4;
                            const unsigned padding =
                                paddingEligible && total - bytes + paddedBytes <= 16 * 1024 * 1024 ? 1 : 0;
                            width += padding * 2;
                            height += padding * 2;
                            bytes = uint64_t(width) * height * 4;
                            // Reserve the parent before descendants check their own caches.
                            e.layerBytes = bytes;
                            e.layerValid = false;
                            try {
                                canvas.renderLayer(*e.layer, {width, height, dpi}, [&](Canvas& layer) {
                                    layer.translate(-area.x + padding / dpi, -area.y + padding / dpi);
                                    if (padding) {
                                        layer.clipped(area, [&] {
                                            paintContent(layer);
                                        });
                                    } else {
                                        paintContent(layer);
                                    }
                                });
                            } catch (...) {
                                e.layerBytes = 0;
                                e.layer.reset();
                                throw;
                            }
                            e.layerValid = true;
                            e.layerDpi = dpi;
                            e.layerPadding = padding;
                            e.layerPaddingEligible = paddingEligible;
                            ++impl_->stats.layerBuilds;
                        } else {
                            ++impl_->stats.layerHits;
                        }
                    };
                    prepareCache();
                    const auto content = [&] {
                        if (!cachedLayer) {
                            paintContent(canvas);
                            return;
                        }
                        const auto size = e.layer->logicalSize();
                        const double border = e.layerPadding / dpi;
                        canvas.target->DrawBitmap(
                            e.layer->bitmap(),
                            D2D1::RectF(float(area.x - border), float(area.y - border),
                                        float(area.x - border + size.width), float(area.y - border + size.height)),
                            float(e.view.transform.opacity), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    };
                    const auto clipped = [&] {
                        if (control(e.view.kind) && i) {
                            content();
                            return;
                        }
                        D2D1::Matrix3x2F transform;
                        canvas.target->GetTransform(&transform);
                        if (transform._12 == 0 && transform._21 == 0) {
                            // At one source pixel per destination pixel, the transparent
                            // gutter already provides exact fractional rectangle coverage.
                            if (cachedLayer && exactCacheScale && e.layerPadding) {
                                content();
                                return;
                            }
                            // Translation and axis-aligned scale preserve this rectangle.
                            // Avoid a compositing surface for the ordinary card path.
                            canvas.clipped(area, content);
                            return;
                        }
                        const auto& old = e.clipBounds;
                        if (!e.clip || e.clipFactory != canvas.factory || old.x != area.x || old.y != area.y ||
                            old.width != area.width || old.height != area.height) {
                            e.clip.Reset();
                            require(canvas.factory->CreateRectangleGeometry(
                                D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width),
                                            float(area.y + area.height)),
                                &e.clip));
                            e.clipBounds = area;
                            e.clipFactory = canvas.factory;
                        }
                        // Axis-aligned clips would expand a rotated rectangle to its AABB.
                        canvas.masked(e.clip.Get(), content);
                    };
                    if (e.view.transform.opacity < 1 && !cachedLayer) {
                        canvas.withOpacity(e.view.transform.opacity, clipped);
                    } else {
                        clipped();
                    }
                });
            };
            drawTree(primary, root, 0);
        }
        ++impl_->stats.paints;
        impl_->paintDirty = false;
        impl_->busy = false;
    } catch (...) {
        impl_->busy = false;
        impl_->paintDirty = true;
        throw;
    }
}
}  // namespace cybercanvas
