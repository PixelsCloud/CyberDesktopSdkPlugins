#include "plugins/mecha-desk/Skin.h"

#include <dwrite_3.h>
#include <sdk/Diagnostics.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <numbers>

namespace aerisskin {
namespace {
Rect box(const nlohmann::json& j) {
    return {j.at(0), j.at(1), j.at(2), j.at(3)};
}

void hardClip(Canvas& canvas, Rect rect, const std::function<void()>& paint) {
    canvas.target->PushAxisAlignedClip(
        D2D1::RectF(float(rect.x), float(rect.y), float(rect.x + rect.width), float(rect.y + rect.height)),
        D2D1_ANTIALIAS_MODE_ALIASED);

    struct Clip {
        ID2D1RenderTarget* target;

        ~Clip() {
            target->PopAxisAlignedClip();
        }
    } scope{canvas.target.Get()};

    paint();
}
}  // namespace

struct Skin::Impl {
    struct Layer {
        std::string name, file;
        cybercanvas::ImageRegion source;
        Rect destination;
        bool repeatX = false, foreground = false;
        std::vector<Point> clip;
        std::vector<std::vector<Point>> silhouettes;
    };

    std::filesystem::path assets;
    std::string folder;
    Size scene;
    unsigned columns = 1, rows = 1;
    std::map<std::string, Rect, std::less<>> regions;
    std::vector<Layer> layers;
    std::map<std::string, Layer, std::less<>> parts;
    std::map<std::string, std::string> errors;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::DiagnosticSource diagnostics;
    cdsdk::Icons icons;
    cdsdk::Controls controls;
    std::unique_ptr<cdsdk::WindowsFonts> latin, cjk, displayFont, selected;
    nlohmann::json config;

    std::filesystem::path resolve(const std::string& file) const {
        return file.starts_with("@shared/") ? cdsdk::bundledFontPath(std::filesystem::path(L"assets/mecha") /
                                                                     std::filesystem::u8path(file.substr(8)))
                                            : assets / file;
    }

    cybercanvas::ComPtr<IDWriteFontCollection> combined;
    double relief = 0;
    std::string fontName = "Chakra Petch", selectedName;
    std::wstring locale = L"zh-CN";
    uint32_t ink = kIvory, accent = kRuby;
    bool background = true;
    std::vector<cdsdk::Controls::Item> items;
    std::function<void(const std::string&)> report;
    View artView, worldView;
    uint64_t builds = 0;
    uint64_t glassSource = 0;
    std::shared_ptr<const cybercanvas::ImageData> glass;
    bool paused = false;
    double horizontalExtension = 0;
    std::vector<double> horizontalCuts;

    void load(const std::filesystem::path& directory) {
        if (assets == directory) {
            return;
        }
        std::ifstream file(directory / "skin.json");
        config = nlohmann::json::parse(file);
        assets = directory;
        folder = config.at("folder");
        columns = config.at("columns");
        rows = config.at("rows");
        scene = {config.at("scene").at(0), config.at("scene").at(1)};
        regions.clear();
        layers.clear();
        images.clear();
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
            if (j.contains("silhouettes")) {
                for (const auto& contour : j.at("silhouettes")) {
                    std::vector<Point> points;
                    for (const auto& point : contour) {
                        points.push_back({point[0], point[1]});
                    }
                    if (points.size() >= 3) {
                        layer.silhouettes.push_back(std::move(points));
                    }
                }
            }
        }
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
        if (config.contains("horizontal")) {
            const auto& layout = config.at("horizontal");
            const double left = layout.at("left"), right = layout.at("right");
            const double s = std::max(.001, std::min(std::max(1., safe.height - 13) / std::max(1., scene.height - 13),
                                                     std::max(1., safe.width - 13) / std::max(1., right - left)));
            horizontalCuts = layout.at("cuts").get<std::vector<double>>();
            horizontalExtension = std::max(0., (safe.width - 13) / s - (right - left));
            artView = {{safe.x + 6.5 - left * s, safe.y + (safe.height - scene.height * s) / 2}, s};
            return;
        }
        horizontalExtension = 0;
        const double s = std::max(.001, std::min(std::max(1., safe.width - 14) / std::max(1., scene.width - 14),
                                                 std::max(1., safe.height - 14) / std::max(1., scene.height - 14)));
        artView = {{safe.x + (safe.width - scene.width * s) / 2, safe.y + (safe.height - scene.height * s) / 2}, s};
    }

