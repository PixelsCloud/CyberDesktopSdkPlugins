#include <algorithm>
#include <cmath>
#include <optional>

#include "plugins/pixel-desk/PixelMatrix.h"

namespace pixeldesk {
MatrixInstance::MatrixInstance(const CdCreateContext& context)
    : settings(matrixDefaults(matrixSpec(context.plugin_id))),
      spec_(matrixSpec(context.plugin_id)),
      host_(*context.host),
      context_(context.host_context),
      services_(context.host, context.host_context),
      renderer_(context, spec_) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    lastTick_ = environment_.snapshot().monotonic_ms;
    subscribe(settings);
}

MatrixInstance::~MatrixInstance() {
    watch_.reset();
    legacy_.reset();
    timer_.stop();
    renderer_.onChange({});
}

CdResult MatrixInstance::subscribe(const cdsdk::Settings& candidate) {
    if (hidden_) {
        return CD_OK;
    }

    struct Gate {
        bool committed = false;
        std::vector<float> pending;
    };

    const auto gate = std::make_shared<Gate>();
    const auto deliver = [this, gate](const float* values, uint32_t count) {
        if (!values || !count || count > 4096) {
            return;
        }
        if (gate->committed) {
            receive(values, count);
        } else {
            gate->pending.assign(values, values + count);
        }
    };
    const CdAudioOptions options{sizeof(CdAudioOptions), uint32_t(widgetplugin::Spectrum::count),
                                 candidate.at("reduced_motion").integer ? 10u : 30u, 0, nullptr};
    auto next = services_.audio(CD_SERVICE_FFT, options, [deliver](const cdsdk::AudioSnapshot& sample) {
        if (sample.samples.size() >= sample.count) {
            deliver(sample.samples.data(), sample.count);
        }
    });
    if (next.result() == CD_OK) {
        watch_ = std::move(next);
        legacy_.reset();
    } else {
        // Retain the SDK's supported legacy host path for older desktop versions.
        if (next.result() != CD_ERROR_UNSUPPORTED || !host_.subscribe_audio || !host_.unsubscribe) {
            return next.result();
        }
        uint64_t token = 0;
        const auto result = host_.subscribe_audio(
            context_, uint32_t(widgetplugin::Spectrum::count),
            [](void* pointer, const float* values, uint32_t count, uint32_t, uint32_t) {
                try {
                    static_cast<MatrixInstance*>(pointer)->receive(values, count);
                } catch (...) {
                }
            },
            this, &token);
        cdsdk::Subscription legacy(&host_, context_, token);
        if (result != CD_OK || !token) {
            return result == CD_OK ? CD_ERROR_INTERNAL : result;
        }
        legacy_ = std::move(legacy);
        watch_.reset();
    }
    gate->committed = true;
    if (!gate->pending.empty()) {
        receive(gate->pending.data(), uint32_t(gate->pending.size()));
    }
    return CD_OK;
}

void MatrixInstance::receive(const float* values, uint32_t count) {
    if (hidden_ || !values || !count || count > 4096) {
        return;
    }
    spectrum_.feed(values, count);
    lastAudio_ = environment_.snapshot().monotonic_ms;
    received_ = true;
    if (schedule(settings) != CD_OK) {
        timer_.stop();
    }
}

CdResult MatrixInstance::schedule(const cdsdk::Settings& candidate) {
    const auto now = environment_.snapshot().monotonic_ms;
    const bool stale =
        !received_ || now < lastAudio_ || now - lastAudio_ > (spec_.form == MatrixForm::Dots ? 300 : 200);
    bool lit = false, moving = false;
    for (size_t i = 0; i < widgetplugin::Spectrum::count; ++i) {
        const float target =
            stale ? 0 : std::pow(std::clamp(float(spectrum_.input[i] * candidate.at("gain").number), 0.f, 1.f), .72f);
        lit |= target > 0 || spectrum_.level[i] > 0 || spectrum_.peak[i] > 0;
        moving |= std::abs(target - spectrum_.level[i]) > .0001f || spectrum_.peak[i] > spectrum_.level[i] + .0001f;
    }
    if (hidden_ || !lit) {
        timer_.stop();
        lastTick_ = now;
        return CD_OK;
    }
    return timer_.replace(candidate.at("reduced_motion").integer ? 100u : moving ? 33u : 250u, tick, this);
}

