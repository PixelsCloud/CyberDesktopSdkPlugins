#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <memory>

namespace streetlink {
// Lifecycle and input adapter for the authored channel / magnetic-drum scenes.
// Box2D and the shared audio onset detector own the actual motion.
class Physics {
public:
    explicit Physics(const CdCreateContext& context);
    ~Physics();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace streetlink
