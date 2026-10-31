#include <CyberCanvas/PhysicsScene.h>
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Icons.h>
#include <sdk/Scene.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/cpp/Plugin.h>

#include <array>
#include <cstdint>

namespace {
using namespace cybercanvas;
namespace physics = cybercanvas::physics;

class PhysicsSandbox {
public:
    cdsdk::Settings settings{{"count", CD_SETTING_INTEGER, 12, 0, {}, 1, 48},
                             {"speed", CD_SETTING_NUMBER, 0, 1, {}, 0, 2},
                             {"spring", CD_SETTING_TOGGLE, 0},
                             {"debug", CD_SETTING_TOGGLE, 0},
                             {"reduced_motion", CD_SETTING_TOGGLE, 0}};

    explicit PhysicsSandbox(const CdCreateContext& context) {
        environment_.bind(context);
        icons_.bind(context);
        scene_.bind(context);
        redraw_.bind(context);
        updates_.bind(context);
        scene_.onInvalidate([this] {
            invalidate();
        });
        scene_.onAction([this](SceneEvent event) {
            action(event);
        });
        updates_.onInvalidate([this] {
            invalidate();
        });
        View title;
        title.kind = ViewKind::Text;
        title.layout.height = Length::points(30);
        title.layout.shrink = 0;
        title.theme.fontSize = 18;
        title_ = scene_.add(0, title);
        View arena;
        arena.layout.grow = 1;
        arena.layout.minimum.height = 60;
        arena.inheritGap = false;
        arena.paintLocal = [this](Canvas& canvas, Rect area, const WidgetTheme& theme) {
            canvas.box(area.x, area.y, area.width, area.height, 12, theme.track);
            drawSpring(canvas, theme);
        };
        arena_ = scene_.add(0, arena);
        View row;
        row.kind = ViewKind::Row;
        row.layout.height = Length::points(34);
        row.layout.shrink = 0;
        const auto footer = scene_.add(0, row);
        View button;
        button.kind = ViewKind::Button;
        button.layout.grow = 1;
        button.key = 1;
        button.icon = "pause";
        pause_ = scene_.add(footer, button);
        button.key = 2;
        button.icon = "rotate-ccw";
        reset_ = scene_.add(footer, button);
        updates_.onUpdate([this](AnimationTick tick) {
            const bool feedback = scene_.advance(tick.delta);
            bool moved = false;
            if (world_ && !scene_.needsLayout()) {
                moved = world_->advance(tick.delta).steps != 0;
                binding_->update();
            }
            return cdsdk::UpdateStep{feedback || moved, scene_.active() || (world_ && world_->active())};
        });
    }

    ~PhysicsSandbox() {
        updates_.stop();
        scene_.onInvalidate({});
        scene_.cancel();
        binding_.reset();
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
        const auto environment = environment_.snapshot();
        const bool chinese = std::string_view(environment.locale).starts_with("zh");
        const auto theme = WidgetTheme::desktop(environment.theme == CD_ENV_THEME_LIGHT);
        scene_.theme(theme);
        scene_.text(title_, settings.at("spring").integer
                                ? (chinese ? L"弹簧沙盒 · 拖动小球" : L"Spring sandbox · drag a ball")
                                : (chinese ? L"重力沙盒 · 拖动小球" : L"Gravity sandbox · drag a ball"));
        scene_.text(pause_, userPaused_ ? (chinese ? L"继续" : L"Resume") : (chinese ? L"暂停" : L"Pause"));
        scene_.icon(pause_, userPaused_ ? "play" : "pause");
        scene_.text(reset_, chinese ? L"重新开始" : L"Reset");
        canvas_.render(frame, layout_.safe_content, [&](auto& canvas, CdRect safe) {
            origin_ = {safe.x, safe.y};
            const Rect area{0, 0, safe.width, safe.height};
            canvas.box(0, 0, area.width, area.height, 16, theme.surface);
            scene_.arrange(canvas, inset(area, {12, 12, 12, 12}), frame.device_pixel_ratio);
            const auto bounds = scene_.layoutBounds(arena_);
            if (resetPending_ || bounds.width != arenaSize_.width || bounds.height != arenaSize_.height) {
                rebuild(bounds);
                scene_.arrange(canvas, inset(area, {12, 12, 12, 12}), frame.device_pixel_ratio);
            }
            binding_->update();
            scene_.draw(canvas, icons_.painter());
            if (settings.at("debug").integer) {
                binding_->debugDraw(canvas);
            }
        });
        schedule();
        return CD_OK;
    }

