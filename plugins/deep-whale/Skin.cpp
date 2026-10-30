#include "plugins/deep-whale/Skin.h"

#include <dwrite_3.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <numbers>

#include "plugins/deep-whale/HorizontalFrame.h"

namespace whaleskin {
namespace {
Rect box(const nlohmann::json& j) {
    return {j.at(0), j.at(1), j.at(2), j.at(3)};
}

double textScale(Canvas& c) {
    D2D1_MATRIX_3X2_F transform;
    c.target->GetTransform(&transform);
    return std::max(.01, std::hypot(double(transform._11), double(transform._12)));
}
}  // namespace

struct Skin::Impl {
    struct Layer {
        std::string name, file;
        cybercanvas::ImageRegion source;
        Rect destination;
        bool repeatX = false, foreground = false;
        std::vector<Point> clip;
    };

    std::filesystem::path assets;
    std::string folder;
    Size scene;
    std::vector<double> horizontalCuts;
    double frameLeft = 0, frameWidth = 0, extension = 0;
    unsigned columns = 1, rows = 1;
    std::map<std::string, Rect, std::less<>> regions;
    std::vector<Layer> layers;
    std::map<std::string, Layer, std::less<>> parts;
    std::map<std::string, std::string> errors;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Icons icons;
    cdsdk::Controls controls;
    std::unique_ptr<cdsdk::WindowsFonts> latin, cjk, selected;
    cybercanvas::ComPtr<IDWriteFontCollection> combined;
    double relief = 0;
    std::string fontName = "Bree Serif", selectedName;
    std::wstring locale = L"zh-CN";
    uint32_t ink = kIvory, accent = kRuby;
    bool background = true;
    std::vector<cdsdk::Controls::Item> items;
    std::function<void(const std::string&)> report;
    View artView, worldView;
    uint64_t builds = 0;
    cybercanvas::OffscreenSurface backgroundLayer, foregroundLayer;
    uint64_t layerRevision = uint64_t(-1);
    cybercanvas::SurfaceSize layerSize{};
    ID2D1RenderTarget* layerOwner = nullptr;
    bool paused = false;

    void load(const std::filesystem::path& directory) {
        if (assets == directory) {
            return;
        }
        std::ifstream file(directory / "skin.json");
        const auto config = nlohmann::json::parse(file);
        assets = directory;
        folder = config.at("folder");
        columns = config.at("columns");
        rows = config.at("rows");
        scene = {config.at("scene").at(0), config.at("scene").at(1)};
        horizontalCuts = config.value("horizontalCuts", std::vector<double>{});
        extension = 0;
        regions.clear();
        layers.clear();
        images.clear();
        ++builds;
        for (const auto& [key, j] : config.at("regions").items()) {
            regions[key] = box(j);
        }
        for (const auto& j : config.at("layers")) {
            const auto r = box(j.at("source"));
            layers.push_back({j.at("name"),
                              j.at("file"),
                              {uint32_t(r.x), uint32_t(r.y), uint32_t(r.width), uint32_t(r.height)},
                              box(j.at("destination")),
                              j.value("repeatX", false)});
            auto& layer = layers.back();
            layer.foreground = j.value("foreground", false);
            if (j.contains("clip")) {
                for (const auto& point : j.at("clip")) {
                    layer.clip.push_back({point[0], point[1]});
                }
            }
        }
        frameLeft = scene.width;
        double right = 0;
        for (const auto& layer : layers) {
            frameLeft = std::min(frameLeft, layer.destination.x);
            right = std::max(right, layer.destination.x + layer.destination.width);
        }
        frameWidth = right - frameLeft;
        parts.clear();
        if (config.contains("parts")) {
            for (const auto& [name, j] : config.at("parts").items()) {
                const auto r = box(j.at("source"));
                parts.emplace(
                    name,
                    Layer{
                        name, j.at("file"), {uint32_t(r.x), uint32_t(r.y), uint32_t(r.width), uint32_t(r.height)}, {}});
            }
        }
    }

