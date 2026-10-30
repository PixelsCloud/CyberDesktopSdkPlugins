#pragma once

#include "plugins/lunar-suite/Lunar.h"
#include "plugins/redline-desk/Skin.h"

namespace redline_lunar {
using namespace lunarplugin;

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bind(const CdCreateContext&, std::function<void(uint32_t)>);
    void input(const CdInput&);
    void cancelInput();
    void pause(bool);
    bool pendingImages() const;
    void syncInput(Spec, const cdsdk::Settings&, View, CdRect);
    bool render(Spec spec, const cdsdk::Settings& settings, View view, const CdFrame& frame, CdRect safe);
    bool save(const std::wstring& filename, const CdFrame& frame);

private:
    std::unique_ptr<redline::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace redline_lunar
