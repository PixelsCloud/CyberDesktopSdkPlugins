#include "tools/SettingsProbe.h"

namespace cdsdk::setting_probe {
auto Value::raw() const -> CdSettingValue {
    return {sizeof(CdSettingValue), type, id.c_str(), integer, number, text.c_str()};
}

bool snapshot(const pb::SettingsPage& page, const CdInstanceApi& api, std::vector<Value>& output) {
    output.clear();
    for (const auto& field : page.fields()) {
        CdSettingValue value{};
        if (!api.get_setting || api.get_setting(api.instance, field.id().c_str(), &value, sizeof(value)) != CD_OK ||
            value.struct_size != sizeof(value) || value.type != uint32_t(field.type()) || !value.id ||
            field.id() != value.id) {
            return false;
        }
        Value owned;
        owned.id = field.id();
        owned.type = value.type;
        if (value.type == CD_SETTING_TOGGLE || value.type == CD_SETTING_INTEGER || value.type == CD_SETTING_COLOR) {
            owned.integer = value.integer;
        } else if (value.type == CD_SETTING_NUMBER) {
            if (!std::isfinite(value.number)) {
                return false;
            }
            owned.number = value.number;
        } else {
            if (value.text && strnlen_s(value.text, 4097) > 4096) {
                return false;
            }
            owned.text = value.text ? value.text : "";
        }
        output.push_back(std::move(owned));
    }
    return true;
}
}  // namespace cdsdk::setting_probe

namespace cdsdk {
bool probeSettings(const pb::SettingsPage& page, const CdInstanceApi& first, const CdInstanceApi& second,
                   Error* error) {
    if (error) {
        *error = {};
    }
    using Values = std::vector<setting_probe::Value>;
    const auto fail = [&](const char* message) {
        if (error) {
            *error = {"runtime.settings", message};
        }
        return false;
    };
    Values original, independent;
    if (page.fields_size() < 1 || page.fields_size() > 32 || !setting_probe::snapshot(page, first, original) ||
        !setting_probe::snapshot(page, second, independent) || !first.apply_settings) {
        return fail("Mandatory setting callback disagrees with schema");
    }
    Values observed;
    const auto unchanged = [&] {
        return setting_probe::snapshot(page, first, observed) && observed == original &&
               setting_probe::snapshot(page, second, observed) && observed == independent;
    };
    std::vector<CdSettingValue> current;
    for (const auto& value : original) {
        current.push_back(value.raw());
    }
    if (first.apply_settings(first.instance, current.data(), uint32_t(current.size())) != CD_OK || !unchanged()) {
        return fail("Current settings transaction rejected or changed values");
    }
    auto missing = original.front().raw();
    missing.id = "__missing_setting";
    if (first.apply_settings(first.instance, &missing, 1) == CD_OK || !unchanged()) {
        return fail("Unknown setting accepted or rejection was not atomic");
    }
    for (size_t index = 0; index < original.size(); ++index) {
        const auto& field = page.fields(int(index));
        auto candidate = original[index];
        if (candidate.type == CD_SETTING_TOGGLE) {
            candidate.integer = !candidate.integer;
        } else if (candidate.type == CD_SETTING_INTEGER) {
            candidate.integer =
                candidate.integer == int64_t(field.minimum()) ? int64_t(field.maximum()) : int64_t(field.minimum());
        } else if (candidate.type == CD_SETTING_NUMBER) {
            candidate.number = candidate.number == field.minimum() ? field.maximum() : field.minimum();
        } else if (candidate.type == CD_SETTING_COLOR) {
            candidate.integer ^= 0xFFFFFF00;
        } else if (candidate.type == CD_SETTING_TEXT) {
            candidate.text = candidate.text == "x" ? "y" : "x";
        } else if (candidate.type == CD_SETTING_FONT) {
            candidate.text = candidate.text == "Segoe UI" ? "Microsoft YaHei" : "Segoe UI";
        }
        std::vector<setting_probe::Value> candidates;
        if (candidate.type == CD_SETTING_CHOICE) {
            for (const auto& choice : field.choices()) {
                candidate.text = choice.value();
                candidates.push_back(candidate);
            }
        } else {
            candidates.push_back(candidate);
        }
        for (const auto& value : candidates) {
            if (value == original[index]) {
                continue;
            }
            auto raw = value.raw();
            CdSettingValue invalid[]{raw, missing};
            if (first.apply_settings(first.instance, invalid, 2) == CD_OK || !unchanged()) {
                return fail("Invalid transaction accepted or rejection was not atomic");
            }
            const auto result = first.apply_settings(first.instance, &raw, 1);
            if (result != CD_OK) {
                if (!unchanged()) {
                    return fail("Rejected domain setting mutated an instance");
                }
                if (result != CD_ERROR_ARGUMENT && result != CD_ERROR_UNSUPPORTED) {
                    return fail("Setting probe returned an unexpected runtime error");
                }
                continue;
            }
            auto expected = original;
            expected[index] = value;
            if (!setting_probe::snapshot(page, first, observed) || observed != expected) {
                return fail("Accepted settings change was not reflected atomically");
            }
            if (!setting_probe::snapshot(page, second, observed) || observed != independent) {
                return fail("Settings shared across instances");
            }
            return true;
        }
    }
    // Some valid schemas expose only fixed or coupled fields. The current-value
    // transaction and all attempted rejection checks still have to pass.
    return true;
}
}  // namespace cdsdk