    void arrange(CdRect safe) {
        const double s = std::max(.001, std::min(std::max(1., safe.width - 14) / std::max(1., scene.width - 14),
                                                 std::max(1., safe.height - 14) / std::max(1., scene.height - 14)));
        artView = {{safe.x + (safe.width - scene.width * s) / 2, safe.y + (safe.height - scene.height * s) / 2}, s};
        extension = 0;
        if (!horizontalCuts.empty()) {
            extension = std::max(0., (safe.width - 14) / s - frameWidth);
            artView.origin.x = safe.x + 7 - frameLeft * s;
        }
    }

    void prepare(Canvas& c) {
        const double density = artView.scale * c.devicePixelRatio();
        for (const auto& l : layers) {
            images.set(
                l.name, assets / l.file, 1,
                uint32_t(std::max(1., std::ceil((l.repeatX ? l.destination.height
                                                           : std::max(l.destination.width, l.destination.height)) *
                                                density))),
                l.source);
        }
        if (images.poll()) {
            ++builds;
        }
        for (const auto& l : layers) {
            const auto s = images.get(l.name);
            if (s.state == cybercanvas::ImageState::Failed && errors[l.name] != s.error) {
                errors[l.name] = s.error;
                if (report) {
                    report("Deep Whale " + l.name + ": " + s.error);
                }
            }
        }
    }

    void drawLayer(Canvas& c, const Layer& l) {
        const auto image = images.get(l.name).image;
        if (!image) {
            return;
        }
        const auto paint = [&] {
            const auto fit = horizontalCuts.empty() ? cybercanvas::ImageFit::Contain : cybercanvas::ImageFit::Stretch;
            if (l.repeatX) {
                const auto r = l.destination;
                const double step = double(l.source.width) * r.height / l.source.height;
                c.clipped(r, [&] {
                    for (double x = r.x; x < r.x + r.width; x += step) {
                        c.image(*image, {x, r.y, step, r.height}, {.fit = fit, .clip = false});
                    }
                });
            } else {
                // The registration retains the original ROI aspect. Restore it after
                // the background worker rounds the decoded pixel dimensions.
                c.image(*image, l.destination, {.fit = fit, .clip = false});
            }
        };
        if (l.clip.empty()) {
            paint();
        } else {
            auto mask = c.path();
            mask.moveTo(l.clip.front());
            for (size_t i = 1; i < l.clip.size(); ++i) {
                mask.lineTo(l.clip[i]);
            }
            mask.close();
            c.masked(mask, paint);
        }
    }

    void fonts(Canvas& c) {
        if (!latin) {
            latin = std::make_unique<cdsdk::WindowsFonts>(c.nativeTextFactory(), assets / "BreeSerif-Regular.ttf");
            cjk = std::make_unique<cdsdk::WindowsFonts>(
                c.nativeTextFactory(), cdsdk::bundledFontPath(L"assets/fonts/DeepWhale/LXGWWenKaiGBScreen.ttf"));
            cybercanvas::ComPtr<IDWriteFactory3> factory;
            cybercanvas::require(c.nativeTextFactory()->QueryInterface(IID_PPV_ARGS(factory.GetAddressOf())));
            cybercanvas::ComPtr<IDWriteFontSetBuilder> builder;
            cybercanvas::require(factory->CreateFontSetBuilder(builder.GetAddressOf()));
            for (const auto* f : {latin.get(), cjk.get()}) {
                cybercanvas::ComPtr<IDWriteFontCollection1> collection;
                cybercanvas::require(f->collection()->QueryInterface(IID_PPV_ARGS(collection.GetAddressOf())));
                cybercanvas::ComPtr<IDWriteFontSet> set;
                cybercanvas::require(collection->GetFontSet(set.GetAddressOf()));
                cybercanvas::require(builder->AddFontSet(set.Get()));
            }
            cybercanvas::ComPtr<IDWriteFontSet> set;
            cybercanvas::require(builder->CreateFontSet(set.GetAddressOf()));
            cybercanvas::ComPtr<IDWriteFontCollection1> collection;
            cybercanvas::require(factory->CreateFontCollectionFromFontSet(set.Get(), collection.GetAddressOf()));
            cybercanvas::require(collection.As(&combined));
        }
        if (fontName.empty() || fontName == "Bree Serif") {
            c.fontCollection(combined.Get());
            return;
        }
        if (!selected) {
            selected = std::make_unique<cdsdk::WindowsFonts>(c.nativeTextFactory());
        }
        selected->select(c.nativeTextFactory(), fontName);
        c.fontCollection(selected->collection());
    }

