#include <Windows.h>

#include <algorithm>
#include <cmath>

#include "plugins/mecha-desk/Player.h"

namespace aeris_player {
const PlayerSpec& playerSpec(std::string_view id) {
    for (const auto& spec : kPlayerCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown AERIS POLAR media player");
}

namespace {
std::wstring mediaText(const std::string& text) {
    if (text.empty() || text.size() > 65536) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
    return result;
}
}  // namespace

widgetplugin::Media pixelMedia(const cdsdk::MediaSnapshot& value, const widgetplugin::Media& previous) {
    widgetplugin::Media result;
    result.source = mediaText(value.sourceId);
    result.title = mediaText(value.title);
    result.artist = mediaText(value.artist);
    result.album = mediaText(value.album);
    result.status = value.status;
    result.controls = value.controls;
    result.position = std::isfinite(value.positionSeconds) ? std::max(0., value.positionSeconds) : 0;
    result.duration = std::isfinite(value.durationSeconds) ? std::max(0., value.durationSeconds) : 0;
    result.rate = std::isfinite(value.playbackRate) ? std::clamp(value.playbackRate, 0., 16.) : 1;
    result.sampledAt = value.sampledAtMs;
    result.commandResult = value.commandResult;
    result.coverGeneration = value.coverGeneration;
    result.recordDegrees = previous.recordDegrees;
    const bool sameTrack = result.source == previous.source && result.title == previous.title &&
                           result.artist == previous.artist && result.album == previous.album;
    result.tonearmProgress = sameTrack             ? previous.tonearmProgress
                             : result.duration > 0 ? std::clamp(result.position / result.duration, 0., 1.)
                                                   : .1;
    result.tonearmSeconds = sameTrack ? previous.tonearmSeconds : 0;
    if (result.coverGeneration == previous.coverGeneration && result.source == previous.source &&
        previous.cover.bitmap) {
        result.cover = previous.cover;
    } else if (value.coverWidth && value.coverHeight && value.coverWidth <= 256 && value.coverHeight <= 256 &&
               value.coverStride >= value.coverWidth * 4 &&
               value.cover.size() >= uint64_t(value.coverStride) * value.coverHeight) {
        std::vector<uint8_t> pixels(size_t(value.coverWidth) * value.coverHeight * 4);
        for (unsigned y = 0; y < value.coverHeight; ++y) {
            for (unsigned x = 0; x < value.coverWidth; ++x) {
                const auto* source = value.cover.data() + size_t(y) * value.coverStride + x * 4;
                auto* output = pixels.data() + (size_t(y) * value.coverWidth + x) * 4;
                output[0] = source[2];
                output[1] = source[1];
                output[2] = source[0];
                output[3] = source[3];
            }
        }
        result.cover.bitmap =
            cybercanvas::ImageData::fromPremultipliedBgra(value.coverWidth, value.coverHeight, std::move(pixels));
    }
    return result;
}

PlayerInstance::PlayerInstance(const CdCreateContext& context)
    : settings(widgetplugin::defaults(widgetplugin::Kind::Player, true)),
      spec_(playerSpec(context.plugin_id)),
      services_(context.host, context.host_context),
      context_(context.host_context),
      renderer_(context, spec_) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    seek_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    renderer_.onAction([this](uint32_t id) {
        action(id);
    });
    lastTick_ = environment_.snapshot().monotonic_ms;
    if (subscribe(settings, true) != CD_OK || schedule() != CD_OK) {
        throw std::runtime_error("Cannot subscribe to media controls");
    }
}

PlayerInstance::~PlayerInstance() {
    cancelSeek();
    watch_.reset();
    timer_.stop();
    renderer_.onAction({});
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult PlayerInstance::subscribe(cdsdk::Settings next, bool clear) {
    if (hidden_) {
        settings = std::move(next);
        if (clear) {
            media_ = {};
        }
        return CD_OK;
    }

    struct Gate {
        bool committed = false;
        std::optional<cdsdk::MediaSnapshot> pending;
    };

    const auto gate = std::make_shared<Gate>();
    auto candidate = services_.media(next.at("player").text.c_str(), [this, gate](const auto& value) {
        if (gate->committed) {
            receive(value);
        } else {
            gate->pending = value;
        }
    });
    if (candidate.result() != CD_OK) {
        return candidate.result();
    }
    settings = std::move(next);
    watch_ = std::move(candidate);
    if (clear) {
        cancelSeek();
        renderer_.cancelInput();
        media_ = {};
        session_ = lastCommand_ = pendingRequest_ = 0;
        commandBegan_.reset();
    }
    gate->committed = true;
    if (gate->pending) {
        receive(*gate->pending);
    }
    invalidate();
    return CD_OK;
}

bool PlayerInstance::motionAllowed(const cdsdk::Settings* candidate) const {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return renderer_.animatedArtwork() && !(candidate ? *candidate : settings).at("reduced_motion").integer && enabled;
}

CdResult PlayerInstance::schedule(const cdsdk::Settings* candidate) {
    if (hidden_) {
        timer_.stop();
        return CD_OK;
    }
    return timer_.replace(spec_.reels && (candidate ? *candidate : settings).at("show_background").integer &&
                                  media_.status == CD_MEDIA_PLAYING && !media_.source.empty() &&
                                  motionAllowed(candidate)
                              ? 33
                              : 1000,
                          tick, this);
}

void PlayerInstance::receive(const cdsdk::MediaSnapshot& value) {
    if (hidden_) {
        return;
    }
    auto next = pixelMedia(value, media_);
    if (next.source == media_.source && session_ && value.sessionGeneration && value.sessionGeneration < session_) {
        return;
    }
    const bool different = next.source != media_.source || next.title != media_.title || next.artist != media_.artist ||
                           next.album != media_.album || (session_ && value.sessionGeneration != session_);
    if (different) {
        cancelSeek();
        pendingRequest_ = 0;
        commandBegan_.reset();
    }
    if (pendingRequest_) {
        if (value.commandRequestId == pendingRequest_ && value.commandResult != CD_ERROR_BUSY) {
            pendingRequest_ = 0;
            if (!seeking_) {
                seekPosition_.reset();
            }
        } else {
            next.commandResult = CD_ERROR_BUSY;
        }
    } else if (next.commandResult != CD_ERROR_BUSY && !seeking_) {
        seekPosition_.reset();
    }
    const double second = std::floor(widgetplugin::mediaPosition(next, environment_.snapshot().monotonic_ms));
    const bool changed = different || next.controls != media_.controls || next.status != media_.status ||
                         next.cover.bitmap != media_.cover.bitmap || next.duration != media_.duration ||
                         next.commandResult != media_.commandResult ||
                         (settings.at("show_progress").integer && second != shownSecond_);
    media_ = std::move(next);
    if (media_.commandResult == CD_ERROR_BUSY) {
        if (!commandBegan_) {
            commandBegan_ = environment_.snapshot().monotonic_ms;
        }
    } else {
        commandBegan_.reset();
    }
    session_ = value.sessionGeneration;
    lastCommand_ = value.commandRequestId;
    shownSecond_ = second;
    schedule();
    if (changed) {
        invalidate();
    }
}

void PlayerInstance::command(uint32_t command, double value) {
    const uint32_t flag = command == CD_MEDIA_PREVIOUS ? CD_MEDIA_CAN_PREVIOUS
                          : command == CD_MEDIA_NEXT   ? CD_MEDIA_CAN_NEXT
                          : command == CD_MEDIA_TOGGLE ? CD_MEDIA_CAN_TOGGLE
                                                       : CD_MEDIA_CAN_SEEK;
    if (hidden_ || !watch_.token() || media_.source.empty() || !(media_.controls & flag) ||
        media_.commandResult == CD_ERROR_BUSY ||
        (command == CD_MEDIA_SEEK && (!settings.at("show_progress").integer || media_.duration <= 0))) {
        return;
    }
    CdMediaServiceApi api{};
    auto result = services_.query(CD_SERVICE_MEDIA, api);
    uint64_t request = 0;
    if (result == CD_OK) {
        result = api.command_request ? api.command_request(context_, watch_.token(), command, value, &request)
                 : api.command       ? api.command(context_, watch_.token(), command, value)
                                     : CD_ERROR_UNSUPPORTED;
    }
    if (result == CD_OK && request && (lastCommand_ != request || media_.commandResult == CD_ERROR_BUSY)) {
        pendingRequest_ = request;
        media_.commandResult = CD_ERROR_BUSY;
    } else if (result != CD_OK) {
        media_.commandResult = result;
        pendingRequest_ = 0;
        seekPosition_.reset();
    } else if (media_.commandResult != CD_ERROR_BUSY) {
        seekPosition_.reset();
    }
    if (media_.commandResult == CD_ERROR_BUSY) {
        commandBegan_ = environment_.snapshot().monotonic_ms;
    } else {
        commandBegan_.reset();
    }
    invalidate();
}

void PlayerInstance::action(uint32_t id) {
    if (id >= 1 && id <= 3) {
        constexpr uint32_t commands[]{CD_MEDIA_PREVIOUS, CD_MEDIA_TOGGLE, CD_MEDIA_NEXT};
        command(commands[id - 1]);
    }
}

void PlayerInstance::syncSeek() {
    auto r = renderer_.timeline({0, 0, inputArea_.width, inputArea_.height});
    r.x -= 8;
    r.width += 16;
    const double padding = std::max(0., 16. - r.height) / 2;
    r.y -= padding;
    r.height += padding * 2;
    const bool enabled = !hidden_ && settings.at("show_progress").integer && !media_.source.empty() &&
                         media_.duration > 0 && (media_.controls & CD_MEDIA_CAN_SEEK) &&
                         media_.commandResult != CD_ERROR_BUSY;
    seek_.regions({{4, cybercanvas::HitShape::rectangle({inputArea_.x + r.x, inputArea_.y + r.y, r.width, r.height}),
                    cybercanvas::PointerBehavior::Drag, 1, enabled}});
}

void PlayerInstance::cancelSeek() {
    seek_.cancel();
    seeking_ = false;
    renderer_.setSeeking(false);
    seekPosition_.reset();
}

void PlayerInstance::input(const CdInput& event) {
    if (hidden_) {
        return;
    }
    if (!watch_.token()) {
        if (event.kind == CD_INPUT_DOUBLE_CLICK && (event.buttons & CD_BUTTON_LEFT) && std::isfinite(event.x) &&
            std::isfinite(event.y) && event.x >= inputArea_.x && event.y >= inputArea_.y &&
            event.x < inputArea_.x + inputArea_.width && event.y < inputArea_.y + inputArea_.height) {
            subscribe(settings, false);
            schedule();
        }
        return;
    }
    syncSeek();
    if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
        // Cancel the button router before the seek router can capture the pointer.
        renderer_.cancelInput();
    }
    const bool wasSeeking = seeking_;
    const auto result = seek_.input(event);
    if (result.drag == cybercanvas::DragPhase::Begin || result.drag == cybercanvas::DragPhase::Update ||
        result.drag == cybercanvas::DragPhase::End) {
        const auto r = renderer_.timeline({0, 0, inputArea_.width, inputArea_.height});
        seekPosition_ =
            r.width > 0 ? std::clamp((event.x - inputArea_.x - r.x) / r.width, 0., 1.) * media_.duration : 0;
        seeking_ = result.drag != cybercanvas::DragPhase::End;
        renderer_.setSeeking(seeking_);
        if (!seeking_) {
            command(CD_MEDIA_SEEK, *seekPosition_);
        }
        invalidate();
        return;
    }
    if (result.drag == cybercanvas::DragPhase::Cancel) {
        seeking_ = false;
        renderer_.setSeeking(false);
        seekPosition_.reset();
        invalidate();
        return;
    }
    if (!wasSeeking && !seeking_) {
        renderer_.syncInput(media_, inputArea_);
        renderer_.input(event);
    }
}

void CD_CALL PlayerInstance::tick(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<PlayerInstance*>(pointer);
    try {
        if (self.hidden_) {
            return;
        }
        const uint64_t now = self.environment_.snapshot().monotonic_ms;
        if (self.commandBegan_ && now >= *self.commandBegan_ && now - *self.commandBegan_ >= 15000) {
            self.pendingRequest_ = 0;
            self.commandBegan_.reset();
            self.seekPosition_.reset();
            self.media_.commandResult = CD_ERROR_INTERNAL;
            self.invalidate();
        }
        const double seconds = now >= self.lastTick_ ? std::min<uint64_t>(250, now - self.lastTick_) * .001 : 0;
        self.lastTick_ = now;
        const double position = widgetplugin::mediaPosition(self.media_, now);
        const double second = std::floor(position);
        const bool spinning = self.spec_.reels && self.settings.at("show_background").integer &&
                              self.media_.status == CD_MEDIA_PLAYING && !self.media_.source.empty() &&
                              self.motionAllowed();
        if (spinning) {
            self.media_.recordDegrees = std::fmod(self.media_.recordDegrees + seconds * 18, 360.);
            widgetplugin::advanceTonearm(self.media_, seconds, position);
        }
        if (spinning || (self.settings.at("show_progress").integer && second != self.shownSecond_)) {
            self.shownSecond_ = second;
            self.invalidate();
        }
        self.schedule();
    } catch (...) {
        self.timer_.stop();
    }
}

CdResult PlayerInstance::render(const CdFrame& frame) {
    if (!std::isfinite(frame.device_pixel_ratio) || frame.device_pixel_ratio <= 0) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    inputArea_ = safe_.width > 0 && safe_.height > 0
                     ? safe_
                     : CdRect{0, 0, frame.width / frame.device_pixel_ratio, frame.height / frame.device_pixel_ratio};
    const double position =
        seekPosition_.value_or(widgetplugin::mediaPosition(media_, environment_.snapshot().monotonic_ms));
    if (!watch_.token()) {
        auto unavailable = media_;
        unavailable.controls = 0;
        return renderer_.render(settings, unavailable, position, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
    }
    return renderer_.render(settings, media_, position, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult PlayerInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    if (next.at("player").text != settings.at("player").text) {
        return subscribe(std::move(next), true);
    }
    const auto scheduled = schedule(&next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    settings = std::move(next);
    cancelSeek();
    renderer_.cancelInput();
    invalidate();
    return CD_OK;
}

void PlayerInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    safe_ = layout.safe_content;
    inputArea_ =
        safe_.width > 0 && safe_.height > 0 ? safe_ : CdRect{0, 0, layout.logical_width, layout.logical_height};
    cancelSeek();
    renderer_.cancelInput();
}

void PlayerInstance::setPaused(bool paused) {
    if (hidden_ == paused) {
        return;
    }
    hidden_ = paused;
    cancelSeek();
    pendingRequest_ = 0;
    commandBegan_.reset();
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (paused) {
        watch_.reset();
        timer_.stop();
    } else {
        lastTick_ = environment_.snapshot().monotonic_ms;
        subscribe(settings, false);
        schedule();
        invalidate();
    }
}

void PlayerInstance::invalidate() {
    redraw_.invalidate();
}

const widgetplugin::Media& PlayerInstance::media() const {
    return media_;
}

uint64_t PlayerInstance::pendingRequest() const {
    return pendingRequest_;
}

bool PlayerInstance::ready() const {
    return renderer_.ready();
}
}  // namespace aeris_player