void CD_CALL MatrixInstance::tick(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<MatrixInstance*>(pointer);
    try {
        if (self.hidden_) {
            self.timer_.stop();
            return;
        }
        const auto now = self.environment_.snapshot().monotonic_ms;
        const float seconds = now >= self.lastTick_ ? float(std::min<uint64_t>(250, now - self.lastTick_)) * .001f : 0;
        self.lastTick_ = now;
        const bool stale = !self.received_ || now < self.lastAudio_ ||
                           now - self.lastAudio_ > (self.spec_.form == MatrixForm::Dots ? 300 : 200);
        bool changed = false;
        if (self.settings.at("reduced_motion").integer) {
            auto levels = self.spectrum_.input;
            for (auto& level : levels) {
                level =
                    stale ? 0 : std::pow(std::clamp(float(level * self.settings.at("gain").number), 0.f, 1.f), .72f);
            }
            changed = levels != self.spectrum_.level || levels != self.spectrum_.peak;
            self.spectrum_.level = self.spectrum_.peak = self.spectrum_.publishedLevel = self.spectrum_.publishedPeak =
                levels;
            self.spectrum_.hold.fill(0);
        } else {
            changed = self.spectrum_.advance(
                seconds, stale, self.settings.at("gain").number, double(self.settings.at("release").integer),
                self.spec_.form == MatrixForm::Rods ? widgetplugin::Spectrum::PeakMotion::FastBall
                                                    : widgetplugin::Spectrum::PeakMotion::Default);
        }
        if (changed) {
            self.invalidate();
        }
        if (self.schedule(self.settings) != CD_OK) {
            self.timer_.stop();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

CdResult MatrixInstance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    return renderer_.render(settings, spectrum_, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult MatrixInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const bool changedRate = next.at("reduced_motion").integer != settings.at("reduced_motion").integer;
    if (schedule(next) != CD_OK) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (changedRate && !hidden_) {
        const auto subscribed = subscribe(next);
        if (subscribed != CD_OK) {
            schedule(settings);
            return subscribed;
        }
    }
    settings = std::move(next);
    schedule(settings);
    invalidate();
    return CD_OK;
}

void MatrixInstance::layoutChanged(const CdLayout& layout) {
    if (std::isfinite(layout.logical_width) && std::isfinite(layout.logical_height) && layout.logical_width > 0 &&
        layout.logical_height > 0) {
        const auto r = layout.safe_content;
        safe_ = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
                        r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 && r.x + r.width <= layout.logical_width &&
                        r.y + r.height <= layout.logical_height
                    ? r
                    : CdRect{0, 0, layout.logical_width, layout.logical_height};
    }
}

void MatrixInstance::setPaused(bool paused) {
    if (hidden_ == paused) {
        return;
    }
    hidden_ = paused;
    renderer_.pause(paused);
    redraw_.pause(paused);
    timer_.stop();
    if (paused) {
        watch_.reset();
        legacy_.reset();
    } else {
        spectrum_ = {};
        received_ = false;
        lastTick_ = environment_.snapshot().monotonic_ms;
        subscribe(settings);
        invalidate();
    }
}

void MatrixInstance::input(const CdInput& event) {
    if (!hidden_ && !watch_.token() && !legacy_.token() && event.kind == CD_INPUT_DOUBLE_CLICK &&
        (event.buttons & CD_BUTTON_LEFT)) {
        subscribe(settings);
    }
}

void MatrixInstance::invalidate() {
    redraw_.invalidate();
}

const widgetplugin::Spectrum& MatrixInstance::spectrum() const {
    return spectrum_;
}

bool MatrixInstance::ready() const {
    return renderer_.ready();
}
}  // namespace pixeldesk
