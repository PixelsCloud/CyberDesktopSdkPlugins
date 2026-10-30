#pragma once
// Optional C++ helper for mandatory C ABI settings. No Qt/protobuf dependency.
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "sdk/PluginApi.h"

namespace cdsdk {
struct Setting {
    std::string id;
    uint32_t type;
    int64_t integer = 0;
    double number = 0;
    std::string text;
    double minimum = 0, maximum = 0;
    std::vector<std::string> choices;
};

class Settings {
    std::vector<Setting> values;

public:
    Settings(std::initializer_list<Setting> initial);

    const Setting& at(const char* id) const;

    // Replace a runtime inventory without retaining a now unsupported selection.
    void setChoices(const char* id, std::vector<std::string> choices, const std::string& fallback);

    CdResult get(const char* id, CdSettingValue* output, uint32_t size) const noexcept;

    CdResult apply(const CdSettingValue* input, uint32_t count) noexcept;
};
}  // namespace cdsdk
