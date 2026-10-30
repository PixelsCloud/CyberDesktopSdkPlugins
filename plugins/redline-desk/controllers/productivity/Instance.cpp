#include <CyberCanvas/InstrumentMotion.h>
#include <Windows.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>
#include <sdk/services/Storage.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "plugins/productivity-suite/Feed.h"
#include "plugins/productivity-suite/Volume.h"
#include "plugins/redline-desk/controllers/productivity/Productivity.h"

namespace redline_productivity {
namespace {
struct Instance {
    Spec spec;
    cdsdk::Settings settings;
    Data data;
    Renderer renderer;
    cdsdk::ContentWatch feed;
    cdsdk::Services services;
    cdsdk::VolumeWatch volume;
    CdHostApi host{};
    void* context = nullptr;
    cdsdk::Timer timer;
    cdsdk::Timer motionTimer;
    cybercanvas::mechanical::NeedleBank<2> needles;
    uint64_t systemToken = 0, lastRotate = 0, lastHistory = 0;
    bool paused = false;
    CdRect safe{};
    HWND clipboardOwner = nullptr;

    Instance(Spec s, const CdCreateContext& creation)
        : spec(s), settings(defaults(s.kind, s.mechanical)), services(creation.host, creation.host_context) {
        if (s.kind == Kind::Todo)
            reconcileTasks(data, settings.at("tasks").text);
    }

    ~Instance() {
        motionTimer.stop();
        timer.stop();
        if (clipboardOwner)
            DestroyWindow(clipboardOwner);
    }

    void redraw() {
        host.request_redraw(context);
    }

    void updateDays() {
        SYSTEMTIME today{};
        GetLocalTime(&today);
        data.days = daysUntil(settings.at("date").text, settings.at("annual").integer != 0, today.wYear, today.wMonth,
                              today.wDay);
    }
};

bool motionAllowed(const Instance& self) {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return !self.paused && self.spec.mechanical && !self.settings.at("reduced_motion").integer && enabled;
}

void CD_CALL moveNeedles(void* pointer, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        const auto now = GetTickCount64();
        self.data.needles = self.needles.sample(now);
        if (!motionAllowed(self) || !self.needles.active(now)) {
            self.data.needles = {-1, -1};
            self.motionTimer.stop();
        }
        self.redraw();
    } catch (...) {
        auto& self = *static_cast<Instance*>(pointer);
        self.data.needles = {-1, -1};
        self.motionTimer.stop();
    }
}

void retarget(Instance& self, std::array<double, 2> next, bool wasValid) {
    if (self.needles.retarget(next, GetTickCount64(), wasValid && motionAllowed(self)) &&
        self.motionTimer.replace(33, moveNeedles, &self) == CD_OK) {
        self.data.needles = self.needles.sample(GetTickCount64());
    } else {
        self.data.needles = {-1, -1};
        self.motionTimer.stop();
    }
}

void CD_CALL system(void* pointer, const CdSystemInfo* info) noexcept {
    if (!pointer || !info || info->struct_size < sizeof(CdSystemInfo))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused)
        return;
    const bool wasValid = self.data.valid;
    const bool changed = self.data.up != info->network_up_bytes_per_second ||
                         self.data.down != info->network_down_bytes_per_second || !self.data.valid;
    self.data.up = info->network_up_bytes_per_second;
    self.data.down = info->network_down_bytes_per_second;
    self.data.valid = true;
    retarget(self,
             {std::log1p(double(self.data.down) / 1024) / std::log1p(1048576.),
              std::log1p(double(self.data.up) / 1024) / std::log1p(1048576.)},
             wasValid);
    const auto now = GetTickCount64();
    bool history = false;
    if (!self.lastHistory || now - self.lastHistory >= 900) {
        self.lastHistory = now;
        self.data.history.emplace_back(self.data.down, self.data.up);
        if (self.data.history.size() > 60)
            self.data.history.pop_front();
        history = self.settings.at("history").integer && self.spec.columns >= 4;
    }
    if (changed || history)
        self.redraw();
}