    cybercanvas::TextStyle textStyle(Canvas& canvas, const std::wstring& text, double size) {
        fonts(canvas);
        const bool chinese = std::any_of(text.begin(), text.end(), [](wchar_t c) {
            return c > 0x2e7f;
        });
        auto& font = chinese ? cjk : latin;
        cybercanvas::TextStyle style;
        style.family = fontName.empty() || fontName == "Bree Serif" ? font->family() : selected->family();
        style.locale = locale;
        style.size = size;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        style.wrap = false;
        style.tabularFigures = true;
        return style;
    }

    void staticLayer(Canvas& c, bool foreground) {
        const double density = artView.scale * c.devicePixelRatio();
        const cybercanvas::SurfaceSize size{
            uint32_t(std::clamp(std::ceil((scene.width + extension) * density), 1., 4096.)),
            uint32_t(std::clamp(std::ceil(scene.height * density), 1., 4096.)), c.devicePixelRatio()};
        if (layerRevision != builds || layerOwner != c.target.Get() || layerSize.width != size.width ||
            layerSize.height != size.height || layerSize.devicePixelRatio != size.devicePixelRatio) {
            backgroundLayer.reset();
            foregroundLayer.reset();
            layerSize = size;
            layerOwner = c.target.Get();
            layerRevision = builds;
        }
        if (std::none_of(layers.begin(), layers.end(), [foreground](const Layer& l) {
                return l.foreground == foreground;
            })) {
            return;
        }
        auto& surface = foreground ? foregroundLayer : backgroundLayer;
        if (!surface.bitmap()) {
            c.renderLayer(surface, size, [&](Canvas& child) {
                child.scale(artView.scale, artView.scale);
                const auto paint = [&](Canvas& destination) {
                    for (const auto& layer : layers) {
                        if (layer.foreground == foreground) {
                            drawLayer(destination, layer);
                        }
                    }
                };
                if (extension > .001) {
                    cybercanvas::OffscreenSurface original;
                    auto originalSize = size;
                    originalSize.width = uint32_t(std::clamp(std::ceil(scene.width * density), 1., 4096.));
                    child.renderLayer(original, originalSize, [&](Canvas& base) {
                        base.scale(artView.scale, artView.scale);
                        paint(base);
                    });
                    drawFrame(child, original.bitmap(), scene, {0, 0, scene.width + extension, scene.height},
                              horizontalCuts, artView.scale);
                } else {
                    paint(child);
                }
            });
        }
        if (horizontalCuts.empty()) {
            c.target->DrawBitmap(surface.bitmap(), D2D1::RectF(0, 0, float(scene.width), float(scene.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            const auto source = D2D1::RectF(0, 0, float((scene.width + extension) * artView.scale),
                                            float(scene.height * artView.scale));
            c.target->DrawBitmap(surface.bitmap(),
                                 D2D1::RectF(0, 0, float(scene.width + extension), float(scene.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
        }
    }
};

Skin::Skin(const std::filesystem::path& assets) : impl_(std::make_unique<Impl>()) {
    if (!assets.empty()) {
        impl_->load(assets);
    }
}

Skin::~Skin() = default;

void Skin::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->load(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                "assets");
    impl_->canvas.bindGpu(context);
    impl_->images.bind(context);
    impl_->icons.bind(context);
    impl_->controls.bind(context, std::move(action));
    if (context.host && context.host->log) {
        const auto log = context.host->log;
        const auto user = context.host_context;
        impl_->report = [log, user](const std::string& s) {
            log(user, 3, s.data(), uint32_t(s.size()));
        };
    }
    onChange([host = *context.host, user = context.host_context] {
        host.request_redraw(user);
    });
}

void Skin::onChange(std::function<void()> callback) {
    impl_->images.onChange([impl = impl_.get(), callback = std::move(callback)] {
        // Images also polls on its own timer. Invalidate before notifying the
        // controller, even if prepare() observes no further transition next frame.
        ++impl->builds;
        if (callback) {
            callback();
        }
    });
}

void Skin::font(std::string name) {
    impl_->fontName = std::move(name);
}

void Skin::appearance(const cdsdk::Settings& settings) {
    CdSettingValue value{};
    if (settings.get("language", &value, sizeof(value)) == CD_OK && value.text) {
        impl_->locale = std::string_view(value.text) == "en"      ? L"en-US"
                        : std::string_view(value.text) == "zh-TW" ? L"zh-TW"
                                                                  : L"zh-CN";
    }
    if (settings.get("font_family", &value, sizeof(value)) == CD_OK && value.text) {
        font(value.text);
    }
    impl_->ink = settings.get("text_color", &value, sizeof(value)) == CD_OK ? uint32_t(value.integer) : kIvory;
    impl_->accent = settings.get("accent_color", &value, sizeof(value)) == CD_OK ? uint32_t(value.integer) : kRuby;
    impl_->relief =
        settings.get("relief_strength", &value, sizeof(value)) == CD_OK ? std::clamp(value.number / 100., 0., 1.) : 0;
    impl_->background = true;
    if (settings.get("show_background", &value, sizeof(value)) == CD_OK) {
        impl_->background = value.integer != 0;
    }
}

void Skin::pause(bool paused) {
    impl_->paused = paused;
    if (!paused && impl_->images.stats().failed) {
        impl_->images.clear();
        impl_->errors.clear();
    }
    impl_->images.pause(paused);
    if (paused) {
        impl_->controls.cancel();
    }
}

bool Skin::poll() {
    const bool changed = impl_->images.poll();
    if (changed) {
        ++impl_->builds;
    }
    return changed;
}

bool Skin::pending() const {
    return impl_->images.pending();
}

bool Skin::ready() const {
    return !pending() && impl_->images.stats().ready >= impl_->layers.size() && !impl_->images.stats().failed;
}

cdsdk::ImageSlotStats Skin::imageStats() const {
    return impl_->images.stats();
}

uint64_t Skin::cacheBuilds() const {
    return impl_->builds;
}

void Skin::input(const CdInput& e) {
    impl_->controls.input(e);
}

void Skin::cancelInput() {
    impl_->controls.cancel();
}

void Skin::controls(CdRect safe, const std::vector<cdsdk::Controls::Item>& items) {
    impl_->arrange(safe);
    impl_->items = items;
    auto transformed = items;
    const auto v = impl_->artView;
    for (auto& item : transformed) {
        const auto found = impl_->regions.find(item.name);
        const auto r = found == impl_->regions.end() || impl_->horizontalCuts.empty() ? item.bounds : region(item.name);
        item.bounds = {v.origin.x - safe.x + r.x * v.scale, v.origin.y - safe.y + r.y * v.scale, r.width * v.scale,
                       r.height * v.scale};
    }
    impl_->controls.items(std::move(transformed), {safe.x, safe.y});
}

Rect Skin::region(std::string_view name) const {
    const auto it = impl_->regions.find(name);
    return it == impl_->regions.end() ? Rect{} : frameRegion(it->second, impl_->extension, impl_->horizontalCuts);
}

Rect Skin::displayRegion(std::string_view name, CdRect safe) const {
    impl_->arrange(safe);
    const auto v = impl_->artView;
    const auto r = region(name);
    return {v.origin.x - safe.x + r.x * v.scale, v.origin.y - safe.y + r.y * v.scale, r.width * v.scale,
            r.height * v.scale};
}

View Skin::view(CdRect safe, Size world) {
    impl_->arrange(safe);
    const auto r = region("world");
    const auto v = impl_->artView;
    const double s = std::max(.001, std::min(r.width / world.width, r.height / world.height));
    impl_->worldView = {{v.origin.x + (r.x + (r.width - world.width * s) / 2) * v.scale,
                         v.origin.y + (r.y + (r.height - world.height * s) / 2) * v.scale},
                        s * v.scale};
    return impl_->worldView;
}

bool Skin::render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& paint,
                  cybercanvas::Pose pose) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](Canvas& c, CdRect area) {
        impl_->arrange(area);
        const auto v = impl_->artView;
        c.saved([&] {
            c.translate(v.origin.x - area.x, v.origin.y - area.y);
            c.scale(v.scale, v.scale);
            c.posed(pose, [&] {
                backdrop(c);
                paint(c);
                if (impl_->background) {
                    impl_->staticLayer(c, true);
                }
            });
        });
    });
    return true;
}

bool Skin::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

void Skin::backdrop(Canvas& c) {
    impl_->prepare(c);
    if (!impl_->background) {
        return;
    }
    impl_->staticLayer(c, false);
}

void Skin::artLayer(Canvas& c, const std::function<void(Canvas&)>& paint) {
    const auto a = impl_->artView, w = impl_->worldView;
    c.saved([&] {
        c.translate((a.origin.x - w.origin.x) / w.scale, (a.origin.y - w.origin.y) / w.scale);
        c.scale(a.scale / w.scale, a.scale / w.scale);
        paint(c);
    });
}

void Skin::worldBackdrop(Canvas& c, bool) {
    artLayer(c, [&](Canvas& a) {
        backdrop(a);
    });
}

void Skin::clear(Canvas& c, Rect r, uint32_t color, bool force) {
    if (force && r.width > 0 && r.height > 0) {
        c.box(r.x, r.y, r.width, r.height, 1, color);
    }
}

void Skin::label(Canvas& c, const std::wstring& text, Rect r, double size, uint32_t ink, bool,
                 std::span<const std::wstring> references) {
    if (r.width <= 0 || r.height <= 0 || text.empty()) {
        return;
    }
    if (ink == kIvory) {
        ink = impl_->ink;
    }
    // Sizes are art coordinates. Keep functional text readable at the native
    // grid; compact renderers explicitly remove optional rows that cannot fit.
    const double scale = textScale(c);
    if (r.height * scale < 9) {
        return;
    }
    const double minimum = 12. / scale;
    auto style = impl_->textStyle(c, text, std::max(minimum, size > 0 ? size : r.height * .77));
    const auto fitted = c.fitTextInk(text, style, r, references);
    c.clipped(r, [&] {
        if (impl_->relief > 0) {
            const double shift = std::max(.2, r.height * .025) * impl_->relief;
            c.ink(0x16365300 | uint32_t(impl_->relief * 75));
            c.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x + shift), float(fitted.origin.y + shift)),
                                     fitted.layout.Get(), c.brush.Get());
        }
        c.ink(ink);
        c.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(),
                                 c.brush.Get());
    });
}

