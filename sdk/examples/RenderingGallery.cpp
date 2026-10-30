#include <CyberCanvas/Charts.h>
#include <CyberCanvas/Motion.h>
#include <CyberCanvas/Particles.h>
#include <CyberCanvas/Text.h>
#include <CyberCanvas/Vector.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Scene.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/cpp/Plugin.h>
#include <sdk/cpp/Services.h>

namespace {
using namespace cybercanvas;

class RenderingGallery {
public:
    cdsdk::Settings settings{{"animate", CD_SETTING_TOGGLE, 1},
                             {"speed", CD_SETTING_NUMBER, 0, 1, {}, 0, 2},
                             {"chart", CD_SETTING_CHOICE, 0, 0, "bars", 0, 0, {"bars", "donut", "heatmap"}}};

    explicit RenderingGallery(const CdCreateContext& context)
        : context_(context.host_context),
          store_(budget_),
          player_(store_.request(std::filesystem::u8path(context.plugin_directory),
                                 vectorAnimation("orbit.json", VectorKind::Lottie))) {
        std::memcpy(&host_, context.host, std::min<size_t>(context.host->struct_size, sizeof(host_)));
        host_.input.media.base.struct_size = uint32_t(std::min<size_t>(context.host->struct_size, sizeof(host_)));
        scene_.bind(context);
        environment_.bind(context);
        redraw_.bind(context);
        updates_.bind(context);
        scene_.onInvalidate([this] {
            invalidate();
        });
        updates_.onInvalidate([this] {
            invalidate();
        });
        View title;
        title.kind = ViewKind::Decoration;
        title.layout.height = Length::points(38);
        title.layout.shrink = 0;
        title.paintLocal = [this](Canvas& canvas, Rect bounds, const WidgetTheme& theme) {
            const bool light = environment_.snapshot().theme == CD_ENV_THEME_LIGHT;
            if (!titleText_ || titleWidth_ != bounds.width || titleLight_ != light) {
                titleWidth_ = bounds.width;
                titleLight_ = light;
                TextSpan span;
                span.text = L"Canvas Lab";
                span.style = {.size = 24, .family = L"Microsoft YaHei", .bold = true};
                span.ink = {light ? 0x366d88ff : 0xbbe8ffff, light ? 0x7064b8ff : 0xbfb0ffff, .5, theme.surface};
                titleText_ = std::make_unique<StyledText>(
                    canvas, std::span(&span, 1), StyledTextOptions{.available = {bounds.width, bounds.height}});
            }
            titleText_->draw(canvas);
        };
        scene_.add(0, title);
        View hero;
        hero.kind = ViewKind::Column;
        hero.layout.height = Length::percent(35);
        hero.layout.shrink = 0;
        hero.inheritGap = false;
        hero.paintLocal = [this](Canvas& canvas, Rect area, const WidgetTheme& theme) {
            canvas.box(0, 0, area.width, area.height, 14, theme.track);
            canvas.saved([&] {
                canvas.translate(area.width * .5, area.height * .85);
                particles_.draw(canvas);
            });
        };
        hero_ = scene_.add(0, hero);
        View art;
        art.kind = ViewKind::Image;
        art.layout.grow = 1;
        art_ = scene_.add(hero_, art);
        View status;
        status.kind = ViewKind::Text;
        status.layout.height = Length::points(25);
        status.layout.shrink = 0;
        status.theme.fontSize = 11;
        status_ = scene_.add(0, status);
        View chart;
        chart.kind = ViewKind::Decoration;
        chart.layout.grow = 1;
        chart.clickable = true;
        chart.semanticRole = SemanticRole::List;
        chart.accessibleName = L"CPU chart; arrow keys select a sample";
        chart.semanticValue = [this] {
            for (const auto& point : chart_.data()) {
                if (point.id == chart_.selected() && point.value) {
                    return point.label + L" " + std::to_wstring(int(std::round(*point.value))) + L"%";
                }
            }
            return std::to_wstring(chart_.data().size()) + L" samples";
        };
        chart.paint = [this](Canvas& canvas, Rect area, const WidgetTheme& theme) {
            chart_.draw(canvas, area, theme);
        };
        chart.onKey = [this](KeyEvent event) {
            SceneEvent result;
            result.handled = chart_.key(event.key);
            if (result.handled) {
                scene_.invalidate();
            }
            return result;
        };
        chartNode_ = scene_.add(0, chart);
        motion_.append(1, Keyframes<Point>{{{0, {0, 0}}, {2, {0, -3}, Ease::InOutSine}}},
                       [this](Point value) {
                           auto pose = scene_.view(hero_).transform;
                           pose.translation = value;
                           scene_.transform(hero_, pose);
                       },
                       {.repeat = Repeat::PingPong, .cycles = 0});
        motion_.join(2, Keyframes<double>{{{0, 1}, {2, .8, Ease::InOutSine}}},
                     [this](double value) {
                         scene_.transform(status_, {.opacity = value});
                     },
                     {.repeat = Repeat::PingPong, .cycles = 0});
        updates_.onUpdate([this](AnimationTick tick) {
            const bool changed = motion_.advance(tick.delta) || scene_.active();
            scene_.advance(tick.delta);
            motion_.dispatch();
            player_.advance(tick.delta);
            particles_.advance(tick.delta * settings.at("speed").number);
            const auto snapshot = player_.snapshot();
            const bool decoded = snapshot.revision != revision_;
            revision_ = snapshot.revision;
            return cdsdk::UpdateStep{changed || decoded || particles_.active(), running()};
        });
        subscribe();
        synchronize();
    }

