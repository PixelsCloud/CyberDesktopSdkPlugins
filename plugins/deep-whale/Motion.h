#pragma once
#include <sdk/Environment.h>
#include <sdk/Interaction.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <array>
#include <cstdint>

#include "plugins/beat-machine/Machine.h"
#include "plugins/deep-whale/Skin.h"

namespace deep_motion {
class Instance {
public:
    explicit Instance(const CdCreateContext&);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame&);
    void input(const CdInput&);
    void layoutChanged(const CdLayout&);
    void setPaused(bool);
    CdResult applySettings(const CdSettingValue*, uint32_t);
    std::array<uint64_t, 4> physicsCounters() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace deep_motion