void Skin::caption(Canvas& c, const std::wstring& text, Rect r, double size, uint32_t ink) {
    if (r.width <= 0 || r.height <= 0 || text.empty()) {
        return;
    }
    const double scale = textScale(c);
    if (r.height * scale < 9) {
        return;
    }
    auto style = impl_->textStyle(c, text, std::max(12. / scale, size > 0 ? size : r.height * .70));
    style.ellipsis = true;
    style.tabularFigures = false;
    c.text(text, style, r, ink == kIvory ? impl_->ink : ink);
}

void Skin::icon(Canvas& c, Rect r, std::string_view name, uint32_t ink) {
    const double side = std::min(r.width, r.height);
    if (side <= 0) {
        return;
    }
    r = {r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side};
    const double scale = impl_->artView.scale;
    c.saved([&] {
        c.translate(r.x, r.y);
        c.scale(1 / scale, 1 / scale);
        impl_->icons.paint(c, {0, 0, r.width * scale, r.height * scale}, name, ink);
    });
}

void Skin::reading(Canvas& c, const std::wstring& value, const std::wstring& unit, Rect r, double size, uint32_t ink) {
    if (r.width <= 0 || r.height <= 0 || value.empty()) {
        return;
    }
    if (unit.empty()) {
        label(c, value, r, size, ink, false);
        return;
    }
    auto style = impl_->textStyle(c, value, size);
    const auto digits = c.fitTextInk(value, style, {0, 0, r.width, r.height});
    const double gap = std::min(.8, r.width * .02);
    style.size *= .56;
    const auto suffix = c.fitTextInk(
        unit, style,
        {0, 0, std::max(.01, r.width - digits.bounds.width - gap), std::min(r.height, digits.bounds.height * .65)});
    const double left = r.x + (r.width - digits.bounds.width - gap - suffix.bounds.width) / 2;
    const double bottom = r.y + (r.height + digits.bounds.height) / 2;
    if (ink == kIvory) {
        ink = impl_->ink;
    }
    c.clipped(r, [&] {
        const auto draw = [&](const cybercanvas::Canvas::InkText& run, double x) {
            const double y = bottom - run.bounds.height;
            const auto origin =
                D2D1::Point2F(float(run.origin.x + x - run.bounds.x), float(run.origin.y + y - run.bounds.y));
            if (impl_->relief > 0) {
                const double shift = std::max(.2, r.height * .025) * impl_->relief;
                c.ink(0x16365300 | uint32_t(impl_->relief * 75));
                c.target->DrawTextLayout(D2D1::Point2F(float(origin.x + shift), float(origin.y + shift)),
                                         run.layout.Get(), c.brush.Get());
            }
            c.ink(ink);
            c.target->DrawTextLayout(origin, run.layout.Get(), c.brush.Get());
        };
        draw(digits, left);
        draw(suffix, left + digits.bounds.width + gap);
    });
}

