#include "plugins/vampire-alice/Skin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>

namespace alice {
namespace {
Rect rect(const nlohmann::json& value) {
    return {value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>(), value.at(3).get<double>()};
}

cybercanvas::ImageRegion imageRegion(Rect bounds) {
    const auto left = uint32_t(std::floor(bounds.x)), top = uint32_t(std::floor(bounds.y));
    return {left, top, uint32_t(std::ceil(bounds.x + bounds.width)) - left,
            uint32_t(std::ceil(bounds.y + bounds.height)) - top};
}

uint32_t imageEdge(Canvas& canvas, Rect bounds, cybercanvas::ImageRegion source) {
    D2D1_MATRIX_3X2_F transform;
    canvas.target->GetTransform(&transform);
    const double width = bounds.width * std::hypot(transform._11, transform._12);
    const double height = bounds.height * std::hypot(transform._21, transform._22);
    const double pixels = std::max(width, height) * canvas.devicePixelRatio();
    // Small buckets avoid excessive oversampling of wide, shallow panels.
    const auto edge = uint32_t(std::clamp(std::ceil(pixels / 32) * 32, 32., 4096.));
    return std::min(edge, std::max(source.width, source.height));
}
}  // namespace

Skin::Skin(const std::filesystem::path& assets) {
    if (!assets.empty()) {
        load(assets);
    }
}

Skin::~Skin() = default;

void Skin::load(const std::filesystem::path& assets) {
    if (assets_ == assets) {
        return;
    }
    std::ifstream file(assets / "skin.json");
    const auto config = nlohmann::json::parse(file);
    crop_ = rect(config.at("crop"));
    folder_ = config.at("folder").get<std::string>();
    columns_ = config.at("columns").get<unsigned>();
    rows_ = config.at("rows").get<unsigned>();
    textured_ = config.value("runtimePlate", false);
    nativeTicks_ = config.value("nativeTicks", false);
    regions_.clear();
    for (const auto& [name, value] : config.at("regions").items()) {
        auto r = rect(value);
        regions_.emplace(name,
                         Rect{r.x * crop_.width, r.y * crop_.height, r.width * crop_.width, r.height * crop_.height});
    }
    assets_ = assets;
    images_.clear();
    art_.reset();
    hardware_.clear();
    if (config.contains("hardware")) {
        for (const auto& [name, value] : config.at("hardware").items()) {
            hardware_[name] = rect(value);
        }
    }
}

void Skin::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    load(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
         "assets");
    images_.bind(context);
    canvas_.bindGpu(context);
    icons_.bind(context);
    controls_.bind(context, std::move(action));
    onChange([host = *context.host, pointer = context.host_context] {
        host.request_redraw(pointer);
    });
}

void Skin::onChange(std::function<void()> callback) {
    changed_ = std::move(callback);
    images_.onChange([this] {
        ++builds_;
        if (changed_) {
            changed_();
        }
    });
}

void Skin::pause(bool paused) {
    images_.pause(paused);
    if (paused) {
        controls_.cancel();
    }
}

bool Skin::poll() {
    const bool changed = images_.poll();
    auto next = images_.get("body").image;
    if (next != art_) {
        art_ = std::move(next);
        ++builds_;
    }
    return changed;
}

bool Skin::pending() const {
    return images_.pending();
}

bool Skin::ready() const {
    return art_ && !pending();
}

uint64_t Skin::cacheBuilds() const {
    return builds_;
}

cdsdk::ImageSlotStats Skin::assetStats() const {
    return images_.stats();
}

void Skin::input(const CdInput& event) {
    controls_.input(event);
}

void Skin::cancelInput() {
    controls_.cancel();
}

void Skin::controls(CdRect safe, const std::vector<cdsdk::Controls::Item>& items) {
    arrange(safe);
    items_ = items;
    auto transformed = items;
    for (auto& item : transformed) {
        item.bounds = {artView_.origin.x - safe.x + item.bounds.x * artView_.scale,
                       artView_.origin.y - safe.y + item.bounds.y * artView_.scale, item.bounds.width * artView_.scale,
                       item.bounds.height * artView_.scale};
    }
    controls_.items(std::move(transformed), {safe.x, safe.y});
}

