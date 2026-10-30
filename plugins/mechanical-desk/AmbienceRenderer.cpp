#include <sdk/Language.h>
#include <sdk/services/AmbientAudio.h>

#include <algorithm>
#include <cmath>

#include "plugins/mechanical-desk/Skin.h"
#include "plugins/mechanical-desk/controllers/ambience/Atmosphere.h"

namespace mechanicaldesk_ambience {
using namespace mechanicaldesk;
using cybercanvas::ViewKind;
using Node = cdsdk::Scene::Node;

struct Renderer::Impl {
    Skin skin;
    cdsdk::Scene scene;
    cdsdk::Icons icons;
    cdsdk::Controls plant;
    std::array<Node, 4> rows{}, groups{}, labels{}, sliders{}, buttons{};
    Node primary = 0, master = 0, buttonRow = 0, spacer = 0;
    bool built = false, compact = false;
    int page = -1;

    Node add(Node parent, ViewKind kind, uint32_t key = 0) {
        cybercanvas::View view;
        view.kind = kind;
        view.key = key;
        view.inheritGap = false;
        view.layout.shrink = 0;
        view.theme.padding = 0;
        view.theme.gap = 0;
        if (kind == ViewKind::Slider) {
            view.maximum = 100;
            view.step = 1;
            view.hitSlop = 1;
            view.theme.foreground = 0;
            view.theme.accent = 0;
            view.theme.track = 0;
            view.theme.thumbRadius = 0;
        }
        return scene.add(parent, view);
    }

    void build(bool compactCase) {
        compact = compactCase;
        if (compact) {
            primary = add(0, ViewKind::Button, 2);
            spacer = add(0, ViewKind::Decoration);
            buttonRow = add(0, ViewKind::Row);
            buttons[0] = add(buttonRow, ViewKind::Button, 1);
            buttons[1] = add(buttonRow, ViewKind::Button, 3);
            rows[0] = add(0, ViewKind::Decoration);
        } else {
            for (unsigned i = 0; i < 4; ++i) {
                rows[i] = add(0, ViewKind::Row);
                groups[i] = add(rows[i], ViewKind::Column);
                labels[i] = add(groups[i], ViewKind::Text);
                buttons[i] = add(rows[i], ViewKind::Button, i + 1);
            }
            spacer = add(0, ViewKind::Decoration);
        }
        master = add(0, ViewKind::Slider, 10);
        built = true;
    }

