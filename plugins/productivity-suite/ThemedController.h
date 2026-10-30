#pragma once

#include <sdk/PluginApi.h>

#include <functional>
#include <memory>

namespace cdsdk {
class Settings;
}

namespace productivity {
struct Spec;
struct Data;

namespace themed {
class View {
public:
    virtual ~View();
    virtual void bind(const CdCreateContext&, std::function<void(uint32_t)>) = 0;
    virtual void input(const CdInput&) = 0;
    virtual void cancelInput() = 0;
    virtual void pause(bool) = 0;
    virtual void syncInput(Spec, const cdsdk::Settings&, const Data&, CdRect) = 0;
    virtual bool render(Spec, const cdsdk::Settings&, const Data&, const CdFrame&, CdRect) = 0;
};

using ViewFactory = std::unique_ptr<View> (*)();
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t, ViewFactory, const char* defaultFont = nullptr);
}  // namespace themed
}  // namespace productivity
