#include "plugins/streetlink-desk/Scene.h"

#include <sdk/Canvas.h>
#include <sdk/Diagnostics.h>
#include <sdk/Images.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace streetlink {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Rect;

Rect readRect(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 4) {
        throw std::invalid_argument("STREETLINK rectangle must have four coordinates");
    }
    Rect r{value.at(0), value.at(1), value.at(2), value.at(3)};
    if (!std::isfinite(r.x + r.y + r.width + r.height) || r.width <= 0 || r.height <= 0) {
        throw std::invalid_argument("STREETLINK rectangle is invalid");
    }
    return r;
}
}  // namespace

Placement place(cybercanvas::Size viewport, cybercanvas::Size visible) {
    constexpr double inset = 7;  // Pixels: 6px border plus 1px reconstruction-filter reserve.
    if (!std::isfinite(viewport.width + viewport.height + visible.width + visible.height) ||
        viewport.width <= 2 * inset || viewport.height <= 2 * inset || visible.width <= 0 || visible.height <= 0) {
        throw std::invalid_argument("Invalid STREETLINK viewport");
    }
    const double scale =
        std::min((viewport.width - 2 * inset) / visible.width, (viewport.height - 2 * inset) / visible.height);
    const double extension = std::max(0., viewport.width - 2 * inset - visible.width * scale);
    return {{inset, (viewport.height - visible.height * scale) / 2, viewport.width - 2 * inset, visible.height * scale},
            scale,
            extension};
}

struct Scene::Impl {
    std::filesystem::path directory;
    cybercanvas::ImageRegion source;
    std::map<std::string, Rect, std::less<>> regions;
    std::vector<Rect> screens;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::DiagnosticSource diagnostics;
    cybercanvas::OffscreenSurface background;
    std::unique_ptr<cdsdk::WindowsFonts> font;
    std::function<void()> changed;
    std::function<void(const std::string&)> log;
    std::string lastError;
    Placement placement;
    uint64_t builds = 0, revision = 0;
    double viewportWidth = 0, viewportHeight = 0, density = 0;

    explicit Impl(const CdCreateContext& context) {
        directory =
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
            "assets";
        std::ifstream file(directory / "scene.json");
        const auto data = nlohmann::json::parse(file);
        const auto bounds = readRect(data.at("bounds"));
        source = {uint32_t(bounds.x), uint32_t(bounds.y), uint32_t(bounds.width), uint32_t(bounds.height)};
        for (const auto& [name, region] : data.at("regions").items()) {
            regions.emplace(name, readRect(region));
        }
        for (const auto& screen : data.at("screens")) {
            screens.push_back(readRect(screen));
        }
        canvas.bindGpu(context);
        images.bind(context);
        diagnostics.bind(context);
        diagnostics.onSample([this] {
            cdsdk::InstanceStats result;
            result.canvas = canvas.stats();
            result.images = images.stats();
            result.sharedImages = images.sharedStoreStats();
            return result;
        });
        images.onChange([this] {
            background.invalidate();
            if (changed) {
                changed();
            }
        });
        if (context.host && context.host->log) {
            const auto callback = context.host->log;
            log = [callback, user = context.host_context](const std::string& text) {
                callback(user, 3, text.data(), uint32_t(text.size()));
            };
        }
    }

    Rect map(Rect r) const {
        const double baseWidth = placement.body.width - placement.extension;
        const double cut = .5;
        const double shift = r.x >= cut ? placement.extension : 0;
        const double growth = r.x < cut && r.x + r.width > cut ? placement.extension : 0;
        return {placement.body.x + r.x * baseWidth + shift, placement.body.y + r.y * placement.body.height,
                r.width * baseWidth + growth, r.height * placement.body.height};
    }