    ~RenderingGallery() {
        updates_.stop();
        telemetry_.reset();
        scene_.onInvalidate({});
    }

    void invalidate() {
        redraw_.invalidate();
    }

    CdResult render(const CdFrame& frame) {
        if (!canvas_.valid(frame)) {
            return CD_ERROR_ARGUMENT;
        }
        updates_.beginFrame();
        redraw_.beginFrame();
        const auto theme = WidgetTheme::desktop(environment_.snapshot().theme == CD_ENV_THEME_LIGHT);
        scene_.theme(theme);
        const auto snapshot = player_.snapshot();
        scene_.imageResource(art_, {snapshot.state, snapshot.image, snapshot.error});
        canvas_.render(frame, layout_.safe_content, [&](auto& canvas, CdRect safe) {
            origin_ = {safe.x, safe.y};
            const bool compact = safe.height < 260;
            scene_.visible(hero_, !compact);
            player_.visible(!compact);
            particles_.visible(!compact);
            scene_.text(status_, snapshot.state == ImageState::Failed ? L"Animation unavailable"
                                 : history_.empty()                   ? L"Waiting for CPU telemetry"
                                 : safe.width < 320                   ? L"CPU history"
                                                                      : L"CPU history · hover or use arrow keys");
            canvas.box(0, 0, safe.width, safe.height, 16, theme.surface);
            scene_.arrange(canvas, inset({0, 0, safe.width, safe.height}, {14, 14, 14, 14}), frame.device_pixel_ratio);
            const auto art = scene_.layoutBounds(art_);
            player_.size(uint32_t(std::max(1., std::ceil(art.width * frame.device_pixel_ratio))),
                         uint32_t(std::max(1., std::ceil(art.height * frame.device_pixel_ratio))));
            scene_.draw(canvas);
        });
        if (running()) {
            updates_.start(16);
        }
        return CD_OK;
    }

    void input(const CdInput& input) {
        if (scene_.needsLayout()) {
            return;
        }
        scene_.input(input, origin_);
        if (chart_.hover(scene_.layoutBounds(chartNode_), {input.x - origin_.x, input.y - origin_.y})) {
            scene_.invalidate();
        }
        if (running()) {
            updates_.start(16);
        }
    }

    void layoutChanged(const CdLayout& layout) {
        scene_.cancel();
        layout_ = layout;
    }

    void setPaused(bool paused) {
        hidden_ = paused;
        updates_.pause(paused);
        redraw_.pause(paused);
        scene_.cancel();
        telemetry_.reset();
        if (!paused) {
            subscribe();
        }
        synchronize();
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        const auto old = settings.at("chart").text;
        const auto result = settings.apply(values, count);
        if (result == CD_OK) {
            if (old != settings.at("chart").text) {
                refreshChart();
            }
            synchronize();
        }
        return result;
    }

