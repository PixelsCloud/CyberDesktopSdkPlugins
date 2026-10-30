// Controller binding reused from plugins/vampire-alice/controllers/lunar/Lunar.h.
#pragma once

#include <memory>

#include "plugins/lunar-suite/Lunar.h"

namespace whaleskin {
class Skin;
}

namespace deep_lunar {
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
    std::unique_ptr<whaleskin::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace deep_lunar