void CD_CALL tick(void* pointer, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused)
            return;
        const auto now = GetTickCount64();
        bool changed = false;
        if (self.spec.kind == Kind::Timer) {
            const auto before = stateBytes(self.data, self.spec.kind);
            advanceTimer(self.data, self.settings, wallSeconds());
            changed = before != stateBytes(self.data, self.spec.kind);
        } else if (self.spec.kind == Kind::Countdown) {
            const auto old = self.data.days;
            self.updateDays();
            changed = old != self.data.days;
        }
        if (self.spec.kind == Kind::Rss && self.data.articles.size() > 1 &&
            now - self.lastRotate >= uint64_t(self.settings.at("rotation_seconds").integer) * 1000) {
            self.lastRotate = now;
            self.data.scroll = (self.data.scroll + 1) % int(self.data.articles.size());
            changed = true;
        }
        if (self.data.copiedUntil && now >= self.data.copiedUntil) {
            self.data.copiedUntil = 0;
            changed = true;
        }
        if (changed)
            self.redraw();
    } catch (...) {
    }
}

void stop(Instance& self) {
    self.feed.reset();
    self.volume.reset();
    self.motionTimer.stop();
    self.needles.reset();
    self.data.needles = {-1, -1};
    self.timer.stop();
    if (self.systemToken)
        self.host.unsubscribe(self.context, self.systemToken);
    self.systemToken = 0;
}

void startVolume(Instance& self) {
    self.data.loading = true;
    const CdVolumeQuery query{sizeof(query), uint32_t(self.settings.at("refresh_seconds").integer),
                              self.settings.at("drive").text.c_str()};
    self.volume = self.services.volume(query, [&self](const cdsdk::VolumeSnapshot& value) {
        if (self.paused) {
            return;
        }
        const auto change = applyVolume(self.data, value);
        if (change.capacityChanged) {
            retarget(self, {change.used, 0}, change.wasValid);
        }
        if (change.changed) {
            self.redraw();
        }
    });
    if (self.volume.result() != CD_OK) {
        self.data.loading = false;
        self.data.stale = self.data.valid;
        self.data.error = "service_unavailable";
    }
}

void startFeed(Instance& self) {
    self.feed.reset();
    const auto* url = self.settings.at("url").text.c_str();
    const CdContentQuery query{sizeof(query), CD_CONTENT_FEEDS,
                               uint32_t(self.settings.at("refresh_minutes").integer * 60), 1, &url};
    self.feed = self.services.content(query, [&self](const cdsdk::ContentSnapshot& value) {
        if (self.paused) {
            return;
        }
        if (productivity::applyFeed(self.data, value)) {
            self.redraw();
        }
    });
    if (self.feed.result() != CD_OK) {
        self.data.loading = false;
        self.data.stale = self.data.valid;
        self.data.error = "service_unavailable";
    }
}

CdResult start(Instance& self) {
    if (self.spec.kind == Kind::Rss) {
        startFeed(self);
        return self.timer.replace(uint32_t(self.settings.at("rotation_seconds").integer * 1000), tick, &self);
    }
    if (self.spec.kind == Kind::Disk) {
        startVolume(self);
        return CD_OK;
    }
    if (self.spec.kind == Kind::Network)
        return self.host.subscribe_system(self.context, 1000, system, &self, &self.systemToken);
    const auto interval = self.spec.kind == Kind::Countdown ? 1000u : 500u;
    return self.timer.replace(interval, tick, &self);
}

void CD_CALL destroy(void* pointer) noexcept {
    if (auto self = std::unique_ptr<Instance>(static_cast<Instance*>(pointer))) {
        stop(*self);
    }
}

void CD_CALL layout(void* pointer, const CdLayout* value) noexcept {
    if (!pointer || !value || value->struct_size < sizeof(CdLayout))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    self.safe = value->safe_content;
    if (self.safe.width <= 0 || self.safe.height <= 0)
        self.safe = {0, 0, value->logical_width, value->logical_height};
    self.renderer.cancelInput();
    self.redraw();
}

void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
    try {
        if (!pointer)
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused == (paused != 0))
            return;
        self.paused = paused != 0;
        self.renderer.pause(self.paused);
        self.renderer.cancelInput();
        if (self.paused) {
            stop(self);

            return;
        }
        if (self.spec.kind == Kind::Timer)
            advanceTimer(self.data, self.settings, wallSeconds());
        if (self.spec.kind == Kind::Countdown)
            self.updateDays();

        self.lastHistory = 0;
        self.lastRotate = GetTickCount64();
        if (start(self) != CD_OK) {
            self.paused = true;
            stop(self);
        }
        self.redraw();
    } catch (...) {
    }
}

