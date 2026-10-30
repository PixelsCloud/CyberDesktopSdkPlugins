#pragma once

#include "CyberCanvas/Components.h"
#include "CyberCanvas/Semantics.h"

namespace cybercanvas {
enum class ViewKind { Column, Row, Text, Image, Button, Slider, Toggle, Decoration };
struct SceneEvent;

// Unspecified tokens inherit from the parent. Font metrics and spacing affect
// layout; colors and other appearance tokens only invalidate painting.
struct ThemePatch {
    std::optional<uint32_t> foreground, muted, accent, surface, border, track, onAccent, error;
    std::optional<double> radius, padding, gap, thumbRadius, fontSize;
    std::optional<TextStyle> font;
    std::optional<double> controlHeight, disabledOpacity;
};

struct View {
    ViewKind kind = ViewKind::Column;
    uint32_t key = 0;
    FlexStyle layout;
    ThemePatch theme;
    std::wstring text, image;
    std::wstring accessibleName;
    std::optional<SemanticRole> semanticRole;
    std::function<std::wstring()> semanticValue;
    std::function<SceneEvent(KeyEvent)> onKey;
    // Set through imageResource() for asynchronous resources; image remains the
    // synchronous compatibility path when resourceImage is false.
    ImageSnapshot resource;
    bool resourceImage = false;
    ImageFit imageFit = ImageFit::Contain;
    std::string icon;
    ButtonStyle appearance = ButtonStyle::Quiet;
    bool enabled = true, selected = false, round = false, inheritGap = true, clickable = false;
    double value = 0, minimum = 0, maximum = 1, step = 0, iconSize = 16, hitSlop = 8;
    Size intrinsic{32, 32};
    uint32_t imageCell = 0, imageCells = 1;
    Pose pose;
    // Runs before children, in world logical coordinates, inside the node clip.
    std::function<void(Canvas&, Rect, const WidgetTheme&)> paint;
    // Opt-in scene transform. Pivot is in logical pixels relative to layout
    // bounds. Applies to the complete subtree, including clips and input.
    Pose transform;
    std::function<void(Canvas&, Rect, const WidgetTheme&)> paintLocal;
    bool cacheSubtree = false;  // Explicit opt-in; invalidate() when custom paint data changes.
    bool draggable = false;     // Requests host pointer capture through the SDK Scene adapter.
};

struct SceneEvent {
    uint32_t key = 0;
    bool clicked = false;
    bool handled = false;
    std::optional<double> value;
    PointerResult pointer;
};

struct SceneStats {
    uint64_t layouts = 0, paints = 0, measurements = 0;
    uint64_t layoutNanoseconds = 0, paintNanoseconds = 0, measureNanoseconds = 0, referencedImageBytes = 0;
    uint64_t layerBuilds = 0, layerHits = 0, layerBytes = 0;
};

// Retained structure and controlled values. Owns no window, timer or thread.
// Arrange before input; draw into each requested, freshly cleared Canvas frame.
class Scene {
public:
    using Node = uint32_t;
    static constexpr Node root = 0;
    explicit Scene(View rootView = {});
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Node add(Node parent, View view);
    // Structural updates preserve surviving handles and controlled values.
    // Root cannot be removed/moved; before=root appends to the new parent.
    void remove(Node node);
    void clear(Node parent = root);
    bool move(Node node, Node parent, Node before = root);
    bool contains(Node node) const;
    Node parent(Node node) const;
    std::vector<Node> children(Node node) const;
    size_t size() const;
    const View& view(Node node) const;
    void style(Node node, const FlexStyle& value);
    void theme(Node node, const ThemePatch& value);
    void theme(const WidgetTheme& value);
    void text(Node node, std::wstring value);
    void accessibleName(Node node, std::wstring value);
    void value(Node node, double value);
    void enabled(Node node, bool value);
    void visible(Node node, bool value);
    void selected(Node node, bool value);
    void icon(Node node, std::string value);
    void imageFrame(Node node, uint32_t cell);
    void image(Node node, std::wstring path);
    void imageResource(Node node, ImageSnapshot resource);
    // Visual-only pose for Image leaves; pivot is relative to its local bounds.
    void pose(Node node, const Pose& value);
    void transform(Node node, const Pose& value);
    void invalidate(bool layout = false);

    bool arrange(Canvas& canvas, Rect area, double dpr = 1, bool rtl = false);
    void draw(Canvas& canvas, const IconPainter& icons = {});
    Rect bounds(Node node) const;
    // Unclipped layout coordinates, independent of whether transforms are active.
    Rect layoutBounds(Node node) const;
    Rect visualBounds(Node node) const;
    D2D1::Matrix3x2F worldTransform(Node node) const;
    Node find(uint32_t key) const;
    WidgetTheme resolvedTheme(Node node) const;
    bool needsLayout() const;
    bool needsPaint() const;
    SceneStats stats() const;
    std::vector<DebugNode> debugNodes() const;

    SceneEvent input(PointerEvent event, Point origin = {});
    void cancel();
    bool advance(double seconds);
    bool active() const;

    void focusGranted(bool granted);
    bool focus(Node node);
    Node focused() const;
    SceneEvent key(KeyEvent event);
    std::vector<SemanticNode> semantics() const;

    // SDK adapters share this router, keeping capture and geometry in one place.
    InputRouter& router();
    void targets(Point origin = {});
    SceneEvent accept(PointerResult result);
    void onInputReset(std::function<void()> callback);
    // Coalesced notification on clean -> dirty; scheduling belongs to the host.
    void onInvalidate(std::function<void()> callback);
    // Host adapter hook; separate from frame scheduling so changing the redraw
    // callback cannot leave stale accessibility nodes in the host.
    void onSemanticsChanged(std::function<void()> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
