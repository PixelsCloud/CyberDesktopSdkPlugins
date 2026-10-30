// Controller binding reused from plugins/vampire-alice/controllers/monitor/Monitor.h.
#pragma once

#include "plugins/mecha-desk/Skin.h"
#include "plugins/system-info-suite/Monitor.h"

namespace aeris_monitor {
using namespace monitorplugin;

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bind(const CdCreateContext&, std::function<void()> changed);
    void pause(bool paused);
    bool poll();
    bool render(Spec, const Options&, Values, const CdFrame&, CdRect safe = {});
    bool savePng(const std::wstring&, const CdFrame&, CdColor background);

private:
    std::unique_ptr<aerisskin::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace aeris_monitor
