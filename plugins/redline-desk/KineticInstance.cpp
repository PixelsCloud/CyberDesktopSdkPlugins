#include <sdk/Environment.h>
#include <sdk/Interaction.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/cpp/Plugin.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "plugins/beat-machine/Machine.h"
#include "plugins/redline-desk/Kinetics.h"
#include "plugins/redline-desk/PercussionAudio.h"

namespace redline {
namespace {
class KineticInstance {
public:
    explicit KineticInstance(const CdCreateContext& context)
        : settings{{"running", CD_SETTING_TOGGLE, 1},
                   {"reduced_motion", CD_SETTING_TOGGLE, 0},
                   {"gain", CD_SETTING_NUMBER, 0, 3.5, "", .25, 5}},
          context_(context) {
        skin_.bind(context);
        const auto& id = std::string_view(context.plugin_id);
        kind_ = id.find("beat-machine") != std::string_view::npos ? (id.ends_with("4x2") ? 2 : 3)
                                                                  : (id.ends_with("4x2") ? 0 : 1);
        const auto art = skin_.size();
        logical_ = {kind_ == 0 || kind_ == 2 ? 308. : 468., 0};
        logical_.height = logical_.width * art.height / art.width;
        model_ = std::make_unique<KineticModel>(kind_, logical_);
        environment_.bind(context);
        redraw_.bind(context);
        interaction_.bind(context);
        updates_.bind(context);
        updates_.onInvalidate([this] {
            redraw_.invalidate();
        });
        updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
            bool changed = skin_.poll();
            if (!frozen()) {
                const auto now = environment_.snapshot().monotonic_ms;
                const bool stale = !lastAudio_ || now < lastAudio_ || now - lastAudio_ > 250;
                audio_.advance(tick.delta, stale, settings.at("gain").number,
                               [this](unsigned channel, double strength) {
                                   model_->strike(channel, strength);
                               });
                changed |= model_->advance(tick.delta);
            }
            changed |= kind_ < 2;
            return cdsdk::UpdateStep{changed, skin_.pending() || (!frozen() && model_->active()) || kind_ < 2};
        });
        subscribe();
        schedule();
    }

    ~KineticInstance() {
        watch_.reset();
        updates_.stop();
        interaction_.cancel();
        model_->cancelDrag();
    }

    cdsdk::Settings settings;

    CdResult render(const CdFrame& frame) {
        updates_.beginFrame();
        redraw_.beginFrame();
        const bool okay = skin_.render(frame, safe_, [&](Canvas& canvas) {
            const double scale = skin_.size().width / logical_.width;
            canvas.saved([&] {
                canvas.scale(scale, scale);
                model_->draw(canvas, [&](std::string_view name, Rect bounds) {
                    if (name.starts_with("drumFace")) {
                        const auto source = skin_.region(name);
                        const Rect face{source.x / scale, source.y / scale, source.width / scale,
                                        source.height / scale};
                        const Point center{face.x + face.width / 2, face.y + face.height / 2};
                        auto mask = canvas.path();
                        mask.moveTo({face.x + face.width, center.y})
                            .arcTo({face.x, center.y}, {face.width / 2, face.height / 2})
                            .arcTo({face.x + face.width, center.y}, {face.width / 2, face.height / 2})
                            .close();
                        canvas.masked(mask, [&] {
                            skin_.part(canvas, name, {face.x, face.y + bounds.y, face.width, face.height});
                        });
                        if (bounds.width > 0) {
                            canvas.oval(center.x, center.y, face.width * .46, face.height * .46, 0xff6258cc, 1.2);
                        }
                        return;
                    }
                    skin_.part(canvas, name, bounds);
                });
                if (kind_ < 2) {
                    const auto time = cdsdk::Environment::localTime(environment_.snapshot());
                    const Rect r{logical_.width * (kind_ == 0 ? .55 : .66), logical_.height * .16,
                                 logical_.width * (kind_ == 0 ? .41 : .31), logical_.height * .26};
                    const auto value = std::format(L"{:02}:{:02}:{:02}", time.tm_hour, time.tm_min, time.tm_sec);
                    skin_.label(canvas, value, r, r.height * .77);
                }
            });
        });
        if (!frozen()) {
            std::vector<cybercanvas::HitRegion> regions;
            if (kind_ < 2) {
                regions.push_back({1, cybercanvas::HitShape::rectangle({0, 0, logical_.width, logical_.height}),
                                   cybercanvas::PointerBehavior::Drag});
            } else {
                for (unsigned i = 0; i < (kind_ == 2 ? 2u : 3u); ++i) {
                    const double count = kind_ == 2 ? 2 : 3;
                    regions.push_back(
                        {i + 1, cybercanvas::HitShape::rectangle(
                                    {logical_.width * i / count, 0, logical_.width / count, logical_.height})});
                }
            }
            interaction_.regions(std::move(regions));
        } else {
            interaction_.regions({});
        }
        if (skin_.pending()) {
            schedule();
        }
        return okay ? CD_OK : CD_ERROR_ARGUMENT;
    }

