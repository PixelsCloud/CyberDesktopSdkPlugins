#pragma once

#include <CyberCanvas/Widgets.h>

#include <bit>
#include <map>
#include <tuple>

#include "sdk/services/Icons.h"

namespace cdsdk {
// The plugin never links Qt. Rasterization comes from the optional host entry.
class Icons {
public:
    void bind(const CdCreateContext& context);

    bool paint(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, std::string_view name, uint32_t color);

    cybercanvas::IconPainter painter();

private:
    CdIconsApi api_{};
    void* context_ = nullptr;
    std::map<std::tuple<std::string, uint32_t, uint32_t, uint32_t, double>, std::vector<uint8_t>> cache_;
};
}  // namespace cdsdk
