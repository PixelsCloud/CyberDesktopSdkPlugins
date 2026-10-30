#include <Windows.h>

#include <algorithm>
#include <cmath>

#include "plugins/pixel-desk/PixelAlbum.h"

namespace pixeldesk {
AlbumInstance::AlbumInstance(const CdCreateContext& context)
    : settings(widgetplugin::defaults(widgetplugin::Kind::Album, true)),
      services_(context.host, context.host_context),
      context_(context.host_context),
      renderer_(context) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    services_.query(CD_SERVICE_IMAGE_VIEWER, viewer_);
    album_.bind(context, [this] {
        if (!hidden_) {
            advance();
        }
    });
    renderer_.onChange([this] {
        invalidate();
    });
    renderer_.onAction([this](uint32_t id) {
        action(id);
    });
    lastTick_ = lastPhoto_ = environment_.snapshot().monotonic_ms;
    request();
    schedule();
}

AlbumInstance::~AlbumInstance() {
    timer_.stop();
    renderer_.onAction({});
    renderer_.onChange({});
    renderer_.pause(true);
}

bool AlbumInstance::motionAllowed() const {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return !settings.at("reduced_motion").integer && enabled;
}

widgetplugin::AlbumPlayback AlbumInstance::playback() const {
    return {slideshowPaused_, pending_, motion_, previousMotion_, motionAllowed()};
}

void AlbumInstance::request(int step, bool rescan) {
    if (hidden_) {
        needsRequest_ = true;
        return;
    }
    album_.request(settings.at("directory").text, settings.at("recursive").integer != 0, step, rescan);
    pending_ = true;
    needsRequest_ = false;
}

void AlbumInstance::advance() {
    const uint64_t now = environment_.snapshot().monotonic_ms;
    const double seconds = now >= lastTick_ ? std::min<uint64_t>(250, now - lastTick_) * .001 : 0;
    lastTick_ = now;
    if (needsRequest_) {
        request(0, true);
    }
    bool changed = false;
    widgetplugin::Photo next;
    if (album_.take(next)) {
        pending_ = false;
        lastPhoto_ = now;
        if (next.bitmap != photo_.bitmap) {
            previousMotion_ = motion_;
            motion_ = 0;
            previous_ = photo_;
            photo_ = std::move(next);
            fadeStart_ = now;
            fade_ = previous_.bitmap && motionAllowed() ? 0.f : 1.f;
            if (fade_ == 1) {
                previous_ = {};
            }
            changed = true;
        } else if (next.index != photo_.index || next.count != photo_.count || next.filepath != photo_.filepath) {
            photo_ = std::move(next);
            changed = true;
        }
    }
    if (fade_ < 1) {
        const double duration = settings.at("transition").number;
        fade_ = duration <= 0 || !motionAllowed() ? 1.f
                                                  : std::clamp(float((now - fadeStart_) / (duration * 1000)), 0.f, 1.f);
        if (fade_ >= 1) {
            previous_ = {};
        }
        changed = true;
    }
    if (photo_.bitmap && !slideshowPaused_ && motionAllowed() && settings.at("photo_motion").integer && seconds > 0) {
        motion_ += seconds;
        changed = true;
    }
    if (!pending_) {
        if (!slideshowPaused_ && photo_.count > 1 && now >= lastPhoto_ &&
            now - lastPhoto_ >= uint64_t(settings.at("interval").integer) * 1000) {
            request(1);
        }
    }
    if (changed) {
        invalidate();
    }
    schedule();
}

void AlbumInstance::schedule() {
    if (hidden_) {
        timer_.stop();
        return;
    }
    const auto interval = widgetplugin::albumTimerInterval(
        photo_, fade_, motionAllowed() && settings.at("photo_motion").integer, slideshowPaused_ || pending_,
        uint64_t(settings.at("interval").integer) * 1000, lastPhoto_, environment_.snapshot().monotonic_ms);
    if (!interval) {
        timer_.stop();
    } else {
        timer_.replace(interval, tick, this);
    }
}