void Skin::button(Canvas& c, Rect r, std::string_view name, bool selected, bool enabled) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    const double side = std::min(r.width, r.height), cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    const Rect face{cx - side / 2, cy - side / 2, side, side};
    c.gradientBox(face, side / 2, selected ? 0xe0f3ffff : 0xfffcf3ff, selected ? 0x84b8d8ff : 0xdcebf4ff);
    c.strokeBox(face, side / 2, 0xc9ae81ff, {.width = .7});
    c.oval(cx - side * .12, cy - side * .16, side * .29, side * .12, 0xffffff80);
    const double inset = side * .23;
    icon(c, {face.x + inset, face.y + inset, side - inset * 2, side - inset * 2}, name, enabled ? kIvory : 0x98a5b2ff);
}

void Skin::part(Canvas& c, std::string_view name, Rect target, bool artCoordinates) {
    const auto found = impl_->parts.find(name);
    if (found == impl_->parts.end() || target.width <= 0 || target.height <= 0) {
        return;
    }
    const auto& part = found->second;
    const double density = (artCoordinates ? impl_->artView.scale : impl_->worldView.scale) * c.devicePixelRatio();
    const auto edge = uint32_t(std::max(1., std::ceil(std::max(target.width, target.height) * density)));
    // Different native geometries may coexist in one scene (for example large and small gears).
    // DPI and widget resizing update each geometry's existing slot rather than retaining old resolutions.
    const std::string slot =
        "part-" + part.name + "-" + std::to_string(target.width) + "x" + std::to_string(target.height);
    impl_->images.set(slot, impl_->assets / part.file, 1, edge, part.source);
    const auto snapshot = impl_->images.get(slot);
    if (snapshot.image) {
        c.image(*snapshot.image, target, {.fit = cybercanvas::ImageFit::Contain, .clip = false});
    }
    if (snapshot.state == cybercanvas::ImageState::Failed && impl_->errors[slot] != snapshot.error) {
        impl_->errors[slot] = snapshot.error;
        if (impl_->report) {
            impl_->report("Deep Whale " + slot + ": " + snapshot.error);
        }
    }
}

