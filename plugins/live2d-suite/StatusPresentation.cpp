#include "plugins/live2d-suite/StatusPresentation.h"

namespace live2dplugin {
FailureExplanation explainFailure(std::string_view detail) {
    const auto contains = [&](std::string_view value) {
        return detail.find(value) != std::string_view::npos;
    };
    if (contains("output exceeds")) {
        return {L"插件输出尺寸过大", L"请减小插件尺寸后重试。"};
    }
    if (contains("eight Live2D")) {
        return {L"同时运行的角色过多", L"请先停用或移除其他 Live2D 角色。"};
    }
    if (contains("Physics vertex") || contains("Invalid physics") || contains("Physics counts")) {
        return {L"模型物理数据有误", L"请重新下载，或联系模型作者修复。"};
    }
    if (contains("Physics structure")) {
        return {L"物理数据超过限制", L"请使用精简版模型，或更换模型。"};
    }
    if (contains("JSON structure") || contains("JSON resource") || contains("Too many") ||
        contains("Model complexity")) {
        return {L"模型数据过于复杂", L"请使用精简版模型，或更换模型。"};
    }
    if (contains("GPU budget") || contains("clipping masks") || contains("allocation")) {
        return {L"模型超出图形资源限制", L"请尝试减小插件尺寸，或更换轻量模型。"};
    }
    if (contains("texture dimensions") || contains("textures exceed") || contains("budget")) {
        return {L"模型文件或贴图过大", L"请使用较小贴图的模型版本。"};
    }
    if (contains("Unsupported") || contains("version check") || contains("descriptor version")) {
        return {L"模型格式不受支持", L"请使用 Cubism 3 或更新版本导出的模型。"};
    }
    if (contains("timed out")) {
        return {L"模型加载或运行超时", L"请重新应用设置，或更换轻量模型。"};
    }
    if (contains("process") || contains("disconnected") || contains("synchronization")) {
        return {L"模型运行异常，已停止", L"请重新应用设置，或更换模型。"};
    }
    if (contains("Missing") || contains("directory") || contains("descriptor") || contains("inaccessible")) {
        return {L"模型目录或文件不完整", L"请检查目录中是否包含模型和全部素材。"};
    }
    if (contains("reference") || contains("path") || contains("outside")) {
        return {L"模型资源路径无效", L"请重新解压完整模型，或更换模型。"};
    }
    return {L"模型文件损坏或数据无效", L"请重新下载完整模型，或更换模型。"};
}

StatusPresentation drawFailure(cybercanvas::Canvas& canvas, cybercanvas::Rect area, std::string_view detail,
                               uint32_t foreground) {
    const auto explanation = explainFailure(detail);
    const auto message = L"模型加载失败\n" + explanation.reason + L"\n" + explanation.advice +
                         L"\n\n在设置中清空模型目录\n可恢复内置角色";
    const double margin = std::min(12., std::max(4., std::min(area.width, area.height) * .06));
    const cybercanvas::Rect bounds{area.x + margin, area.y + margin, std::max(1., area.width - margin * 2),
                                   std::max(1., area.height - margin * 2)};
    cybercanvas::TextStyle style;
    style.family = L"Microsoft YaHei UI";
    style.locale = L"zh-CN";
    style.size = 13;
    style.align = cybercanvas::TextAlign::Center;
    style.wrap = true;
    auto metrics = canvas.measureText(message, style, {bounds.width, 10000});
    while ((metrics.height > bounds.height || metrics.width > bounds.width) && style.size > 9) {
        style.size -= .5;
        metrics = canvas.measureText(message, style, {bounds.width, 10000});
    }
    // Keep status legible over both light and dark desktop materials.
    const unsigned brightness = ((foreground >> 24) & 255) + ((foreground >> 16) & 255) + ((foreground >> 8) & 255);
    canvas.rectangle({area.x, area.y, area.width, area.height}, brightness > 384 ? 0x101828E8 : 0xFFFFFFE8);
    canvas.text(message, style, bounds, foreground);
    return {bounds, metrics, style.size};
}
}  // namespace live2dplugin