    Rect reflow(Rect r) const {
        if (r.width <= 0 || horizontalExtension <= 0) {
            return r;
        }
        const double step = horizontalExtension / horizontalCuts.size();
        double shift = 0, growth = 0;
        for (const double cut : horizontalCuts) {
            if (r.x >= cut) {
                shift += step;
            } else if (r.x + r.width > cut) {
                growth += step;
            }
        }
        return {r.x + shift, r.y, r.width + growth, r.height};
    }

    void prepare(Canvas& c) {
        const double density = artView.scale * c.devicePixelRatio();
        for (const auto& l : layers) {
            images.set(
                l.name, resolve(l.file), 1,
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
                    report("Artwork " + folder + "/" + l.name + ": " + s.error);
                }
            }
        }
    }

    void drawOriginalLayer(Canvas& c, const Layer& l) {
        const auto image = images.get(l.name).image;
        if (!image) {
            return;
        }
        const auto paint = [&] {
            if (l.repeatX) {
                const auto r = l.destination;
                const double step = double(l.source.width) * r.height / l.source.height;
                c.clipped(r, [&] {
                    for (double x = r.x; x < r.x + r.width; x += step) {
                        c.image(*image, {x, r.y, step, r.height},
                                {.fit = cybercanvas::ImageFit::Contain, .clip = false});
                    }
                });
            } else {
                c.image(*image, l.destination, {.fit = cybercanvas::ImageFit::Contain, .clip = false});
            }
        };
        const auto clipped = [&] {
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
        };
        if (l.silhouettes.empty()) {
            clipped();
            return;
        }
        auto silhouette = c.path();
        for (const auto& contour : l.silhouettes) {
            silhouette.moveTo(contour.front());
            for (size_t i = 1; i < contour.size(); ++i) {
                silhouette.lineTo(contour[i]);
            }
            silhouette.close();
        }
        c.masked(silhouette, clipped);
    }

    void drawLayer(Canvas& c, const Layer& l) {
        if (horizontalExtension <= .001) {
            drawOriginalLayer(c, l);
            return;
        }
        const double extra = horizontalExtension / horizontalCuts.size();
        double previous = 0, shift = 0;
        for (const double cut : horizontalCuts) {
            hardClip(c, {previous + shift, 0, cut - previous, scene.height}, [&] {
                c.saved([&] {
                    c.translate(shift, 0);
                    drawOriginalLayer(c, l);
                });
            });
            hardClip(c, {cut + shift, 0, extra, scene.height}, [&] {
                c.saved([&] {
                    c.translate(cut + shift, 0);
                    c.scale(extra, 1);
                    c.translate(-cut, 0);
                    drawOriginalLayer(c, l);
                });
            });
            shift += extra;
            previous = cut;
        }
        hardClip(c, {previous + shift, 0, scene.width - previous, scene.height}, [&] {
            c.saved([&] {
                c.translate(shift, 0);
                drawOriginalLayer(c, l);
            });
        });
    }

    void fonts(Canvas& c) {
        if (!latin) {
            latin = std::make_unique<cdsdk::WindowsFonts>(
                c.nativeTextFactory(), cdsdk::bundledFontPath(L"assets/fonts/Aeris/ChakraPetch-Medium.ttf"));
            cjk = std::make_unique<cdsdk::WindowsFonts>(
                c.nativeTextFactory(),
                cdsdk::bundledFontPath(config.value("palette", "") == "terminal"
                                           ? L"assets/fonts/Streetlink/StreetlinkSansSC-Medium.ttf"
                                           : L"assets/fonts/Aeris/NotoSansSC-Variable.ttf"));
            cybercanvas::ComPtr<IDWriteFactory3> factory;
            cybercanvas::require(c.nativeTextFactory()->QueryInterface(IID_PPV_ARGS(factory.GetAddressOf())));
            cybercanvas::ComPtr<IDWriteFontSetBuilder> builder;
            cybercanvas::require(factory->CreateFontSetBuilder(builder.GetAddressOf()));
            displayFont = std::make_unique<cdsdk::WindowsFonts>(
                c.nativeTextFactory(),
                cdsdk::bundledFontPath(config.value("palette", "") == "terminal"
                                           ? L"assets/fonts/Streetlink/StreetlinkDisplay-Black.ttf"
                                           : L"assets/fonts/Aeris/Orbitron-Variable.ttf"));
            for (const auto* f : {latin.get(), cjk.get(), displayFont.get()}) {
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
        if (fontName.empty() || fontName == "Chakra Petch") {
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
        style.family = fontName.empty() || fontName == "Chakra Petch" ? font->family() : selected->family();
        style.locale = locale;
        style.size = size;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        style.wrap = false;
        style.tabularFigures = true;
        style.weight = cybercanvas::FontWeight::Medium;
        return style;
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
    impl_->images.bind(context);
    impl_->canvas.bindGpu(context);
    impl_->icons.bind(context);
    impl_->diagnostics.bind(context);
    impl_->diagnostics.onSample([this] {
        cdsdk::InstanceStats s;
        s.canvas = impl_->canvas.stats();
        s.images = impl_->images.stats();
        s.sharedImages = impl_->images.sharedStoreStats();
        return s;
    });
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
    impl_->images.onChange([this, callback = std::move(callback)] {
        ++impl_->builds;
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
    impl_->ink =
        foreground(settings.get("text_color", &value, sizeof(value)) == CD_OK ? uint32_t(value.integer) : kIvory);
    impl_->accent = settings.get("accent_color", &value, sizeof(value)) == CD_OK ? uint32_t(value.integer) : kRuby;
    impl_->relief =
        settings.get("relief_strength", &value, sizeof(value)) == CD_OK ? std::clamp(value.number / 100., 0., 1.) : 0;
    impl_->background = true;
    if (settings.get("show_background", &value, sizeof(value)) == CD_OK) {
        impl_->background = impl_->folder.starts_with("plant-") || value.integer != 0;
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
        const auto r = item.bounds;
        item.bounds = {v.origin.x - safe.x + r.x * v.scale, v.origin.y - safe.y + r.y * v.scale, r.width * v.scale,
                       r.height * v.scale};
    }
    impl_->controls.items(std::move(transformed), {safe.x, safe.y});
}

Rect Skin::region(std::string_view name) const {
    const auto it = impl_->regions.find(name);
    if (it == impl_->regions.end()) {
        return {};
    }
    return impl_->reflow(it->second);
}

Rect Skin::displayRegion(std::string_view name, CdRect safe) const {
    impl_->arrange(safe);
    const auto v = impl_->artView;
    const auto r = region(name);
    return {v.origin.x - safe.x + r.x * v.scale, v.origin.y - safe.y + r.y * v.scale, r.width * v.scale,
            r.height * v.scale};
}

Point Skin::reflow(Point point) const {
    const auto mapped = impl_->reflow({point.x, point.y, .0001, .0001});
    return {mapped.x, mapped.y};
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
                    for (const auto& layer : impl_->layers) {
                        if (layer.foreground) {
                            impl_->drawLayer(c, layer);
                        }
                    }
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
    for (const auto& l : impl_->layers) {
        if (!l.foreground) {
            if (impl_->config.contains("exclusions")) {
                auto mask = c.path(cybercanvas::FillRule::Alternate);
                const auto add = [&](Rect r) {
                    mask.moveTo({r.x, r.y})
                        .lineTo({r.x + r.width, r.y})
                        .lineTo({r.x + r.width, r.y + r.height})
                        .lineTo({r.x, r.y + r.height})
                        .close();
                };
                add({0, 0, impl_->scene.width + impl_->horizontalExtension, impl_->scene.height});
                for (const auto& j : impl_->config.at("exclusions")) {
                    add(impl_->reflow(box(j)));
                }
                c.masked(mask, [&] {
                    impl_->drawLayer(c, l);
                });
            } else {
                impl_->drawLayer(c, l);
            }
        }
    }
    if (impl_->config.contains("screens")) {
        for (const auto& screen : impl_->config.at("screens")) {
            const auto r = impl_->reflow(box(screen));
            c.gradientBox(r, 0, 0x081215ff, 0x101c20ff);
        }
    }
    if (impl_->config.contains("roundScreens")) {
        for (const auto& screen : impl_->config.at("roundScreens")) {
            const auto r = impl_->reflow(box(screen));
            c.oval(r.x + r.width / 2, r.y + r.height / 2, r.width / 2, r.height / 2, 0x081215ff);
        }
    }
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
        c.box(r.x, r.y, r.width, r.height, 1, terminal() ? 0x081215ff : color);
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
    ink = foreground(ink);
    std::string role, overflow;
    double minimumSize = 0;
    if (impl_->config.contains("styles")) {
        for (const auto& [name, value] : impl_->config.at("styles").items()) {
            const auto registered = region(name);
            if (std::abs(registered.x - r.x) + std::abs(registered.y - r.y) + std::abs(registered.width - r.width) +
                    std::abs(registered.height - r.height) <
                .1) {
                size = value.at("size").get<double>();
                const uint32_t authored = impl_->config.at("defaults").value("text_color", kIvory);
                ink = impl_->ink == authored ? value.at("color").get<uint32_t>() : impl_->ink;
                role = value.value("role", "");
                overflow = value.value("overflow", "");
                minimumSize = value.value("minimumSize", 0.);
                break;
            }
        }
    }
    auto style = impl_->textStyle(c, text, size > 0 ? size : r.height * .77);
    const bool mechanicalDigit =
        impl_->folder.starts_with("spring-clock-") && text.size() == 1 && text.front() >= L'0' && text.front() <= L'9';
    if ((role.find("display") != std::string::npos || mechanicalDigit) &&
        (impl_->fontName.empty() || impl_->fontName == "Chakra Petch")) {
        style.family = impl_->displayFont->family();
        style.weight = cybercanvas::FontWeight::SemiBold;
    }
    auto displayed = text;
    auto fitted = c.fitTextInk(displayed, style, r, references);
    float fittedSize = 0;
    cybercanvas::require(fitted.layout->GetFontSize(0, &fittedSize));
    if (minimumSize > 0 && fittedSize < minimumSize) {
        if (overflow == "optional") {
            return;
        }
        if (overflow == "ellipsis") {
            // Keep descriptive text legible instead of shrinking a long city/event/
            // song title into microprint. Centre the visible, shortened ink too.
            style.size = minimumSize;
            const auto fits = [&](const std::wstring& value) {
                const auto ink = c.measureTextInk(value, style);
                return ink.width <= r.width && ink.height <= r.height;
            };
            size_t lower = 0, upper = std::min<size_t>(displayed.size(), 256);
            while (lower < upper) {
                const size_t middle = (lower + upper + 1) / 2;
                if (fits(displayed.substr(0, middle) + L"…")) {
                    lower = middle;
                } else {
                    upper = middle - 1;
                }
            }
            if (lower && displayed[lower - 1] >= 0xd800 && displayed[lower - 1] <= 0xdbff) {
                --lower;
            }
            displayed = displayed.substr(0, lower) + L"…";
            fitted = c.fitTextInk(displayed, style, r);
        }
    }
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
    label(c, text, r, size, ink);
}

void Skin::metric(Canvas& c, const std::wstring& value, const std::wstring& unit, Rect r, double valueSize,
                  double unitSize, uint32_t ink) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    auto numberStyle = impl_->textStyle(c, value, valueSize);
    auto unitStyle = impl_->textStyle(c, unit, unitSize);
    numberStyle.align = unitStyle.align = cybercanvas::TextAlign::Start;
    numberStyle.vertical = unitStyle.vertical = cybercanvas::VerticalAlign::Top;
    auto number = c.measureTextInk(value, numberStyle);
    auto suffix = c.measureTextInk(unit, unitStyle);
    constexpr double gap = 2;
    const double fit = std::min(
        {1., r.width / (number.width + suffix.width + gap), r.height / std::max(number.height, suffix.height)});
    if (fit < 1) {
        numberStyle.size *= fit * .995;
        unitStyle.size *= fit * .995;
        number = c.measureTextInk(value, numberStyle);
        suffix = c.measureTextInk(unit, unitStyle);
    }
    const double x = r.x + (r.width - number.width - suffix.width - gap) / 2;
    const double bottom = r.y + (r.height + std::max(number.height, suffix.height)) / 2;
    c.ink(foreground(ink));
    const auto draw = [&](const std::wstring& text, const auto& style, Rect bounds, double left) {
        const auto layout = c.nativeTextLayout(text, style, {1000, 1000});
        c.target->DrawTextLayout(D2D1::Point2F(float(left - bounds.x), float(bottom - bounds.height - bounds.y)),
                                 layout.Get(), c.brush.Get());
    };
    draw(value, numberStyle, number, x);
    draw(unit, unitStyle, suffix, x + number.width + gap);
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
        impl_->icons.paint(c, {0, 0, r.width * scale, r.height * scale}, name, foreground(ink));
    });
}

void Skin::button(Canvas& c, Rect r, std::string_view name, bool selected, bool enabled) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    if (terminal()) {
        c.box(r.x, r.y, r.width, r.height, 2, 0x081215ff);
        if (impl_->config.value("buttonStyle", "") != "embedded") {
            c.strokeBox(r, 2, selected ? 0xfbe923ff : 0x52626aff, {.width = .65});
        }
        const double side =
            std::min(r.width, r.height) * (impl_->config.value("buttonStyle", "") == "embedded" ? .85 : .66);
        icon(c, {r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side}, name,
             enabled ? (selected ? 0xfbe923ff : 0xf4f8faff) : 0x64777bff);
        return;
    }
    if (selected) {
        c.box(r.x, r.y, r.width, r.height, 2, 0x91d7e222);
    }
    const double side = std::min(r.width, r.height) * .68;
    icon(c, {r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side}, name,
         enabled ? 0x273e4dff : 0x728690aa);
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
    const std::string slot = "part-" + part.name;
    impl_->images.set(slot, impl_->resolve(part.file), 1, edge, part.source);
    impl_->images.poll();
    const auto snapshot = impl_->images.get(slot);
    if (snapshot.image) {
        auto image = snapshot.image;
        if (name == "glass") {
            if (impl_->glassSource != image->id()) {
                auto pixels = image->pixels();
                for (size_t i = 0; i < pixels.size(); i += 4) {
                    const double a = pixels[i + 3];
                    if (a <= 0) {
                        continue;
                    }
                    const double red = pixels[i + 2] * 255. / a, green = pixels[i + 1] * 255. / a,
                                 blue = pixels[i] * 255. / a;
                    const double keep = red > green + 2 ? 1 - std::clamp((red - blue - 8) / 24., 0., 1.) : 1.;
                    for (unsigned channel = 0; channel < 4; ++channel) {
                        pixels[i + channel] = uint8_t(pixels[i + channel] * keep);
                    }
                }
                impl_->glassSource = image->id();
                impl_->glass =
                    cybercanvas::ImageData::fromPremultipliedBgra(image->width(), image->height(), std::move(pixels));
            }
            image = impl_->glass;
        }
        const auto& spec = impl_->config.at("parts").at(std::string(name));
        const auto paint = [&] {
            const auto draw = [&] {
                c.image(*image, target, {.fit = cybercanvas::ImageFit::Contain, .clip = false});
                if (spec.contains("blank")) {
                    const auto b = box(spec.at("blank"));
                    c.box(target.x + b.x * target.width, target.y + b.y * target.height, b.width * target.width,
                          b.height * target.height, 0, 0x081215ff);
                }
            };
            if (!spec.contains("sourceSilhouettes") || spec.at("sourceSilhouettes").empty()) {
                draw();
                return;
            }
            auto silhouette = c.path();
            for (const auto& contour : spec.at("sourceSilhouettes")) {
                bool first = true;
                for (const auto& point : contour) {
                    const Point p{
                        target.x + (point[0].get<double>() - part.source.x) * target.width / part.source.width,
                        target.y + (point[1].get<double>() - part.source.y) * target.height / part.source.height};
                    if (first) {
                        silhouette.moveTo(p);
                        first = false;
                    } else {
                        silhouette.lineTo(p);
                    }
                }
                silhouette.close();
            }
            c.masked(silhouette, draw);
        };
        if (spec.contains("sourcePolygon") && spec.at("sourcePolygon").is_array()) {
            auto mask = c.path();
            bool first = true;
            for (const auto& p : spec.at("sourcePolygon")) {
                const Point q{target.x + (p[0].get<double>() - part.source.x) * target.width / part.source.width,
                              target.y + (p[1].get<double>() - part.source.y) * target.height / part.source.height};
                if (first) {
                    mask.moveTo(q);
                    first = false;
                } else {
                    mask.lineTo(q);
                }
            }
            mask.close();
            c.masked(mask, paint);
        } else {
            paint();
        }
    }
    if (snapshot.state == cybercanvas::ImageState::Failed && impl_->errors[slot] != snapshot.error) {
        impl_->errors[slot] = snapshot.error;
        if (impl_->report) {
            impl_->report("AERIS POLAR " + slot + ": " + snapshot.error);
        }
    }
}

void Skin::progress(Canvas& c, Rect r, double fraction) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    fraction = std::clamp(fraction, 0., 1.);
    if (terminal()) {
        const double height = std::max(1.5, std::min(4., r.height));
        const double y = r.y + (r.height - height) / 2;
        c.box(r.x, y, r.width, height, height / 2, 0x1d424aff);
        c.box(r.x, y, std::max(0., r.width * fraction), height, height / 2, 0x00e7efff);
        return;
    }
    c.box(r.x, r.y, r.width, r.height, r.height / 2, 0xb7c9c9ff);
    if (fraction > 0) {
        c.box(r.x, r.y, r.width * fraction, r.height, r.height / 2, 0x78aaa4ff);
    }
}

