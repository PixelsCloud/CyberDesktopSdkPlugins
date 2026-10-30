#include <CyberCanvas/PanelLayout.h>

#include <algorithm>
#include <cmath>
#include <string_view>

#include "plugins/mechanical-instruments/PhysicsMaterials.h"
#include "plugins/vampire-alice/controllers/beat/Machine.h"

namespace alice_beat {
Instance::Instance(const CdCreateContext& context)
    : settings{{"theme", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light", "system"}},
               {"show_background", CD_SETTING_TOGGLE, 1},
               {"gain", CD_SETTING_NUMBER, 0, 3.5, {}, .25, 5},
               {"balls", CD_SETTING_CHOICE, 0, 0, "36", 0, 0, {"12", "16", "24", "36", "48"}},
               {"running", CD_SETTING_TOGGLE, 1},
               {"reduced_motion", CD_SETTING_TOGGLE, 0}},
      context_(context),
      model_(36, context.plugin_id && std::string_view(context.plugin_id).ends_with(".4x2")),
      renderer_(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                    "assets",
                model_.compact()) {
    safe_.width = model_.compact() ? 400 : 600;
    canvas_.bindGpu(context);
    renderer_.bind(context);
    environment_.bind(context);
    interaction_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
        bool changed = renderer_.pollAssets();
        const auto now = environment_.snapshot().monotonic_ms;
        const bool stale = !lastAudio_ || now < lastAudio_ || now - lastAudio_ > 250 || status_ != CD_DATA_READY;
        if (!frozen()) {
            changed |= audio_.advance(tick.delta, stale, settings.at("gain").number);
            changed |= model_.advance(tick.delta, audio_.signal());
        }
        const bool active = renderer_.pendingAssets() || (!frozen() && (model_.active() || audio_.active()));
        return cdsdk::UpdateStep{changed, active};
    });
    subscribe();
    schedule();
}

Instance::~Instance() {
    watch_.reset();
    statusWatch_.reset();
    updates_.stop();
    cancel();
}

bool Instance::frozen() const {
    return hidden_ || !settings.at("running").integer || settings.at("reduced_motion").integer;
}

void Instance::subscribe() {
    watch_.reset();
    statusWatch_.reset();
    if (frozen()) {
        return;
    }
    const cdsdk::Services services(context_.host, context_.host_context);
    const CdAudioOptions options{sizeof(CdAudioOptions), 63, 30, 0, nullptr};
    statusWatch_ = services.audioStatus(CD_SERVICE_FFT, options, [this](const CdAudioStatus& status) {
        if (status_ != status.status) {
            status_ = status.status;
            invalidate();
            schedule();
        }
    });
    watch_ = services.audio(CD_SERVICE_FFT, options, [this](const cdsdk::AudioSnapshot& data) {
        if (frozen()) {
            return;
        }
        const bool changedStatus = status_ != CD_DATA_READY;
        status_ = CD_DATA_READY;
        audio_.feed(data.samples, data.sampleRate);
        lastAudio_ = environment_.snapshot().monotonic_ms;
        if (changedStatus) {
            invalidate();
        }
        if (audio_.active() || model_.active()) {
            schedule();
        }
    });
    if (watch_.result() != CD_OK) {
        status_ = CD_DATA_UNAVAILABLE;
    }
}

void Instance::schedule() {
    if (hidden_ || (!renderer_.pendingAssets() && (frozen() || (!model_.active() && !audio_.active())))) {
        updates_.stop();
        return;
    }
    updates_.start(16, environment_.snapshot().monotonic_ms);
}

void Instance::arrange() {
    const auto view = renderer_.view(safe_, {model_.width(), kHeight});
    origin_ = view.origin;
    scale_ = view.scale;
}

