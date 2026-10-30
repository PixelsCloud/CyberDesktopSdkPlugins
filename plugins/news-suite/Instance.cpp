#include <Windows.h>
#include <sdk/Interaction.h>
#include <sdk/Update.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "plugins/news-suite/News.h"
#include "sdk/cpp/Services.h"

namespace news {
namespace {
struct Instance {
    Spec spec;
    cdsdk::Settings settings;
    Model model;
    Renderer renderer;
    cdsdk::Services services;
    cdsdk::ContentWatch content;
    CdHostApi host{};
    void* context = nullptr;
    CdRect safe{};
    double dpr = 1;
    cdsdk::RedrawRequests updates;
    cdsdk::Interaction interaction;
    cdsdk::UpdateLoop feedback;
    bool paused = false;

    explicit Instance(Spec value, const CdCreateContext& creation)
        : spec(value), settings(defaults(value.kind)), services(creation.host, creation.host_context) {}

    ~Instance() {
        interaction.cancel();
        feedback.stop();
        content.reset();
    }

    void redraw() {
        updates.invalidate();
    }

    void request(bool manual = false) {
        if (paused) {
            return;
        }
        if (manual && content.token()) {
            services.refreshContent(content.token());
            return;
        }
        content.reset();
        const auto q = query(spec.kind, settings);
        std::vector<const char*> urls;
        for (const auto& url : q.urls) {
            urls.push_back(url.c_str());
        }
        const CdContentQuery options{sizeof(options),
                                     q.kind == Kind::Github ? CD_CONTENT_GITHUB
                                     : q.source == "hn"     ? CD_CONTENT_HACKER_NEWS
                                                            : CD_CONTENT_FEEDS,
                                     uint32_t(settings.at("refresh_minutes").integer * 60),
                                     uint32_t(urls.size()),
                                     urls.data(),
                                     q.period.c_str(),
                                     q.language.c_str(),
                                     q.hnOrder.c_str()};
        content = services.content(options, [this](const cdsdk::ContentSnapshot& value) {
            if (paused) {
                return;
            }
            const bool loading = value.status == CD_DATA_LOADING && model.items.empty();
            const bool changedItems = value.valid() && model.items != value.items;
            const bool changed = changedItems || model.loading != loading || model.error != value.errorCode ||
                                 model.partial != value.partial;
            model.loading = loading;
            model.error = value.errorCode;
            model.partial = value.partial;
            if (value.valid()) {
                model.fetched = int64_t(value.observedAtUtc / 1000);
                if (changedItems) {
                    model.items = value.items;
                    model.page = 0;
                    renderer.resetList();
                }
            }
            if (changed) {
                resetInput();
                redraw();
            }
        });
        if (content.result() != CD_OK) {
            model.loading = false;
            model.error = "service_unavailable";
            redraw();
        }
    }

    void resetInput() {
        interaction.cancel();
        renderer.cancelList();
        model.widgets = {};
        feedback.stop();
    }

