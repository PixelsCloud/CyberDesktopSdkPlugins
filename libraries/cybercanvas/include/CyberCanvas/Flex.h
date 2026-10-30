#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
enum class FlexAlign { Start, Center, End, Stretch };
enum class FlexJustify { Start, Center, End, SpaceBetween };
enum class MeasureMode { Undefined, Exactly, AtMost };

struct Length {
    enum class Unit { Auto, Points, Percent };
    Unit unit = Unit::Auto;
    double value = 0;

    static Length points(double value);

    static Length percent(double value);
};

struct FlexStyle {
    Axis axis = Axis::Vertical;
    bool wrap = false, visible = true;
    FlexAlign align = FlexAlign::Stretch;
    FlexAlign lineAlign = FlexAlign::Start;
    FlexJustify justify = FlexJustify::Start;
    Length width{}, height{}, basis{};
    Size minimum{};
    std::optional<Size> maximum;
    Insets padding{}, margin{};
    double gap = 0, grow = 0, shrink = 1;
};

struct MeasureConstraint {
    double width = 0, height = 0;
    MeasureMode widthMode = MeasureMode::Undefined, heightMode = MeasureMode::Undefined;
};

// Small retained layout tree. Yoga is hidden behind a compiled static adapter;
// measurement is supplied by the caller, with no dependency on a renderer.
class FlexLayout {
public:
    using Node = uint32_t;
    using Measure = std::function<Size(MeasureConstraint)>;
    static constexpr Node root = 0;

    explicit FlexLayout(const FlexStyle& rootStyle = {});
    ~FlexLayout();
    FlexLayout(const FlexLayout&) = delete;
    FlexLayout& operator=(const FlexLayout&) = delete;
    FlexLayout(FlexLayout&&) noexcept;
    FlexLayout& operator=(FlexLayout&&) noexcept;

    Node add(Node parent, const FlexStyle& style = {}, Measure measure = {});
    // Remove a subtree, or move it before a sibling (root means append).
    // Root cannot be removed/moved. Deleted handles are never reused.
    void remove(Node node);
    bool move(Node node, Node parent, Node before = root);
    bool contains(Node node) const;
    Node parent(Node node) const;
    std::vector<Node> children(Node node) const;
    void style(Node node, const FlexStyle& value);
    void measure(Node node, Measure value);
    void invalidate(Node measuredLeaf);
    // Returns false for an unchanged, cached layout. All results are logical
    // world coordinates; Yoga rounds edges to the requested physical pixel grid.
    bool calculate(Rect available, double dpr = 1, bool rtl = false);
    Rect bounds(Node node) const;
    Rect clippedBounds(Node node) const;
    size_t size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