bool copy(Instance& self, const std::string& text) {
    const auto value = wide(text);
    if (value.empty())
        return false;
    // EmptyClipboard with a null owner prevents SetClipboardData from succeeding.
    // A message-only window gives this instance an owner without visible UI.
    if (!self.clipboardOwner)
        self.clipboardOwner = CreateWindowExW(0, L"STATIC", L"CyberDesktop note clipboard", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                              nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!self.clipboardOwner || !OpenClipboard(self.clipboardOwner))
        return false;
    const size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool okay = false;
    if (memory) {
        auto* dest = GlobalLock(memory);
        if (dest) {
            std::memcpy(dest, value.c_str(), bytes);
            GlobalUnlock(memory);
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) {
                okay = true;
                memory = nullptr;
            }
        }
        if (memory)
            GlobalFree(memory);
    }
    CloseClipboard();
    return okay;
}

void action(Instance& self, int value) {
    auto& data = self.data;
    if (self.spec.kind == Kind::Timer) {
        if (value == 2) {
            data.running = data.resting = data.finished = false;
            data.deadline = 0;
            data.remaining = self.settings.at("focus_minutes").integer * 60;
        } else if (value == 1) {
            advanceTimer(data, self.settings, wallSeconds());
            if (data.running) {
                data.running = false;
                data.deadline = 0;
            } else {
                if (data.remaining == 0) {
                    data.resting = data.finished = false;
                    data.remaining = self.settings.at("focus_minutes").integer * 60;
                }
                data.deadline = wallSeconds() + data.remaining;
                data.running = true;
                data.finished = false;
            }
        }
    } else if (value == 3 || value == 4) {
        int count = self.spec.kind == Kind::Todo    ? int(data.tasks.size())
                    : self.spec.kind == Kind::Notes ? int(lines(self.settings.at("text").text).size())
                                                    : int(data.articles.size());
        if (count)
            data.scroll = (data.scroll + (value == 3 ? count - 1 : 1)) % count;
    } else if (value >= 100) {
        const int index = value - 100;
        if (self.spec.kind == Kind::Todo && index < int(data.tasks.size()))
            data.tasks[index].done = !data.tasks[index].done;
        else if (self.spec.kind == Kind::Notes) {
            const auto list = lines(self.settings.at("text").text);
            const auto text = self.settings.at("copy_all").integer ? self.settings.at("text").text
                              : index < int(list.size())           ? list[index]
                                                                   : "";
            if (copy(self, text))
                data.copiedUntil = GetTickCount64() + 1500;
        } else if (self.spec.kind == Kind::Rss && index < int(data.articles.size())) {
            self.services.openUrl(data.articles[index].url.c_str());

            data.loading = false;
        }
    }
    self.lastRotate = GetTickCount64();
    self.redraw();
}

void CD_CALL input(void* pointer, const CdInput* event) noexcept {
    try {
        if (!pointer || !event || event->struct_size < sizeof(CdInput))
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused)
            return;
        self.renderer.syncInput(self.spec, self.settings, self.data, self.safe);
        self.renderer.input(*event);
        if (event->kind == CD_INPUT_WHEEL &&
            (self.spec.kind == Kind::Todo || self.spec.kind == Kind::Notes || self.spec.kind == Kind::Rss))
            action(self, event->wheel_delta > 0 ? 3 : 4);
    } catch (...) {
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* out, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, out, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        auto settings = self.settings;
        const auto status = settings.apply(values, count);
        if (status != CD_OK)
            return status;
        for (uint32_t i = 0; i < count; ++i)
            if (values[i].type == CD_SETTING_TEXT || values[i].type == CD_SETTING_FONT)
                wide(values[i].text);
        auto data = self.data;
        if (self.spec.kind == Kind::Todo)
            reconcileTasks(data, settings.at("tasks").text);
        if (self.spec.kind == Kind::Countdown) {
            SYSTEMTIME today{};
            GetLocalTime(&today);
            data.days = daysUntil(settings.at("date").text, settings.at("annual").integer != 0, today.wYear,
                                  today.wMonth, today.wDay);
        }
        if (self.spec.kind == Kind::Disk && !driveRoot(settings.at("drive").text))
            return CD_ERROR_ARGUMENT;
        if (self.spec.kind == Kind::Rss && !settings.at("url").text.empty() && !webUrl(settings.at("url").text))
            return CD_ERROR_ARGUMENT;
        if (self.spec.kind == Kind::Timer &&
            (settings.at("mode").text != self.settings.at("mode").text ||
             settings.at("focus_minutes").integer != self.settings.at("focus_minutes").integer)) {
            data.running = data.resting = data.finished = false;
            data.deadline = 0;
            data.remaining = settings.at("focus_minutes").integer * 60;
        }
        const bool sourceChanged =
            self.spec.kind == Kind::Rss && settings.at("url").text != self.settings.at("url").text;
        const bool feedChanged =
            self.spec.kind == Kind::Rss &&
            (sourceChanged || settings.at("refresh_minutes").integer != self.settings.at("refresh_minutes").integer ||
             settings.at("rotation_seconds").integer != self.settings.at("rotation_seconds").integer);
        const bool driveChanged =
            self.spec.kind == Kind::Disk && settings.at("drive").text != self.settings.at("drive").text;
        const bool volumeChanged =
            self.spec.kind == Kind::Disk &&
            (driveChanged || settings.at("refresh_seconds").integer != self.settings.at("refresh_seconds").integer);
        const bool notesChanged =
            self.spec.kind == Kind::Notes && settings.at("text").text != self.settings.at("text").text;
        if (notesChanged)
            data.scroll = 0;
        self.settings = std::move(settings);
        self.data = std::move(data);
        self.motionTimer.stop();
        self.needles.reset();
        self.data.needles = {-1, -1};
        self.renderer.cancelInput();
        if (volumeChanged) {
            self.volume.reset();
            if (driveChanged) {
                self.data.valid = self.data.stale = false;
                self.data.available = self.data.total = self.data.free = 0;
                self.data.error.clear();
            }
            if (!self.paused) {
                startVolume(self);
            }
        }
        if (sourceChanged) {
            self.feed.reset();
            self.data.valid = false;
            self.data.stale = false;
            self.data.error.clear();
            self.data.articles.clear();
            self.data.source.clear();
            self.data.scroll = 0;
            self.data.loading = true;
        }
        if (feedChanged && !self.paused) {
            startFeed(self);
            self.timer.replace(uint32_t(self.settings.at("rotation_seconds").integer * 1000), tick, &self);
        }
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
}