    cdsdk::Scene& prepare(Spec spec, const cdsdk::Settings& settings, const Model& model, double width, double height,
                          double dpi) {
        using namespace cybercanvas;
        if (!built) {
            build(spec.rows == 1);
        }
        const auto v = skin.view({0, 0, width, height});
        const auto area = skin.absolute(compact ? Rect{801, 739, 141, 175} : Rect{1215, 701, 264, 233});
        const double scale = v.scale;
        const double left = v.origin.x + area.x * scale, top = v.origin.y + area.y * scale;
        auto root = scene.view(0).layout;
        root.padding = {left, top, std::max(0., width - left - area.width * scale),
                        std::max(0., height - top - area.height * scale)};
        root.gap = 0;
        scene.style(0, root);
        auto theme = WidgetTheme::dark();
        theme.foreground = kIvory;
        theme.muted = kSteel;
        theme.accent = kBrass;
        theme.track = 0x273030ff;
        theme.surface = theme.border = 0;
        theme.padding = theme.gap = theme.radius = 0;
        theme.font.family = L"Microsoft YaHei UI";
        theme.font.bold = false;
        theme.font.size = (compact ? 27 : 19) * scale;
        theme.thumbRadius = 5 * scale;
        scene.theme(theme);
        const auto fixed = [&](Node node, double w, double h) {
            auto layout = scene.view(node).layout;
            layout.width = w > 0 ? Length::points(w * scale) : Length{};
            layout.height = Length::points(h * scale);
            layout.shrink = 0;
            layout.gap = 0;
            return layout;
        };
        const bool en = settings.at("language").text == "en";
        constexpr const wchar_t* zh[]{L"雨声", L"海浪", L"溪流", L"鸟鸣", L"炉火", L"微风"};
        constexpr const wchar_t* english[]{L"Rain", L"Ocean", L"Stream", L"Birds", L"Fire", L"Wind"};
        const auto* names = en ? english : zh;
        const bool playing = model.audioStatus == CD_AMBIENT_PLAYING;
        if (compact) {
            scene.style(primary, fixed(primary, 0, 45));
            scene.text(primary,
                       model.audioError ? (en ? L"Unavailable" : L"不可用") : names[settings.at("primary").integer]);
            scene.accessibleName(primary, en ? L"Change sound" : L"切换声源");
            scene.style(spacer, fixed(spacer, 0, 22));
            auto row = fixed(buttonRow, 0, 49);
            row.padding = {6 * scale, 0, 14 * scale, 0};
            row.justify = FlexJustify::SpaceBetween;
            scene.style(buttonRow, row);
            for (unsigned i = 0; i < 2; ++i) {
                scene.style(buttons[i], fixed(buttons[i], 49, 49));
            }
            scene.icon(buttons[0], playing ? "pause" : "play");
            scene.icon(buttons[1], "timer");
            scene.accessibleName(buttons[0], en ? L"Play or pause" : L"播放或暂停");
            scene.accessibleName(buttons[1], en ? L"Sleep timer" : L"定时停止");
            scene.theme(buttons[0], {.foreground = kInk});
            scene.theme(buttons[1], {.foreground = kInk});
            scene.style(rows[0], fixed(rows[0], 0, 21));
        } else {
            const unsigned order[2][4]{{0, 1, 2, 3}, {4, 5, 0, 1}};
            if (page != model.noisePage) {
                scene.cancel();
                for (auto& slider : sliders) {
                    if (slider) {
                        scene.remove(slider);
                    }
                    slider = 0;
                }
                page = model.noisePage;
            }
            const char* symbols[]{playing ? "pause" : "play", "volume-2", "timer", "ellipsis"};
            const wchar_t* descriptions[]{L"播放或暂停", L"切换主声源", L"定时停止", L"更多声源"};
            for (unsigned i = 0; i < 4; ++i) {
                const auto track = order[page ? 1 : 0][i];
                auto row = fixed(rows[i], 0, 47);
                row.align = FlexAlign::Start;
                scene.style(rows[i], row);
                scene.style(groups[i], fixed(groups[i], 200, 40));
                scene.style(labels[i], fixed(labels[i], 0, 20));
                scene.text(labels[i], std::wstring(names[track]) + L"  " +
                                          std::to_wstring(int(std::lround(model.levels[track] * 100))) + L"%");
                if (!sliders[i]) {
                    sliders[i] = add(groups[i], ViewKind::Slider, 20 + track);
                }
                scene.style(sliders[i], fixed(sliders[i], 0, 17));
                scene.accessibleName(sliders[i], names[track]);
                scene.value(sliders[i], std::round(model.levels[track] * 100));
                auto button = fixed(buttons[i], 29, 29);
                button.margin = {28 * scale, 1 * scale, 0, 0};
                scene.style(buttons[i], button);
                scene.icon(buttons[i], symbols[i]);
                scene.accessibleName(buttons[i], descriptions[i]);
                scene.theme(buttons[i], {.foreground = kInk});
            }
            scene.style(spacer, fixed(spacer, 0, 21));
        }
        const auto masterArea = skin.region("master");
        auto masterStyle = fixed(master, masterArea.width, 22);
        masterStyle.margin.left = 0;
        scene.style(master, masterStyle);
        scene.transform(master, {.translation = {(masterArea.x - area.x) * scale, 0}});
        scene.value(master, std::round(model.master * 100));
        scene.accessibleName(master, en ? L"Master volume" : L"主音量");
        // Measure with the actual drawing canvas. Alternating factories/canvases
        // invalidates Scene layout on every frame and queues another redraw.
        scene.arrange(skin.layoutCanvas(), {0, 0, width, height}, dpi);
        return scene;
    }

    void thumbs(Canvas& canvas, double width, double height) {
        const auto v = skin.view({0, 0, width, height});
        for (unsigned i = 0; i < 5; ++i) {
            const auto node = i == 4 ? master : sliders[i];
            if (!node) {
                continue;
            }
            const auto r = scene.visualBounds(node);
            const double x = (r.x + r.width * scene.view(node).value / 100 - v.origin.x) / v.scale;
            const double y = (r.y + r.height / 2 - v.origin.y) / v.scale;
            const Rect knob{x - 8, y - 12, 16, 24};
            canvas.gradientBox(knob, 4, 0xe5ce96ff, 0x9a7136ff);
            canvas.strokeBox(knob, 4, 0xf3dfacff, {.width = 1.2});
            canvas.line(x - 4, y - 8, x - 4, y + 8, 0xffefc5aa, 1);
            if (scene.focused() == node) {
                canvas.strokeBox({knob.x - 3, knob.y - 3, knob.width + 6, knob.height + 6}, 5, kIvory, {.width = 1.2});
            }
        }
    }
};

Renderer::Renderer(std::wstring) : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bindHost(const CdCreateContext& context) {
    impl_->skin.bind(context);
    impl_->scene.bind(context);
    impl_->icons.bind(context);
}

void Renderer::bindPlant(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->plant.bind(context, std::move(action));
}

void Renderer::onInvalidate(std::function<void()> callback) {
    impl_->skin.onChange(callback);
    impl_->scene.onInvalidate(std::move(callback));
}

void Renderer::onAction(std::function<void(cybercanvas::SceneEvent)> callback) {
    impl_->scene.onAction(std::move(callback));
}

void Renderer::onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) {
    impl_->scene.diagnostics().onSample([this, callback = std::move(callback)] {
        cdsdk::InstanceStats result;
        result.scene = impl_->scene.stats();
        if (callback) {
            callback(result);
        }
        return result;
    });
}