void Skin::progress(Canvas& c, Rect r, double fraction) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    c.clipped(r, [&] {
        fraction = std::clamp(fraction, 0., 1.);
        const double h = std::min(4., r.height * .32), cy = r.y + r.height / 2, knob = std::min(4.5, r.height * .36);
        const double left = r.x + knob, right = r.x + r.width - knob, cx = left + (right - left) * fraction;
        const Rect track{left, cy - h / 2, right - left, h};
        c.gradientBox(track, h / 2, 0xe8f4faff, 0x8eacc8ff);
        c.strokeBox(track, h / 2, 0xbda780ff, {.width = .65});
        if (fraction > 0) {
            c.gradientBox({left, cy - h / 2, cx - left, h}, h / 2, 0xc5eafbff, 0x5f94bcff);
        }
        for (unsigned i = 0; i < 5; ++i) {
            const double x = left + (right - left) * i / 4;
            c.line(x, cy + h / 2 + 1, x, cy + h / 2 + 2, 0xc0aa84bb, .55);
        }
        c.oval(cx, cy, knob, knob, 0xbda780ff);
        c.oval(cx, cy - .2, std::max(.05, knob - .7), std::max(.05, knob - .7), 0x9dc7e2ff);
        c.oval(cx - knob * .23, cy - knob * .32, knob * .30, knob * .20, 0xfffcf0ee);
    });
}

