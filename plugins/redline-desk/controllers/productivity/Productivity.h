#pragma once

#include "plugins/productivity-suite/Productivity.h"
#include "plugins/redline-desk/Skin.h"

namespace redline_productivity {
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
    std::unique_ptr<redline::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace redline_productivity