    void targets() {
        std::vector<cybercanvas::HitRegion> regions;
        const auto hits = hitRegions(spec, model, safe, dpr, settings.at("list_mode").text == "scroll");
        int priority = int(hits.size());
        for (const auto& hit : hits) {
            const auto& r = hit.area;
            cybercanvas::Button button(uint32_t(hit.action), {r.x, r.y, r.width, r.height});
            button.priority = priority--;
            regions.push_back(button.region());
        }
        interaction.regions(std::move(regions));
    }
};

CdResult subscribe(Instance& self) {
    self.request();
    return CD_OK;
}

void sourceChanged(Instance& self) {
    self.resetInput();
    self.content.reset();
    self.model.items.clear();
    self.model.fetched = 0;
    self.model.error.clear();
    self.model.loading = true;
    self.model.page = 0;
    self.renderer.resetList();
    self.model.key = query(self.spec.kind, self.settings).key();
    self.request();
}

void action(Instance& self, int code) {
    auto& m = self.model;
    const auto& list = visible(m);
    const auto g = geometry(self.spec, self.safe, self.dpr);
    const int pages = std::max(1, (int(list.size()) + g.capacity - 1) / g.capacity);
    if (code == 1) {
        self.request(true);
    } else if (code == 2) {
        m.savedOnly = !m.savedOnly;
        m.page = 0;
        self.renderer.resetList();
    } else if (code == 5) {
        self.resetInput();
        const CdSettingValue mode{sizeof(mode),
                                  CD_SETTING_CHOICE,
                                  "list_mode",
                                  0,
                                  0,
                                  self.settings.at("list_mode").text == "scroll" ? "pages" : "scroll"};
        self.settings.apply(&mode, 1);
        m.page = 0;
        self.renderer.resetList();
    } else if (code == 3 || code == 4) {
        m.page = (std::clamp(m.page, 0, pages - 1) + (code == 3 ? pages - 1 : 1)) % pages;
    } else if (code >= 10 && code < 15) {
        const char* reader[]{"bbc", "ars", "ithome", "hn", "custom"};
        const char* periods[]{"daily", "weekly", "monthly"};
        const bool github = self.spec.kind == Kind::Github;
        if (github && code >= 13) {
            return;
        }
        const CdSettingValue change{sizeof(change),
                                    CD_SETTING_CHOICE,
                                    github ? "period" : "source",
                                    0,
                                    0,
                                    github ? periods[code - 10] : reader[code - 10]};
        self.settings.apply(&change, 1);
        m.savedOnly = false;
        sourceChanged(self);
    } else if (code >= 100) {
        const int index = (code >= 300 ? code - 300 : code >= 200 ? code - 200 : code - 100);
        if (index >= int(list.size())) {
            return;
        }
        const auto item = list[index];
        if (code >= 300) {
            if (!item.discussion.empty()) {
                self.services.openUrl(item.discussion.c_str());
            }
        } else if (code >= 200) {
            toggleSaved(m, item);
        } else {
            self.services.openUrl(item.url.c_str());
            markRead(m, item.url);
        }
    }
    self.redraw();
}

void updateFeedback(Instance& self) {
    self.model.widgets.sync(self.interaction.router);
    if (!self.feedback.active() && (self.model.widgets.active() || self.renderer.listActive()) && !self.paused) {
        if (self.feedback.start(33, GetTickCount64()) != CD_OK) {
            self.model.widgets.advance(.14);
            self.renderer.advanceList(1);
        }
    }
}

void CD_CALL input(void* pointer, const CdInput* event) noexcept {
    try {
        if (!pointer || !event || event->struct_size < sizeof(*event)) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused) {
            return;
        }
        if (self.settings.at("list_mode").text == "scroll" &&
            (!self.interaction.router.pressed() || event->kind == CD_INPUT_WHEEL)) {
            auto& list = self.renderer.listView(self.spec, self.settings, self.model, self.safe, self.dpr);
            if ((event->kind == CD_INPUT_PRESS || event->kind == CD_INPUT_WHEEL) &&
                list.contains({event->x - self.safe.x, event->y - self.safe.y})) {
                self.interaction.cancel();
            }
            const auto result = list.input(*event, {self.safe.x, self.safe.y});
            if (result.handled) {
                self.interaction.router.dispatch({cybercanvas::PointerKind::Leave});
                if (result.retry) {
                    self.request(true);
                } else if (result.clicked) {
                    const auto& items = visible(self.model);
                    const auto found = std::find_if(items.begin(), items.end(), [&](const Item& item) {
                        return item.url == result.id;
                    });
                    if (found != items.end()) {
                        const auto item = *found;
                        if (result.action == 1) {
                            toggleSaved(self.model, item);
                        } else if (result.action == 2 && !item.discussion.empty()) {
                            self.services.openUrl(item.discussion.c_str());
                        } else if (!result.action) {
                            self.services.openUrl(item.url.c_str());
                            markRead(self.model, item.url);
                        }
                        self.redraw();
                    }
                }
                updateFeedback(self);
                return;
            }
        }
        self.targets();
        const auto result = self.interaction.input(*event);
        if (result.clicked) {
            action(self, int(result.clicked));
            self.interaction.cancel();
        } else if (event->kind == CD_INPUT_WHEEL && event->wheel_delta != 0 &&
                   self.settings.at("list_mode").text == "pages") {
            self.interaction.cancel();
            action(self, event->wheel_delta > 0 ? 3 : 4);
        }
        updateFeedback(self);
        if (result.changed) {
            self.redraw();
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
    self.dpr = std::isfinite(value->device_pixel_ratio) ? std::clamp(value->device_pixel_ratio, .5, 4.) : 1;
    if (!std::isfinite(self.safe.width) || !std::isfinite(self.safe.height) || self.safe.width <= 0 ||
        self.safe.height <= 0) {
        self.safe = {0, 0, value->logical_width, value->logical_height};
    }
    self.model.light = value->text_color.r * .2126 + value->text_color.g * .7152 + value->text_color.b * .0722 < 128;
    self.model.page = 0;
    self.resetInput();
    self.redraw();
}

void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
    try {
        if (!pointer) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused == bool(paused)) {
            return;
        }
        self.paused = paused != 0;
        self.updates.pause(self.paused);
        self.resetInput();
        if (self.paused) {
            self.content.reset();
            self.content.reset();
            self.model.loading = false;
        } else {
            if (subscribe(self) != CD_OK) {
                self.paused = true;
                return;
            }
            self.redraw();
        }
    } catch (...) {
    }
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer) {
            return CD_ERROR_ARGUMENT;
        }
        auto& self = *static_cast<Instance*>(pointer);
        auto next = self.settings;
        const auto status = next.apply(values, count);
        if (status != CD_OK) {
            return status;
        }
        wide(next.at("feeds").text);
        const auto previous = query(self.spec.kind, self.settings).key();
        const auto changed = query(self.spec.kind, next).key() != previous;
        // Validate custom subscriptions even when a built-in source is selected.
        auto validate = next;
        const CdSettingValue custom{sizeof(custom), CD_SETTING_CHOICE, "source", 0, 0, "custom"};
        validate.apply(&custom, 1);
        query(Kind::Reader, validate);
        const bool intervalChanged = next.at("refresh_minutes").integer != self.settings.at("refresh_minutes").integer;
        const bool modeChanged = next.at("list_mode").text != self.settings.at("list_mode").text;
        self.settings = std::move(next);
        self.resetInput();
        if (modeChanged) {
            self.renderer.resetList();
            self.model.page = 0;
        }
        if (changed) {
            sourceChanged(self);
        } else if (intervalChanged) {
            self.request();
        }
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* out, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, out, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL save(void* pointer, uint8_t* out, uint64_t capacity, uint64_t* size) noexcept {
    try {
        if (!pointer || !size) {
            return CD_ERROR_ARGUMENT;
        }
        const auto bytes = stateBytes(static_cast<Instance*>(pointer)->model);
        *size = bytes.size();
        if (!out && !capacity) {
            return CD_OK;
        }
        if (!out || capacity < bytes.size()) {
            return CD_ERROR_BUFFER;
        }
        std::memcpy(out, bytes.data(), bytes.size());
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    try {
        if (!pointer || (size && !bytes) || size > 2 * 1024 * 1024) {
            return CD_ERROR_ARGUMENT;
        }
        auto& self = *static_cast<Instance*>(pointer);
        auto next = self.model;
        if (!size) {
            next = Model{};
            next.light = self.model.light;
        } else if (!restoreBytes(next, std::string(reinterpret_cast<const char*>(bytes), size_t(size)))) {
            return CD_ERROR_ARGUMENT;
        }
        const auto key = query(self.spec.kind, self.settings).key();
        if (key != next.key) {
            next.items.clear();
            next.fetched = 0;
        }
        next.key = key;
        self.content.reset();
        self.resetInput();
        self.model = std::move(next);
        self.model.widgets = {};
        self.request();
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame) {
            return CD_ERROR_ARGUMENT;
        }
        auto& self = *static_cast<Instance*>(pointer);
        self.updates.beginFrame();
        return self.renderer.render(self.spec, self.settings, self.model, *frame, self.safe) ? CD_OK
                                                                                             : CD_ERROR_ARGUMENT;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(*context) || context->abi_version != CD_PLUGIN_ABI_VERSION ||
            !out || size < sizeof(*out) || !context->host || context->host->struct_size < sizeof(CdHostApi) ||
            context->host->abi_version != CD_PLUGIN_ABI_VERSION || !context->host->request_redraw ||
            !context->host->subscribe_timer || !context->host->unsubscribe) {
            return CD_ERROR_ARGUMENT;
        }
        auto self = std::make_unique<Instance>(spec, *context);
        self->host = *context->host;
        self->context = context->host_context;
        self->updates.bind(*context);
        self->feedback.bind(*context);
        self->feedback.onInvalidate([pointer = self.get()] {
            pointer->redraw();
        });
        self->feedback.onUpdate([pointer = self.get()](cybercanvas::AnimationTick tick) {
            bool changed = pointer->model.widgets.advance(tick.unscaledDelta);
            changed |= pointer->renderer.advanceList(tick.unscaledDelta);
            return cdsdk::UpdateStep{changed, pointer->model.widgets.active() || pointer->renderer.listActive()};
        });
        self->renderer.bindHost(*context);
        self->renderer.onDiagnostics([pointer = self.get()](auto& stats) {
            stats.redraw = pointer->updates.stats();
        });
        self->renderer.onInvalidate([pointer = self.get()] {
            pointer->redraw();
        });
        self->interaction.bind(*context);
        self->model.key = query(spec.kind, self->settings).key();
        const auto result = subscribe(*self);
        if (result != CD_OK) {
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
}  // namespace news