Rect Skin::region(std::string_view name) const {
    const auto found = regions_.find(name);
    return found == regions_.end() ? Rect{} : found->second;
}

void Skin::arrange(CdRect safe) {
    const double w = std::max(0., safe.width - 12.5), h = std::max(0., safe.height - 12.5);
    artView_.scale = std::max(.001, std::min(w / crop_.width, h / crop_.height));
    artView_.origin = {safe.x + (safe.width - crop_.width * artView_.scale) / 2,
                       safe.y + (safe.height - crop_.height * artView_.scale) / 2};
}

Rect Skin::displayRegion(std::string_view name, CdRect safe) const {
    const double scale = std::max(
        .001, std::min(std::max(0., safe.width - 12.5) / crop_.width, std::max(0., safe.height - 12.5) / crop_.height));
    const auto r = region(name);
    return {(safe.width - crop_.width * scale) / 2 + r.x * scale,
            (safe.height - crop_.height * scale) / 2 + r.y * scale, r.width * scale, r.height * scale};
}

View Skin::view(CdRect safe, Size world) {
    arrange(safe);
    const auto r = region("world");
    const double scale = std::min(r.width / world.width, r.height / world.height) * artView_.scale;
    worldView_ = {{artView_.origin.x + r.x * artView_.scale + (r.width * artView_.scale - world.width * scale) / 2,
                   artView_.origin.y + r.y * artView_.scale + (r.height * artView_.scale - world.height * scale) / 2},
                  std::max(.001, scale)};
    return worldView_;
}

bool Skin::render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& paint) {
    if (!canvas_.valid(frame)) {
        return false;
    }
    canvas_.render(frame, safe, [&](auto& canvas, CdRect area) {
        arrange(area);
        canvas.saved([&] {
            canvas.translate(artView_.origin.x - area.x, artView_.origin.y - area.y);
            canvas.scale(artView_.scale, artView_.scale);
            backdrop(canvas);
            paint(canvas);
        });
    });
    return true;
}

bool Skin::save(const std::filesystem::path& path, const CdFrame& frame) {
    return canvas_.save(path.wstring(), frame);
}

