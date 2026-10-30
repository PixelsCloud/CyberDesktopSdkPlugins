#include "plugins/songyun-desk/Skin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>

namespace songyun {
namespace {
Rect rect(const nlohmann::json& value) {
    return {value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>(), value.at(3).get<double>()};
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
    source_ = {config.at("imageSize").at(0).get<double>(), config.at("imageSize").at(1).get<double>()};
    folder_ = config.at("folder").get<std::string>();
    columns_ = config.at("columns").get<unsigned>();
    rows_ = config.at("rows").get<unsigned>();
    inset_ = std::max(6., config.value("renderInset", 7.));
    textured_ = config.value("runtimePlate", false);
    nativeTicks_ = config.value("nativeTicks", false);
    regions_.clear();
    for (const auto& [name, value] : config.at("regions").items()) {
        auto r = rect(value);
        regions_.emplace(name,
                         Rect{r.x * crop_.width, r.y * crop_.height, r.width * crop_.width, r.height * crop_.height});
    }
    assets_ = assets;
    hasPlate_ = config.contains("plateSha256");
    if (config.contains("hardware")) {
        if (config.contains("hardwareSize")) {
            hardwareSize_ = {config.at("hardwareSize").at(0).get<double>(),
                             config.at("hardwareSize").at(1).get<double>()};
        }
        double left = hardwareSize_.width, top = hardwareSize_.height, right = 0, bottom = 0;
        for (const auto& [name, value] : config.at("hardware").items()) {
            const auto r = rect(value);
            hardware_[name] = r;
            left = std::min(left, r.x);
            top = std::min(top, r.y);
            right = std::max(right, r.x + r.width);
            bottom = std::max(bottom, r.y + r.height);
        }
        hardwareCrop_ = {left, top, right - left, bottom - top};
    }
}

void Skin::requestImages(double dpi) {
    const auto request = [&](const char* slot, Rect bounds) {
        const auto now = GetTickCount64();
        const auto image = images_.get(slot);
        if (image.state == cybercanvas::ImageState::Failed && host_.log && reportedImages_.insert(slot).second) {
            const auto message = std::string("Songyun image failed: ") + folder_ + "/" + slot + ": " + image.error;
            host_.log(hostContext_, 2, message.data(), uint32_t(message.size()));
        }
        if (image.state == cybercanvas::ImageState::Failed && now >= retryAfter_[slot]) {
            images_.erase(slot);
            retryAfter_[slot] = now + 2000;
        }
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(bounds.width, bounds.height) * artView_.scale * dpi / 64) * 64, 64., 4096.));
        const cybercanvas::ImageRegion region{uint32_t(bounds.x), uint32_t(bounds.y), uint32_t(bounds.width),
                                              uint32_t(bounds.height)};
        images_.set(slot, assets_ / (std::string(slot) + ".png"), 1, edge, region);
    };
    request("body", crop_);
    if (hasPlate_) {
        request("plate", crop_);
    }
    if (!hardware_.empty()) {
        request("hardware", hardwareCrop_);
    }
}

void Skin::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    host_ = *context.host;
    hostContext_ = context.host_context;
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
    images_.onChange(changed_);
}

void Skin::pause(bool paused) {
    images_.pause(paused);
    if (paused) {
        controls_.cancel();
    }
}

