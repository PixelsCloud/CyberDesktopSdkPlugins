#include "sdk/Controls.h"

namespace cdsdk {
Controls::~Controls() {
    if (keyboard_.connect) {
        keyboard_.connect(context_, nullptr, nullptr);
    }
}

auto Controls::bind(const CdCreateContext& context, std::function<void(uint32_t)> activate) -> void {
    if (!context.host || context.host->struct_size < sizeof(CdHostApi)) {
        throw std::invalid_argument("Controls require a valid host");
    }
    if (keyboard_.connect) {
        keyboard_.connect(context_, nullptr, nullptr);
    }
    keyboard_ = {};
    signature_.clear();
    granted_ = false;
    action_ = std::move(activate);
    context_ = context.host_context;
    host_ = *context.host;
    pointer_.bind(context);
    icons.bind(context);
    if (context.host->struct_size >= sizeof(CdServiceHostApi)) {
        const auto* services = reinterpret_cast<const CdServiceHostApi*>(context.host);
        CdKeyboardApi api{};
        if (services->query_service &&
            services->query_service(context_, CD_SERVICE_KEYBOARD, 1, &api, sizeof(api)) == CD_OK &&
            api.struct_size >= sizeof(api) && api.version == 1 && api.connect && api.publish && api.request_focus &&
            api.connect(context_, deliver, this) == CD_OK) {
            keyboard_ = api;
        }
    }
}

auto Controls::items(std::vector<Item> values, cybercanvas::Point origin) -> void {
    std::vector<cybercanvas::HitRegion> regions;
    std::set<uint32_t> ids;
    if (values.size() > 256 || !std::isfinite(origin.x) || !std::isfinite(origin.y)) {
        throw std::invalid_argument("Invalid control collection");
    }
    for (auto& item : values) {
        if (!item.id || !ids.insert(item.id).second) {
            throw std::invalid_argument("Control IDs must be unique");
        }
        if (item.name.size() > 4096) {
            size_t end = 4096;
            while (end && (uint8_t(item.name[end]) & 0xC0) == 0x80) {
                --end;
            }
            item.name.resize(end);
        }
        cybercanvas::Control control{item.id, item.bounds, item.enabled};
        regions.push_back(control.region(origin));
    }
    // Invalidate a pressed row when its identity or geometry changes.
    const auto old = find(pointer_.router.pressed());
    if (old) {
        const auto next = std::find_if(values.begin(), values.end(), [&](const Item& item) {
            return item.id == old->id && item.name == old->name && item.bounds.x == old->bounds.x &&
                   item.bounds.y == old->bounds.y && item.bounds.width == old->bounds.width &&
                   item.bounds.height == old->bounds.height;
        });
        if (next == values.end() || origin.x != origin_.x || origin.y != origin_.y) {
            pointer_.cancel();
        }
    }
    items_ = std::move(values);
    origin_ = origin;
    pointer_.regions(std::move(regions));
    if (!find(focused_) || !find(focused_)->enabled) {
        focused_ = 0;
    }
    publish();
}

auto Controls::input(const CdInput& event) -> void {
    const auto result = pointer_.input(event);
    if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
        focused_ = pointer_.router.pressed();
        if (focused_ && keyboard_.request_focus) {
            keyboard_.request_focus(context_, 1);
        }
    }
    if (result.changed) {
        publish();
        redraw();
    }
    if (result.clicked && action_) {
        action_(result.clicked);
    }
}

auto Controls::cancel() -> void {
    pointer_.cancel();
    focused_ = 0;
    granted_ = false;
    items_ = {};
    pointer_.router.regions({});
    publish();
    // Keep the host grant in sync, otherwise a later request can be deduplicated
    // by a host that still considers this plugin focused.
    if (keyboard_.request_focus) {
        keyboard_.request_focus(context_, 0);
    }
}

auto Controls::visual(uint32_t id) const -> cybercanvas::ControlVisual {
    const bool pressed = pointer_.router.pressed() == id;
    const bool hovered = pointer_.router.hovered() == id;
    return {hovered, pressed, pressed ? 1. : hovered ? .55 : 0.};
}

auto Controls::hasFocus() const -> bool {
    return granted_ && focused_ != 0;
}