void Skin::backdrop(Canvas& canvas) {
    const auto roi = imageRegion(crop_);
    const Rect full{double(roi.x) - crop_.x, double(roi.y) - crop_.y, double(roi.width), double(roi.height)};
    const auto edge = imageEdge(canvas, full, roi);
    if (images_.set("body", assets_ / "body.png", 1, edge, roi)) {
        art_.reset();
    }
    if (textured_) {
        if (images_.set("matte", assets_ / "matte.png", 1, edge, roi)) {
            matteBrush_.Reset();
            matteBitmap_.Reset();
            matteTarget_.Reset();
        }
    }
    poll();
    if (art_) {
        canvas.clipped({0, 0, crop_.width, crop_.height}, [&] {
            const auto draw = [&] {
                const auto image = [&] {
                    canvas.image(*art_, full, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                };
                if (textured_) {
                    maskedMaterial(canvas, full, {0, 0, crop_.width, crop_.height}, image);
                } else {
                    image();
                }
            };
            const bool radial = folder_.starts_with("radial-") && !textured_;
            const bool columns = folder_.starts_with("spectrum-");
            const bool cutouts = regions_.contains("cutout0");
            if (radial || columns || cutouts) {
                const auto r = region("plot");
                const double cx = r.x + r.width / 2, cy = r.y + r.height / 2, radius = std::min(r.width, r.height) * .5;
                auto mask = canvas.path();
                mask.moveTo({0, 0})
                    .lineTo({crop_.width, 0})
                    .lineTo({crop_.width, crop_.height})
                    .lineTo({0, crop_.height})
                    .close();
                if (radial) {
                    mask.moveTo({cx + radius, cy})
                        .arcTo({cx - radius, cy}, {radius, radius})
                        .arcTo({cx + radius, cy}, {radius, radius})
                        .close();
                }
                for (const auto& [name, hole] : regions_) {
                    if (name.starts_with("cutout") || (columns && name == "motion")) {
                        mask.moveTo({hole.x, hole.y})
                            .lineTo({hole.x + hole.width, hole.y})
                            .lineTo({hole.x + hole.width, hole.y + hole.height})
                            .lineTo({hole.x, hole.y + hole.height})
                            .close();
                    }
                }
                canvas.masked(mask, draw);
            } else {
                draw();
            }
        });
    }
}

void Skin::artLayer(Canvas& canvas, const std::function<void(Canvas&)>& paint) {
    canvas.saved([&] {
        canvas.translate((artView_.origin.x - worldView_.origin.x) / worldView_.scale,
                         (artView_.origin.y - worldView_.origin.y) / worldView_.scale);
        canvas.scale(artView_.scale / worldView_.scale, artView_.scale / worldView_.scale);
        paint(canvas);
    });
}

void Skin::worldBackdrop(Canvas& canvas, bool clear) {
    artLayer(canvas, [&](auto&) {
        const auto motion = region("motion");
        const auto hole = motion.width > 0 ? motion : region("world");
        if (!clear) {
            backdrop(canvas);
            return;
        }
        // The art is immutable. Omit only its moving-work region at runtime so
        // baked demonstration balls/digits never coexist with the real model.
        for (auto r : {Rect{0, 0, crop_.width, hole.y}, Rect{0, hole.y, hole.x, hole.height},
                       Rect{hole.x + hole.width, hole.y, crop_.width - hole.x - hole.width, hole.height},
                       Rect{0, hole.y + hole.height, crop_.width, crop_.height - hole.y - hole.height}}) {
            if (r.width > 0 && r.height > 0) {
                canvas.clipped(r, [&] {
                    backdrop(canvas);
                });
            }
        }
    });
}

void Skin::clear(Canvas& canvas, Rect r, uint32_t color, bool force) {
    if (textured_ && !force) {
        return;
    }
    if (r.width > 0 && r.height > 0) {
        canvas.box(r.x, r.y, r.width, r.height, std::min(r.width, r.height) * .06, color);
    }
}

void Skin::label(Canvas& canvas, const std::wstring& text, Rect r, double size, uint32_t ink, bool fill) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    if (fill && !textured_) {
        clear(canvas, r);
    }
    cybercanvas::TextStyle style;
    style.family = L"Times New Roman";
    style.locale = L"zh-CN";
    style.size = size > 0 ? size : r.height * .77;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    style.wrap = false;
    style.tabularFigures = true;
    const auto title = region("title"), artist = region("artist");
    const auto same = [&](Rect a) {
        return a.x == r.x && a.y == r.y && a.width == r.width && a.height == r.height;
    };
    const bool mediaText = folder_.starts_with("player-") && (same(title) || same(artist));
    if (mediaText && folder_ != "player-2x2") {
        style.align = cybercanvas::TextAlign::Start;
    }
    if (mediaText) {
        auto natural = style;
        natural.align = cybercanvas::TextAlign::Start;
        natural.vertical = cybercanvas::VerticalAlign::Top;
        const auto visible = canvas.measureTextInk(text, natural);
        if (visible.width > r.width) {
            natural.ellipsis = true;
            canvas.clipped(r, [&] {
                canvas.text(text, natural,
                            {r.x, r.y + (r.height - visible.height) / 2 - visible.y, r.width,
                             std::max(r.height, style.size * 2)},
                            ink);
            });
            return;
        }
    }
    const bool numeric = std::any_of(text.begin(), text.end(), [](wchar_t c) {
        return c >= L'0' && c <= L'9';
    });
    if (textured_ && numeric && text.size() < 14 && !folder_.starts_with("calendar-") && folder_ != "lunar-4x4") {
        style.weight = cybercanvas::FontWeight::SemiBold;
    }
    std::wstring reference = text;
    for (auto& glyph : reference) {
        if (glyph >= L'0' && glyph <= L'9') {
            glyph = L'8';
        }
    }
    const std::array<std::wstring, 1> references{reference};
    const auto fitted = canvas.fitTextInk(text, style, r, references, .96);
    canvas.clipped(r, [&] {
        if (textured_ && ink == kIvory) {
            canvas.ink(0x100609cc);
            const double shadow = style.size * .035;
            canvas.target->DrawTextLayout(
                D2D1::Point2F(float(fitted.origin.x + shadow), float(fitted.origin.y + shadow)), fitted.layout.Get(),
                canvas.brush.Get());
            const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xfffcf3ff)},
                                             {.48f, cybercanvas::color(0xf5ead3ff)},
                                             {1, cybercanvas::color(0xe3d2b0ff)}};
            const auto gradient =
                canvas.linearGradient({r.x, fitted.bounds.y}, {r.x, fitted.bounds.y + fitted.bounds.height}, stops);
            canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)),
                                          fitted.layout.Get(), gradient.Get());
            return;
        }
        canvas.ink(ink);
        canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)),
                                      fitted.layout.Get(), canvas.brush.Get());
    });
}

