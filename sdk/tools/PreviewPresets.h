#pragma once

#include "tools/Runtime.h"

namespace cdsdk {
struct PreviewScenario {
    const char* id;
    const wchar_t* english;
    const wchar_t* chinese;
    MockScenario value;
};

inline constexpr PreviewScenario previewScenarios[]{
    {"normal", L"Normal", L"正常", MockScenario::Normal},
    {"loading", L"Loading (wait for data)", L"加载中（等待数据）", MockScenario::Loading},
    {"empty", L"Empty data / no player", L"空数据 / 无播放器", MockScenario::Empty},
    {"error", L"Service failure", L"服务失败", MockScenario::Error}};

struct PreviewSize {
    uint32_t width, height;
    const wchar_t* label;
};

inline constexpr PreviewSize previewSizes[]{{160, 160, L"160 × 160"},
                                            {320, 200, L"320 × 200"},
                                            {320, 320, L"320 × 320"},
                                            {320, 480, L"320 × 480"},
                                            {480, 320, L"480 × 320"}};

size_t previewScenarioIndex(const std::string& id);
}  // namespace cdsdk
