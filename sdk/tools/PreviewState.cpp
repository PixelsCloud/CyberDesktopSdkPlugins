#include "tools/PreviewState.h"

namespace cdsdk {
pb::PreviewState capturePreviewState(const NativeInstance& instance, const pb::PluginDescriptor& descriptor) {
    pb::PreviewState state;
    state.set_format_version(1);
    state.set_plugin_id(descriptor.id());
    state.set_plugin_version(descriptor.version());
    Error error;
    state.set_native_state(instance.state(&error));
    if (!error.code.empty()) {
        throw std::runtime_error(errorText(error));
    }
    for (const auto& field : descriptor.settings_page().fields()) {
        CdSettingValue value{sizeof(value)};
        if (instance.api.get_setting(instance.api.instance, field.id().c_str(), &value, sizeof(value)) != CD_OK ||
            value.type != uint32_t(field.type()) || !value.id || field.id() != value.id ||
            (value.text && strnlen(value.text, 4097) > 4096)) {
            throw std::runtime_error("Cannot capture setting: " + field.id());
        }
        auto* saved = state.add_settings();
        saved->set_id(field.id());
        saved->set_type(value.type);
        saved->set_integer(value.integer);
        saved->set_number(value.number);
        saved->set_text(value.text ? value.text : "");
    }
    return state;
}

void restorePreviewState(NativeInstance& instance, const pb::PluginDescriptor& descriptor,
                         const pb::PreviewState& state) {
    if (state.format_version() != 1 || state.plugin_id() != descriptor.id() ||
        state.native_state().size() > 2 * 1024 * 1024 || state.settings_size() > 512) {
        throw std::runtime_error("Incompatible preview state; retained on disk for recovery");
    }
    std::set<std::string> known, seen;
    for (const auto& field : descriptor.settings_page().fields()) {
        known.insert(field.id());
    }
    std::vector<CdSettingValue> settings;
    for (const auto& saved : state.settings()) {
        if (saved.id().empty() || !seen.insert(saved.id()).second || saved.text().size() > 4096 ||
            !std::isfinite(saved.number())) {
            throw std::runtime_error("Invalid saved setting");
        }
        if (known.contains(saved.id())) {
            settings.push_back({sizeof(CdSettingValue), saved.type(), saved.id().c_str(), saved.integer(),
                                saved.number(), saved.text().c_str()});
        }
    }
    if (instance.api.apply_settings(instance.api.instance, settings.data(), uint32_t(settings.size())) != CD_OK) {
        throw std::runtime_error("Saved settings rejected by plugin");
    }
    const auto& bytes = state.native_state();
    if (instance.api.restore_state(instance.api.instance, reinterpret_cast<const uint8_t*>(bytes.data()),
                                   bytes.size()) != CD_OK) {
        throw std::runtime_error("Saved state rejected by plugin version " + descriptor.version());
    }
}

pb::PreviewState readPreviewState(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 5 * 1024 * 1024) {
        throw std::length_error("Preview state exceeds 5 MiB");
    }
    std::ifstream input(path, std::ios::binary);
    pb::PreviewState state;
    if (!state.ParseFromIstream(&input)) {
        throw std::runtime_error("Cannot parse preview state");
    }
    return state;
}

void writePreviewState(const std::filesystem::path& path, const pb::PreviewState& state) {
    const auto temporary =
        std::filesystem::path(path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!state.SerializeToOstream(&output)) {
            throw std::runtime_error("Cannot serialize preview state");
        }
        output.close();
        if (!output) {
            throw std::runtime_error("Cannot write preview state");
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace preview state: " + std::to_string(error));
    }
}
}  // namespace cdsdk
