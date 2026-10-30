#include "plugins/deep-whale/SpringClock.h"

#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <tuple>

namespace deepwhale {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

toys::ClockLayout springLayout(bool compact) {
    if (!compact) {
        return {{360, 206}, .27, .55, .238, .54, {.40, .9041, 1.504, 2.0102, 2.6192, 3.13}};
    }
    return {{360, 106}, .27, .093, .238, .316, {.40, .9008, 1.5162, 2.0121, 2.6177, 3.13}};
}

const SpringSpec& springSpec(std::string_view id) {
    for (const auto& spec : kSpringCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Deep Whale spring clock");
}

cdsdk::Settings springDefaults(bool) {
    return {{{"speed", CD_SETTING_NUMBER, 0, .75, {}, .25, 2},
             {"running", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"twelve_hour", CD_SETTING_TOGGLE, 0},
             {"text_color", CD_SETTING_COLOR, 0x3f5b89ff},
             {"accent_color", CD_SETTING_COLOR, 0xbfa57dff}}};
}

cybercanvas::Rect springFrameBounds(const toys::Model& model) {
    // Includes the complete beam, Box2D chamber and free movement at every edge.
    return model.size().height > 150 ? cybercanvas::Rect{-4, -4, 368, 214} : cybercanvas::Rect{0, -2, 360, 110};
}

SpringView springView(CdRect safe, const toys::Model& model) {
    const auto b = springFrameBounds(model);
    const double scale = std::min(std::max(1., safe.width - 14) / b.width, std::max(1., safe.height - 14) / b.height);
    return {{safe.x + (safe.width - b.width * scale) / 2 - b.x * scale,
             safe.y + (safe.height - b.height * scale) / 2 - b.y * scale},
            scale};
}

struct SpringRenderer::Impl {
    using Key = std::tuple<double, uint32_t, std::array<const cybercanvas::ImageData*, 10>>;
    cdsdk::Images images;
    std::function<void(const std::string&)> report;
    std::map<std::string, std::string> errors;
    std::filesystem::path path, fontPath;
    std::array<std::shared_ptr<const cybercanvas::ImageData>, 10> parts;
    std::array<cybercanvas::SurfaceImage, 10> digits;
    std::array<cybercanvas::SurfaceImage, 6> cards;
    std::unique_ptr<cdsdk::WindowsFonts> font;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::OffscreenSurface wideBeam;
    ComPtr<ID2D1RenderTarget> beamOwner;
    std::optional<std::tuple<double, double, double, uint32_t>> beamKey;
    uint64_t builds = 0;
    bool compact;

    unsigned fixedCount() const {
        return compact ? 1 : 4;
    }

    double cardHeight() const {
        return compact ? 66 : 112;
    }

    cybercanvas::ImageRegion region(unsigned i) const {
        if (compact) {
            if (i == 0) {
                return {24, 64, 2076, 174};
            }
            return {uint32_t(left[i - 1]), 295, uint32_t(right[i - 1] - left[i - 1]), 382};
        }
        constexpr std::array<cybercanvas::ImageRegion, 10> r{{{13, 54, 1593, 134},
                                                              {700, 188, 180, 40},
                                                              {13, 188, 150, 130},
                                                              {1450, 188, 156, 130},
                                                              {96, 440, 216, 468},
                                                              {316, 440, 222, 468},
                                                              {587, 440, 221, 468},
                                                              {812, 440, 223, 468},
                                                              {1083, 440, 224, 468},
                                                              {1310, 440, 223, 468}}};
        return r[i];
    }

    static constexpr std::array<int, 6> left{78, 384, 759, 1064, 1430, 1743};
    static constexpr std::array<int, 6> right{366, 679, 1050, 1357, 1725, 2040};

    explicit Impl(const CdCreateContext& context) : compact(springSpec(context.plugin_id).compact) {
        images.bind(context);
        if (context.host && context.host->log) {
            const auto log = context.host->log;
            const auto user = context.host_context;
            report = [log, user](const std::string& message) {
                log(user, 3, message.data(), uint32_t(message.size()));
            };
        }

        const auto directory =
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory)));
        path = directory / "assets/body.png";
        fontPath = directory / "assets/BreeSerif-Regular.ttf";
    }

    void prepare(Canvas& canvas, double density, uint32_t ink) {
        // Each source region is clipped by WIC before decoding at display density.
        std::array<const cybercanvas::ImageData*, 10> pointers{};
        for (unsigned i = compact ? 1 : 0; i < fixedCount() + 6; ++i) {
            const auto slot = "part-" + std::to_string(i);
            const auto r = region(i);
            const double sourceScale = compact ? 273. / 1668. : 273. / 1224.;
            images.set(slot, path, 1, uint32_t(std::ceil(std::max(r.width, r.height) * sourceScale * density)), r);
        }
        images.poll();
        for (unsigned i = compact ? 1 : 0; i < fixedCount() + 6; ++i) {
            const auto slot = "part-" + std::to_string(i);
            const auto snapshot = images.get(slot);
            parts[i] = snapshot.image;
            pointers[i] = parts[i].get();
            if (snapshot.state == cybercanvas::ImageState::Failed && errors[slot] != snapshot.error) {
                errors[slot] = snapshot.error;
                if (report) {
                    report("Deep Whale " + slot + ": " + snapshot.error);
                }
            }
        }
        if (!font) {
            font = std::make_unique<cdsdk::WindowsFonts>(canvas.nativeTextFactory(), fontPath);
        }
        canvas.fontCollection(font->collection());
        const Key next{density, ink, pointers};
        if (owner == canvas.target && key == next) {
            return;
        }
        for (unsigned i = 0; i < 6; ++i) {
            cybercanvas::OffscreenSurface surface;
            canvas.renderLayer(surface,
                               {uint32_t(std::ceil(52 * density)), uint32_t(std::ceil(cardHeight() * density)), 1},
                               [&](Canvas& layer) {
                                   layer.scale(density, density);
                                   if (parts[i + fixedCount()]) {
                                       const auto r = region(i + fixedCount());
                                       const double w = r.width, h = r.height * 47.2 / w;
                                       layer.image(*parts[i + fixedCount()], {2.4, 1.4, 47.2, h},
                                                   {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                                   }
                               });
            cards[i] = surface.snapshot();
        }
        for (unsigned i = 0; i < 10; ++i) {
            cybercanvas::OffscreenSurface surface;
            canvas.renderLayer(
                surface, {uint32_t(std::ceil(52 * density)), uint32_t(std::ceil(cardHeight() * density)), 1},
                [&](Canvas& layer) {
                    layer.scale(density, density);
                    cybercanvas::TextStyle style;
                    style.family = font->family();
                    style.size = compact ? 42 : 72;
                    style.align = cybercanvas::TextAlign::Center;
                    const std::wstring text(1, wchar_t(L'0' + i));
                    const std::array<std::wstring, 10> references{L"0", L"1", L"2", L"3", L"4",
                                                                  L"5", L"6", L"7", L"8", L"9"};
                    const auto fit = layer.fitTextInk(
                        text, style, compact ? cybercanvas::Rect{8, 10, 36, 40} : cybercanvas::Rect{8, 21, 36, 62},
                        references);
                    layer.ink(ink);
                    layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)),
                                                 fit.layout.Get(), layer.brush.Get());
                });
            digits[i] = surface.snapshot();
        }
        owner = canvas.target;
        key = next;
        ++builds;
    }
};

