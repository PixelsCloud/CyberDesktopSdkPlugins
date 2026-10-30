#pragma once

#include <sdk/PluginApi.h>

#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "support/Package.h"

namespace cdsdk {
namespace setting_probe {
struct Value {
    std::string id;
    uint32_t type = 0;
    int64_t integer = 0;
    double number = 0;
    std::string text;

    CdSettingValue raw() const;

    bool operator==(const Value&) const = default;
};

bool snapshot(const pb::SettingsPage& page, const CdInstanceApi& api, std::vector<Value>& output);
}  // namespace setting_probe

// A schema describes individual types/ranges, not coupled domain rules. For
// example, disabling auto-location also requires a nonempty city. Rejections
// must remain atomic; an independently accepted field exercises instance isolation.
bool probeSettings(const pb::SettingsPage& page, const CdInstanceApi& first, const CdInstanceApi& second, Error* error);
}  // namespace cdsdk