void CD_CALL AlbumInstance::tick(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<AlbumInstance*>(pointer);
    try {
        if (!self.hidden_) {
            self.advance();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

void AlbumInstance::action(uint32_t id) {
    if (hidden_ || !photo_.bitmap || id < 1 || id > 3) {
        return;
    }
    if (id == 2) {
        slideshowPaused_ = !slideshowPaused_;
        lastPhoto_ = environment_.snapshot().monotonic_ms;
    } else if (photo_.count > 1 && !pending_) {
        request(id == 1 ? -1 : 1);
    }
    schedule();
    invalidate();
}

CdResult AlbumInstance::render(const CdFrame& frame) {
    if (!std::isfinite(frame.device_pixel_ratio) || frame.device_pixel_ratio <= 0) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    inputArea_ = safe_.width > 0 && safe_.height > 0
                     ? safe_
                     : CdRect{0, 0, frame.width / frame.device_pixel_ratio, frame.height / frame.device_pixel_ratio};
    const auto photoRegion = pixelAlbumLayout(inputArea_.width, inputArea_.height).photo;
    album_.pixelEdge(uint32_t(std::clamp(
        std::ceil(std::max(photoRegion.width, photoRegion.height) * frame.device_pixel_ratio), 1.0, 2048.0)));
    return renderer_.render(settings, photo_, previous_, fade_, playback(), frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult AlbumInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const bool folderChanged = next.at("directory").text != settings.at("directory").text ||
                               next.at("recursive").integer != settings.at("recursive").integer;
    if (folderChanged && !hidden_) {
        album_.request(next.at("directory").text, next.at("recursive").integer != 0, 0, true);
    }
    settings = std::move(next);
    if (folderChanged) {
        photo_ = previous_ = {};
        motion_ = previousMotion_ = 0;
        fade_ = 1;
        pending_ = true;
        needsRequest_ = hidden_;
    }
    if (!motionAllowed()) {
        previous_ = {};
        fade_ = 1;
    }
    renderer_.cancelInput();
    schedule();
    invalidate();
    return CD_OK;
}

void AlbumInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    safe_ = layout.safe_content;
    inputArea_ =
        safe_.width > 0 && safe_.height > 0 ? safe_ : CdRect{0, 0, layout.logical_width, layout.logical_height};
    renderer_.cancelInput();
}

void AlbumInstance::setPaused(bool paused) {
    if (hidden_ == paused) {
        return;
    }
    hidden_ = paused;
    album_.pause(paused);
    renderer_.pause(paused);
    redraw_.pause(paused);
    renderer_.cancelInput();
    if (paused) {
        timer_.stop();
    } else {
        lastTick_ = lastPhoto_ = environment_.snapshot().monotonic_ms;
        if (needsRequest_) {
            request(0, true);
        }
        schedule();
        invalidate();
    }
}

void AlbumInstance::input(const CdInput& event) {
    if (hidden_) {
        return;
    }
    renderer_.syncInput(photo_, playback(), inputArea_);
    renderer_.input(event);
    if (event.kind != CD_INPUT_DOUBLE_CLICK || !(event.buttons & CD_BUTTON_LEFT) || pending_ || !photo_.bitmap ||
        photo_.filepath.empty() || !viewer_.open) {
        return;
    }
    const auto r = pixelAlbumLayout(inputArea_.width, inputArea_.height).photo;
    const double x = event.x - inputArea_.x, y = event.y - inputArea_.y;
    if (std::isfinite(x) && std::isfinite(y) && x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height) {
        const auto path = std::filesystem::path(photo_.filepath).u8string();
        viewer_.open(context_, reinterpret_cast<const char*>(path.data()), uint32_t(path.size()));
    }
}

void AlbumInstance::invalidate() {
    redraw_.invalidate();
}

std::vector<uint8_t> AlbumInstance::saveState() const {
    return {'W', 'G', 1, uint8_t(widgetplugin::Kind::Album), uint8_t(slideshowPaused_), 0, 0, 0};
}

CdResult AlbumInstance::restoreState(std::span<const uint8_t> bytes) {
    if (!bytes.empty() &&
        (bytes.size() != 8 || bytes[0] != 'W' || bytes[1] != 'G' || bytes[2] != 1 ||
         bytes[3] != uint8_t(widgetplugin::Kind::Album) || bytes[4] > 1 || bytes[5] || bytes[6] || bytes[7])) {
        return CD_ERROR_ARGUMENT;
    }
    slideshowPaused_ = !bytes.empty() && bytes[4] != 0;
    lastPhoto_ = environment_.snapshot().monotonic_ms;
    schedule();
    invalidate();
    return CD_OK;
}

const widgetplugin::Photo& AlbumInstance::photo() const {
    return photo_;
}

bool AlbumInstance::slideshowPaused() const {
    return slideshowPaused_;
}

bool AlbumInstance::pending() const {
    return pending_;
}

bool AlbumInstance::ready() const {
    return renderer_.ready();
}
}  // namespace pixeldesk