SpringRenderer::SpringRenderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

SpringRenderer::~SpringRenderer() = default;

void SpringRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void SpringRenderer::pause(bool paused) {
    if (!paused && impl_->images.stats().failed) {
        impl_->images.clear();
        impl_->errors.clear();
        impl_->key.reset();
    }
    impl_->images.pause(paused);
}

bool SpringRenderer::ready() const {
    return std::all_of(impl_->parts.begin() + (impl_->compact ? 1 : 0), impl_->parts.begin() + impl_->fixedCount() + 6,
                       [](const auto& p) {
                           return bool(p);
                       }) &&
           !impl_->images.pending();
}

uint64_t SpringRenderer::cacheBuilds() const {
    return impl_->builds;
}

cdsdk::ImageSlotStats SpringRenderer::imageStats() const {
    return impl_->images.stats();
}

void SpringRenderer::draw(Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double scale,
                          double dpi, cybercanvas::Rect beam) {
    impl_->prepare(canvas, scale * dpi, uint32_t(settings.at("text_color").integer));
    const double sourceScale = impl_->compact ? 273. / 1668. : 273. / 1224.;
    const double anchorX = impl_->compact ? 224 : 202, anchorY = impl_->compact ? 238 : 188;
    if (impl_->compact) {
        if (beam.width <= 0) {
            beam = {7, 0, model.size().width - 14, 0};
        }
        const uint32_t gold = uint32_t(settings.at("accent_color").integer);
        const auto key = std::tuple{scale, dpi, beam.width, gold};
        if (impl_->beamKey != key || impl_->beamOwner != canvas.target) {
            impl_->wideBeam.reset();
            canvas.renderLayer(
                impl_->wideBeam,
                {uint32_t(std::ceil(beam.width * scale * dpi)), uint32_t(std::ceil(33 * scale * dpi)), dpi},
                [&](Canvas& layer) {
                    layer.scale(scale, scale);
                    layer.translate(-beam.x, 2);
                    const cybercanvas::Rect rail{beam.x, 10, beam.width, 17};
                    layer.gradientBox(rail, 4, 0x285d94ff, 0x102a50ff);
                    layer.strokeBox(rail, 4, gold, {.width = 1.1});
                    layer.line(rail.x + 4, 12, rail.x + rail.width - 4, 12, 0xd2efffbb, .65);
                    for (double x : {rail.x + 7, rail.x + rail.width - 7}) {
                        layer.oval(x, 18.5, 5.5, 5.5, 0x3d87bdff);
                        layer.oval(x, 18.5, 5.5, 5.5, gold, .9);
                        layer.oval(x - 1.6, 16.5, 1.3, 1.3, 0xe4f8ffff);
                    }
                    const double center = model.size().width / 2;
                    auto tail = layer.path();
                    tail.moveTo({center, 14})
                        .lineTo({center - 10, 5})
                        .lineTo({center - 11, -1})
                        .lineTo({center - 4, 0})
                        .lineTo({center, 5})
                        .lineTo({center + 4, 0})
                        .lineTo({center + 11, -1})
                        .lineTo({center + 10, 5})
                        .close();
                    layer.fill(tail, 0x29588aff);
                    layer.stroke(tail, gold, {.width = .9});
                    // These eyes share the physics anchors, independent of the rail span.
                    for (const auto& piece : model.pieces()) {
                        const auto anchor = model.anchor(piece);
                        layer.oval(anchor.x * 100, anchor.y * 100, 2.4, 2.4, gold);
                        layer.oval(anchor.x * 100 - .5, anchor.y * 100 - .5, 1, 1, 0xfff1d2ff);
                    }
                });
            impl_->beamOwner = canvas.target;
            impl_->beamKey = key;
        }
        const auto source = D2D1::RectF(0, 0, float(beam.width * scale), float(33 * scale));
        canvas.target->DrawBitmap(impl_->wideBeam.bitmap(),
                                  D2D1::RectF(float(beam.x), -2, float(beam.x + beam.width), 31), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
    } else {
        for (unsigned i = 0; i < impl_->fixedCount(); ++i) {
            const auto r = impl_->region(i);
            if (impl_->parts[i]) {
                canvas.image(*impl_->parts[i],
                             {40 + (r.x - anchorX) * sourceScale, 27 + (r.y - anchorY) * sourceScale,
                              r.width * sourceScale, r.height * sourceScale},
                             {.fit = cybercanvas::ImageFit::Contain, .clip = false});
            }
        }
    }
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto& piece = model.pieces()[i];
        const auto from = model.anchor(piece), to = model.attachment(piece);
        auto spring = canvas.path();
        spring.moveTo({from.x * 100, from.y * 100});
        for (unsigned j = 1; j <= 48; ++j) {
            const double t = j / 48., side = std::sin(t * 12 * std::numbers::pi) * 2.1;
            spring.lineTo({(from.x + (to.x - from.x) * t) * 100 + side, (from.y + (to.y - from.y) * t) * 100});
        }
        canvas.stroke(spring, 0x536d8dff, {.width = 1.6});
        canvas.stroke(spring, uint32_t(settings.at("accent_color").integer), {.width = .8});
        const auto state = model.state(i);
        canvas.saved([&] {
            canvas.translate(state.position.x * 100, state.position.y * 100);
            canvas.rotate(state.angle * 180 / std::numbers::pi);
            const float half = float(impl_->cardHeight() / 2);
            const auto bounds = D2D1::RectF(-26, -half, 26, half);
            canvas.target->DrawBitmap(impl_->cards[i].Get(), bounds, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            const unsigned digit = unsigned(model.digits()[i] - L'0');
            canvas.target->DrawBitmap(impl_->digits[digit].Get(), bounds, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        });
    }
    for (double x : {120.5, 232.5}) {
        for (double y : impl_->compact ? std::array<double, 2>{73., 85.} : std::array<double, 2>{127., 148.}) {
            canvas.oval(x, y, 2.5, 2.5, 0xd2b483ff);
            canvas.oval(x - .45, y - .55, 1.4, 1.4, 0xf6f5e4ff);
        }
    }
    if (settings.at("twelve_hour").integer) {
        cybercanvas::TextStyle style;
        style.size = 12 / std::max(.1, scale);
        style.align = cybercanvas::TextAlign::Center;
        canvas.text(model.afternoon() ? L"PM" : L"AM", style, {160, model.size().height - 20, 42, 14}, 0x526d91ff);
    }
}
}  // namespace deepwhale