CdResult CD_CALL save(void* pointer, uint8_t* out, uint64_t capacity, uint64_t* size) noexcept {
    try {
        if (!pointer || !size)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        const auto bytes = stateBytes(self.data, self.spec.kind);
        *size = bytes.size();
        if (!out && !capacity)
            return CD_OK;
        if (!out || capacity < bytes.size())
            return CD_ERROR_BUFFER;
        std::memcpy(out, bytes.data(), bytes.size());
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    try {
        if (!pointer || (size && !bytes) || size > 16384)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        auto changed = self.data;
        if (!size) {
            changed = Data{};
            if (self.spec.kind == Kind::Timer)
                changed.remaining = self.settings.at("focus_minutes").integer * 60;
        } else if (!restoreBytes(changed, self.spec.kind,
                                 std::string(reinterpret_cast<const char*>(bytes), size_t(size))))
            return CD_ERROR_ARGUMENT;
        if (self.spec.kind == Kind::Todo)
            reconcileTasks(changed, self.settings.at("tasks").text);
        if (self.spec.kind == Kind::Timer && !self.paused)
            advanceTimer(changed, self.settings, wallSeconds());
        self.data = std::move(changed);
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height || !std::isfinite(frame->device_pixel_ratio) ||
            frame->device_pixel_ratio <= 0 || frame->device_pixel_ratio > 8)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        return self.renderer.render(self.spec, self.settings, self.data, *frame, self.safe) ? CD_OK : CD_ERROR_INTERNAL;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) ||
            context->abi_version != CD_PLUGIN_ABI_VERSION || !context->host ||
            context->host->struct_size < sizeof(CdHostApi) || context->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !context->host->request_redraw || !context->host->subscribe_timer || !context->host->unsubscribe || !out ||
            size < sizeof(CdInstanceApi) || (spec.kind == Kind::Network && !context->host->subscribe_system))
            return CD_ERROR_ARGUMENT;
        auto self = std::make_unique<Instance>(spec, *context);
        self->host = *context->host;
        self->context = context->host_context;
        self->timer.bind(*context);
        self->motionTimer.bind(*context);
        self->renderer.bind(*context, [ptr = self.get()](uint32_t id) {
            action(*ptr, int(id));
        });
        self->lastRotate = GetTickCount64();
        if (spec.kind == Kind::Countdown)
            self->updateDays();
        if (self->spec.kind == Kind::Rss) {
            self->data.loading = true;
        }
        const auto result = start(*self);
        if (result != CD_OK) {
            stop(*self);
            return result;
        }
        *out = {sizeof(CdInstanceApi),
                CD_PLUGIN_ABI_VERSION,
                self.get(),
                destroy,
                render,
                input,
                layout,
                pause,
                save,
                restore,
                get,
                apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace redline_productivity
