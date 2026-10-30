#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "plugins/system-info-suite/ThemedController.h"

namespace monitorplugin {
struct Spec {
    const char* id;
    int rows, columns;
    bool mechanical = false;
};

struct Values {
    double cpu = 0, ram = 0, gpu = 0, temperature = 0;
    uint32_t valid = 0;
    bool operator==(const Values&) const = default;
};

struct Options {
    std::string palette = "theme", primary = "cpu", fontFamily = "@siyuan-heavy", language = "zh-CN";
    CdColor themeText{227, 241, 245, 255};
    Values needles{};
    bool interpolate = false;
};

struct Position {
    double x, y, scale;
    int metric;
};

std::vector<Position> arrange(Spec, double width, double height, const Options&);
Values decode(const CdSystemInfo&);

class Renderer final : public monitorplugin::themed::View {
public:
    Renderer();
    ~Renderer() override;
    void bind(const CdCreateContext&, std::function<void()> changed) override;
    void pause(bool paused) override;
    bool render(Spec, const Options&, Values, const CdFrame&, CdRect safe = {}) override;
    bool savePng(const std::wstring&, const CdFrame&, CdColor background);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace monitorplugin
