#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "plugins/mechanical-pixels/Pixels.h"

namespace mechanicalpixels {
Instance::Instance(const CdCreateContext& context)
    : Instance(context, specification(context.plugin_id), std::make_unique<Renderer>(context)) {}

Instance::Instance(const CdCreateContext& context, const Spec& spec, std::unique_ptr<Face> face)
    : settings(defaults(spec.kind)),
      spec_(spec),
      host_(*context.host),
      hostContext_(context.host_context),
      renderer_(std::move(face)) {
    if (!renderer_) {
        throw std::invalid_argument("A matrix rendering strategy is required");
    }
    environment_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](auto) {
        const bool changed = isClock(spec_.kind) ? synchronize() : advance();
        schedule();
        return cdsdk::UpdateStep{changed, true};
    });
    renderer_->onChange([this] {
        invalidate();
    });
    lastAdvance_ = environment_.snapshot().monotonic_ms;
    if (isClock(spec_.kind)) {
        synchronize();
    } else {
        cdsdk::Services(context.host, hostContext_).query(CD_SERVICE_FFT, audioService_);
        // No source/device is represented by an idle matrix, never generated music.
        subscribe(settings.at("reduced_motion").integer != 0);
    }
    if (schedule() != CD_OK) {
        throw std::runtime_error("Mechanical pixel timer could not be started");
    }
}

Instance::~Instance() {
    audio_.reset();
    legacyAudio_.reset();
    updates_.stop();
    renderer_->onChange({});
}

CdResult Instance::subscribe(bool reduced) {
    if (paused_ || isClock(spec_.kind)) {
        return CD_OK;
    }
    if (audioService_.struct_size >= sizeof(audioService_) && audioService_.version == 1 && audioService_.subscribe) {
        const CdAudioOptions options{sizeof(CdAudioOptions), uint32_t(widgetplugin::Spectrum::count),
                                     reduced ? 10u : 30u, 0, nullptr};
        cdsdk::AudioWatch next(
            &host_, hostContext_,
            [this](const cdsdk::AudioSnapshot& sample) {
                if (sample.samples.size() >= sample.count) {
                    audio(sample.samples.data(), sample.count);
                }
            },
            [&](auto callback, void* user, uint64_t* token) {
                return audioService_.subscribe(hostContext_, &options, callback, user, token);
            });
        const auto result = next.result();
        if (result != CD_OK) {
            return result;
        }
        audio_ = std::move(next);
        legacyAudio_.reset();
        return CD_OK;
    }
    if (!host_.subscribe_audio || !host_.unsubscribe) {
        return CD_ERROR_UNSUPPORTED;
    }
    uint64_t token = 0;
    const auto result = host_.subscribe_audio(
        hostContext_, uint32_t(widgetplugin::Spectrum::count),
        [](void* pointer, const float* values, uint32_t count, uint32_t, uint32_t) {
            try {
                static_cast<Instance*>(pointer)->audio(values, count);
            } catch (...) {
            }
        },
        this, &token);
    cdsdk::Subscription next(&host_, hostContext_, token);
    if (result != CD_OK || !token) {
        return result == CD_OK ? CD_ERROR_INTERNAL : result;
    }
    legacyAudio_ = std::move(next);
    audio_.reset();
    return CD_OK;
}

void Instance::audio(const float* values, uint32_t count) {
    if (paused_ || !values || !count || count > 4096) {
        return;
    }
    spectrum_.feed(values, count);
    haveAudio_ = true;
    lastAudio_ = environment_.snapshot().monotonic_ms;
    schedule();
}

bool Instance::synchronize() {
    const auto now = environment_.snapshot();
    const auto text = clockText(now, settings);
    const bool colon = !settings.at("blink_colon").integer || (now.unix_ms / 1000) % 2 == 0;
    const bool changed = text_ != text || colonVisible_ != colon;
    text_ = text;
    colonVisible_ = colon;
    return changed;
}