    std::vector<uint8_t> saveState() const {
        return {'R', 'G', 1};
    }

    CdResult restoreState(std::span<const uint8_t> bytes) {
        return bytes.empty() || (bytes.size() == 3 && bytes[0] == 'R' && bytes[1] == 'G' && bytes[2] == 1)
                   ? CD_OK
                   : CD_ERROR_ARGUMENT;
    }

private:
    CdServiceHostApi host_{};
    void* context_;
    cdsdk::Canvas canvas_;
    cdsdk::Scene scene_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    std::shared_ptr<MemoryBudget> budget_ = std::make_shared<MemoryBudget>();
    AnimationStore store_;
    FramePlayer player_;
    Timeline motion_;
    ParticleEmitter particles_{[] {
        ParticleSettings settings;
        settings.maximum = 32;
        settings.rate = 5;
        settings.minimumLife = 1.5;
        settings.maximumLife = 3;
        settings.gravity = {0, -2};
        settings.shape = EmissionShape::Line;
        settings.extent = {180, 0};
        settings.size = Keyframes<double>{{{0, 1.5}, {1, 0}}};
        settings.colors = {{0, 0xa4baffb0}, {1, 0xbdeed000}};
        return settings;
    }()};
    DataChart chart_;
    cdsdk::SystemWatch telemetry_;
    std::vector<ChartDatum> history_;
    std::unique_ptr<StyledText> titleText_;
    double titleWidth_ = -1;
    bool titleLight_ = false, hidden_ = false;
    Scene::Node hero_ = 0, art_ = 0, status_ = 0, chartNode_ = 0;
    CdLayout layout_{};
    Point origin_;
    uint64_t revision_ = 0;
    uint32_t nextSample_ = 1;

    bool running() const {
        return !hidden_ && (scene_.active() || motion_.active() || particles_.active() || player_.active() ||
                            player_.snapshot().state == ImageState::Loading);
    }

    void synchronize() {
        const bool paused = hidden_ || !settings.at("animate").integer || settings.at("speed").number == 0;
        motion_.paused(paused);
        motion_.speed(settings.at("speed").number);
        particles_.paused(paused);
        player_.paused(paused);
        player_.speed(settings.at("speed").number);
        invalidate();
        if (running()) {
            updates_.start(16);
        }
    }

    void subscribe() {
        telemetry_ = cdsdk::Services(&host_.input.media.base, context_).system(1000, [this](const auto& value) {
            const auto cpu = value.data.base.cpu_percent;
            const bool valid = (value.data.base.struct_size < sizeof(CdSystemInfoEx) ||
                                (value.data.valid_fields & CD_SYSTEM_CPU_VALID)) &&
                               std::isfinite(cpu);
            history_.push_back(
                {nextSample_++, L"CPU", valid ? std::optional(std::clamp(cpu, 0., 100.)) : std::nullopt});
            if (history_.size() > 48) {
                history_.erase(history_.begin());
            }
            refreshChart();
            scene_.invalidate();
        });
    }

    void refreshChart() {
        const auto kind = settings.at("chart").text;
        const auto selected = chart_.selected();
        chart_ = DataChart(kind == "donut"     ? ChartKind::Donut
                           : kind == "heatmap" ? ChartKind::Heatmap
                                               : ChartKind::Bars);
        chart_.axis(std::make_shared<NumericAxis>(NumericAxis{0, 100}));
        chart_.columns(12);
        chart_.formatter([](double value) {
            return std::to_wstring(int(std::round(value))) + L"%";
        });
        if (kind == "donut" && !history_.empty() && history_.back().value) {
            chart_.data({{1, L"CPU", history_.back().value, 0x8fa9e9ff},
                         {2, L"Available", 100 - *history_.back().value, 0x687a9170}});
        } else {
            chart_.data(history_);
        }
        if (selected && std::any_of(chart_.data().begin(), chart_.data().end(), [=](const auto& point) {
                return point.id == selected && point.value;
            })) {
            chart_.select(selected);
        }
    }
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<RenderingGallery>::query(abi, output, size, "org.cyberdesktop.sdk.rendering-gallery",
                                                         "1.0.0");
}