    void input(const CdInput& event) {
        if (!binding_ || scene_.needsLayout()) {
            return;
        }
        const auto result = scene_.input(event, origin_);
        const Point point{event.x - origin_.x, event.y - origin_.y};
        switch (result.pointer.drag) {
            case DragPhase::Begin:
                if (!frozen()) {
                    binding_->beginDrag(point, 1000, origin_);
                }
                break;
            case DragPhase::Update:
                binding_->drag(point);
                break;
            case DragPhase::End:
            case DragPhase::Cancel:
                binding_->cancelDrag();
                break;
            default:
                break;
        }
        if (!scene_.router().captured()) {
            binding_->cancelDrag();
        }
        action(result);
        schedule();
    }

    void layoutChanged(const CdLayout& layout) {
        scene_.cancel();
        if (binding_) {
            binding_->cancelDrag();
        }
        layout_ = layout;
    }

    void setPaused(bool paused) {
        hidden_ = paused;
        scene_.cancel();
        if (binding_) {
            binding_->cancelDrag();
        }
        updates_.pause(paused);
        redraw_.pause(paused);
        synchronize();
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        const auto previous = settings.at("count").integer;
        const auto previousSpring = settings.at("spring").integer;
        const auto result = settings.apply(values, count);
        if (result == CD_OK) {
            resetPending_ |=
                previous != settings.at("count").integer || previousSpring != settings.at("spring").integer;
            synchronize();
        }
        return result;
    }

    std::vector<uint8_t> saveState() const {
        return {'P', 'H', 1, uint8_t(userPaused_)};
    }

    CdResult restoreState(std::span<const uint8_t> bytes) {
        if (bytes.empty()) {
            return CD_OK;
        }
        if (bytes.size() != 4 || bytes[0] != 'P' || bytes[1] != 'H' || bytes[2] != 1 || bytes[3] > 1) {
            return CD_ERROR_ARGUMENT;
        }
        userPaused_ = bytes[3] != 0;
        synchronize();
        return CD_OK;
    }

    std::array<uint64_t, 4> physicsCounters() const {
        if (!world_) {
            return {};
        }
        const auto value = world_->stats();
        return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
    }

private:
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Icons icons_;
    cdsdk::Scene scene_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    std::unique_ptr<physics::World> world_;
    std::unique_ptr<physics::SceneBinding> binding_;
    Scene::Node title_ = 0, arena_ = 0, pause_ = 0, reset_ = 0;
    CdLayout layout_{};
    Point origin_;
    Size arenaSize_;
    physics::Body springBody_;
    Point springAnchor_;
    bool hidden_ = false, userPaused_ = false, resetPending_ = true;

    bool frozen() const {
        return hidden_ || userPaused_ || settings.at("reduced_motion").integer != 0;
    }

    void drawSpring(Canvas& canvas, const WidgetTheme& theme) const {
        if (!world_ || !world_->valid(springBody_)) {
            return;
        }
        const auto position = world_->interpolated(springBody_).position;
        const Point first{springAnchor_.x * 100, springAnchor_.y * 100};
        const Point last{position.x * 100, position.y * 100};
        const double dx = last.x - first.x, dy = last.y - first.y;
        const double distance = std::hypot(dx, dy);
        if (distance < 1) {
            return;
        }
        Point previous = first;
        const auto color = (theme.foreground & 0xffffff00) | 0x90;
        for (int segment = 1; segment <= 16; ++segment) {
            const double t = segment / 16.;
            const double offset = segment == 16 ? 0 : (segment % 2 ? 3. : -3.);
            const Point next{first.x + dx * t - dy / distance * offset, first.y + dy * t + dx / distance * offset};
            canvas.line(previous.x, previous.y, next.x, next.y, color, 1.5);
            previous = next;
        }
        canvas.oval(first.x, first.y, 3, 3, theme.foreground);
    }