bool Instance::advance() {
    const auto now = environment_.snapshot().monotonic_ms;
    const float seconds = now >= lastAdvance_ ? float(std::min(uint64_t{250}, now - lastAdvance_)) / 1000 : 0;
    lastAdvance_ = now;
    const bool stale = !haveAudio_ || now < lastAudio_ || now - lastAudio_ > 300;
    if (!settings.at("reduced_motion").integer) {
        return spectrum_.advance(seconds, stale, settings.at("gain").number, double(settings.at("release").integer));
    }
    // Reduced motion uses current discrete levels at ten updates per second,
    // without interpolation or moving peak trails. It still follows actual audio.
    auto levels = spectrum_.input;
    for (auto& value : levels) {
        value = stale ? 0 : std::pow(std::clamp(float(value * settings.at("gain").number), 0.f, 1.f), .72f);
    }
    const bool changed = levels != spectrum_.level;
    spectrum_.level = spectrum_.peak = spectrum_.publishedLevel = spectrum_.publishedPeak = levels;
    spectrum_.hold.fill(0);
    return changed;
}

CdResult Instance::schedule() {
    if (paused_) {
        updates_.stop();
        return CD_OK;
    }
    const auto now = environment_.snapshot();
    if (isClock(spec_.kind)) {
        const uint64_t period = settings.at("seconds").integer || settings.at("blink_colon").integer ? 1000 : 60000;
        return updates_.start(std::max(16u, uint32_t(period - now.unix_ms % period)), now.monotonic_ms);
    }
    const bool stale = !haveAudio_ || now.monotonic_ms < lastAudio_ || now.monotonic_ms - lastAudio_ > 300;
    bool moving = false, lit = false;
    for (size_t i = 0; i < widgetplugin::Spectrum::count; ++i) {
        const float target =
            stale ? 0 : std::pow(std::clamp(float(spectrum_.input[i] * settings.at("gain").number), 0.f, 1.f), .72f);
        moving |= std::abs(target - spectrum_.level[i]) > .0001f || spectrum_.peak[i] > spectrum_.level[i] + .0001f;
        lit |= spectrum_.level[i] > 0 || target > 0 || spectrum_.peak[i] > 0;
    }
    if (!lit) {
        updates_.stop();
        return CD_OK;
    }
    const auto interval = moving ? (settings.at("reduced_motion").integer ? 100u : 33u) : 250u;
    return updates_.start(interval, now.monotonic_ms);
}

CdResult Instance::render(const CdFrame& frame) {
    updates_.beginFrame();
    redraw_.beginFrame();
    return renderer_->render(spec_.kind, settings, text_, colonVisible_, spectrum_, frame, safe_) ? CD_OK
                                                                                                  : CD_ERROR_ARGUMENT;
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto safe = layout.safe_content;
    safe_ = std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.width) && std::isfinite(safe.height) &&
                    safe.x >= 0 && safe.y >= 0 && safe.width > 0 && safe.height > 0 &&
                    safe.x + safe.width <= layout.logical_width && safe.y + safe.height <= layout.logical_height
                ? safe
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
}

void Instance::setPaused(bool paused) {
    paused_ = paused;
    renderer_->pause(paused);
    redraw_.pause(paused);
    updates_.pause(paused);
    if (paused) {
        audio_.reset();
        legacyAudio_.reset();
    } else {
        lastAdvance_ = environment_.snapshot().monotonic_ms;
        if (isClock(spec_.kind)) {
            synchronize();
        } else {
            spectrum_ = {};
            haveAudio_ = false;
            subscribe(settings.at("reduced_motion").integer != 0);
        }
        schedule();
        invalidate();
    }
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const bool clock = isClock(spec_.kind);
    const bool subscriptionChanged =
        !clock && !paused_ && next.at("reduced_motion").integer != settings.at("reduced_motion").integer;
    if (subscriptionChanged) {
        const auto subscribed = subscribe(next.at("reduced_motion").integer != 0);
        if (subscribed != CD_OK) {
            return subscribed;
        }
    }
    auto previous = settings;
    settings = std::move(next);
    if (clock) {
        synchronize();
    }
    const auto scheduled = schedule();
    if (scheduled != CD_OK) {
        settings = std::move(previous);
        if (subscriptionChanged) {
            subscribe(settings.at("reduced_motion").integer != 0);
        }
        if (clock) {
            synchronize();
        }
        schedule();
        return scheduled;
    }
    return CD_OK;
}

void Instance::invalidate() {
    redraw_.invalidate();
}
}  // namespace mechanicalpixels
