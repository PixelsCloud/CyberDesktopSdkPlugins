#pragma once

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include <fstream>

#include "tools/PreviewPresets.h"

namespace cdsdk {
google::protobuf::Struct readPreviewJson(const std::filesystem::path& path);

struct PreviewOptions {
    double dpr = 1;
    uint32_t logicalWidth = 0, logicalHeight = 0, frames = 1, watchMs = 0;
    uint32_t theme = CD_ENV_THEME_DARK;
    std::string language = "en-US", layout, services = "mock";
    std::string scenario = "normal";
    std::optional<uint64_t> time;
    std::optional<int32_t> utcOffset;
    std::filesystem::path screenshot, state, mock, diagnostics, replay;
    std::filesystem::path exportDirectory;
    std::wstring stopEvent;
    bool overlay = false, languageOverride = false;

    static uint64_t integer(const std::wstring& text, uint64_t maximum);

    static PreviewOptions parse(int argc, wchar_t** argv);

    void configure(MockHost& host) const;
};
}  // namespace cdsdk
