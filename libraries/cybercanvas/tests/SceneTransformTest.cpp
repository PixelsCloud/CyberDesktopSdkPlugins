#include <CyberCanvas/Scene.h>

#include <iostream>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        View root;
        root.inheritGap = false;
        Scene scene(root);
        View group;
        group.inheritGap = false;
        group.layout.width = Length::points(100);
        group.layout.height = Length::points(100);
        group.layout.shrink = 0;
        const auto parent = scene.add(0, group);
        View leaf;
        leaf.kind = ViewKind::Decoration;
        leaf.key = 42;
        leaf.clickable = true;
        leaf.layout.width = Length::points(80);
        leaf.layout.height = Length::points(30);
        leaf.layout.shrink = 0;
        leaf.paintLocal = [](Canvas& c, Rect r, const WidgetTheme&) {
            c.rectangle(r, 0xff0000ff);
        };
        const auto child = scene.add(parent, leaf);
        scene.arrange(canvas, {0, 0, 400, 300});
        const auto layout = scene.bounds(child);
        const auto paint = [&](double dpi = 1.) {
            const auto width = uint32_t(400 * dpi), height = uint32_t(300 * dpi);
            std::vector<uint8_t> rgba(size_t(width) * height * 4);
            canvas.render({width, height, width * 4, rgba.size(), rgba.data(), dpi}, {}, [&](Canvas& c, Rect) {
                scene.draw(c);
            });
            return rgba;
        };
        Pose pose{.translation = {150, 50}, .pivot = {50, 50}, .rotation = 90};
        scene.transform(parent, pose);
        const auto point = scene.worldTransform(child).TransformPoint({40, 15});
        check("child inherits rotation around parent pivot",
              std::abs(point.x - 235) < 1e-5 && std::abs(point.y - 90) < 1e-5);
        check("visual transforms do not reflow layout", !scene.needsLayout() && scene.bounds(child).x == layout.x);
        scene.input({PointerKind::Press, {235, 90}, true});
        check("rotated child clicks at its visible location", scene.input({PointerKind::Release, {235, 90}}).key == 42);
        scene.targets();
        check("original location is no longer interactive", scene.router().hit({40, 15}) == 0);
        const auto pixels = paint();
        check("transformed geometry appears where input hits", pixels[(90 * 400 + 235) * 4] > 240);
        const auto visual = scene.visualBounds(child);
        check("visual bounds include the rotated leaf",
              std::abs(visual.x - 220) < 1e-5 && std::abs(visual.y - 50) < 1e-5 && std::abs(visual.width - 30) < 1e-5 &&
                  std::abs(visual.height - 80) < 1e-5);
        const auto semantics = scene.semantics();
        const auto found = std::find_if(semantics.begin(), semantics.end(), [&](const auto& v) {
            return v.id == child;
        });
        check("accessibility tracks visual bounds", found != semantics.end() && found->bounds.x == visual.x);
        scene.targets({10, 20});
        check("host origin composes after scene transforms", scene.router().hit({245, 110}) == child);
        pose.scale.width = 0;
        scene.transform(parent, pose);
        scene.targets();
        check("singular groups cannot receive input", scene.router().hit({235, 90}) == 0);
        pose = {.translation = {150, 50}, .pivot = {50, 50}, .rotation = 45};
        scene.transform(parent, pose);
        auto full = leaf.layout;
        full.width = full.height = Length::points(100);
        scene.style(child, full);
        scene.arrange(canvas, {0, 0, 400, 300});
        const auto rotated = paint();
        scene.targets();
        check("rotated clipping uses geometry not AABB", rotated[(35 * 400 + 135) * 4 + 3] == 0 &&
                                                             scene.router().hit({135, 35}) == 0 &&
                                                             rotated[(100 * 400 + 200) * 4 + 3] > 240);
        {
            Canvas replacement;
            std::vector<uint8_t> replacementPixels(rotated.size());
            const FrameBuffer frame{400, 300, 1600, replacementPixels.size(), replacementPixels.data(), 1};
            replacement.render(frame, {}, [&](Canvas& target, Rect) {
                canvas.targetForFrame(target.target.Get(), frame, true);
                canvas.render(frame, {}, [&](Canvas& borrowed, Rect) {
                    scene.draw(borrowed);
                });
            });
            check("rotated clip follows replacement rendering factory", replacementPixels == rotated);
            canvas.targetForFrame(nullptr, frame);
            check("rotated clip returns to original rendering factory", paint() == rotated);
        }
        scene.transform(parent, {.translation = {150, 50}, .opacity = .5});
        full.height = Length::points(60);
        scene.style(child, full);
        // Two opaque red leaves overlap at y=30..60 within the group.
        View overlay = leaf;
        overlay.key = 43;
        overlay.layout.width = Length::points(100);
        overlay.layout.height = Length::points(60);
        overlay.transform.translation = {0, -30};
        scene.add(parent, overlay);
        scene.arrange(canvas, {0, 0, 400, 300});
        const auto transparent = paint();
        const auto alpha = transparent[(90 * 400 + 170) * 4 + 3];
        check("group opacity is applied once across overlapping descendants", alpha >= 126 && alpha <= 129);
        scene.targets();
        check("translated second child wins overlapping hit", scene.router().hit({170, 90}) == scene.find(43));
        check("translated child is clipped at its final location",
              transparent[(135 * 400 + 170) * 4 + 3] >= 126 && transparent[(155 * 400 + 170) * 4 + 3] == 0);
        scene.transform(parent, {.opacity = 0});
        scene.targets();
        check("transparent groups have no focus or hits",
              scene.router().hit({20, 20}) == 0 && scene.visualBounds(child).width == 0);
        scene.transform(parent, {.translation = {20, 20}});
        scene.focusGranted(true);
        scene.focus(child);
        scene.remove(parent);
        scene.arrange(canvas, {0, 0, 400, 300});
        check("deleting a transformed group clears focus and capture",
              scene.focused() == 0 && !scene.router().captured());
        paint(1.5);
        Scene cached;
        View cachedView;
        cachedView.layout.width = Length::points(100);
        cachedView.layout.height = Length::points(100);
        cachedView.cacheSubtree = true;
        const auto cachedGroup = cached.add(Scene::root, cachedView);
        View content;
        content.kind = ViewKind::Decoration;
        content.layout.width = Length::points(100);
        content.layout.height = Length::points(100);
        unsigned paintCount = 0;
        content.paintLocal = [&](Canvas& draw, Rect area, const WidgetTheme&) {
            ++paintCount;
            draw.box(0, 0, area.width, area.height, 0, 0xff0000ff);
        };
        cached.add(cachedGroup, content);
        cached.arrange(canvas, {0, 0, 400, 300});
        std::vector<uint8_t> cachePixels(400 * 300 * 4);
        const auto paintCache = [&] {
            canvas.render({400, 300, 1600, cachePixels.size(), cachePixels.data(), 1}, {}, [&](Canvas& target, Rect) {
                cached.draw(target);
            });
        };
        paintCache();
        cached.transform(cachedGroup, {.translation = {100, 50}, .opacity = .5});
        paintCache();
        check("group translation and opacity reuse local bitmap",
              paintCount == 1 && cached.stats().layerBuilds == 1 && cached.stats().layerHits == 1);
        check("cached group opacity and position match",
              cachePixels[(75 * 400 + 150) * 4 + 3] >= 126 && cachePixels[(25 * 400 + 50) * 4 + 3] == 0);
        cached.invalidate();
        paintCache();
        check("custom content invalidation rebuilds cache", paintCount == 2);
        cached.transform(cachedGroup, {.translation = {100, 50}, .scale = {1.5, 1.5}});
        paintCache();
        check("scaled groups regenerate at effective DPI",
              paintCount == 3 && cached.stats().layerBytes == 152 * 152 * 4);
        const auto debug = cached.debugNodes();
        check("debug bounds reflect group transform", debug[1].bounds.x == 100 && debug[1].bounds.width == 150);
        cached.transform(cachedGroup, {.translation = {100, 50}, .pivot = {50, 50}, .rotation = 45, .opacity = .5});
        paintCache();
        check("rotated cache fallback preserves geometry clipping and opacity",
              cachePixels[(35 * 400 + 85) * 4 + 3] == 0 &&
                  std::abs(int(cachePixels[(100 * 400 + 150) * 4 + 3]) - 128) <= 2);
        const auto rotatedBuilds = cached.stats().layerBuilds;
        paintCache();
        check("rotated cache is reused while its pose is stable", cached.stats().layerBuilds == rotatedBuilds);
        cached.transform(cachedGroup, {.translation = {100.25, 50.75}, .opacity = .5});
        paintCache();
        const auto fractionalBuilds = cached.stats().layerBuilds;
        const auto fractionalPixels = cachePixels;
        paintCache();
        check("cache preparation and reuse preserve fractional edge coverage",
              cached.stats().layerBuilds == fractionalBuilds && fractionalPixels == cachePixels &&
                  std::abs(int(cachePixels[(50 * 400 + 150) * 4 + 3]) - 32) <= 2 &&
                  std::abs(int(cachePixels[(100 * 400 + 100) * 4 + 3]) - 96) <= 2);
        {
            View rootGroup;
            rootGroup.cacheSubtree = true;
            rootGroup.inheritGap = false;
            Scene nested(rootGroup);
            auto groupView = cachedView;
            groupView.inheritGap = false;
            groupView.layout.shrink = 0;
            const auto inner = nested.add(0, groupView);
            nested.add(inner, content);
            nested.arrange(canvas, {0, 0, 200, 200});
            const auto renderNested = [&] {
                canvas.render({400, 300, 1600, cachePixels.size(), cachePixels.data(), 1}, {},
                              [&](Canvas& target, Rect) {
                                  nested.draw(target);
                              });
            };
            renderNested();
            const auto builds = nested.stats().layerBuilds;
            nested.transform(inner, {.translation = {50, 20}});
            renderNested();
            check("moving a cached descendant rebuilds only its ancestor",
                  nested.stats().layerBuilds == builds + 1 && cachePixels[(30 * 400 + 60) * 4 + 3] == 255 &&
                      cachePixels[(10 * 400 + 10) * 4 + 3] == 0);
            nested.transform(inner, {.translation = {50, 20}, .opacity = .5});
            nested.transform(0, {.opacity = .5});
            renderNested();
            const auto nestedAlpha = cachePixels[(30 * 400 + 60) * 4 + 3];
            check("nested cached opacity multiplies composed groups exactly once",
                  nestedAlpha >= 63 && nestedAlpha <= 65);
            nested.transform(0, {.scale = {5, 5}, .opacity = .5});
            nested.transform(inner, {.opacity = .5});
            renderNested();
            const auto uncachedAlpha = cachePixels[(30 * 400 + 60) * 4 + 3];
            check("extreme-scale cache fallback retains group opacity", uncachedAlpha >= 63 && uncachedAlpha <= 65);
            nested.transform(0, {});
            nested.clear();
            auto huge = groupView;
            huge.layout.width = huge.layout.height = Length::points(2049);
            const auto excessive = nested.add(0, huge);
            nested.arrange(canvas, {0, 0, 2200, 2200});
            bool rejected = false;
            try {
                renderNested();
            } catch (const std::length_error&) {
                rejected = true;
            }
            check("oversized group cache fails within the shared scene limit", rejected);
            nested.remove(excessive);
            nested.arrange(canvas, {0, 0, 200, 200});
            renderNested();
            check("cache budget failure does not poison subsequent canvas rendering",
                  nested.stats().layerBytes <= 202 * 202 * 4);
        }
        {
            View parentView;
            parentView.cacheSubtree = true;
            parentView.inheritGap = false;
            parentView.paintLocal = [](Canvas& draw, Rect area, const WidgetTheme&) {
                draw.rectangle(area, 0x33aa55ff);
            };
            Scene bounded(parentView);
            View childView;
            childView.cacheSubtree = true;
            childView.layout.width = childView.layout.height = Length::points(1536);
            const auto childNode = bounded.add(0, childView);
            bounded.arrange(canvas, {0, 0, 1536, 1536});
            const auto renderBounded = [&] {
                canvas.render({400, 300, 1600, cachePixels.size(), cachePixels.data(), 1}, {}, [&](Canvas& draw, Rect) {
                    bounded.draw(draw);
                });
            };
            bool rejected = false;
            try {
                renderBounded();
            } catch (const std::length_error&) {
                rejected = true;
            }
            check("nested caches include the in-progress parent in the shared memory limit",
                  rejected && bounded.stats().layerBytes <= 16 * 1024 * 1024);
            bounded.remove(childNode);
            bounded.arrange(canvas, {0, 0, 100, 100});
            renderBounded();
            const auto index = (10 * 400 + 10) * 4;
            check("nested allocation rejection releases its reservation and renders again",
                  bounded.stats().layerBytes <= 64 * 1024 && cachePixels[index] == 0x33 &&
                      cachePixels[index + 1] == 0xaa && cachePixels[index + 2] == 0x55 &&
                      cachePixels[index + 3] == 255);
            bounded.arrange(canvas, {0, 0, 2048, 2048});
            renderBounded();
            const auto fullBudgetBuilds = bounded.stats().layerBuilds;
            renderBounded();
            check("optional cache gutter respects the exact budget without rebuilding each frame",
                  bounded.stats().layerBytes == 16 * 1024 * 1024 && bounded.stats().layerBuilds == fullBudgetBuilds &&
                      cachePixels[index + 3] == 255);
        }
        for (double dpi : {1., 1.25, 2.}) {
            for (bool cache : {false, true}) {
                for (double verticalScale : {.75, 1.25}) {
                    for (double horizontalScale : {1.25, -1.25}) {
                        View rootView;
                        rootView.inheritGap = false;
                        Scene clippedScene(rootView);
                        View rectangle;
                        rectangle.cacheSubtree = cache;
                        rectangle.layout.width = Length::points(cache ? 16 : 20.5);
                        rectangle.layout.height = Length::points(cache ? 16 : 15.25);
                        rectangle.paintLocal = [](Canvas& draw, Rect, const WidgetTheme&) {
                            draw.rectangle({-100, -100, 300, 300}, 0xff0000ff);
                        };
                        const auto node = clippedScene.add(0, rectangle);
                        clippedScene.transform(
                            node,
                            {.translation = {32.25, 12.5}, .scale = {horizontalScale, verticalScale}, .opacity = .5});
                        clippedScene.arrange(canvas, {0, 0, 64, 64}, dpi);
                        const uint32_t edge = uint32_t(64 * dpi);
                        std::vector<uint8_t> actual(edge * edge * 4);
                        canvas.render({edge, edge, edge * 4, actual.size(), actual.data(), dpi}, {},
                                      [&](Canvas& draw, Rect) {
                                          clippedScene.draw(draw);
                                      });
                        if (cache && verticalScale != std::abs(horizontalScale)) {
                            // Nonuniform bitmap resampling is not analytic rectangle coverage.
                            // Preserve the original clipped, unpadded native bitmap path exactly.
                            std::vector<uint8_t> reference(actual.size());
                            OffscreenSurface surface;
                            canvas.render({edge, edge, edge * 4, reference.size(), reference.data(), dpi}, {},
                                          [&](Canvas& draw, Rect) {
                                              const double cacheDpi = dpi * std::abs(horizontalScale);
                                              draw.renderLayer(
                                                  surface, {uint32_t(16 * cacheDpi), uint32_t(16 * cacheDpi), cacheDpi},
                                                  [](Canvas& layer) {
                                                      layer.rectangle({-100, -100, 300, 300}, 0xff0000ff);
                                                  });
                                              draw.target->SetTransform(D2D1::Matrix3x2F::Scale(float(horizontalScale),
                                                                                                float(verticalScale)) *
                                                                        D2D1::Matrix3x2F::Translation(32.25f, 12.5f));
                                              draw.target->PushAxisAlignedClip(D2D1::RectF(0, 0, 16, 16),
                                                                               D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                                              draw.target->DrawBitmap(surface.bitmap(), D2D1::RectF(0, 0, 16, 16), .5f,
                                                                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                                              draw.target->PopAxisAlignedClip();
                                          });
                            check("nonuniform cache fallback matches original native bitmap sampling",
                                  actual == reference);
                            continue;
                        }
                        // Compare with analytic pixel coverage, not another D2D path: geometry
                        // masks quantize fractional edges more coarsely than rectangle clips.
                        const auto area = clippedScene.layoutBounds(node);
                        const double x1 = (32.25 + area.x * horizontalScale) * dpi;
                        const double x2 = (32.25 + (area.x + area.width) * horizontalScale) * dpi;
                        const double top = (12.5 + area.y * verticalScale) * dpi;
                        const double bottom = (12.5 + (area.y + area.height) * verticalScale) * dpi;
                        bool equivalent = true;
                        for (uint32_t y = 0; y < edge; ++y) {
                            for (uint32_t x = 0; x < edge; ++x) {
                                const double cx = std::max(0., std::min(double(x + 1), std::max(x1, x2)) -
                                                                   std::max(double(x), std::min(x1, x2)));
                                const double cy =
                                    std::max(0., std::min(double(y + 1), bottom) - std::max(double(y), top));
                                const int alpha = int(std::lround(127.5 * cx * cy));
                                const size_t pixel = (size_t(y) * edge + x) * 4;
                                equivalent &= std::abs(int(actual[pixel]) - alpha) <= 2 && actual[pixel + 1] == 0 &&
                                              actual[pixel + 2] == 0 && std::abs(int(actual[pixel + 3]) - alpha) <= 2;
                            }
                        }
                        check("fractional clip has analytic antialias coverage including reflection and opacity",
                              equivalent);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
