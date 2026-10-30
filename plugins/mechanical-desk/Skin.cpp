#include "plugins/mechanical-desk/Skin.h"

#include <sdk/Language.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace mechanicaldesk {
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

void Skin::load(const std::filesystem::path& assets) {
    if (assets == assets_) {
        return;
    }
    std::ifstream file(assets / "skin.json");
    const auto config = nlohmann::json::parse(file);
    crop_ = rect(config.at("crop"));
    if (crop_.width <= 0 || crop_.height <= 0) {
        throw std::invalid_argument("Invalid mechanical artwork bounds");
    }
    assets_ = assets;
    folder_ = config.at("folder").get<std::string>();
    columns_ = config.at("columns").get<unsigned>();
    rows_ = config.at("rows").get<unsigned>();
    inset_ = std::max(6.25, config.value("inset", 6.25));
    regions_.clear();
    parts_.clear();
    for (const auto& [name, value] : config.at("regions").items()) {
        regions_[name] = absolute(rect(value));
    }
    parts_["body"] = {"body.png", crop_, {0, 0, crop_.width, crop_.height}};
    if (config.contains("parts")) {
        for (const auto& [name, value] : config.at("parts").items()) {
            const auto source = rect(value.at("source"));
            const auto destination = absolute(rect(value.value("destination", value.at("source"))));
            Point root{.5, 1};
            if (value.contains("root")) {
                root = {value.at("root").at(0).get<double>(), value.at("root").at(1).get<double>()};
            }
            parts_[name] = {value.value("file", "body.png"), source, destination, root};
        }
    }
    images_.clear();
    ++revision_;
}

void Skin::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    host_ = *context.host;
    context_ = context.host_context;
    load(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
         "assets");
    images_.bind(context);
    canvas_.bindGpu(context);
    icons_.bind(context);
    controls_.bind(context, std::move(action));
    diagnostics_.bind(context);
    diagnostics_.onSample([this] {
        cdsdk::InstanceStats result;
        result.canvas = canvas_.stats();
        result.images = images_.stats();
        result.sharedImages = images_.sharedStoreStats();
        return result;
    });
    onChange([this] {
        if (host_.request_redraw) {
            host_.request_redraw(context_);
        }
    });
}

void Skin::onChange(std::function<void()> changed) {
    images_.onChange([this, changed = std::move(changed)] {
        ++revision_;
        if (changed) {
            changed();
        }
    });
}

void Skin::pause(bool paused) {
    images_.pause(paused);
    if (paused) {
        cancel();
    }
}

bool Skin::pending() const {
    return images_.pending();
}

bool Skin::ready() const {
    const auto stats = images_.stats();
    return stats.ready > 0 && !stats.pending && !stats.failed;
}

bool Skin::poll() {
    return images_.poll();
}

uint64_t Skin::revision() const {
    return revision_;
}

cdsdk::ImageSlotStats Skin::stats() const {
    return images_.stats();
}

void Skin::input(const CdInput& event) {
    controls_.input(event);
}

void Skin::cancel() {
    controls_.cancel();
}

void Skin::controls(CdRect safe, std::vector<cdsdk::Controls::Item> items) {
    for (auto& item : items) {
        item.bounds = display(item.bounds, safe);
    }
    controls_.items(std::move(items));
}

Rect Skin::absolute(Rect native) const {
    return {native.x - crop_.x, native.y - crop_.y, native.width, native.height};
}

Rect Skin::region(std::string_view name) const {
    const auto found = regions_.find(name);
    return found == regions_.end() ? Rect{} : found->second;
}

View Skin::view(CdRect safe) const {
    const double scale = std::max(.001, std::min(std::max(1., safe.width - inset_ * 2) / crop_.width,
                                                 std::max(1., safe.height - inset_ * 2) / crop_.height));
    return {{safe.x + (safe.width - crop_.width * scale) / 2, safe.y + (safe.height - crop_.height * scale) / 2},
            scale};
}

Rect Skin::display(Rect local, CdRect safe) const {
    const auto v = view(safe);
    return {v.origin.x + local.x * v.scale, v.origin.y + local.y * v.scale, local.width * v.scale,
            local.height * v.scale};
}

