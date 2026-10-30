#pragma once

#include <sdk/PluginApi.h>

#include <functional>
#include <memory>

namespace monitorplugin {
struct Spec;
struct Options;
struct Values;

namespace themed {
class View {
public:
    virtual ~View();
    virtual void bind(const CdCreateContext&, std::function<void()>) = 0;
    virtual void pause(bool) = 0;
    virtual bool poll();
    virtual bool render(Spec, const Options&, Values, const CdFrame&, CdRect) = 0;
};

using ViewFactory = std::unique_ptr<View> (*)();
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t, ViewFactory, bool singleMemory = false);
}  // namespace themed
}  // namespace monitorplugin