void Skin::photo(Canvas& c, Rect r, const cybercanvas::ImageData& image, double opacity, std::string_view fit) {
    if (r.width > 0 && r.height > 0) {
        c.image(image, r,
                {.fit = fit == "contain" ? cybercanvas::ImageFit::Contain : cybercanvas::ImageFit::Cover,
                 .opacity = opacity});
    }
}

void Skin::clipRegion(Canvas& c, std::string_view name, const std::function<void()>& paint) {
    const auto apertures = impl_->config.find("apertures");
    if (apertures == impl_->config.end() || !apertures->contains(name)) {
        c.clipped(region(name), paint);
        return;
    }
    const auto& points = apertures->at(std::string(name));
    auto path = c.path();
    for (size_t i = 0; i < points.size(); ++i) {
        const auto p = reflow(Point{points[i][0].get<double>(), points[i][1].get<double>()});
        if (i == 0) {
            path.moveTo(p);
        } else {
            path.lineTo(p);
        }
    }
    path.close();
    c.masked(path, paint);
}

void Skin::weather(Canvas& c, Rect r, std::string_view name, std::string_view slot) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    if (terminal()) {
        const double side = std::min(r.width, r.height);
        const Rect face{r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side};
        const bool night = name.find("night") != std::string_view::npos;
        const bool storm = name.starts_with("thunderstorms");
        const bool snow = name == "snow" || name == "snowflake" || name == "sleet";
        const bool wet = name.find("rain") != std::string_view::npos || name == "drizzle" || name == "sleet";
        const bool plainCloud = snow || name == "overcast" || (night && !name.starts_with("clear-") && !storm);
        const std::string_view symbol = name == "not-available"      ? "circle-help"
                                        : name == "fog"              ? "wind"
                                        : storm                      ? "cloud-rain"
                                        : wet                        ? "cloud-rain"
                                        : name.starts_with("clear-") ? (night ? "moon" : "sun")
                                                                     : "cloud-sun";
        // Crisp, monochrome HUD glyphs share the application's Lucide resource cache.
        // Their transparent negative space preserves the authored hologram platform.
        if (plainCloud) {
            if (night) {
                icon(c, {face.x + side * .50, face.y + side * .06, side * .37, side * .37}, "moon", 0x85c9e5ff);
            }
            auto cloud = c.path();
            const auto point = [&](double x, double y) {
                return Point{face.x + x * side, face.y + y * side};
            };
            cloud.moveTo(point(.17, .62))
                .lineTo(point(.12, .53))
                .lineTo(point(.17, .42))
                .lineTo(point(.31, .39))
                .lineTo(point(.37, .25))
                .lineTo(point(.55, .22))
                .lineTo(point(.67, .35))
                .lineTo(point(.80, .38))
                .lineTo(point(.87, .49))
                .lineTo(point(.84, .62))
                .close();
            c.fill(cloud, 0x06343c99);
            c.stroke(cloud, 0x40f3ffff, {.width = std::max(1., side * .025)});
        } else {
            icon(c, {face.x + side * .12, face.y + side * .08, side * .76, side * .76}, symbol, 0x40f3ffff);
        }
        if (storm) {
            icon(c, {face.x + side * .47, face.y + side * .42, side * .32, side * .42}, "zap", 0xf6d848ff);
        } else if (snow) {
            for (double x : {.32, .50, .68}) {
                const double cx = face.x + side * x, cy = face.y + side * .75, radius = side * .045;
                for (unsigned ray = 0; ray < 3; ++ray) {
                    const double a = ray * std::numbers::pi / 3;
                    c.line(cx - std::cos(a) * radius, cy - std::sin(a) * radius, cx + std::cos(a) * radius,
                           cy + std::sin(a) * radius, 0xd7faffff, std::max(.8, side * .015));
                }
            }
        }
        for (double x : {.12, .88}) {
            const double px = face.x + side * x;
            c.line(px, face.y + side * .82, px, face.y + side * .90, 0x00dce999, .8);
        }
        c.line(face.x + side * .12, face.y + side * .90, face.x + side * .88, face.y + side * .90, 0x00dce977, .8);
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

void Skin::gauge(Canvas& c, Rect r, double fraction, const std::wstring& value, const std::wstring& title, uint32_t) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    if (terminal()) {
        const double titleHeight = title.empty() ? 0 : std::clamp(r.height * .22, 9., 13.);
        if (!title.empty()) {
            label(c, title, {r.x, r.y, r.width, titleHeight}, 12, 0x00e7efff);
        }
        if (!value.empty()) {
            auto split = value.find_first_of(L"%°");
            const auto bounds =
                Rect{r.x, r.y + titleHeight + 1, r.width, std::max(1., r.height * .86 - titleHeight - 1)};
            const double numberSize = std::clamp(bounds.height * 1.3, 16., 28.);
            if (split != std::wstring::npos) {
                metric(c, value.substr(0, split), value.substr(split), bounds, numberSize, numberSize * .52,
                       0xf4f8faff);
            } else {
                label(c, value, bounds, numberSize, 0xf4f8faff);
            }
        }
        progress(c, {r.x, r.y + r.height * .90, r.width, std::max(1.5, r.height * .04)}, fraction);
        return;
    }
    if (impl_->folder == "timer-2x2") {
        const double cx = r.x + r.width / 2, cy = r.y + r.height / 2, radius = std::min(r.width, r.height) / 2;
        for (unsigned i = 0; i < 60; ++i) {
            const double a = (i * 6 - 90) * std::numbers::pi / 180, inner = i % 5 ? .91 : .84;
            c.line(cx + std::cos(a) * radius * inner, cy + std::sin(a) * radius * inner,
                   cx + std::cos(a) * radius * .96, cy + std::sin(a) * radius * .96, 0x273e4dcc,
                   radius * (i % 5 ? .009 : .022));
        }
        // The remaining time owns the centre. A short perimeter indicator never
        // sweeps across it; fine dial numerals add no information at this size.
        const double a = (std::clamp(fraction, 0., 1.) * 300 - 90) * std::numbers::pi / 180;
        c.line(cx + std::cos(a) * radius * .79, cy + std::sin(a) * radius * .79, cx + std::cos(a) * radius * .97,
               cy + std::sin(a) * radius * .97, 0x43899aff, 2);
        return;
    }
    const double scale = std::min(r.width, r.height) / 140, cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    const auto point = [&](double degrees, double radius) {
        const double a = degrees * std::numbers::pi / 180;
        return Point{cx + std::cos(a) * radius * scale, cy + std::sin(a) * radius * scale};
    };
    for (unsigned i = 0; i < 29; ++i) {
        const double angle = 135 + i * 270. / 28;
        const auto a = point(angle, i % 4 ? 57 : 53), b = point(angle, 61);
        c.line(a.x, a.y, b.x, b.y, i > 23 ? 0x4b94a4ff : 0xa0b0b4ff, (i % 4 ? 1 : 1.7) * scale);
    }
    const double angle = 135 + std::clamp(fraction, 0., 1.) * 270;
    const auto a = point(angle, 52), b = point(angle, 60);
    c.line(a.x, a.y, b.x, b.y, 0x43899aff, 3.3 * scale);
    if (!title.empty()) {
        label(c, title, {r.x + r.width * .04, r.y + r.height * .12, r.width * .92, r.height * .28},
              std::clamp(r.height * .18, 13., 15.), 0x344f63ff);
    }
    if (!value.empty()) {
        label(c, value, {r.x + r.width * .06, r.y + r.height * .40, r.width * .88, r.height * .34},
              std::clamp(r.height * .34, 16., 30.), kIvory);
    }
}

Size Skin::size() const {
    return {impl_->scene.width + impl_->horizontalExtension, impl_->scene.height};
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

bool Skin::terminal() const {
    return impl_->config.value("palette", "") == "terminal";
}

uint32_t Skin::foreground(uint32_t color) const {
    if (!terminal()) {
        return color;
    }
    switch (color & 0xffffff00) {
        case 0x24394c00:
        case 0x273e4d00:
        case 0x344f6300:
        case 0x526b7900:
            return 0xf3f8fa00 | (color & 255);
        case 0x39788b00:
        case 0x529bac00:
            return 0x00e7ef00 | (color & 255);
        default:
            return color;
    }
}
}  // namespace aerisskin
