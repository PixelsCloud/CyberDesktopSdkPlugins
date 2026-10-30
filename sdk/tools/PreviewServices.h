#pragma once

#include <Windows.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sdk/PluginApi.h"

namespace cyberdesktop::package::v1 {
class SettingsPage;
}

namespace cdsdk {
struct MockHost;

// Per-instance preview adapters. They never open Desktop databases or invoke
// private executables; external application commands remain observable records.
class PreviewServices {
public:
    explicit PreviewServices(MockHost& host);
    ~PreviewServices();
    PreviewServices(const PreviewServices&) = delete;
    PreviewServices& operator=(const PreviewServices&) = delete;

    void configure(std::filesystem::path root, std::map<std::string, bool> settings);
    std::optional<CdResult> query(const char* id, uint32_t version, void* output, uint32_t capacity);
    const std::vector<std::string>& openedImages() const;
    std::string settingsChoices() const;
    std::string settingsContext(const std::string& id) const;
    void applySettingsChoices(cyberdesktop::package::v1::SettingsPage& page) const;
    void settingsChanged(std::function<void()> callback);
    uint64_t imageSourceCount() const;
    void unsubscribe(uint64_t token);
    void placement(HWND parent, double dpr);
    void pause(bool paused);
    void tick();
    void render(const CdFrame& frame);
    CdResult renderPlugin(const CdInstanceApi& instance, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
