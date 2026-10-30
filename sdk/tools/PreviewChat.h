#pragma once

#include <functional>
#include <memory>
#include <string>

#include "sdk/services/CharacterChat.h"

namespace cdsdk {
enum class MockScenario;

class PreviewChat {
public:
    explicit PreviewChat(std::function<void()> redraw);
    ~PreviewChat();
    CdResult connect(CdCharacterChatCallback callback, void* user);
    CdResult command(const char* bytes, uint32_t length);
    void metadata(const std::string& bytes, const std::string& error);
    void tick(double seconds, MockScenario scenario, bool paused);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
