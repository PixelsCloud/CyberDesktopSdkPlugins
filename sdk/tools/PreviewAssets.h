#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "sdk/services/CharacterImages.h"

namespace cdsdk {
class PreviewAssets {
public:
    explicit PreviewAssets(std::function<void()> redraw);
    ~PreviewAssets();
    PreviewAssets(const PreviewAssets&) = delete;
    PreviewAssets& operator=(const PreviewAssets&) = delete;
    void root(const std::filesystem::path& path);
    void tick();
    CdResult request(const char* pack, const char* id, uint32_t edge, CdCharacterImage* output);
    std::string metadata() const;
    std::string error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