bool Skin::poll() {
    const bool changed = images_.poll();
    for (const auto* slot : {"body", "plate", "hardware"}) {
        const auto image = images_.get(slot);
        if (image.state == cybercanvas::ImageState::Failed && host_.log && reportedImages_.insert(slot).second) {
            const auto message = std::string("Songyun image failed: ") + folder_ + "/" + slot + ": " + image.error;
            host_.log(hostContext_, 2, message.data(), uint32_t(message.size()));
        }
        if (image.state == cybercanvas::ImageState::Ready) {
            reportedImages_.erase(slot);
        }
    }
    const auto update = [&](auto& current, const char* slot) {
        auto next = images_.get(slot).image;
        if (next && next != current) {
            current = std::move(next);
            ++builds_;
        }
    };
    // Compatible layers may be painted before every asynchronous atlas is ready.
    // Every arriving texture invalidates consumers such as cached clock cards.
    update(art_, "body");
    update(plate_, "plate");
    update(hardwareImage_, "hardware");
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
    const double w = std::max(0., safe.width - inset_ * 2), h = std::max(0., safe.height - inset_ * 2);
    artView_.scale = std::max(.001, std::min(w / crop_.width, h / crop_.height));
    artView_.origin = {safe.x + (safe.width - crop_.width * artView_.scale) / 2,
                       safe.y + (safe.height - crop_.height * artView_.scale) / 2};
}