void Skin::button(Canvas& canvas, Rect r, std::string_view icon, bool selected, bool enabled) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    const double side = std::min(r.width, r.height);
    const Rect face{r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side};
    double feedback = 0;
    for (const auto& item : items_) {
        const auto bounds = item.bounds;
        if (bounds.x == r.x && bounds.y == r.y && bounds.width == r.width && bounds.height == r.height) {
            if (enabled) {
                feedback = controls_.visual(item.id).feedback;
            }
        }
    }
    const double cx = face.x + side / 2, cy = face.y + side / 2;
    const bool active = selected || feedback > 0;
    if (!textured_) {
        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(active ? 0x881a35ff : 0x521321ff)},
                                         {.48f, cybercanvas::color(active ? 0x5a1026ff : 0x320d18ff)},
                                         {1, cybercanvas::color(0x180b10ff)}};
        const auto gradient =
            canvas.radialGradient({cx - side * .08, cy - side * .13}, {side * .61, side * .61}, stops);
        canvas.target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(float(cx), float(cy)), float(side * .40), float(side * .40)), gradient.Get());
    } else if (active) {
        canvas.oval(cx, cy, side * .40, side * .40, 0xff809c33);
    }
    const double pad = side * .27;
    icons_.paint(canvas, {face.x + pad, face.y + pad, side - 2 * pad, side - 2 * pad}, icon,
                 enabled ? kIvory : 0x8d8078ff);
}

void Skin::progress(Canvas& canvas, Rect r, double fraction) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    fraction = std::clamp(fraction, 0., 1.);
    if (textured_ && folder_.starts_with("hourglass")) {
        constexpr unsigned count = 20;
        for (unsigned i = 0; i < unsigned(std::ceil(fraction * count)); ++i) {
            const Rect led{r.x + (i + .12) * r.width / count, r.y + r.height * .09, r.width * .76 / count,
                           r.height * .82};
            canvas.gradientBox(led, .6, 0xff5c7aff, kRuby);
            canvas.box(led.x + led.width * .22, led.y + led.height * .12, led.width * .25, led.height * .76, .2,
                       0xffb0c699);
        }
        return;
    }
    canvas.box(r.x, r.y, r.width, r.height, r.height / 2, 0x120d10ff);
    canvas.strokeBox(r, r.height / 2, 0x9e958488, {.width = .8});
    if (fraction > 0) {
        canvas.gradientBox({r.x, r.y, r.width * fraction, r.height}, r.height / 2, 0xff5a78ff, kRuby);
    }
    if (textured_ && folder_.starts_with("player-")) {
        const double radius = r.height * .92, cx = r.x + r.width * fraction, cy = r.y + r.height / 2;
        if (folder_ == "player-4x1") {
            const double side = radius * 1.7;
            auto diamond = canvas.path();
            diamond.moveTo({cx, cy - side})
                .lineTo({cx + side, cy})
                .lineTo({cx, cy + side})
                .lineTo({cx - side, cy})
                .close();
            canvas.fill(diamond, 0xd3bea0ff);
            canvas.stroke(diamond, 0x554135ff, {.width = 1.1});
            auto ruby = canvas.path();
            ruby.moveTo({cx, cy - side * .67})
                .lineTo({cx + side * .52, cy})
                .lineTo({cx, cy + side * .67})
                .lineTo({cx - side * .52, cy})
                .close();
            canvas.fill(ruby, kRuby);
            canvas.line(cx, cy - side * .55, cx - side * .34, cy, 0xffd6cbaa, 1);
            return;
        }
        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xfff7dcff)},
                                         {.35f, cybercanvas::color(0xcbb79dff)},
                                         {1, cybercanvas::color(0x5b383bff)}};
        const auto metal =
            canvas.radialGradient({cx - radius * .25, cy - radius * .25}, {radius * 1.5, radius * 1.5}, stops);
        canvas.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(cx), float(cy)), float(radius), float(radius)),
                                   metal.Get());
        canvas.oval(cx, cy, radius, radius, 0xe6d5b9cc, .9);
    }
}

