#include "sdk/Settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <utility>

namespace cdsdk {
Settings::Settings(std::initializer_list<Setting> initial) : values(initial) {
    for (auto& setting : values) {
        if (setting.id == "language" && setting.type == CD_SETTING_CHOICE &&
            std::find(setting.choices.begin(), setting.choices.end(), "zh-CN") != setting.choices.end() &&
            std::find(setting.choices.begin(), setting.choices.end(), "zh-TW") == setting.choices.end()) {
            setting.choices.push_back("zh-TW");
        }
    }
}

auto Settings::at(const char* id) const -> const Setting& {
    for (const auto& value : values)
        if (value.id == id)
            return value;
    throw std::runtime_error("Unknown setting");
}

auto Settings::setChoices(const char* id, std::vector<std::string> choices, const std::string& fallback) -> void {
    if (choices.empty() || choices.size() > 257 ||
        std::set<std::string>(choices.begin(), choices.end()).size() != choices.size() ||
        std::find(choices.begin(), choices.end(), fallback) == choices.end()) {
        throw std::runtime_error("Invalid setting choices");
    }
    for (auto& value : values) {
        if (value.id == id && value.type == CD_SETTING_CHOICE) {
            if (std::find(choices.begin(), choices.end(), value.text) == choices.end()) {
                value.text = fallback;
            }
            value.choices = std::move(choices);
            return;
        }
    }
    throw std::runtime_error("Unknown choice setting");
}

auto Settings::get(const char* id, CdSettingValue* output, uint32_t size) const noexcept -> CdResult {
    if (!output || size < sizeof(CdSettingValue))
        return CD_ERROR_BUFFER;
    if (!id)
        return CD_ERROR_ARGUMENT;
    for (const auto& value : values)
        if (value.id == id) {
            *output = {sizeof(CdSettingValue), value.type,   value.id.c_str(),
                       value.integer,          value.number, value.text.c_str()};
            return CD_OK;
        }
    return CD_ERROR_ARGUMENT;
}

auto Settings::apply(const CdSettingValue* input, uint32_t count) noexcept -> CdResult {
    try {
        if ((count && !input) || count > values.size())
            return CD_ERROR_ARGUMENT;
        auto updated = values;
        std::set<std::string> seen;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& change = input[i];
            if (change.struct_size < sizeof(change) || !change.id || !seen.insert(change.id).second)
                return CD_ERROR_ARGUMENT;
            auto found = std::find_if(updated.begin(), updated.end(), [&](const auto& value) {
                return value.id == change.id;
            });
            if (found == updated.end() || change.type != found->type)
                return CD_ERROR_ARGUMENT;
            if (change.type == CD_SETTING_TOGGLE) {
                if (change.integer != 0 && change.integer != 1)
                    return CD_ERROR_ARGUMENT;
                found->integer = change.integer;
            } else if (change.type == CD_SETTING_INTEGER) {
                if (change.integer < found->minimum || change.integer > found->maximum)
                    return CD_ERROR_ARGUMENT;
                found->integer = change.integer;
            } else if (change.type == CD_SETTING_NUMBER) {
                if (!std::isfinite(change.number) || change.number < found->minimum || change.number > found->maximum)
                    return CD_ERROR_ARGUMENT;
                found->number = change.number;
            } else if (change.type == CD_SETTING_COLOR) {
                if (change.integer < 0 || uint64_t(change.integer) > UINT32_MAX)
                    return CD_ERROR_ARGUMENT;
                found->integer = change.integer;
            } else if (change.type == CD_SETTING_TEXT || change.type == CD_SETTING_CHOICE ||
                       change.type == CD_SETTING_FONT) {
                if (!change.text)
                    return CD_ERROR_ARGUMENT;
                size_t length = 0;
                while (length <= 4096 && change.text[length])
                    ++length;
                if (length > 4096)
                    return CD_ERROR_ARGUMENT;
                if (change.type == CD_SETTING_FONT && (length == 0 || length > 128))
                    return CD_ERROR_ARGUMENT;
                if (change.type == CD_SETTING_CHOICE &&
                    std::find(found->choices.begin(), found->choices.end(), change.text) == found->choices.end())
                    return CD_ERROR_ARGUMENT;
                if (change.type == CD_SETTING_TEXT && length > found->maximum)
                    return CD_ERROR_ARGUMENT;
                found->text = change.text;
            } else
                return CD_ERROR_ARGUMENT;
        }
        values = std::move(updated);
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace cdsdk
