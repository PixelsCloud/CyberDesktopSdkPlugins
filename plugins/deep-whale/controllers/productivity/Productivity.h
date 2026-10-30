// Controller binding reused from plugins/vampire-alice/controllers/productivity/Productivity.h.
#pragma once

#include <memory>

#include "plugins/productivity-suite/Productivity.h"

namespace whaleskin {
class Skin;
}

namespace deep_productivity {
using namespace productivity;

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bind(const CdCreateContext&, std::function<void(uint32_t)>);
    void input(const CdInput&);
    void cancelInput();
    void pause(bool paused);
    void syncInput(Spec spec, const cdsdk::Settings& settings, const Data& data, CdRect safe);
    bool render(Spec, const cdsdk::Settings&, const Data&, const CdFrame&, CdRect);
    bool savePng(const std::filesystem::path&, const CdFrame&);

private:
    std::unique_ptr<whaleskin::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace deep_productivity