void Skin::request(Canvas& canvas, const std::string& name, const Part& part, Rect destination) {
    frameParts_.insert(name);
    const auto current = images_.get(name);
    const auto now = GetTickCount64();
    if (current.state == cybercanvas::ImageState::Failed && now >= retries_[name]) {
        if (host_.log) {
            const auto message = "Mechanical image failed: " + folder_ + "/" + name + ": " + current.error;
            host_.log(context_, 2, message.data(), uint32_t(message.size()));
        }
        images_.erase(name);
        retries_[name] = now + 2000;
    }
    const uint32_t edge = uint32_t(std::clamp(
        std::ceil(std::max(destination.width, destination.height) * scale_ * canvas.devicePixelRatio() / 16) * 16, 16.,
        4096.));
    const auto r = part.source;
    images_.set(name, assets_ / part.file, 1, edge,
                {uint32_t(r.x), uint32_t(r.y), uint32_t(r.width), uint32_t(r.height)});
    images_.poll();
}

void Skin::part(Canvas& canvas, std::string_view name, Rect destination) {
    const auto found = parts_.find(name);
    if (found == parts_.end()) {
        return;
    }
    if (destination.width <= 0 || destination.height <= 0) {
        destination = found->second.destination;
    }
    request(canvas, found->first, found->second, destination);
    if (const auto image = images_.get(found->first).image) {
        canvas.image(*image, destination, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    }
}

void Skin::backdrop(Canvas& canvas) {
    part(canvas, "body");
}

void Skin::sprite(Canvas& canvas, std::string_view name, Rect area, double fraction) {
    const auto found = parts_.find(name);
    if (found == parts_.end()) {
        return;
    }
    const auto& p = found->second;
    const double scale = std::min(area.width / p.source.width, area.height / (p.source.height * p.root.y)) * fraction;
    part(canvas, name,
         {area.x + area.width / 2 - p.source.width * p.root.x * scale,
          area.y + area.height - p.source.height * p.root.y * scale, p.source.width * scale, p.source.height * scale});
}

void Skin::backdrop(Canvas& canvas, double scale) {
    scale_ = scale;
    backdrop(canvas);
}

bool Skin::render(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& draw,
                  const std::function<void(Canvas&)>& overlay) {
    if (!canvas_.valid(frame)) {
        return false;
    }
    canvas_.render(frame, safe, [&](auto& canvas, CdRect area) {
        frameParts_.clear();
        const auto v = view(area);
        scale_ = v.scale;
        canvas.saved([&] {
            canvas.translate(v.origin.x - area.x, v.origin.y - area.y);
            canvas.scale(v.scale, v.scale);
            backdrop(canvas);
            draw(canvas);
        });
        if (overlay) {
            overlay(canvas);
        }
        for (const auto& name : retainedParts_) {
            if (!frameParts_.contains(name)) {
                images_.erase(name);
            }
        }
        retainedParts_ = frameParts_;
    });
    return true;
}

bool Skin::save(const std::filesystem::path& path, const CdFrame& frame) {
    return canvas_.save(path.wstring(), frame);
}

void Skin::label(Canvas& canvas, const std::wstring& value, Rect area, double size, uint32_t color, bool bold,
                 const std::wstring& family) {
    if (area.width <= 0 || area.height <= 0) {
        return;
    }
    cybercanvas::TextStyle style;
    style.family = family;
    style.locale = cdsdk::drawingLocale();
    style.size = size;
    style.bold = bold;
    style.tabularFigures = true;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    const auto fitted = canvas.fitTextInk(value, style, area);
    canvas.brush->SetColor(cybercanvas::color(color));
    canvas.brush->SetOpacity(1);
    canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(),
                                  canvas.brush.Get());
}

void Skin::icon(Canvas& canvas, Rect area, std::string_view name, uint32_t color) {
    icons_.paint(canvas, area, name, color);
}

std::wstring Skin::font(const std::string& family) {
    fonts_.select(canvas_.nativeTextFactory(), family);
    canvas_.fontCollection(fonts_.collection());
    return fonts_.family();
}

Canvas& Skin::layoutCanvas() {
    return canvas_;
}

Size Skin::size() const {
    return {crop_.width, crop_.height};
}

const std::string& Skin::folder() const {
    return folder_;
}

unsigned Skin::columns() const {
    return columns_;
}

unsigned Skin::rows() const {
    return rows_;
}
}  // namespace mechanicaldesk