Rect Skin::displayRegion(std::string_view name, CdRect safe) const {
    const double scale = std::max(.001, std::min(std::max(0., safe.width - inset_ * 2) / crop_.width,
                                                 std::max(0., safe.height - inset_ * 2) / crop_.height));
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
        requestImages(frame.device_pixel_ratio);
        poll();
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
    requestImages(canvas.devicePixelRatio());
    poll();
    if (!art_) {
        return;
    }
    if (backdropMask_) {
        Microsoft::WRL::ComPtr<ID2D1Factory> owner;
        backdropMask_->GetFactory(&owner);
        if (owner != canvas.factory) {
            backdropMask_.Reset();
        }
    }
    if (!backdropMask_ || maskWithPlate_ != bool(plate_)) {
        Microsoft::WRL::ComPtr<ID2D1Geometry> removed;
        const auto add = [&](Rect r, bool circle) {
            auto hole = canvas.path();
            if (circle) {
                const double cy = r.y + r.height / 2;
                hole.moveTo({r.x + r.width, cy})
                    .arcTo({r.x, cy}, {r.width / 2, r.height / 2})
                    .arcTo({r.x + r.width, cy}, {r.width / 2, r.height / 2})
                    .close();
            } else {
                hole.moveTo({r.x, r.y})
                    .lineTo({r.x + r.width, r.y})
                    .lineTo({r.x + r.width, r.y + r.height})
                    .lineTo({r.x, r.y + r.height})
                    .close();
            }
            auto* geometry = hole.geometry(canvas.factory.Get());
            if (!removed) {
                removed = geometry;
                return;
            }
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> combined;
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(canvas.factory->CreatePathGeometry(&combined)) || FAILED(combined->Open(&sink)) ||
                FAILED(removed->CombineWithGeometry(geometry, D2D1_COMBINE_MODE_UNION, nullptr, sink.Get())) ||
                FAILED(sink->Close())) {
                throw std::runtime_error("Could not combine Songyun live surface masks");
            }
            removed = combined;
        };
        if (folder_.starts_with("radial-")) {
            const auto plot = region("plot"), hub = region("hubCircle");
            if (hub.width > 0 && hub.height > 0) {
                Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> outside, inside;
                Microsoft::WRL::ComPtr<ID2D1PathGeometry> annulus;
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                cybercanvas::require(canvas.factory->CreateEllipseGeometry(
                    D2D1::Ellipse(D2D1::Point2F(float(plot.x + plot.width / 2), float(plot.y + plot.height / 2)),
                                  float(plot.width / 2), float(plot.height / 2)),
                    &outside));
                cybercanvas::require(canvas.factory->CreateEllipseGeometry(
                    D2D1::Ellipse(D2D1::Point2F(float(hub.x + hub.width / 2), float(hub.y + hub.height / 2)),
                                  float(hub.width / 2), float(hub.height / 2)),
                    &inside));
                cybercanvas::require(canvas.factory->CreatePathGeometry(&annulus));
                cybercanvas::require(annulus->Open(&sink));
                cybercanvas::require(
                    outside->CombineWithGeometry(inside.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get()));
                cybercanvas::require(sink->Close());
                removed = annulus;
            } else {
                add(plot, true);
            }
        }
        for (const auto& [name, r] : regions_) {
            if (name.starts_with("cutout") || (folder_.starts_with("spectrum-") && name == "motion") ||
                (plate_ && name.starts_with("surface"))) {
                add(r, name.ends_with("Circle"));
            }
        }
        // Remove only the preceding atlas row's stray fragment, as in the approved
        // normalized design. This is outside the weather component's own silhouette.
        if (folder_ == "weather-4x2") {
            add({crop_.width * .83, 0, crop_.width * .17, crop_.height * .04}, false);
        }
        auto outer = canvas.path();
        outer.moveTo({0, 0})
            .lineTo({crop_.width, 0})
            .lineTo({crop_.width, crop_.height})
            .lineTo({0, crop_.height})
            .close();
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(canvas.factory->CreatePathGeometry(&backdropMask_)) || FAILED(backdropMask_->Open(&sink))) {
            throw std::runtime_error("Could not create Songyun body mask");
        }
        const auto result =
            removed ? outer.geometry(canvas.factory.Get())
                          ->CombineWithGeometry(removed.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get())
                    : outer.geometry(canvas.factory.Get())
                          ->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, nullptr, sink.Get());
        if (FAILED(result) || FAILED(sink->Close())) {
            throw std::runtime_error("Could not close Songyun body mask");
        }
        maskWithPlate_ = bool(plate_);
    }
    canvas.masked(backdropMask_.Get(), [&] {
        canvas.image(*art_, {0, 0, crop_.width, crop_.height}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    });
    if (plate_) {
        canvas.clipped({0, 0, crop_.width, crop_.height}, [&] {
            for (const auto& [name, surface] : regions_) {
                if (!name.starts_with("surface")) {
                    continue;
                }
                const auto patch = [&] {
                    canvas.image(*plate_, {0, 0, crop_.width, crop_.height},
                                 {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                };
                if (name.ends_with("Circle")) {
                    auto mask = canvas.path();
                    const double cy = surface.y + surface.height / 2;
                    mask.moveTo({surface.x + surface.width, cy})
                        .arcTo({surface.x, cy}, {surface.width / 2, surface.height / 2})
                        .arcTo({surface.x + surface.width, cy}, {surface.width / 2, surface.height / 2})
                        .close();
                    canvas.masked(mask, patch);
                } else {
                    canvas.clipped(surface, patch);
                }
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
    if (plate_ && !force) {
        return;
    }
    if (r.width > 0 && r.height > 0) {
        if (!force) {
            color = folder_.starts_with("music-") || folder_.starts_with("clock-") ? kInk : kPaper;
        }
        canvas.box(r.x, r.y, r.width, r.height, std::min(r.width, r.height) * .06, color);
    }
}

void Skin::label(Canvas& canvas, const std::wstring& text, Rect r, double size, uint32_t ink, bool fill) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    if (fill && !plate_) {
        clear(canvas, r);
    }
    cybercanvas::TextStyle style;
    style.family = std::any_of(text.begin(), text.end(),
                               [](wchar_t c) {
                                   return c >= 0x2e80;
                               })
                       ? L"SimSun"
                       : L"Times New Roman";
    style.locale = L"zh-CN";
    style.size = size > 0 ? size : r.height * .77;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    style.wrap = false;
    style.tabularFigures = true;
    std::wstring reference = text;
    for (auto& glyph : reference) {
        if (glyph >= L'0' && glyph <= L'9') {
            glyph = L'8';
        }
    }
    const std::array<std::wstring, 1> references{reference};
    const auto fitted = canvas.fitTextInk(text, style, r, references, .96);
    canvas.clipped(r, [&] {
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
    const bool calendar = folder_ == "calendar-4x2" || folder_ == "lunar-4x4";
    if (calendar) {
        canvas.box(face.x, face.y, side, side, side * .13, active ? 0x7f9d7fff : 0xb5c8a4ff);
        canvas.strokeBox(face, side * .13, 0x9b9468aa, {.width = side * .025});
    }
    if (folder_.starts_with("album-") && !active) {
        return;
    }
    if (!textured_) {
        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(active ? 0x881a35ff : 0x521321ff)},
                                         {.48f, cybercanvas::color(active ? 0x5a1026ff : 0x320d18ff)},
                                         {1, cybercanvas::color(0x180b10ff)}};
        const auto gradient =
            canvas.radialGradient({cx - side * .08, cy - side * .13}, {side * .61, side * .61}, stops);
        canvas.target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(float(cx), float(cy)), float(side * .40), float(side * .40)), gradient.Get());
    } else if (active) {
        canvas.oval(cx, cy, side * .40, side * .40, 0x86b49a44);
    }
    const double pad = side * .27;
    const auto ink = folder_.starts_with("timer-") && (icon == "play" || icon == "pause") ? kPaper : kIvory;
    icons_.paint(canvas, {face.x + pad, face.y + pad, side - 2 * pad, side - 2 * pad}, icon,
                 enabled ? ink : 0x8d928078);
}

void Skin::progress(Canvas& canvas, Rect r, double fraction) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    fraction = std::clamp(fraction, 0., 1.);
    if (folder_.starts_with("air-")) {
        canvas.box(r.x, r.y, r.width, r.height, r.height / 2, 0xd9d1baff);
        if (folder_ == "air-4x1" && r.x == region("progress0").x) {
            constexpr std::array<uint32_t, 4> colors{0x599756ff, 0xddce64ff, 0xdd9951ff, 0xb94236ff};
            for (unsigned i = 0; i < colors.size(); ++i) {
                canvas.box(r.x + r.width * i / colors.size(), r.y, r.width / colors.size(), r.height, 0, colors[i]);
            }
            const double x = r.x + r.width * fraction;
            canvas.line(x, r.y - r.height * .15, x, r.y + r.height * 1.15, kIvory, r.height * .15);
        } else if (fraction > 0) {
            canvas.gradientBox({r.x, r.y, r.width * fraction, r.height}, r.height / 2, 0x9bb987ff, 0x56885bff);
        }
        canvas.strokeBox(r, r.height / 2, 0xa69b7e99, {.width = r.height * .07});
        return;
    }
    if (textured_ && folder_.starts_with("hourglass")) {
        constexpr unsigned count = 20;
        for (unsigned i = 0; i < unsigned(std::ceil(fraction * count)); ++i) {
            const Rect led{r.x + (i + .12) * r.width / count, r.y + r.height * .09, r.width * .76 / count,
                           r.height * .82};
            canvas.gradientBox(led, .6, 0x9bb9a1ff, 0x52745bff);
            canvas.box(led.x + led.width * .22, led.y + led.height * .12, led.width * .25, led.height * .76, .2,
                       0xe4efd699);
        }
        return;
    }
    canvas.box(r.x, r.y, r.width, r.height, r.height / 2, 0x30271cff);
    canvas.strokeBox(r, r.height / 2, 0x9e958488, {.width = .8});
    if (fraction > 0) {
        canvas.gradientBox({r.x, r.y, r.width * fraction, r.height}, r.height / 2, 0xd8e8c5ff, 0x8bad8eff);
    }
    if (textured_ && folder_.starts_with("player-")) {
        const double radius = r.height * .70, cx = r.x + r.width * fraction, cy = r.y + r.height / 2;
        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xfff7dcff)},
                                         {.35f, cybercanvas::color(kGold)},
                                         {1, cybercanvas::color(0x806333ff)}};
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
    std::string_view glyph = "sun";
    uint32_t tint = 0xd6a23cff;
    if (name.find("thunder") != std::string_view::npos) {
        glyph = "cloud-lightning";
    } else if (name.find("snow") != std::string_view::npos || name.find("sleet") != std::string_view::npos) {
        glyph = "snowflake";
        tint = 0x73999fff;
    } else if (name.find("rain") != std::string_view::npos || name.find("drizzle") != std::string_view::npos) {
        glyph = "cloud-rain";
        tint = 0x608e9aff;
    } else if (name.find("fog") != std::string_view::npos || name.find("mist") != std::string_view::npos) {
        glyph = "cloud-fog";
        tint = 0x7a8b79ff;
    } else if (name.find("cloud") != std::string_view::npos || name.find("overcast") != std::string_view::npos) {
        glyph = "cloud-sun";
        tint = 0x577d6aff;
    } else if (name.find("night") != std::string_view::npos) {
        glyph = "moon";
    } else if (name == "not-available") {
        glyph = "cloud";
        tint = 0x8c958288;
    }
    (void)slot;
    icons_.paint(canvas, bounds, glyph, tint);
}