void Skin::photo(Canvas& c, Rect r, const cybercanvas::ImageData& image, double opacity, std::string_view fit) {
    if (r.width > 0 && r.height > 0) {
        c.image(image, r,
                {.fit = fit == "contain" ? cybercanvas::ImageFit::Contain : cybercanvas::ImageFit::Cover,
                 .opacity = opacity});
    }
}

void Skin::weather(Canvas& c, Rect r, std::string_view name, std::string_view slot) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    const std::string key = "weather-" + std::string(slot);
    const auto edge =
        uint32_t(std::max(1., std::ceil(std::max(r.width, r.height) * impl_->artView.scale * c.devicePixelRatio())));
    impl_->images.set(key, impl_->assets / "icons" / (std::string(name) + ".png"), 1, edge);
    impl_->images.poll();
    if (const auto image = impl_->images.get(key).image) {
        c.image(*image, r, {.fit = cybercanvas::ImageFit::Contain});
    }
}

void Skin::gauge(Canvas& c, Rect r, double fraction, const std::wstring& value, const std::wstring& title,
                 uint32_t accent, const std::wstring& unit) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    const double s = std::min(r.width, r.height) / 100, cx = r.x + r.width / 2, cy = r.y + r.height * .52;
    const auto point = [&](double degrees, double radius) {
        const double a = degrees * std::numbers::pi / 180;
        return Point{cx + std::sin(a) * radius * s, cy - std::cos(a) * radius * s};
    };
    for (unsigned i = 0; i < 21; ++i) {
        const double angle = -125 + i * 12.5;
        const auto a = point(angle, i % 5 ? 35 : 32), b = point(angle, 39);
        c.line(a.x, a.y, b.x, b.y, i >= 17 ? 0xddc59dff : 0xb8d5d7ff, (i % 5 ? .75 : 1.7) * s);
    }
    c.saved([&] {
        c.translate(cx, cy);
        c.rotate(-125 + std::clamp(fraction, 0., 1.) * 250);
        auto needle = c.path();
        needle.moveTo({0, -29 * s}).lineTo({2 * s, 2 * s}).lineTo({0, 8 * s}).lineTo({-2 * s, 2 * s}).close();
        c.fill(needle, 0xf9ecd560);
        c.stroke(needle, (accent & 0xffffff00) | 0x90, {.width = .5 * s});
    });
    c.oval(cx, cy, 3.2 * s, 3.2 * s, accent);
    c.oval(cx - .7 * s, cy - .9 * s, s, s, 0xfff9e9ff);
    // A 36px dial cannot contain both a scale, needle and two tiny footer rows.
    // Put the metric and reading in the centre, with the needle behind them.
    label(c, title, {r.x + r.width * .04, r.y + r.height * .16, r.width * .92, r.height * .32}, 12., 0xd7eaf4ff, false);
    reading(c, value, unit, {r.x + r.width * .04, r.y + r.height * .48, r.width * .92, r.height * .45},
            std::max(18., r.height * .36), 0xfff3dcff);
}

Size Skin::size() const {
    return impl_->scene;
}

unsigned Skin::columns() const {
    return impl_->columns;
}

unsigned Skin::rows() const {
    return impl_->rows;
}

const std::string& Skin::folder() const {
    return impl_->folder;
}

bool Skin::textured() const {
    return true;
}
}  // namespace whaleskin