    void schedule() {
        if (!hidden_ && (scene_.active() || (world_ && world_->active()))) {
            updates_.start(16);
        }
    }

    void synchronize() {
        if (world_) {
            world_->paused(frozen());
            world_->speed(settings.at("speed").number);
            if (frozen() || settings.at("speed").number == 0) {
                binding_->cancelDrag();
                scene_.cancel();
            }
        }
        invalidate();
        schedule();
    }

    void action(SceneEvent event) {
        if (event.clicked && event.key == 1) {
            userPaused_ = !userPaused_;
            synchronize();
        }
        if (event.clicked && event.key == 2) {
            resetPending_ = true;
            invalidate();
        }
        schedule();
    }

    void rebuild(Rect area) {
        scene_.cancel();
        binding_.reset();
        world_ = std::make_unique<physics::World>(physics::WorldOptions{.collectEvents = false});
        springBody_ = {};
        scene_.clear(arena_);
        arenaSize_ = {area.width, area.height};
        const double width = std::max(.4, area.width / 100), height = std::max(.4, area.height / 100);
        const auto walls = world_->createBody({.type = physics::BodyType::Static});
        world_->box(walls, {width / 2, .05}, {width / 2, height});
        world_->box(walls, {.05, height / 2}, {0, height / 2});
        world_->box(walls, {.05, height / 2}, {width, height / 2});
        world_->box(walls, {width / 2, .05}, {width / 2, 0});
        binding_ = std::make_unique<physics::SceneBinding>(*world_, scene_, arena_);
        const auto count = settings.at("count").integer;
        const double radius = std::clamp(std::sqrt(width * height / (count * 12.)), .04, .17);
        constexpr uint32_t palette[]{0x60b9edff, 0x9b8be5ff, 0xf1b96bff, 0x77ccaeff, 0xec8f9eff};
        const int columns = std::max(1, int((width - .12) / (radius * 2.4)));
        for (int i = 0; i < count; ++i) {
            const double x =
                std::clamp(.08 + radius + (i % columns) * radius * 2.4, radius + .06, width - radius - .06);
            const double y =
                std::clamp(.08 + radius + (i / columns) * radius * 2.4, radius + .06, height - radius - .06);
            const bool suspended = i == 0 && settings.at("spring").integer;
            const double length = std::clamp(height * .35, .12, std::max(.12, height - radius * 2 - .12));
            springAnchor_ = {width / 2, .1};
            const Point position =
                suspended ? Point{width / 2 + std::min(width * .15, .25), .1 + length * .7} : Point{x, y};
            const auto body =
                world_->createBody({.position = position, .linearDamping = suspended ? .8 : 0, .angularDamping = .2});
            world_->circle(body, {}, radius, {.friction = .4, .restitution = .35});
            if (suspended) {
                world_->distance(walls, body, springAnchor_, {}, length, 3, .5);
                springBody_ = body;
            }
            View ball;
            ball.kind = ViewKind::Decoration;
            ball.layout.width = ball.layout.height = Length::points(radius * 200);
            ball.layout.shrink = 0;
            ball.draggable = true;
            ball.round = true;
            ball.hitSlop = 0;
            ball.accessibleName = L"Ball " + std::to_wstring(i + 1);
            const auto color = palette[i % std::size(palette)];
            ball.paintLocal = [color](Canvas& canvas, Rect bounds, const WidgetTheme&) {
                const double r = bounds.width / 2;
                canvas.oval(r, r, r, r, color);
                canvas.oval(r * .65, r * .6, r * .23, r * .16, 0xffffff80);
                canvas.oval(r * 1.2, r * 1.45, r * .15, r * .08, 0x172b4938);
            };
            binding_->bind(body, scene_.add(arena_, std::move(ball)));
        }
        resetPending_ = false;
        synchronize();
    }
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<PhysicsSandbox>::query(abi, output, size, "org.cyberdesktop.sdk.physics-sandbox",
                                                       "1.0.0");
}