auto Controls::drawFocus(cybercanvas::Canvas& canvas, const cybercanvas::WidgetTheme& theme) const -> void {
    const auto* item = granted_ ? find(focused_) : nullptr;
    if (item && item->enabled) {
        auto area = cybercanvas::inset(item->bounds, {1, 1, 1, 1});
        if (area.width > 0 && area.height > 0) {
            canvas.strokeBox(area, theme.radius, theme.accent, {.width = 1});
        }
    }
}

auto Controls::find(uint32_t id) const -> const Item* {
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const Item& item) {
        return item.id == id;
    });
    return found == items_.end() ? nullptr : &*found;
}

auto Controls::redraw() -> void {
    if (host_.request_redraw) {
        host_.request_redraw(context_);
    }
}

auto Controls::publish() -> void {
    if (!keyboard_.publish || publishing_) {
        return;
    }
    std::vector<CdSemanticNode> nodes;
    std::string signature;
    for (const auto& item : items_) {
        const uint32_t flags = (item.enabled ? CD_UI_ENABLED | CD_UI_FOCUSABLE : 0) |
                               (granted_ && focused_ == item.id ? CD_UI_FOCUSED : 0) |
                               (item.checked ? CD_UI_CHECKED : 0);
        CdSemanticNode node{
            sizeof(node),
            item.id,
            0,
            item.toggle ? CD_ROLE_TOGGLE : CD_ROLE_BUTTON,
            flags,
            0,
            {item.bounds.x + origin_.x, item.bounds.y + origin_.y, item.bounds.width, item.bounds.height},
            nullptr,
            nullptr,
            0,
            0,
            0,
            0};
        signature.append(reinterpret_cast<const char*>(&node), sizeof(node));
        signature.append(item.name).push_back('\0');
        node.name = item.name.c_str();
        node.value = item.toggle ? (item.checked ? "true" : "false") : "";
        nodes.push_back(node);
    }
    if (signature != signature_) {
        publishing_ = true;
        const auto result = keyboard_.publish(context_, nodes.data(), uint32_t(nodes.size()));
        publishing_ = false;
        if (result == CD_OK) {
            signature_ = std::move(signature);
        }
    }
}

CdResult CD_CALL Controls::deliver(void* user, const CdUiEvent* event) noexcept {
    if (!user || !event || event->struct_size < sizeof(*event)) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        auto& self = *static_cast<Controls*>(user);
        uint32_t activated = 0;
        if (event->kind == CD_UI_FOCUS) {
            if (event->value != 0 && event->value != 1) {
                return CD_ERROR_ARGUMENT;
            }
            self.granted_ = event->value == 1;
        } else if (!self.granted_ || (event->modifiers & (CD_MOD_CONTROL | CD_MOD_ALT | CD_MOD_META))) {
            return CD_ERROR_UNSUPPORTED;
        } else if (event->kind == CD_UI_KEY && event->key == CD_KEY_TAB) {
            std::vector<uint32_t> ids;
            for (const auto& item : self.items_) {
                if (item.enabled && item.bounds.width > 0 && item.bounds.height > 0) {
                    ids.push_back(item.id);
                }
            }
            if (!ids.empty()) {
                const auto found = std::find(ids.begin(), ids.end(), self.focused_);
                const bool reverse = (event->modifiers & CD_MOD_SHIFT) != 0;
                const size_t index = found == ids.end()
                                         ? (reverse ? ids.size() - 1 : 0)
                                         : (size_t(found - ids.begin()) + (reverse ? ids.size() - 1 : 1)) % ids.size();
                self.focused_ = ids[index];
            }
        } else if (event->kind == CD_UI_KEY && (event->key == CD_KEY_ENTER || event->key == CD_KEY_SPACE)) {
            activated = self.focused_;
        } else if (event->kind == CD_UI_KEY && event->key == CD_KEY_ESCAPE) {
            self.focused_ = 0;
        } else if (event->kind == CD_UI_ACTION && self.find(event->node) && self.find(event->node)->enabled &&
                   (event->key == CD_UI_SET_FOCUS || event->key == CD_UI_PRESS)) {
            self.focused_ = event->node;
            activated = event->key == CD_UI_PRESS ? event->node : 0;
        } else {
            return CD_ERROR_UNSUPPORTED;
        }
        self.publish();
        self.redraw();
        if (activated && self.find(activated) && self.find(activated)->enabled && self.action_) {
            self.action_(activated);
        }
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace cdsdk
