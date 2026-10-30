#include "sdk/Keyboard.h"

namespace cdsdk {
Keyboard::Keyboard(cybercanvas::Scene& scene) : scene_(scene) {}

Keyboard::~Keyboard() {
    unbind();
}

auto Keyboard::bind(const CdCreateContext& context) -> void {
    unbind();
    if (!context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
        return;
    }
    const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
    CdKeyboardApi api{};
    if (!host->query_service ||
        host->query_service(context.host_context, CD_SERVICE_KEYBOARD, 1, &api, sizeof(api)) != CD_OK ||
        api.struct_size < sizeof(api) || api.version != 1 || !api.connect || !api.request_focus || !api.publish) {
        return;
    }
    api_ = api;
    context_ = context.host_context;
    if (api_.connect(context_, deliver, this) != CD_OK) {
        api_ = {};
        context_ = nullptr;
        return;
    }
    sync();
}

auto Keyboard::unbind() -> void {
    if (api_.connect) {
        api_.connect(context_, nullptr, nullptr);
    }
    api_ = {};
    context_ = nullptr;
    granted_ = false;
    signature_.clear();
    scene_.focusGranted(false);
}

auto Keyboard::available() const -> bool {
    return api_.connect != nullptr;
}

auto Keyboard::request() -> bool {
    return api_.request_focus && api_.request_focus(context_, 1) == CD_OK;
}

auto Keyboard::onAction(std::function<void(cybercanvas::SceneEvent)> callback) -> void {
    action_ = std::move(callback);
}

auto Keyboard::origin(cybercanvas::Point value) -> void {
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        throw std::invalid_argument("Invalid semantic origin");
    }
    origin_ = value;
    sync();
}

auto Keyboard::origin() const -> cybercanvas::Point {
    return origin_;
}

auto Keyboard::sync() -> void {
    if (!api_.publish || publishing_) {
        return;
    }
    const auto semantic = scene_.semantics();
    std::vector<std::pair<std::string, std::string>> strings;
    strings.reserve(semantic.size());
    for (const auto& node : semantic) {
        strings.emplace_back(utf8(node.name), utf8(node.value));
    }
    std::vector<CdSemanticNode> nodes;
    std::string signature;
    for (size_t i = 0; i < semantic.size(); ++i) {
        const auto& node = semantic[i];
        const uint32_t state = (node.enabled ? CD_UI_ENABLED : 0) | (node.focusable ? CD_UI_FOCUSABLE : 0) |
                               (node.focused ? CD_UI_FOCUSED : 0) | (node.selected ? CD_UI_SELECTED : 0) |
                               (node.checked ? CD_UI_CHECKED : 0) | (node.busy ? CD_UI_BUSY : 0) |
                               (node.error ? CD_UI_ERROR : 0);
        CdSemanticNode out{
            sizeof(out),
            node.id,
            node.parent,
            uint32_t(node.role),
            state,
            0,
            {node.bounds.x + origin_.x, node.bounds.y + origin_.y, node.bounds.width, node.bounds.height},
            nullptr,
            nullptr,
            node.number,
            node.minimum,
            node.maximum,
            node.step};
        signature.append(reinterpret_cast<const char*>(&out), sizeof(out));
        signature.append(strings[i].first).push_back('\0');
        signature.append(strings[i].second).push_back('\0');
        out.name = strings[i].first.c_str();
        out.value = strings[i].second.c_str();
        nodes.push_back(out);
    }
    if (signature == signature_) {
        return;
    }
    publishing_ = true;
    const auto result = api_.publish(context_, nodes.data(), uint32_t(nodes.size()));
    publishing_ = false;
    if (result == CD_OK) {
        signature_ = std::move(signature);
    }
}

auto Keyboard::utf8(const std::wstring& value) -> std::string {
    const int count = int(std::min<size_t>(value.size(), 4096));
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), count, nullptr, 0, nullptr, nullptr);
    if (size < 0) {
        return {};
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), count, result.data(), size, nullptr, nullptr);
    if (result.size() > 4096) {
        size_t end = 4096;
        while (end && (uint8_t(result[end]) & 0xC0) == 0x80) {
            --end;
        }
        result.resize(end);
    }
    return result;
}

CdResult CD_CALL Keyboard::deliver(void* user, const CdUiEvent* event) noexcept {
    if (!user || !event || event->struct_size < sizeof(*event)) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        auto& self = *static_cast<Keyboard*>(user);
        cybercanvas::SceneEvent result;
        if (event->kind == CD_UI_FOCUS) {
            if (event->value != 0 && event->value != 1) {
                return CD_ERROR_ARGUMENT;
            }
            self.granted_ = event->value == 1;
            self.scene_.focusGranted(self.granted_);
            self.sync();
            return CD_OK;
        }
        if (!self.granted_) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (event->modifiers & (CD_MOD_CONTROL | CD_MOD_ALT | CD_MOD_META)) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (event->kind == CD_UI_KEY && event->key >= 1 && event->key <= 10) {
            result = self.scene_.key({cybercanvas::Key(event->key - 1), bool(event->modifiers & CD_MOD_SHIFT)});
        } else if (event->kind == CD_UI_ACTION && self.scene_.focus(event->node)) {
            if (event->key == CD_UI_SET_FOCUS) {
                result.handled = true;
            } else if (event->key == CD_UI_PRESS) {
                result = self.scene_.key({cybercanvas::Key::Enter});
            } else if (event->key == CD_UI_INCREASE || event->key == CD_UI_DECREASE) {
                result =
                    self.scene_.key({event->key == CD_UI_INCREASE ? cybercanvas::Key::Right : cybercanvas::Key::Left});
            } else if (event->key == CD_UI_SET_VALUE && std::isfinite(event->value)) {
                const auto& view = self.scene_.view(event->node);
                if (view.kind == cybercanvas::ViewKind::Slider) {
                    self.scene_.value(event->node, event->value);
                    result.key = view.key;
                    result.value = self.scene_.view(event->node).value;
                    result.handled = true;
                }
            }
        }
        self.sync();
        if (result.handled && self.action_) {
            const auto callback = self.action_;
            callback(result);
        }
        return result.handled ? CD_OK : CD_ERROR_UNSUPPORTED;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace cdsdk
