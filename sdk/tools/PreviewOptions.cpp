#include "tools/PreviewOptions.h"

namespace cdsdk {
google::protobuf::Struct readPreviewJson(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 1024 * 1024) {
        throw std::length_error("Preview JSON exceeds 1 MiB");
    }
    std::ifstream input(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(input), {}};
    google::protobuf::Struct result;
    const auto status = google::protobuf::util::JsonStringToMessage(text, &result);
    if (!status.ok()) {
        throw std::invalid_argument("Invalid preview JSON: " + status.ToString());
    }
    return result;
}

auto PreviewOptions::integer(const std::wstring& text, uint64_t maximum) -> uint64_t {
    if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos) {
        throw std::invalid_argument("Expected unsigned integer");
    }
    size_t used = 0;
    const auto result = std::stoull(text, &used);
    if (used != text.size() || result > maximum) {
        throw std::invalid_argument("Integer out of range");
    }
    return result;
}

auto PreviewOptions::parse(int argc, wchar_t** argv) -> PreviewOptions {
    PreviewOptions result;
    for (int i = 2; i < argc; ++i) {
        const std::wstring option = argv[i];
        if (option == L"--overlay") {
            result.overlay = true;
            continue;
        }
        if (++i >= argc) {
            throw std::invalid_argument("Missing preview option value");
        }
        const std::wstring value = argv[i];
        if (option == L"--screenshot") {
            result.screenshot = value;
        } else if (option == L"--export-dir") {
            result.exportDirectory = value;
        } else if (option == L"--scenario") {
            result.scenario = utf8Argument(value.c_str());
            previewScenarioIndex(result.scenario);
        } else if (option == L"--stop-event") {
            if (!value.starts_with(L"Local\\CyberDesktop.SdkPreview.") || value.size() > 128 ||
                value.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.\\-") !=
                    std::wstring::npos) {
                throw std::invalid_argument("Invalid SDK preview stop event name");
            }
            result.stopEvent = value;
        } else if (option == L"--state") {
            result.state = value;
        } else if (option == L"--mock") {
            result.mock = value;
        } else if (option == L"--diagnostics") {
            result.diagnostics = value;
        } else if (option == L"--replay") {
            result.replay = value;
        } else if (option == L"--layout") {
            result.layout = utf8Argument(value.c_str());
        } else if (option == L"--language") {
            result.languageOverride = true;
            result.language = utf8Argument(value.c_str());
            if (result.language.empty() || result.language.size() > 31 ||
                result.language.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") !=
                    std::string::npos) {
                throw std::invalid_argument("Language must be a BCP47 tag of at most 31 ASCII bytes");
            }
        } else if (option == L"--theme") {
            if (value != L"light" && value != L"dark") {
                throw std::invalid_argument("Theme must be light or dark");
            }
            result.theme = value == L"light" ? CD_ENV_THEME_LIGHT : CD_ENV_THEME_DARK;
        } else if (option == L"--services") {
            if (value != L"mock" && value != L"none") {
                throw std::invalid_argument("Services must be mock or none; live services use the production host");
            }
            result.services = utf8Argument(value.c_str());
        } else if (option == L"--dpr") {
            size_t used = 0;
            result.dpr = std::stod(value, &used);
            if (used != value.size() || !std::isfinite(result.dpr) || result.dpr < .5 || result.dpr > 4) {
                throw std::invalid_argument("DPR must be 0.5..4");
            }
        } else if (option == L"--size") {
            const auto split = value.find(L'x');
            if (split == std::wstring::npos) {
                throw std::invalid_argument("Size must be WIDTHxHEIGHT in logical pixels");
            }
            result.logicalWidth = uint32_t(integer(value.substr(0, split), 4096));
            result.logicalHeight = uint32_t(integer(value.substr(split + 1), 4096));
            if (!result.logicalWidth || !result.logicalHeight) {
                throw std::invalid_argument("Size must be positive");
            }
        } else if (option == L"--time") {
            result.time = integer(value, 32503680000000ull);
        } else if (option == L"--utc-offset") {
            size_t used = 0;
            const auto offset = std::stoi(value, &used);
            if (used != value.size() || offset < -840 || offset > 840) {
                throw std::invalid_argument("UTC offset must be -840..840 minutes");
            }
            result.utcOffset = offset;
        } else if (option == L"--frames") {
            result.frames = uint32_t(integer(value, 36000));
            if (!result.frames) {
                throw std::invalid_argument("Frames must be 1..36000");
            }
        } else if (option == L"--watch-ms") {
            result.watchMs = uint32_t(integer(value, 3600000));
        } else {
            throw std::invalid_argument("Unknown preview option: " + utf8Argument(option.c_str()));
        }
    }
    if ((result.logicalWidth && result.logicalWidth * result.dpr > 4096) ||
        (result.logicalHeight && result.logicalHeight * result.dpr > 4096)) {
        throw std::invalid_argument("Physical preview dimensions exceed 4096");
    }
    if (!result.mock.empty() && result.services != "mock") {
        throw std::invalid_argument("--mock requires --services mock");
    }
    if (result.scenario != "normal" && result.services != "mock") {
        throw std::invalid_argument("--scenario requires --services mock");
    }
    return result;
}

auto PreviewOptions::configure(MockHost& host) const -> void {
    host.fixedUnixMs = time;
    host.utcOffsetMinutes = utcOffset.value_or(time ? 0 : host.utcOffsetMinutes);
    if (languageOverride) {
        host.locale = language;
    }
    host.theme = theme;
    host.dataServicesEnabled = services != "none";
    host.scenario = previewScenarios[previewScenarioIndex(scenario)].value;
    if (mock.empty()) {
        return;
    }
    const auto data = readPreviewJson(mock);
    for (const auto& [key, value] : data.fields()) {
        const auto number = [&](double low, double high) {
            if (value.kind_case() != google::protobuf::Value::kNumberValue || !std::isfinite(value.number_value()) ||
                value.number_value() < low || value.number_value() > high) {
                throw std::invalid_argument("Mock value out of range: " + key);
            }
            return value.number_value();
        };
        const auto boolean = [&] {
            if (value.kind_case() != google::protobuf::Value::kBoolValue) {
                throw std::invalid_argument("Mock value must be boolean: " + key);
            }
            return value.bool_value();
        };
        if (key == "cpuPercent") {
            host.cpuPercent = number(0, 100);
        } else if (key == "gpuPercent") {
            host.gpuPercent = number(0, 100);
        } else if (key == "temperature") {
            host.temperature = number(-100, 100);
        } else if (key == "humidity") {
            host.humidity = number(0, 100);
        } else if (key == "aqi") {
            host.aqi = number(0, 1000);
        } else if (key == "dataError") {
            host.dataError = uint32_t(number(0, 10000));
        } else if (key == "hasNetworkData") {
            host.hasNetworkData = boolean();
        } else if (key == "hasAudioDevice") {
            host.hasAudioDevice = boolean();
        } else if (key == "hasMediaSession") {
            host.hasMediaSession = boolean();
        } else {
            throw std::invalid_argument("Unknown mock field: " + key);
        }
    }
}
}  // namespace cdsdk