void Skin::photo(Canvas& canvas, Rect r, const cybercanvas::ImageData& image, double opacity, std::string_view fit) {
    if (folder_.starts_with("player-") && std::abs(r.width - r.height) < r.width * .08) {
        auto mask = canvas.path();
        const double cy = r.y + r.height / 2;
        mask.moveTo({r.x + r.width, cy})
            .arcTo({r.x, cy}, {r.width / 2, r.height / 2})
            .arcTo({r.x + r.width, cy}, {r.width / 2, r.height / 2})
            .close();
        canvas.masked(mask, [&] {
            canvas.image(image, r, {.fit = cybercanvas::ImageFit::Cover, .opacity = opacity});
        });
        return;
    }
    canvas.image(
        image, r,
        {.fit = fit == "contain" ? cybercanvas::ImageFit::Contain : cybercanvas::ImageFit::Cover, .opacity = opacity});
}

void Skin::part(Canvas& canvas, std::string_view name, Rect target, bool spring) {
    const auto hardware = hardware_.find(name);
    if (hardware != hardware_.end()) {
        if (const auto& image = hardwareImage_) {
            const auto r = hardware->second;
            const double scale = std::min(target.width / r.width, target.height / r.height);
            const Rect box =
                spring ? target
                       : Rect{target.x + (target.width - r.width * scale) / 2,
                              target.y + (target.height - r.height * scale) / 2, r.width * scale, r.height * scale};
            const double sx = box.width / r.width, sy = box.height / r.height;
            canvas.clipped(box, [&] {
                canvas.image(*image,
                             {box.x - (r.x - hardwareCrop_.x) * sx, box.y - (r.y - hardwareCrop_.y) * sy,
                              hardwareCrop_.width * sx, hardwareCrop_.height * sy},
                             {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
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
        canvas.image(
            *art_,
            {destination.x - r.x * scale, destination.y - r.y * scale, crop_.width * scale, crop_.height * scale},
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
    const auto placement = regions_.find("part_" + std::string(name));
    const Rect r =
        placement == regions_.end() ? Rect{src.x - crop_.x, src.y - crop_.y, src.width, src.height} : placement->second;
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

void Skin::springPart(Canvas& canvas, std::string_view name, Point delta) {
    const auto found = hardware_.find(name);
    if (found == hardware_.end()) {
        return;
    }
    const auto src = found->second;
    const double scale = worldView_.scale / artView_.scale;
    const auto placement = regions_.find("part_" + std::string(name));
    const Rect pose =
        placement == regions_.end() ? Rect{src.x - crop_.x, src.y - crop_.y, src.width, src.height} : placement->second;
    artLayer(canvas, [&](auto& art) {
        const double shift = delta.y * scale;
        part(art, name, {pose.x, pose.y + shift, pose.width, std::max(1., pose.height - shift)}, true);
    });
}

void Skin::gauge(Canvas& canvas, Rect r, double fraction, const std::wstring& value, const std::wstring& title,
                 uint32_t accent) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    fraction = std::clamp(fraction, 0., 1.);
    if (r.width > r.height * 1.3) {
        const double cx = r.x + r.width / 2, cy = r.y + r.height * .87;
        for (unsigned segment = 0; segment < 6; ++segment) {
            auto track = canvas.path();
            for (unsigned step = 0; step <= 10; ++step) {
                const double a = (-165. + segment * 25 + step * 2.3) * std::numbers::pi / 180;
                const Point p{cx + std::cos(a) * r.width * .45, cy + std::sin(a) * r.height * .78};
                if (step) {
                    track.lineTo(p);
                } else {
                    track.moveTo(p);
                }
            }
            canvas.stroke(track,
                          folder_.starts_with("air-")
                              ? std::array<uint32_t, 6>{0x48774dff, 0x689255ff, 0x98b668ff, 0xb9c783ff, 0xd9d297ff,
                                                        0xe1d8b2ff}[segment]
                              : 0xd4cbb3ff,
                          {.width = r.height * .10});
        }
        auto arc = canvas.path();
        for (unsigned i = 0; i <= 64; ++i) {
            const double a = (-165 + 150 * fraction * i / 64) * std::numbers::pi / 180;
            const Point p{cx + std::cos(a) * r.width * .45, cy + std::sin(a) * r.height * .78};
            if (i) {
                arc.lineTo(p);
            } else {
                arc.moveTo(p);
            }
        }
        if (fraction > 0) {
            canvas.stroke(arc, accent == kRuby ? 0x638b6bff : accent, {.width = r.height * .09});
        }
        const double a = (-165 + 150 * fraction) * std::numbers::pi / 180;
        canvas.line(cx, cy, cx + std::cos(a) * r.width * .40, cy + std::sin(a) * r.height * .70, kGold,
                    r.height * .026);
        canvas.oval(cx, cy, r.height * .042, r.height * .042, kGold);
        return;
    }
    const double radius = std::min(r.width, r.height) * .48;
    const double cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    const bool timer = folder_.starts_with("timer-");
    auto arc = canvas.path();
    for (unsigned i = 0; i <= 96; ++i) {
        const double angle = ((timer ? 155 : 135) + fraction * (timer ? 115 : 270) * i / 96) * std::numbers::pi / 180;
        const double track = radius * (timer ? .98 : .90);
        const Point p{cx + std::cos(angle) * track, cy + std::sin(angle) * track};
        if (i == 0) {
            arc.moveTo(p);
        } else {
            arc.lineTo(p);
        }
    }
    if (fraction > 0) {
        canvas.stroke(arc, accent == kRuby ? 0x638b6bff : accent, {.width = radius * .10});
    }
    if (timer) {
        return;
    }
    label(canvas, title, {cx - radius * .67, cy - radius * .50, radius * 1.34, radius * .28}, radius * .25, kIvory,
          false);
    label(canvas, value, {cx - radius * .74, cy - radius * .10, radius * 1.48, radius * .48}, radius * .43, kIvory,
          false);
    const Point pivot{cx + radius * .48, cy + radius * .48};
    const double angle = (-170 + fraction * 100) * std::numbers::pi / 180;
    const Point tip{pivot.x + std::cos(angle) * radius * .65, pivot.y + std::sin(angle) * radius * .65};
    canvas.line(pivot.x + 1, pivot.y + 1, tip.x + 1, tip.y + 1, 0x4b361b88, radius * .06);
    canvas.line(pivot.x, pivot.y, tip.x, tip.y, kGold, radius * .043);
    canvas.line(pivot.x, pivot.y, tip.x, tip.y, 0xffebadff, radius * .012);
    canvas.oval(pivot.x, pivot.y, radius * .062, radius * .062, 0x9f6f2fff);
    canvas.oval(pivot.x - radius * .014, pivot.y - radius * .018, radius * .033, radius * .033, 0xf9d98fff);
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
}  // namespace songyun
