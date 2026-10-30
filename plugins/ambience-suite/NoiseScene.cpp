#include "plugins/ambience-suite/NoiseScene.h"

#include <CyberCanvas/Noise.h>

namespace atmosphere {
namespace {
using namespace cybercanvas;
constexpr const wchar_t* kImages[]{L"rain.png", L"ocean.png", L"stream.png", L"forest.png", L"fire.png", L"wind.png"};
constexpr const wchar_t* kNames[]{L"雨声", L"海浪", L"溪流", L"森林", L"壁炉", L"风声"};
constexpr const wchar_t* kEnglish[]{L"Rain", L"Ocean", L"Stream", L"Forest", L"Fire", L"Wind"};

View slot(ViewKind kind, double height = 0, uint32_t key = 0) {
    View view;
    view.kind = kind;
    view.key = key;
    if (height) {
        view.layout.height = Length::points(height);
        view.layout.shrink = 0;
    }
    return view;
}

FlexStyle growing() {
    FlexStyle style;
    style.grow = 1;
    style.basis = Length::points(0);
    return style;
}
}  // namespace

NoiseScene::NoiseScene(std::filesystem::path directory) : directory_(std::move(directory)) {
    images_.onChange([this] {
        if (scene_) {
            scene_->invalidate();
        }
    });
}

NoiseScene::~NoiseScene() {
    if (scene_) {
        scene_->onInvalidate({});
    }
    scene_.reset();
}

void NoiseScene::build(bool compact) {
    images_.clear();
    for (auto& emitter : particles_) {
        emitter.reset();
    }
    particleKinds_.fill(-1);
    if (scene_) {
        scene_->cancel();
    }
    compact_ = compact;
    footerWidth_ = -1;
    auto root = slot(ViewKind::Column);
    root.inheritGap = false;
    root.paint = [this](Canvas& canvas, Rect area, const WidgetTheme&) {
        if (!background_) {
            return;
        }
        const Rect card{1, 1, std::max(0., area.width - 3), std::max(0., area.height - 4)};
        canvas.gradientBox(card, corner_, dark_ ? 0x2A3941FF : 0xFFFEF9FF, dark_ ? 0x19282FFF : 0xEEEDE4FF);
        canvas.strokeBox(card, corner_, dark_ ? 0x78909A66 : 0xBDBDAF70, {.width = .65});
    };
    scene_ = std::make_unique<cdsdk::Scene>(std::move(root));
    auto& scene = *scene_;
    scene.onAction(action_);
    scene.diagnostics().extras([this](auto& stats) {
        stats.images = images_.stats();
        stats.sharedImages = images_.sharedStoreStats();
        if (diagnostics_) {
            diagnostics_(stats);
        }
    });
    if (context_) {
        scene.bind(*context_);
    }
    if (invalidated_) {
        scene.onInvalidate(invalidated_);
    }
    auto header = slot(ViewKind::Row);
    header.layout.align = FlexAlign::Center;
    header.clickable = compact;
    header.key = compact ? 2 : 0;
    header_ = scene.add(0, header);
    auto play = slot(ViewKind::Button, 34, 1);
    play.layout.width = Length::points(34);
    play.appearance = ButtonStyle::Filled;
    play.round = true;
    play.icon = "play";
    play.iconSize = 17;
    if (compact) {
        auto image = slot(ViewKind::Image);
        image.layout.shrink = 0;
        image.imageCells = 2;
        art_[0] = scene.add(header_, image);
        auto label = slot(ViewKind::Text);
        label.layout = growing();
        label.theme.fontSize = 12;
        labels_[0] = scene.add(header_, label);
        spacer_ = scene.add(0, slot(ViewKind::Decoration));
        auto bottom = slot(ViewKind::Row);
        bottom.layout.align = FlexAlign::Center;
        bottom.layout.gap = 12;
        footer_ = scene.add(0, bottom);
        play_ = scene.add(footer_, play);
        auto master = slot(ViewKind::Slider, 4, 10);
        master.layout.grow = 1;
        master.layout.basis = Length::points(0);
        master.maximum = 100;
        master.step = 1;
        master_ = scene.add(footer_, master);
        return;
    }
    auto titles = slot(ViewKind::Column);
    titles.layout = growing();
    titles.inheritGap = false;
    const auto group = scene.add(header_, titles);
    auto title = slot(ViewKind::Text, 23);
    title.theme.fontSize = 18;
    auto font = WidgetTheme::dark().font;
    font.family = L"Microsoft YaHei";
    font.bold = true;
    font.ellipsis = true;
    title.theme.font = font;
    heading_ = scene.add(group, title);
    auto subtitle = slot(ViewKind::Text, 11);
    subtitle.theme.fontSize = 8.3;
    subtitle_ = scene.add(group, subtitle);
    play_ = scene.add(header_, play);
    auto body = slot(ViewKind::Row);
    body.layout = growing();
    body.layout.wrap = true;
    body.layout.lineAlign = FlexAlign::Stretch;
    body.inheritGap = false;
    body_ = scene.add(0, body);
    auto footer = slot(ViewKind::Row);
    footer.layout.wrap = true;
    footer.layout.align = FlexAlign::Center;
    footer.layout.gap = 6;
    footer.paint = [this](Canvas& canvas, Rect area, const WidgetTheme&) {
        if (background_) {
            canvas.box(area.x, area.y, area.width, area.height, 5, dark_ ? 0xB8D5C906 : 0xFFFFFF55);
        }
    };
    footer_ = scene.add(0, footer);
    for (size_t i = 0; i < cells_.size(); ++i) {
        auto cell = slot(ViewKind::Column);
        cell.layout.grow = 1;
        cell.layout.padding = {7, 0, 7, 5};
        cell.layout.gap = 3;
        cells_[i] = scene.add(body_, cell);
        auto image = slot(ViewKind::Image);
        image.layout = growing();
        image.image = (directory_ / "assets" / "noise" / kImages[i]).wstring();
        image.imageCells = 2;
        art_[i] = scene.add(cells_[i], image);
        auto label = slot(ViewKind::Text, 16);
        label.text = kNames[i];
        labels_[i] = scene.add(cells_[i], label);
        auto slider = slot(ViewKind::Slider, 3, 20 + uint32_t(i));
        slider.maximum = 100;
        slider.step = 1;
        tracks_[i] = scene.add(cells_[i], slider);
    }
    auto button = slot(ViewKind::Button, 22, 3);
    button.layout.width = Length::points(80);
    button.appearance = ButtonStyle::Outline;
    timer_ = scene.add(footer_, button);
    auto volume = slot(ViewKind::Row);
    volume.layout.basis = Length::points(96);
    volume.layout.grow = 1;
    volume.layout.shrink = 0;
    volume.layout.align = FlexAlign::Center;
    volume.layout.gap = 3;
    const auto groupVolume = scene.add(footer_, volume);
    auto label = slot(ViewKind::Text, 16);
    label.layout.width = Length::points(37);
    label.theme.fontSize = 7;
    volumeLabel_ = scene.add(groupVolume, label);
    auto master = slot(ViewKind::Slider, 4, 10);
    master.layout.grow = 1;
    master.layout.basis = Length::points(0);
    master.maximum = 100;
    master.step = 1;
    master_ = scene.add(groupVolume, master);
    button.key = 4;
    button.layout.width = Length::points(47);
    more_ = scene.add(footer_, button);
}

cdsdk::Scene& NoiseScene::prepare(Canvas& canvas, Spec spec, const cdsdk::Settings& settings, const Model& model,
                                  double width, double height, double dpr) {
    const bool compact = spec.rows == 1;
    if (!scene_ || compact != compact_) {
        build(compact);
    }
    auto& scene = *scene_;
    const auto mode = settings.at("lighting").text;
    const bool dark = mode == "night" || mode == "theme" && model.dark;
    const bool background = settings.at("show_background").integer != 0;
    const double corner = double(settings.at("background_corner_radius").integer);
    if (dark != dark_ || background != background_ || corner != corner_) {
        scene.invalidate();
    }
    dark_ = dark;
    background_ = background;
    corner_ = corner;
    const bool english = settings.at("language").text == "en";
    auto theme = dark ? WidgetTheme::dark() : WidgetTheme::light();
    theme.foreground = dark ? 0xE2EDEAFF : 0x293D37FF;
    theme.muted = dark ? 0x9DB4B6FF : 0x7A8376FF;
    theme.accent = dark ? 0x88C0C6FF : 0x6D8C5BFF;
    theme.onAccent = 0xFFFEF2FF;
    theme.track = dark ? 0x46585CFF : 0xDAD8CEFF;
    theme.border = dark ? 0x789E9B55 : 0x9BA88B55;
    theme.font.family = L"Microsoft YaHei";
    theme.font.locale = english ? L"en-US" : L"zh-CN";
    theme.font.size = 8.5;
    theme.font.align = TextAlign::Center;
    theme.font.ellipsis = true;
    theme.radius = 4;
    theme.padding = 3;
    theme.gap = 6;
    theme.thumbRadius = compact ? 3.6 : 3.3;
    scene.theme(theme);
    scene.accessibleName(master_, english ? L"Master volume" : L"主音量");
    if (compact) {
        scene.accessibleName(header_, english ? L"Change sound" : L"切换声音");
    } else {
        for (size_t i = 0; i < tracks_.size(); ++i) {
            scene.accessibleName(tracks_[i], english ? kEnglish[i] : kNames[i]);
        }
    }
    scene.text(play_, model.audioStatus == 1 ? (english ? L"Pause" : L"暂停") : (english ? L"Play" : L"播放"));
    scene.icon(play_, model.audioStatus == 1 ? "pause" : "play");
    scene.value(master_, std::round(model.master * 100));
    auto root = scene.view(0).layout;
    root.padding = compact ? Insets{5, 1, 12, 0} : Insets{10, 6, 10, 8};
    root.gap = compact ? 0 : 6;
    scene.style(0, root);
    auto header = scene.view(header_).layout;
    header.height = Length::points(compact ? std::max(0., height * .49 - 1) : height > 145 ? 34 : 25);
    header.shrink = 0;
    scene.style(header_, header);
    if (compact) {
        const int selected = int(settings.at("primary").integer);
        scene.text(labels_[0], english ? kEnglish[selected] : kNames[selected]);
        auto art = scene.view(art_[0]).layout;
        art.width = Length::points(std::min(33., width * .3));
        scene.style(art_[0], art);
        const double diameter = std::clamp(height * .26, 16., 26.);
        auto play = scene.view(play_).layout;
        play.width = play.height = Length::points(diameter);
        scene.style(play_, play);
        auto spacer = scene.view(spacer_).layout;
        spacer.height = Length::points(std::max(0., height * .73 - diameter * .5 - height * .49));
        scene.style(spacer_, spacer);
        auto bottom = scene.view(footer_).layout;
        bottom.height = Length::points(diameter);
        bottom.shrink = 0;
        scene.style(footer_, bottom);
    } else {
        scene.text(heading_, english ? L"Nature sounds" : L"白噪音");
        ThemePatch heading;
        heading.font = scene.view(heading_).theme.font;
        heading.fontSize = std::clamp(width * .06, 12., 18.);
        scene.theme(heading_, heading);
        scene.text(subtitle_, english ? L"Nature · Mix · Sleep timer" : L"自然声景 · 混音 · 定时关闭");
        scene.visible(subtitle_, height > 145);
        ThemePatch muted;
        muted.foreground = theme.muted;
        muted.fontSize = 8.3;
        muted.font = theme.font;
        muted.font->align = TextAlign::Start;
        scene.theme(subtitle_, muted);
        auto footer = scene.view(footer_).layout;
        if (width != footerWidth_ || dpr != footerDpr_) {
            footerHeight_ =
                wrappingRow({0, 0, std::max(0., width - 20), 100}, {{80, 22}, {96, 22}, {47, 22}}, 6, false, dpr)
                    .height;
            footerWidth_ = width;
            footerDpr_ = dpr;
        }
        footer.height = Length::points(footerHeight_);
        footer.shrink = 0;
        scene.style(footer_, footer);
        const double available = std::max(0., height - 14 - (height > 145 ? 34 : 25) - 12 - footer.height.value);
        for (int i = 0; i < 6; ++i) {
            auto cell = scene.view(cells_[i]).layout;
            cell.visible = model.noisePage ? (i == 2 || i == 5) : (i == 0 || i == 1 || i == 3 || i == 4);
            cell.basis = Length::points(std::max(60., available * .55));
            scene.style(cells_[i], cell);
            scene.text(labels_[i], english ? kEnglish[i] : kNames[i]);
            ThemePatch label;
            label.fontSize = std::clamp(width * .037, 9., 11.);
            scene.theme(labels_[i], label);
            scene.value(tracks_[i], std::round(model.levels[i] * 100));
        }
        const auto minutes = settings.at("timer_minutes").integer;
        auto timer = minutes ? std::to_wstring(minutes) + (english ? L" min" : L" 分钟") : english ? L"Off" : L"不限时";
        if (model.audioStatus == 1 && model.deadline) {
            const auto now = wallSeconds(), remaining = model.deadline > now ? model.deadline - now : 0;
            timer = std::to_wstring(remaining / 60) + L":" + (remaining % 60 < 10 ? L"0" : L"") +
                    std::to_wstring(remaining % 60);
        }
        if (model.audioError) {
            timer = english ? L"No audio" : L"音频不可用";
        }
        scene.text(timer_, timer);
        scene.text(more_, model.noisePage ? (english ? L"Back" : L"返回") : (english ? L"More" : L"更多"));
        scene.text(volumeLabel_, english ? L"Vol" : L"主音量");
        muted.fontSize = 7;
        scene.theme(volumeLabel_, muted);
    }
    scene.arrange(canvas, {0, 0, width, height}, dpr);
    // Decode for the actual texture demand, including both horizontal sprite
    // cells and breathing headroom. Bucket sizes avoid redecoding per pixel.
    double demand = 0;
    for (int i = 0; i < (compact ? 1 : 6); ++i) {
        const auto area = scene.bounds(art_[i]);
        demand = std::max(demand, std::max(area.width * 2, area.height) * dpr * 1.08);
    }
    const uint32_t edge = uint32_t(std::clamp(std::ceil(demand / 64) * 64, 128., 4096.));
    for (int i = 0; i < (compact ? 1 : 6); ++i) {
        const auto slot = std::to_string(i);
        const int type = compact ? int(settings.at("primary").integer) : i;
        images_.set(slot, directory_ / "assets" / "noise" / kImages[type], 0, edge);
        scene.imageResource(art_[i], images_.get(slot));
    }
    for (int i = 0; i < (compact ? 1 : 6); ++i) {
        scene.imageFrame(art_[i], dark ? 1 : 0);
        const int type = compact ? int(settings.at("primary").integer) : i;
        auto pose =
            settings.at("animation").integer && model.audioStatus == 1 && model.master > 0 && model.levels[type] > 0
                ? model.breathingPose()
                : Pose{};
        const auto area = scene.bounds(art_[i]);
        pose.pivot = {area.width * .5, area.height * .5};
        scene.pose(art_[i], pose);
        const bool emit = settings.at("animation").integer && model.audioStatus == 1 && model.master > 0 &&
                          model.levels[type] > 0 && (compact || scene.view(cells_[i]).layout.visible) &&
                          (type == 3 || type == 4);
        if (particleKinds_[i] != type) {
            particles_[i].reset();
            particleKinds_[i] = type;
        }
        if (emit && !particles_[i]) {
            ParticleSettings particles;
            particles.maximum = 24;
            particles.seed = uint32_t(type + 1);
            particles.rate = type == 4 ? 5 : 2;
            particles.minimumLife = 1;
            particles.maximumLife = 2.5;
            particles.minimumSpeed = 4;
            particles.maximumSpeed = 12;
            particles.gravity = {0, -2};
            particles.shape = EmissionShape::Line;
            particles.extent = {36, 0};
            particles.size = Keyframes<double>{{{0, .8}, {.3, 1.2}, {1, 0}}};
            particles.opacity = Keyframes<double>{{{0, 0}, {.2, .7}, {1, 0}}};
            particles.colors = {{0, type == 4 ? 0xffcb77ff : 0xdcf499ff}, {1, 0xfff3caff}};
            particles_[i] = std::make_unique<ParticleEmitter>(std::move(particles));
            particles_[i]->wind(NoiseWind({.seed = type + 401,
                                           .strength = type == 4 ? 9. : 5.,
                                           .spatialFrequency = .035,
                                           .temporalFrequency = .45}));
            particles_[i]->origin({50, type == 4 ? 60. : 70.});
        }
        if (particles_[i]) {
            particles_[i]->emitting(emit);
            particles_[i]->visible(emit);
        }
    }
    return scene;
}

void NoiseScene::bind(const CdCreateContext& context) {
    context_ = context;
    images_.bind(context);
    if (scene_) {
        scene_->bind(context);
        if (invalidated_) {
            scene_->onInvalidate(invalidated_);
        }
    }
}

void NoiseScene::cancel() {
    if (scene_) {
        scene_->cancel();
    }
}

bool NoiseScene::advance(double seconds) {
    bool changed = scene_ && scene_->advance(seconds);
    for (auto& emitter : particles_) {
        if (emitter && emitter->active()) {
            emitter->advance(seconds);
            changed = true;
        }
    }
    return changed;
}

bool NoiseScene::active() const {
    return (scene_ && scene_->active()) || std::any_of(particles_.begin(), particles_.end(), [](const auto& emitter) {
               return emitter && emitter->active();
           });
}

void NoiseScene::draw(Canvas& canvas, const IconPainter& icons) {
    scene_->draw(canvas, icons);
    for (int i = 0; i < (compact_ ? 1 : 6); ++i) {
        if (!particles_[i] || !particles_[i]->active()) {
            continue;
        }
        const auto area = scene_->bounds(art_[i]);
        if (area.width <= 0 || area.height <= 0) {
            continue;
        }
        canvas.clipped(area, [&] {
            canvas.saved([&] {
                canvas.translate(area.x, area.y);
                canvas.scale(area.width / 100, area.height / 100);
                particles_[i]->draw(canvas);
            });
        });
    }
}

bool NoiseScene::imagesPending() const {
    return images_.pending();
}

bool NoiseScene::pollImages() {
    return images_.poll();
}

void NoiseScene::pauseImages(bool paused) {
    images_.pause(paused);
    for (auto& emitter : particles_) {
        if (emitter) {
            emitter->paused(paused);
        }
    }
}

void NoiseScene::onInvalidate(std::function<void()> callback) {
    invalidated_ = std::move(callback);
    if (scene_) {
        scene_->onInvalidate(invalidated_);
    }
}

void NoiseScene::onAction(std::function<void(cybercanvas::SceneEvent)> callback) {
    action_ = std::move(callback);
    if (scene_) {
        scene_->onAction(action_);
    }
}

void NoiseScene::onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) {
    diagnostics_ = std::move(callback);
}
}  // namespace atmosphere