CdResult Instance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    updates_.beginFrame();
    redraw_.beginFrame();
    const auto environment = environment_.snapshot();
    const auto& theme = settings.at("theme").text;
    const bool light = theme == "light" || (theme == "system" && environment.theme == CD_ENV_THEME_LIGHT);
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        safe_ = safe;
        arrange();
        const bool background = settings.at("show_background").integer != 0;
        canvas.saved([&] {
            canvas.translate(origin_.x - safe.x, origin_.y - safe.y);
            canvas.scale(scale_, scale_);
            renderer_.draw(canvas, model_, audio_.signal(),
                           {light, background, std::string_view(environment.locale).starts_with("en"), status_},
                           scale_);
        });
        std::vector<cybercanvas::HitRegion> regions;
        if (!frozen()) {
            for (size_t i = 0; i < model_.pistons().size(); ++i) {
                const auto& p = model_.pistons()[i].rest;
                const double half = model_.compact() ? 39 : 55;
                regions.push_back(
                    {uint32_t(i + 1), cybercanvas::HitShape::rectangle({p.x - half, p.y - 82, half * 2, 120})});
            }
            for (size_t i = 0; i < model_.paddles().size(); ++i) {
                const auto& p = model_.paddles()[i].rest;
                regions.push_back({uint32_t(i + 10), cybercanvas::HitShape::rectangle(
                                                         {p.x - 15, p.y - 30, model_.compact() ? 102. : 135., 90})});
            }
            if (interaction_.streaming()) {
                for (const auto& ball : model_.balls()) {
                    const auto p = model_.state(ball.body).position;
                    regions.push_back({ball.token,
                                       cybercanvas::HitShape::ellipse({p.x * kUnits - 15, p.y * kUnits - 15, 30, 30}),
                                       cybercanvas::PointerBehavior::Drag, 1});
                }
            }
        }
        if (interaction_.regions(std::move(regions)).drag == cybercanvas::DragPhase::Cancel) {
            model_.cancelDrag();
        }
        arranged_ = true;
    });
    if (renderer_.pendingAssets()) {
        schedule();
    }
    return CD_OK;
}

void Instance::input(const CdInput& input) {
    if (input.kind == CD_INPUT_CANCEL) {
        cancel();
        return;
    }
    if (frozen() || !arranged_ || !std::isfinite(input.x) || !std::isfinite(input.y)) {
        return;
    }
    auto local = input;
    local.x = (input.x - origin_.x) / scale_;
    local.y = (input.y - origin_.y) / scale_;
    const auto result = interaction_.input(local);
    if (result.drag == cybercanvas::DragPhase::Begin) {
        if (!model_.beginDrag(result.target, {local.x, local.y})) {
            interaction_.cancel();
        }
    } else if (result.drag == cybercanvas::DragPhase::Update) {
        model_.drag({local.x, local.y});
    } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
        model_.cancelDrag();
    }
    if (result.clicked >= 1 && result.clicked <= 5) {
        model_.strike(result.clicked - 1);
    } else if (result.clicked == 10 || result.clicked == 11) {
        model_.nudge(result.clicked - 10);
    }
    schedule();
    invalidate();
}

void Instance::cancel() {
    interaction_.cancel();
    model_.cancelDrag();
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    cancel();
    arranged_ = false;
    const auto r = layout.safe_content;
    safe_ = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
                    r.width > 0 && r.height > 0
                ? r
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    arrange();
    invalidate();
}

void Instance::setPaused(bool paused) {
    hidden_ = paused;
    watch_.reset();
    statusWatch_.reset();
    renderer_.pauseAssets(paused);
    cancel();
    audio_.clear();
    lastAudio_ = 0;
    model_.quiet();
    updates_.pause(paused);
    redraw_.pause(paused);
    if (!paused) {
        subscribe();
        invalidate();
    }
    schedule();
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    if (next.at("balls").text == "12" || next.at("balls").text == "16") {
        const CdSettingValue upgraded{sizeof(CdSettingValue), CD_SETTING_CHOICE, "balls", 0, 0, "36"};
        next.apply(&upgraded, 1);
    }
    const bool countChanged = next.at("balls").text != settings.at("balls").text;
    const bool motionChanged = next.at("running").integer != settings.at("running").integer ||
                               next.at("reduced_motion").integer != settings.at("reduced_motion").integer;
    cancel();
    settings = std::move(next);
    if (countChanged) {
        model_.reset(unsigned(std::stoi(settings.at("balls").text)));
    }
    if (motionChanged || countChanged) {
        model_.quiet();
        audio_.clear();
        lastAudio_ = 0;
        subscribe();
    }
    schedule();
    invalidate();
    return CD_OK;
}

void Instance::invalidate() {
    redraw_.invalidate();
}
}  // namespace alice_beat