cdsdk::Scene& Renderer::noiseScene(Spec spec, const cdsdk::Settings& settings, const Model& model, double width,
                                   double height, double dpr) {
    return impl_->prepare(spec, settings, model, width, height, dpr);
}

void Renderer::cancelInput() {
    impl_->scene.cancel();
    impl_->plant.cancel();
}

bool Renderer::plantInput(Spec, const cdsdk::Settings&, CdRect safe, const CdInput& event) {
    const auto area = impl_->skin.display(impl_->skin.region("pot"), safe);
    impl_->plant.items({{1, area, "Water plant"}});
    impl_->plant.input(event);
    return true;
}

bool Renderer::advanceFeedback(double seconds) {
    return impl_->scene.advance(seconds);
}

bool Renderer::feedbackActive() const {
    return impl_->scene.active();
}

bool Renderer::imagesPending() const {
    return impl_->skin.pending();
}

bool Renderer::pollImages() {
    return impl_->skin.poll();
}

void Renderer::pauseImages(bool paused) {
    impl_->skin.pause(paused);
    if (paused) {
        cancelInput();
    }
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Model& model, const CdFrame& frame,
                      CdRect safe) try {
    const cdsdk::LanguageScope language(settings.at("language").text);
    const bool noise = spec.kind == Kind::Noise;
    if (noise) {
        noiseScene(spec, settings, model, safe.width, safe.height, frame.device_pixel_ratio);
    }
    return impl_->skin.render(
        frame, safe,
        [&](Canvas& canvas) {
            if (noise) {
                for (unsigned i = 0; i < (spec.rows > 1 ? 5u : 1u); ++i) {
                    impl_->skin.part(canvas, "track" + std::to_string(i));
                }
                impl_->thumbs(canvas, safe.width, safe.height);
                return;
            }
            const double growth = model.growth(atmosphere::wallSeconds(), settings.at("mode").text == "grow");
            const unsigned stage = growth < .38 ? 0 : growth < .70 ? 1 : 2;
            const auto species = settings.at("species").text;
            const unsigned selected = species == "succulent" ? 1 : species == "daisy" ? 2 : 0;
            const bool garden = spec.columns == 4 && settings.at("garden").integer;
            for (unsigned i = 0; i < (garden ? 3u : 1u); ++i) {
                const auto area = impl_->skin.region(garden ? "foliage" + std::to_string(i) : "foliage");
                canvas.saved([&] {
                    const Point root{area.x + area.width / 2, area.y + area.height};
                    canvas.translate(root.x, root.y);
                    canvas.rotate(model.sway(i * .7) * 1.0 + model.reaction() * 1.5);
                    canvas.translate(-root.x, -root.y);
                    impl_->skin.sprite(
                        canvas, "foliage-" + std::to_string(garden ? i : selected) + "-" + std::to_string(stage), area,
                        stage == 0   ? .42
                        : stage == 1 ? .72
                                     : 1);
                });
            }
            if (model.waterFeedback && model.waterOpacity() > 0) {
                const auto area = impl_->skin.region("pot");
                const double opacity = model.waterOpacity();
                for (unsigned i = 0; i < 4; ++i) {
                    const double x = area.x + area.width * (.35 + .1 * i),
                                 y = area.y + std::fmod(model.interaction.seconds() * 50 + i * 15, 40.);
                    canvas.oval(x, y, 2.5, 5, 0x83d9ed00 | uint32_t(opacity * 230));
                }
            }
        },
        noise ? std::function<void(Canvas&)>([&](Canvas& canvas) {
            impl_->scene.keyboard().origin({safe.x, safe.y});
            impl_->scene.arrange(canvas, {0, 0, safe.width, safe.height}, frame.device_pixel_ratio);
            impl_->scene.draw(canvas, impl_->icons.painter());
        })
              : std::function<void(Canvas&)>{});
} catch (...) {
    return false;
}

bool Renderer::save(const std::wstring& path, const CdFrame& frame) {
    return impl_->skin.save(path, frame);
}
}  // namespace mechanicaldesk_ambience