    void input(const CdInput& input) {
        if (input.kind == CD_INPUT_CANCEL) {
            model_->cancelDrag();
            interaction_.cancel();
            return;
        }
        if (frozen() || !std::isfinite(input.x) || !std::isfinite(input.y)) {
            return;
        }
        const double s = std::min((safe_.width - 12.5) / logical_.width, (safe_.height - 12.5) / logical_.height);
        if (s <= 0) {
            return;
        }
        const Point point{(input.x - safe_.x - (safe_.width - logical_.width * s) / 2) / s,
                          (input.y - safe_.y - (safe_.height - logical_.height * s) / 2) / s};
        auto local = input;
        local.x = point.x;
        local.y = point.y;
        const auto result = interaction_.input(local);
        if (result.drag == cybercanvas::DragPhase::Begin) {
            model_->beginDrag(point);
        } else if (result.drag == cybercanvas::DragPhase::Update) {
            model_->drag(point);
        } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
            model_->cancelDrag();
        }
        if (kind_ >= 2 && result.clicked > 0) {
            model_->strike(result.clicked - 1);
        }
        schedule();
        redraw_.invalidate();
    }

    void layoutChanged(const CdLayout& layout) {
        interaction_.cancel();
        model_->cancelDrag();
        if (std::isfinite(layout.safe_content.width) && std::isfinite(layout.safe_content.height) &&
            layout.safe_content.width > 0 && layout.safe_content.height > 0) {
            safe_ = layout.safe_content;
        }
        redraw_.invalidate();
    }

    void setPaused(bool value) {
        hidden_ = value;
        interaction_.cancel();
        model_->quiet();
        skin_.pause(value);
        audio_.clear();
        lastAudio_ = 0;
        subscribe();
        if (value) {
            updates_.stop();
        } else {
            schedule();
            redraw_.invalidate();
        }
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        auto next = settings;
        const auto result = next.apply(values, count);
        if (result != CD_OK) {
            return result;
        }
        settings = std::move(next);
        interaction_.cancel();
        model_->quiet();
        audio_.clear();
        lastAudio_ = 0;
        subscribe();
        schedule();
        redraw_.invalidate();
        return CD_OK;
    }

private:
    bool frozen() const {
        return hidden_ || !settings.at("running").integer || settings.at("reduced_motion").integer;
    }

    void subscribe() {
        watch_.reset();
        if (kind_ < 2 || frozen()) {
            return;
        }
        const cdsdk::Services services(context_.host, context_.host_context);
        const CdAudioOptions options{sizeof(CdAudioOptions), 63, 30, 0, nullptr};
        watch_ = services.audio(CD_SERVICE_FFT, options, [this](const cdsdk::AudioSnapshot& data) {
            if (frozen()) {
                return;
            }
            audio_.feed(data.samples, data.sampleRate);
            lastAudio_ = environment_.snapshot().monotonic_ms;
            schedule();
        });
        if (watch_.result() != CD_OK && context_.host->log) {
            const auto message = std::format("RedLine audio.spectrum subscription failed: {} ({})", context_.plugin_id,
                                             int(watch_.result()));
            context_.host->log(context_.host_context, 2, message.data(), uint32_t(message.size()));
        }
    }

    void schedule() {
        if (!hidden_) {
            updates_.start(frozen() && kind_ < 2 ? 1000 : 16, environment_.snapshot().monotonic_ms);
        }
    }

    CdCreateContext context_;
    int kind_ = 0;
    Size logical_;
    Skin skin_;
    std::unique_ptr<KineticModel> model_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    cdsdk::AudioWatch watch_;
    PercussionAudio audio_;
    CdRect safe_{0, 0, 480, 200};
    uint64_t lastAudio_ = 0;
    bool hidden_ = false;
};
}  // namespace

CdResult createKinetic(int, const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    CdPluginApi api{};
    const auto result = cdsdk::PluginAdapter<KineticInstance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api),
                                                                     context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
}  // namespace redline
