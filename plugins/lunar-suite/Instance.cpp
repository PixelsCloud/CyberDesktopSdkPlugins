#include <Windows.h>
#include <sdk/Environment.h>
#include <sdk/Update.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <tuple>

#include "plugins/lunar-suite/Lunar.h"

namespace lunarplugin {
cdsdk::Settings defaults(bool mechanical) {
    if (mechanical) {
        return {{{"week_start", CD_SETTING_CHOICE, 0, 0, "monday", 0, 0, {"monday", "sunday"}},
                 {"adjacent_days", CD_SETTING_TOGGLE, 0},
                 {"reduced_motion", CD_SETTING_TOGGLE, 0},
                 {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
    }
    return {{{"lighting", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "day", "night"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"week_start", CD_SETTING_CHOICE, 0, 0, "monday", 0, 0, {"monday", "sunday"}},
             {"adjacent_days", CD_SETTING_TOGGLE, 1},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

namespace {
void CD_CALL animate(void* pointer, uint64_t) noexcept;

struct Instance {
    explicit Instance(Spec value) : spec(value), settings(defaults(value.mechanical)) {
        view.current = view.selected = today();
    }

    Spec spec;
    cdsdk::Settings settings;
    Renderer renderer;
    View view;
    CdHostApi host{};
    void* context = nullptr;
    CdRect safe{};
    uint64_t timer = 0;
    cdsdk::Timer animation;
    cdsdk::Environment environment;
    uint64_t rollStart = 0;
    bool hidden = false, followToday = true;

    void redraw() {
        host.request_redraw(context);
    }

    Date currentDate() const {
        if (!spec.mechanical) {
            return today();
        }
        const auto local = environment.localTime();
        const Date date{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
        return valid(date) ? date : today();
    }

    void finishAnimation() {
        animation.stop();
        view.progress = 1;
    }

    void select(Date date, bool animated = true) {
        if (date == view.selected) {
            if (!animated) {
                finishAnimation();
            }
            return;
        }
        const bool digitsChanged =
            spec.rows < 4 || date.year != view.selected.year || date.month != view.selected.month;
        bool rolling =
            spec.mechanical && animated && !hidden && digitsChanged && !settings.at("reduced_motion").integer;
        if (rolling && animation.replace(33, animate, this) != CD_OK) {
            rolling = false;
        }
        if (!rolling) {
            finishAnimation();
        }
        view.previous = view.selected;
        view.direction = std::tie(date.year, date.month, date.day) >
                                 std::tie(view.selected.year, view.selected.month, view.selected.day)
                             ? 1
                             : -1;
        view.selected = date;
        view.progress = rolling ? 0 : 1;
        rollStart = environment.snapshot().monotonic_ms;
    }

    void advanceAnimation() {
        if (hidden || view.progress >= 1) {
            finishAnimation();
            return;
        }
        const auto now = environment.snapshot().monotonic_ms;
        const double seconds = now >= rollStart ? (now - rollStart) / 1000. : .18;
        const cybercanvas::Tween<double> track(0, 1, {.duration = .18, .easing = cybercanvas::Ease::InOutCubic});
        view.progress = track.sample(seconds).value;
        if (view.progress >= 1) {
            finishAnimation();
        }
        redraw();
    }

    void updateDate(bool animated = true) {
        const auto date = currentDate();
        if (valid(date) && date != view.current) {
            view.current = date;
            if (followToday) {
                select(date, animated);
            }
            redraw();
        }
    }

    void stop() {
        if (timer) {
            host.unsubscribe(context, timer);
            timer = 0;
        }
    }

    ~Instance() {
        finishAnimation();
        stop();
    }
};

void CD_CALL animate(void* pointer, uint64_t) noexcept {
    try {
        static_cast<Instance*>(pointer)->advanceAnimation();
    } catch (...) {
        static_cast<Instance*>(pointer)->finishAnimation();
    }
}

void CD_CALL tick(void* pointer, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (!self.hidden) {
            self.updateDate();
        }
    } catch (...) {
    }
}

void CD_CALL destroy(void* pointer) noexcept {
    const auto instance = std::unique_ptr<Instance>(static_cast<Instance*>(pointer));
}

void CD_CALL layout(void* pointer, const CdLayout* value) noexcept {
    if (!pointer || !value || value->struct_size < sizeof(*value)) {
        return;
    }
    auto& self = *static_cast<Instance*>(pointer);
    self.safe = value->safe_content;
    if (self.safe.width <= 0 || self.safe.height <= 0) {
        self.safe = {0, 0, value->logical_width, value->logical_height};
    }
    self.view.dark = value->text_color.r * .2126 + value->text_color.g * .7152 + value->text_color.b * .0722 > 140;
    self.renderer.cancelInput();
    self.redraw();
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    if (!pointer || !frame || frame->struct_size < sizeof(*frame) || !frame->rgba) {
        return CD_ERROR_ARGUMENT;
    }
    auto& self = *static_cast<Instance*>(pointer);
    return self.renderer.render(self.spec, self.settings, self.view, *frame, self.safe) ? CD_OK : CD_ERROR_ARGUMENT;
}

void CD_CALL input(void* pointer, const CdInput* event) noexcept {
    try {
        if (!pointer || !event || event->struct_size < sizeof(*event) || !std::isfinite(event->x) ||
            !std::isfinite(event->y)) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.hidden) {
            return;
        }
        if (event->kind == CD_INPUT_DOUBLE_CLICK) {
            self.followToday = true;
            self.select(self.view.current);
            self.renderer.cancelInput();
            self.redraw();
            return;
        }
        self.renderer.syncInput(self.spec, self.settings, self.view, self.safe);
        self.renderer.input(*event);
    } catch (...) {
    }
}

void CD_CALL pause(void* pointer, uint32_t hidden) noexcept {
    try {
        if (!pointer) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.hidden == (hidden != 0)) {
            return;
        }
        self.hidden = hidden != 0;
        self.finishAnimation();
        self.renderer.pause(self.hidden);
        self.stop();
        self.renderer.cancelInput();
        if (!self.hidden) {
            self.updateDate(false);
            self.host.subscribe_timer(self.context, 1000, tick, &self, &self.timer);
            self.redraw();
        }
    } catch (...) {
    }
}

CdResult CD_CALL get(void* p, const char* id, CdSettingValue* value, uint32_t size) noexcept {
    return p ? static_cast<Instance*>(p)->settings.get(id, value, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* p, const CdSettingValue* values, uint32_t count) noexcept {
    if (!p) {
        return CD_ERROR_ARGUMENT;
    }
    auto& self = *static_cast<Instance*>(p);
    const auto result = self.settings.apply(values, count);
    if (result == CD_OK) {
        if (self.spec.mechanical && self.settings.at("reduced_motion").integer) {
            self.finishAnimation();
        }
        self.renderer.cancelInput();
        self.redraw();
    }
    return result;
}

CdResult CD_CALL save(void* p, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!p || !size) {
        return CD_ERROR_ARGUMENT;
    }
    *size = 8;
    if (!bytes && !capacity) {
        return CD_OK;
    }
    if (!bytes || capacity < 8) {
        return CD_ERROR_BUFFER;
    }
    const auto& self = *static_cast<Instance*>(p);
    const auto date = self.view.selected;
    const uint8_t state[]{'L',
                          'U',
                          1,
                          uint8_t(date.year),
                          uint8_t(date.year >> 8),
                          uint8_t(date.month),
                          uint8_t(date.day),
                          uint8_t(self.followToday)};
    std::memcpy(bytes, state, 8);
    return CD_OK;
}

CdResult CD_CALL restore(void* p, const uint8_t* bytes, uint64_t size) noexcept {
    if (!p || size && !bytes) {
        return CD_ERROR_ARGUMENT;
    }
    auto& self = *static_cast<Instance*>(p);
    if (!size) {
        return CD_OK;
    }
    if (size != 8 || bytes[0] != 'L' || bytes[1] != 'U' || bytes[2] != 1 || bytes[7] > 1) {
        return CD_ERROR_ARGUMENT;
    }
    const Date date{bytes[3] | int(bytes[4]) << 8, bytes[5], bytes[6]};
    if (!valid(date)) {
        return CD_ERROR_ARGUMENT;
    }
    self.followToday = bytes[7] != 0;
    self.select(self.followToday ? self.view.current : date, false);
    self.redraw();
    return CD_OK;
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* c, CdInstanceApi* out, uint32_t size) {
    try {
        if (!c || c->struct_size < sizeof(*c) || c->abi_version != CD_PLUGIN_ABI_VERSION || !c->host ||
            c->host->struct_size < sizeof(CdHostApi) || c->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !c->host->request_redraw || !c->host->subscribe_timer || !c->host->unsubscribe || !out ||
            size < sizeof(*out)) {
            return CD_ERROR_ARGUMENT;
        }
        auto self = std::make_unique<Instance>(spec);
        self->host = *c->host;
        self->context = c->host_context;
        self->animation.bind(*c);
        self->environment.bind(*c);
        self->view.current = self->view.selected = self->currentDate();
        self->renderer.bind(*c, [ptr = self.get()](uint32_t id) {
            if (ptr->hidden) {
                return;
            }
            if (id == 1 || id == 2) {
                ptr->select(shiftMonth(ptr->view.selected, id == 1 ? -1 : 1));
                ptr->followToday = false;
            } else if (id == 3) {
                ptr->select(ptr->view.current);
                ptr->followToday = true;
            } else if (id >= 100 && id < 142) {
                const auto grid = monthGrid(ptr->view.selected, ptr->settings.at("week_start").text == "monday");
                const auto date = grid.dates[id - 100];
                if (valid(date)) {
                    ptr->select(date);
                    ptr->followToday = date == ptr->view.current;
                }
            }
            ptr->redraw();
        });
        const auto result = self->host.subscribe_timer(self->context, 1000, tick, self.get(), &self->timer);
        if (result != CD_OK) {
            return result;
        }
        *out = {
            sizeof(*out), CD_PLUGIN_ABI_VERSION, self.get(), destroy, render, input, layout, pause, save, restore, get,
            apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace lunarplugin