void Skin::icon(Canvas& canvas, Rect bounds, std::string_view name, uint32_t ink) {
    icons_.paint(canvas, bounds, name, ink);
}

void Skin::weather(Canvas& canvas, Rect bounds, std::string_view name, std::string_view slot) {
    if (name == "not-available") {
        images_.erase(std::string(slot));
        icons_.paint(canvas, bounds, "cloud-off", kSilver);
        return;
    }
    if (textured_) {
        const bool night = name.find("night") != std::string_view::npos;
        const bool wet = name.find("rain") != std::string_view::npos || name.find("drizzle") != std::string_view::npos;
        const bool snow = name.find("snow") != std::string_view::npos || name.find("sleet") != std::string_view::npos;
        const bool cloudy = wet || snow || name.find("overcast") != std::string_view::npos || name == "cloudy";
        const Rect source = cloudy  ? Rect{1377, 325, 121, 82}
                            : night ? Rect{296, 703, 140, 116}
                                    : Rect{583, 281, 194, 162};
        const auto region = imageRegion(source);
        images_.set(std::string(slot), assets_ / "illustrations.png", 1, imageEdge(canvas, bounds, region), region);
        if (const auto art = images_.get(std::string(slot)).image) {
            const double scale = std::min(bounds.width / source.width, bounds.height / source.height);
            const Rect target{bounds.x + (bounds.width - source.width * scale) / 2,
                              bounds.y + (bounds.height - source.height * scale) / 2, source.width * scale,
                              source.height * scale};
            canvas.clipped(target, [&] {
                canvas.image(*art, target, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            });
            if (wet || snow) {
                icons_.paint(canvas,
                             {bounds.x + bounds.width * .2, bounds.y + bounds.height * .65, bounds.width * .6,
                              bounds.height * .35},
                             snow ? "snowflake" : "droplets", 0x91b7d5ff);
            }
        }
        return;
    }
    images_.set(std::string(slot), assets_ / "icons" / (std::string(name) + ".png"), 0,
                imageEdge(canvas, bounds, {0, 0, 256, 256}));
    if (const auto image = images_.get(std::string(slot)).image) {
        canvas.image(*image, bounds, {.fit = cybercanvas::ImageFit::Contain});
    }
}

void Skin::photo(Canvas& canvas, Rect r, const cybercanvas::ImageData& image, double opacity, std::string_view fit) {
    canvas.image(
        image, r,
        {.fit = fit == "contain" ? cybercanvas::ImageFit::Contain : cybercanvas::ImageFit::Cover, .opacity = opacity});
}

void Skin::maskedMaterial(Canvas& canvas, Rect imageBounds, Rect clip, const std::function<void()>& paint) {
    const auto matte = images_.get("matte").image;
    if (!matte) {
        return;
    }
    if (matteTarget_.Get() != canvas.target.Get() || matteId_ != matte->id()) {
        matteBitmap_.Reset();
        matteBrush_.Reset();
        matteTarget_ = canvas.target;
        auto lease = canvas.resourceAccount().acquire(uint64_t(matte->width()) * matte->height() * 4);
        cybercanvas::ComPtr<ID2D1Bitmap> bitmap;
        cybercanvas::require(canvas.target->CreateBitmap(
            D2D1::SizeU(matte->width(), matte->height()), matte->pixels().data(), matte->stride(),
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            bitmap.GetAddressOf()));
        cybercanvas::retainTextureLease(bitmap.Get(), lease.Get());
        matteBitmap_ = {bitmap.Get(), lease.Get()};
        cybercanvas::require(canvas.target->CreateBitmapBrush(matteBitmap_.Get(), matteBrush_.GetAddressOf()));
        matteId_ = matte->id();
    }
    matteBrush_->SetTransform(D2D1::Matrix3x2F::Scale(float(imageBounds.width / matte->width()),
                                                      float(imageBounds.height / matte->height())) *
                              D2D1::Matrix3x2F::Translation(float(imageBounds.x), float(imageBounds.y)));
    const auto layer = D2D1::LayerParameters(
        D2D1::RectF(float(clip.x), float(clip.y), float(clip.x + clip.width), float(clip.y + clip.height)), nullptr,
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, matteBrush_.Get(), D2D1_LAYER_OPTIONS_NONE);
    canvas.target->PushLayer(layer, nullptr);
    paint();
    canvas.target->PopLayer();
}

void Skin::part(Canvas& canvas, std::string_view name, Rect target, bool spring) {
    const auto hardware = hardware_.find(name);
    if (hardware != hardware_.end()) {
        const auto r = hardware->second;
        const auto region = imageRegion(r);
        const std::string slot = "hardware:" + std::string(name);
        images_.set(slot, assets_ / "hardware.png", 1, imageEdge(canvas, target, region), region);
        if (const auto image = images_.get(slot).image) {
            const double scale = std::min(target.width / r.width, target.height / r.height);
            const Rect box =
                spring ? target
                       : Rect{target.x + (target.width - r.width * scale) / 2,
                              target.y + (target.height - r.height * scale) / 2, r.width * scale, r.height * scale};
            const double sx = box.width / r.width, sy = box.height / r.height;
            canvas.clipped(box, [&] {
                const Rect full{box.x + (double(region.x) - r.x) * sx, box.y + (double(region.y) - r.y) * sy,
                                region.width * sx, region.height * sy};
                const auto draw = [&] {
                    canvas.image(*image, full, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                };
                if (folder_ == "spring-clock-4x2" && name == "card") {
                    auto silhouette = canvas.path();
                    for (const auto p : std::array<Point, 8>{{{.5, 0},
                                                              {.78, .08},
                                                              {.98, .13},
                                                              {.96, .87},
                                                              {.5, 1},
                                                              {.04, .87},
                                                              {.02, .13},
                                                              {.22, .08}}}) {
                        const Point point{box.x + p.x * box.width, box.y + p.y * box.height};
                        if (p.x == .5 && p.y == 0) {
                            silhouette.moveTo(point);
                        } else {
                            silhouette.lineTo(point);
                        }
                    }
                    silhouette.close();
                    canvas.masked(silhouette, draw);
                } else {
                    draw();
                }
            });
        }
        return;
    }
    const auto r = region(name);
    if (!art_ || r.width <= 0 || r.height <= 0) {
        return;
    }
    const double scale = std::min(target.width / r.width, target.height / r.height);
    const Rect destination{target.x + (target.width - r.width * scale) / 2,
                           target.y + (target.height - r.height * scale) / 2, r.width * scale, r.height * scale};
    canvas.clipped(destination, [&] {
        const auto source = imageRegion(crop_);
        canvas.image(
            *art_,
            {destination.x + (double(source.x) - crop_.x - r.x) * scale,
             destination.y + (double(source.y) - crop_.y - r.y) * scale, source.width * scale, source.height * scale},
            {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    });
}

Rect Skin::worldRegion(std::string_view name, Size world) const {
    const auto plane = region("world"), r = region(name);
    const double scale = std::min(plane.width / world.width, plane.height / world.height);
    const Point origin{plane.x + (plane.width - world.width * scale) / 2,
                       plane.y + (plane.height - world.height * scale) / 2};
    return {(r.x - origin.x) / scale, (r.y - origin.y) / scale, r.width / scale, r.height / scale};
}

void Skin::movingPart(Canvas& canvas, std::string_view name, Point delta, double angle, std::string_view pivot) {
    const auto found = hardware_.find(name);
    if (found == hardware_.end()) {
        return;
    }
    const auto src = found->second;
    const Rect r{src.x - crop_.x, src.y - crop_.y, src.width, src.height};
    const auto anchor = region(pivot);
    const Point center = pivot.empty() ? Point{r.x + r.width / 2, r.y + r.height / 2} : Point{anchor.x, anchor.y};
    const double scale = worldView_.scale / artView_.scale;
    artLayer(canvas, [&](auto& art) {
        art.saved([&] {
            art.translate(center.x + delta.x * scale, center.y + delta.y * scale);
            art.rotate(angle);
            part(art, name, {r.x - center.x, r.y - center.y, r.width, r.height});
        });
    });
}

void Skin::gauge(Canvas& canvas, Rect r, double fraction, const std::wstring& value, const std::wstring& title,
                 uint32_t accent) {
    const double radius = std::min(r.width, r.height) * .49, cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    if (!textured_) {
        canvas.oval(cx, cy, radius, radius, 0x130e13ff);
    }
    if (!nativeTicks_) {
        constexpr unsigned divisions = 60;
        for (unsigned i = 0; i <= divisions; ++i) {
            const double angle = (-90. + i * 270. / divisions) * std::numbers::pi / 180;
            const auto color = 0x88786aff;
            const bool major = i % 6 == 0;
            const double inner = major ? .82 : .89;
            canvas.line(cx + std::cos(angle) * radius * inner, cy + std::sin(angle) * radius * inner,
                        cx + std::cos(angle) * radius * .96, cy + std::sin(angle) * radius * .96, color,
                        radius * (major ? .026 : .012));
        }
    }
    if (fraction > 0) {
        auto arc = canvas.path();
        const double start = -std::numbers::pi / 2;
        for (unsigned i = 0; i <= 96; ++i) {
            const double angle = start + std::clamp(fraction, 0., 1.) * std::numbers::pi * 1.5 * i / 96;
            const Point point{cx + std::cos(angle) * radius * .88, cy + std::sin(angle) * radius * .88};
            if (i) {
                arc.lineTo(point);
            } else {
                arc.moveTo(point);
            }
        }
        const double thickness = radius * (folder_ == "timer-2x2" ? .10 : .16);
        canvas.stroke(arc, accent, {.width = thickness});
        canvas.stroke(arc, (accent & 0xffffff00) | 0x66, {.width = thickness * .45});
    }
    if (value.empty() && title.empty()) {
        return;
    }
    const double titleOffset = radius < 75 ? .24 : .32;
    label(canvas, title, {cx - radius * .70, cy - radius * (titleOffset + .15), radius * 1.4, radius * .3},
          radius * .28, kIvory, false);
    label(canvas, value, {cx - radius * .88, cy - radius * .09, radius * 1.76, radius * .6}, radius * .60, kIvory,
          false);
    const double angle = (-90. + std::clamp(fraction, 0., 1.) * 270) * std::numbers::pi / 180;
    canvas.saved([&] {
        canvas.translate(cx, cy);
        canvas.rotate(angle * 180 / std::numbers::pi);
        auto needle = canvas.path();
        needle.moveTo({radius * .24, -radius * .021})
            .lineTo({radius * .88, 0})
            .lineTo({radius * .24, radius * .021})
            .close();
        canvas.fill(needle, 0xead5aeff);
        canvas.line(radius * .28, 0, radius * .85, 0, 0xfff5d8ff, radius * .012);
    });
}

bool Skin::textured() const {
    return textured_;
}

Size Skin::size() const {
    return {crop_.width, crop_.height};
}

unsigned Skin::columns() const {
    return columns_;
}

unsigned Skin::rows() const {
    return rows_;
}

const std::string& Skin::folder() const {
    return folder_;
}
}  // namespace alice
