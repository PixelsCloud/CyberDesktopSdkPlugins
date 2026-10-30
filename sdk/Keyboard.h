#pragma once

#include <CyberCanvas/Scene.h>

#include "services/Keyboard.h"

namespace cdsdk {
// Owns the optional host registration. No keyboard hooks or worker callbacks.
class Keyboard {
public:
    explicit Keyboard(cybercanvas::Scene& scene);

    ~Keyboard();

    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;

    void bind(const CdCreateContext& context);

    void unbind();

    bool available() const;

    bool request();

    void onAction(std::function<void(cybercanvas::SceneEvent)> callback);

    void origin(cybercanvas::Point value);

    cybercanvas::Point origin() const;

    void sync();

private:
    static std::string utf8(const std::wstring& value);

    static CdResult CD_CALL deliver(void* user, const CdUiEvent* event) noexcept;

    cybercanvas::Scene& scene_;
    CdKeyboardApi api_{};
    void* context_ = nullptr;
    bool granted_ = false, publishing_ = false;
    std::string signature_;
    cybercanvas::Point origin_{};
    std::function<void(cybercanvas::SceneEvent)> action_;
};
}  // namespace cdsdk
