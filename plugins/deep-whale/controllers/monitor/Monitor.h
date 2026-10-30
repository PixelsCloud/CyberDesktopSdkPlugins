// Controller binding reused from plugins/vampire-alice/controllers/monitor/Monitor.h.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "plugins/system-info-suite/Monitor.h"
#include "plugins/system-info-suite/ThemedController.h"

namespace whaleskin {
class Skin;
}

namespace deep_monitor {
using namespace monitorplugin;

class Renderer final : public monitorplugin::themed::View {
public:
    Renderer();
    ~Renderer() override;
    void bind(const CdCreateContext&, std::function<void()> changed) override;
    void pause(bool paused) override;
    bool poll() override;
    bool render(Spec, const Options&, Values, const CdFrame&, CdRect safe = {}) override;
    bool savePng(const std::wstring&, const CdFrame&, CdColor background);

private:
    std::unique_ptr<whaleskin::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace deep_monitor