    void prepare(double width, double height, double dpi) {
        placement = place({width, height}, {double(source.width), double(source.height)});
        images.set("housing", directory / "body.png", 1,
                   uint32_t(std::ceil(std::max(placement.body.width, placement.body.height) * dpi)), source);
        images.poll();
        const auto snapshot = images.get("housing");
        if (snapshot.state == cybercanvas::ImageState::Failed && snapshot.error != lastError) {
            lastError = snapshot.error;
            if (log) {
                log("STREETLINK " + directory.string() + "/body.png: " + lastError);
            }
        }
        const auto id = snapshot.image ? snapshot.image->id() : 0;
        if (viewportWidth != width || viewportHeight != height || density != dpi || revision != id) {
            viewportWidth = width;
            viewportHeight = height;
            density = dpi;
            revision = id;
            background.invalidate();
        }
        const cybercanvas::SurfaceSize size{uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), 1};
        if (background.matches(canvas.target.Get(), size)) {
            return;
        }
        canvas.renderLayer(background, size, [&](Canvas& c) {
            c.scale(dpi, dpi);
            if (!snapshot.image) {
                return;
            }
            const double baseWidth = placement.body.width - placement.extension;
            const double cut = placement.body.x + baseWidth / 2;
            const Rect original{placement.body.x, placement.body.y, baseWidth, placement.body.height};
            c.clipped(Rect{original.x, original.y, baseWidth / 2, original.height}, [&] {
                c.image(*snapshot.image, original, {.fit = cybercanvas::ImageFit::Contain, .clip = false});
            });
            c.clipped(Rect{cut + placement.extension, original.y, baseWidth / 2, original.height}, [&] {
                c.image(*snapshot.image,
                        {original.x + placement.extension, original.y, original.width, original.height},
                        {.fit = cybercanvas::ImageFit::Contain, .clip = false});
            });
            if (placement.extension > 0) {
                // Repeat only the straight middle rail. Neither end coupler, the
                // fingerprint plate nor the removable cartridge is stretched.
                c.clipped(Rect{cut, original.y, placement.extension, original.height}, [&] {
                    for (double x = cut; x < cut + placement.extension; x += 1) {
                        c.clipped(Rect{x, original.y, 1, original.height}, [&] {
                            c.image(*snapshot.image,
                                    {original.x + x - cut, original.y, original.width, original.height},
                                    {.fit = cybercanvas::ImageFit::Contain, .clip = false});
                        });
                    }
                });
            }
            // Only the registered display interior is reconstructed. The frame and
            // its end couplers remain the original unmodified, equally scaled image.
            for (const auto normalized : screens) {
                const auto r = map(normalized);
                const D2D1_GRADIENT_STOP colors[]{{0, cybercanvas::color(0x071012ff)},
                                                  {.55f, cybercanvas::color(0x111d21ff)},
                                                  {1, cybercanvas::color(0x040a0cff)}};
                const auto glass = c.linearGradient({r.x, r.y}, {r.x + r.width, r.y + r.height}, colors);
                c.target->FillRectangle(
                    D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height)), glass.Get());
            }
        });
        ++builds;
    }
};

Scene::Scene(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

Scene::~Scene() = default;

void Scene::onChange(std::function<void()> callback) {
    impl_->changed = std::move(callback);
}

void Scene::pause(bool paused) {
    if (!paused && impl_->images.stats().failed) {
        impl_->images.retry("housing");
        impl_->lastError.clear();
    }
    impl_->images.pause(paused);
}

bool Scene::ready() const {
    return impl_->images.stats().ready == 1 && impl_->images.stats().failed == 0;
}

uint64_t Scene::cacheBuilds() const {
    return impl_->builds;
}

uint64_t Scene::referencedBytes() const {
    return impl_->images.stats().referencedBytes;
}

bool Scene::render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& paint) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto& c, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        c.target->DrawBitmap(impl_->background.bitmap(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        paint(c);
    });
    return true;
}

Rect Scene::region(std::string_view name) const {
    const auto it = impl_->regions.find(name);
    return it == impl_->regions.end() ? Rect{} : impl_->map(it->second);
}

void Scene::text(Canvas& c, const std::wstring& value, Rect bounds, uint32_t color, double maximumSize) {
    if (bounds.width <= 0 || bounds.height <= 0 || value.empty()) {
        return;
    }
    if (!impl_->font) {
        impl_->font = std::make_unique<cdsdk::WindowsFonts>(
            c.nativeTextFactory(), cdsdk::bundledFontPath(L"assets/fonts/Streetlink/StreetlinkDisplay-Black.ttf"));
    }
    c.fontCollection(impl_->font->collection());
    cybercanvas::TextStyle style;
    style.family = impl_->font->family();
    style.weight = static_cast<cybercanvas::FontWeight>(900);
    style.size = maximumSize > 0 ? maximumSize : bounds.height;
    style.tabularFigures = true;
    style.wrap = false;
    const auto fit = c.fitTextInk(value, style, bounds);
    c.ink(color);
    c.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(), c.brush.Get());
}

bool Scene::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}
}  // namespace streetlink
